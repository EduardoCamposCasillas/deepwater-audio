#pragma once

#include <algorithm>
#include <array>

#include "deepwater/dsp/DcBlocker.h"
#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/LevelMeter.h"
#include "deepwater/dsp/Smoothing.h"
#include "dsp/Saturator.h"
#include "dsp/Tone.h"

namespace riptide::dsp
{

using deepwater::dsp::LevelMeter;

// Factores de oversampling que ofrece el plugin, en el orden del selector: índice = número de etapas ×2.
inline constexpr int numOversamplingChoices = deepwater::dsp::Oversampler::maxStages + 1; // Off, 2x, 4x, 8x, 16x

// Lo que el motor necesita de los parámetros, en unidades reales. El procesador lo lee del APVTS una vez por bloque.
struct EngineSettings
{
    float inputGainDb = 0.0f;
    float outputGainDb = 0.0f;
    bool bypassed = false;
    int oversampling = 2; // índice: 0 = Off, 1 = 2x, 2 = 4x, 3 = 8x, 4 = 16x
    Mode mode = Mode::tape;
    ToneSettings tone {};
};

// Fase 4: cadena de audio completa del saturador, sin JUCE (se prueba en tests/EngineTests.cpp):
//
//   entrada ─┬─ × input (drive) ─ tono pre ─[medidor IN]─ modo (Saturator) ─ tono post ─ filtro DC ─ × output ─┐
//            │                                                                                              ├─ fundido ─ × fundido de factor ─[medidor OUT]─ salida
//            └─────────────────────────── señal original retrasada lo mismo que el oversampling ──────────────┘
//
// Input es el drive: decide con qué nivel llega la señal a la curva del modo (ver Saturator.h). El tono (Tone.h)
// decide QUÉ frecuencias llegan a la curva y con cuánto nivel (cortes PRE y emphasis) y el color de lo que sale (de-
// emphasis, cortes POST y tilt). IN mide justo antes de la curva: lo que la curva "ve". Cada modo calcula su
// curva con oversampling (etapas halfband de la librería) para que los armónicos que pasan de Nyquist no se
// reflejen como aliasing.
//
// Cambiar de modo sin clics: cada canal tiene DOS saturadores. El activo suena; al pedir otro modo, el segundo se
// configura con él (vacío), procesa en silencio el tiempo que tarda su oversampler en llenarse (2 × latencia) y
// después los dos se cruzan en 20 ms. Las dos señales vienen de la misma entrada y salen alineadas, así que el cruce
// es suave. Solo durante el cambio se paga el doble de CPU. Los filtros retrasan la señal; el camino original del bypass se retrasa lo
// mismo para que los dos lleguen alineados al fundido (si no, el fundido mezclaría dos señales desfasadas: filtro de
// peine y clics).
//
// Cambiar el factor cambia la latencia y borra la historia de los filtros: es un salto. Por eso se hace en silencio:
// la salida baja a cero en 10 ms, se cambia el factor y vuelve a subir.
class Engine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double gainRampSeconds = 0.02;   // 20 ms: girar la perilla no hace "zipper" y sigue siendo ágil
    static constexpr double bypassFadeSeconds = 0.02;
    static constexpr double factorFadeSeconds = 0.01; // fundido a silencio al cambiar el oversampling
    static constexpr double modeFadeSeconds = 0.02;   // cruce entre el modo anterior y el nuevo
    static constexpr double dcCutoffHz = 5.0;         // por debajo de lo audible; asienta en ~30 ms

    void prepare (double sampleRate, const EngineSettings& settings) noexcept
    {
        inputGain.setTime (gainRampSeconds, sampleRate);
        outputGain.setTime (gainRampSeconds, sampleRate);
        bypassFade.setTime (bypassFadeSeconds, sampleRate);
        factorFade.setTime (factorFadeSeconds, sampleRate);
        modeFade.setTime (modeFadeSeconds, sampleRate);
        tone.prepare (sampleRate, settings.tone);

        // Al preparar no hay nada que suavizar: se empieza directamente en los valores actuales.
        inputGain.snap (deepwater::dsp::dbToGain (settings.inputGainDb));
        outputGain.snap (deepwater::dsp::dbToGain (settings.outputGainDb));
        processing = ! settings.bypassed;
        bypassFade.snap (processing);

        requestedStages = activeStages = clampStages (settings.oversampling);
        factorFade.snap (true);

        requestedMode = settings.mode;
        activeSlot = 0;
        switching = false;

        for (auto& channel : channels)
        {
            for (auto& slot : channel.slots)
                slot.prepare (sampleRate, activeStages, requestedMode);
            channel.tone.reset();
            channel.dcBlocker.prepare (dcCutoffHz, sampleRate);
            channel.dryDelay.fill (0.0f);
            channel.input.prepare (sampleRate);
            channel.output.prepare (sampleRate);
        }
        dryWriteIndex = 0;
        latency = channels[0].slots[0].getLatency();
    }

    void setSettings (const EngineSettings& settings) noexcept
    {
        inputGain.setTarget (deepwater::dsp::dbToGain (settings.inputGainDb));
        outputGain.setTarget (deepwater::dsp::dbToGain (settings.outputGainDb));
        processing = ! settings.bypassed;
        requestedStages = clampStages (settings.oversampling);
        requestedMode = settings.mode;
        tone.setSettings (settings.tone);
    }

    // Procesa en el sitio. Con más de maxChannels canales, los de más se dejan intactos.
    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            // El fundido de factor baja mientras haya un cambio pendiente; en silencio se aplica y vuelve a subir.
            const bool factorSettled = requestedStages == activeStages;
            const float level = factorFade.next (factorSettled);
            if (! factorSettled && level == 0.0f)
                applyRequestedFactor();

            // Cruce de modos: 'blend' = cuánto se oye el saturador entrante (0 mientras se llena su oversampler).
            if (! switching && requestedMode != activeMode())
                beginModeSwitch();
            float blend = 0.0f;
            if (switching)
            {
                if (warmUpRemaining > 0)
                    --warmUpRemaining;
                else
                    blend = modeFade.next();
            }

            // Las rampas avanzan una vez por muestra para todos los canales: L y R reciben exactamente la misma ganancia.
            const float gainIn = inputGain.next();
            const float gainOut = outputGain.next();
            const float wet = bypassFade.next (processing); // 1 = procesado, 0 = original
            tone.advance();
            const size_t readIndex = (dryWriteIndex - static_cast<size_t> (latency)) & (dryDelaySize - 1);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& channel = channels[static_cast<size_t> (ch)];
                float& sample = audio[ch][i];

                channel.dryDelay[dryWriteIndex] = sample;
                const float dry = channel.dryDelay[readIndex];

                const float driven = channel.tone.pre (sample * gainIn, tone);
                channel.input.push (driven);
                float saturated = channel.slots[activeSlot].process (driven);
                if (switching)
                {
                    const float incoming = channel.slots[1 - activeSlot].process (driven);
                    saturated = blend == 1.0f ? incoming : saturated + (incoming - saturated) * blend;
                }
                const float processed = channel.dcBlocker.process (channel.tone.post (saturated, tone)) * gainOut;

                // Fuera de los fundidos se copia la señal tal cual: con el bypass puesto la salida es la entrada
                // retrasada, bit a bit.
                float out = wet == 1.0f ? processed : wet == 0.0f ? dry : dry + (processed - dry) * wet;
                if (level != 1.0f)
                    out *= level;
                channel.output.push (out);
                sample = out;
            }
            dryWriteIndex = (dryWriteIndex + 1) & (dryDelaySize - 1);

            if (switching && blend == 1.0f)
                finishModeSwitch();
        }
    }

    // Latencia actual en muestras del host (la que hay que informar al host).
    [[nodiscard]] int getLatency() const noexcept { return latency; }
    [[nodiscard]] int getActiveOversampling() const noexcept { return activeStages; }
    [[nodiscard]] Mode getActiveMode() const noexcept { return activeMode(); }
    [[nodiscard]] bool isSwitchingMode() const noexcept { return switching; }

    [[nodiscard]] LevelMeter& inputMeter (int channel) noexcept { return channels[static_cast<size_t> (channel)].input; }
    [[nodiscard]] LevelMeter& outputMeter (int channel) noexcept { return channels[static_cast<size_t> (channel)].output; }

private:
    // La latencia máxima (×16 a frecuencias bajas, donde todas las etapas usan el filtro más largo) es de unas 140
    // muestras: el retardo del camino original cabe en 256.
    static constexpr size_t dryDelaySize = 256;

    struct Channel
    {
        std::array<Saturator, 2> slots; // el activo y el que entra durante un cambio de modo
        ToneChannel tone;
        deepwater::dsp::DcBlocker dcBlocker;
        std::array<float, dryDelaySize> dryDelay {};
        LevelMeter input, output;
    };

    static int clampStages (int index) noexcept { return std::clamp (index, 0, numOversamplingChoices - 1); }

    [[nodiscard]] Mode activeMode() const noexcept { return channels[0].slots[activeSlot].getMode(); }

    // Se llama en silencio (el fundido de factor está en 0): si había un cruce de modos a medias, se termina de golpe.
    void applyRequestedFactor() noexcept
    {
        if (switching)
            finishModeSwitch();
        activeStages = requestedStages;
        for (auto& channel : channels)
            for (auto& slot : channel.slots)
                slot.setNumStages (activeStages);
        // El retardo del camino original conserva su historia: solo cambia desde dónde se lee.
        latency = channels[0].slots[0].getLatency();
    }

    void beginModeSwitch() noexcept
    {
        for (auto& channel : channels)
            channel.slots[1 - activeSlot].setMode (requestedMode); // vacío: arranca desde silencio
        // La respuesta al impulso de los filtros halfband dura unas 2 × latencia: pasado ese tiempo el saturador
        // entrante ya no "recuerda" que empezó vacío.
        warmUpRemaining = 2 * latency;
        modeFade.snap (0.0f);
        modeFade.setTarget (1.0f);
        switching = true;
    }

    void finishModeSwitch() noexcept
    {
        activeSlot = 1 - activeSlot;
        switching = false;
    }

    deepwater::dsp::LinearRamp inputGain;
    deepwater::dsp::LinearRamp outputGain;
    deepwater::dsp::EffectSwitch bypassFade;
    deepwater::dsp::EffectSwitch factorFade;
    deepwater::dsp::LinearRamp modeFade;
    ToneControls tone;
    bool processing = true;
    int requestedStages = 0;
    int activeStages = 0;
    int latency = 0;
    Mode requestedMode = Mode::tape;
    size_t activeSlot = 0;
    bool switching = false;
    int warmUpRemaining = 0;

    std::array<Channel, maxChannels> channels;
    size_t dryWriteIndex = 0;
};

} // namespace riptide::dsp
