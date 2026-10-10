#pragma once

#include <algorithm>
#include <array>

#include "deepwater/dsp/DcBlocker.h"
#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/FastMath.h"
#include "deepwater/dsp/LevelMeter.h"
#include "deepwater/dsp/Oversampler.h"
#include "deepwater/dsp/Smoothing.h"

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
};

// Fase 2: cadena de audio completa del saturador, sin JUCE (se prueba en tests/EngineTests.cpp):
//
//   entrada ─┬─ × input (drive) ─[medidor IN]─ ↑N ─ tanh ─ ↓N ─ filtro DC ─ × output ─┐
//            │                                                                     ├─ fundido ─ × fundido de factor ─[medidor OUT]─ salida
//            └──────────────── señal original retrasada lo mismo que el oversampling ──┘
//
// Input es el drive: decide con qué nivel llega la señal a la curva. tanh es casi recta para señales bajas (pasan
// limpias) y se dobla hacia ±1 con las altas: redondea los picos y crea armónicos impares. Su salida nunca pasa de
// ±1, así que el techo antes de Output es 0 dBFS.
//
// La curva se calcula con oversampling (↑N, ↓N: etapas halfband de la librería) para que los armónicos que pasan de
// Nyquist no se reflejen como aliasing. Los filtros retrasan la señal; el camino original del bypass se retrasa lo
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
    static constexpr double dcCutoffHz = 5.0;         // por debajo de lo audible; asienta en ~30 ms

    void prepare (double sampleRate, const EngineSettings& settings) noexcept
    {
        inputGain.setTime (gainRampSeconds, sampleRate);
        outputGain.setTime (gainRampSeconds, sampleRate);
        bypassFade.setTime (bypassFadeSeconds, sampleRate);
        factorFade.setTime (factorFadeSeconds, sampleRate);

        // Al preparar no hay nada que suavizar: se empieza directamente en los valores actuales.
        inputGain.snap (deepwater::dsp::dbToGain (settings.inputGainDb));
        outputGain.snap (deepwater::dsp::dbToGain (settings.outputGainDb));
        processing = ! settings.bypassed;
        bypassFade.snap (processing);

        requestedStages = activeStages = clampStages (settings.oversampling);
        factorFade.snap (true);

        for (auto& channel : channels)
        {
            channel.oversampler.prepare (sampleRate, activeStages);
            channel.dcBlocker.prepare (dcCutoffHz, sampleRate);
            channel.dryDelay.fill (0.0f);
            channel.input.prepare (sampleRate);
            channel.output.prepare (sampleRate);
        }
        dryWriteIndex = 0;
        latency = channels[0].oversampler.getLatency();
    }

    void setSettings (const EngineSettings& settings) noexcept
    {
        inputGain.setTarget (deepwater::dsp::dbToGain (settings.inputGainDb));
        outputGain.setTarget (deepwater::dsp::dbToGain (settings.outputGainDb));
        processing = ! settings.bypassed;
        requestedStages = clampStages (settings.oversampling);
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

            // Las rampas avanzan una vez por muestra para todos los canales: L y R reciben exactamente la misma ganancia.
            const float gainIn = inputGain.next();
            const float gainOut = outputGain.next();
            const float wet = bypassFade.next (processing); // 1 = procesado, 0 = original
            const size_t readIndex = (dryWriteIndex - static_cast<size_t> (latency)) & (dryDelaySize - 1);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& channel = channels[static_cast<size_t> (ch)];
                float& sample = audio[ch][i];

                channel.dryDelay[dryWriteIndex] = sample;
                const float dry = channel.dryDelay[readIndex];

                const float driven = sample * gainIn;
                channel.input.push (driven);
                const float saturated = channel.oversampler.process (driven, [] (float x) { return deepwater::dsp::fastTanh (x); });
                const float processed = channel.dcBlocker.process (saturated) * gainOut;

                // Fuera de los fundidos se copia la señal tal cual: con el bypass puesto la salida es la entrada
                // retrasada, bit a bit.
                float out = wet == 1.0f ? processed : wet == 0.0f ? dry : dry + (processed - dry) * wet;
                if (level != 1.0f)
                    out *= level;
                channel.output.push (out);
                sample = out;
            }
            dryWriteIndex = (dryWriteIndex + 1) & (dryDelaySize - 1);
        }
    }

    // Latencia actual en muestras del host (la que hay que informar al host).
    [[nodiscard]] int getLatency() const noexcept { return latency; }
    [[nodiscard]] int getActiveOversampling() const noexcept { return activeStages; }

    [[nodiscard]] LevelMeter& inputMeter (int channel) noexcept { return channels[static_cast<size_t> (channel)].input; }
    [[nodiscard]] LevelMeter& outputMeter (int channel) noexcept { return channels[static_cast<size_t> (channel)].output; }

private:
    // La latencia máxima (×16 a frecuencias bajas, donde todas las etapas usan el filtro más largo) es de unas 140
    // muestras: el retardo del camino original cabe en 256.
    static constexpr size_t dryDelaySize = 256;

    struct Channel
    {
        deepwater::dsp::Oversampler oversampler;
        deepwater::dsp::DcBlocker dcBlocker;
        std::array<float, dryDelaySize> dryDelay {};
        LevelMeter input, output;
    };

    static int clampStages (int index) noexcept { return std::clamp (index, 0, numOversamplingChoices - 1); }

    void applyRequestedFactor() noexcept
    {
        activeStages = requestedStages;
        for (auto& channel : channels)
            channel.oversampler.setNumStages (activeStages);
        // El retardo del camino original conserva su historia: solo cambia desde dónde se lee.
        latency = channels[0].oversampler.getLatency();
    }

    deepwater::dsp::LinearRamp inputGain;
    deepwater::dsp::LinearRamp outputGain;
    deepwater::dsp::EffectSwitch bypassFade;
    deepwater::dsp::EffectSwitch factorFade;
    bool processing = true;
    int requestedStages = 0;
    int activeStages = 0;
    int latency = 0;

    std::array<Channel, maxChannels> channels;
    size_t dryWriteIndex = 0;
};

} // namespace riptide::dsp
