#pragma once

#include <algorithm>
#include <array>

#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/LevelMeter.h"
#include "deepwater/dsp/Smoothing.h"

namespace riptide::dsp
{

using deepwater::dsp::LevelMeter;

// Lo que el motor necesita de los parámetros, en unidades reales. El procesador lo lee del APVTS una vez por bloque.
struct EngineSettings
{
    float inputGainDb = 0.0f;
    float outputGainDb = 0.0f;
    bool bypassed = false;
};

// Fase 1: cadena de audio completa del saturador, sin JUCE (se prueba en tests/EngineTests.cpp):
//
//   entrada ──┬── × input gain ──[medidor IN]── (aquí irá la saturación) ── × output gain ──┐
//             │                                                                           ├─ fundido ──[medidor OUT]── salida
//             └──────────────────────────── señal original (dry) ──────────────────────────┘
//
// Input gain decide cuánto "empuja" la señal contra la saturación (en Fase 2 será el drive principal); output gain
// devuelve el volumen a su sitio. Por eso el medidor IN mide DESPUÉS del input gain: muestra el nivel con el que la
// señal llega al saturador, que es el que importa para saber cuánto va a saturar.
//
// Bypass: en vez de conmutar de golpe (un salto en la forma de onda = clic) se hace un fundido cruzado de 20 ms entre
// la señal procesada y la original. Cuando los procesos agreguen latencia (oversampling, Fase 2) la señal original
// tendrá que retrasarse lo mismo antes de este fundido.
class Engine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double gainRampSeconds = 0.02;   // 20 ms: girar la perilla no hace "zipper" y sigue siendo ágil
    static constexpr double bypassFadeSeconds = 0.02;

    void prepare (double sampleRate, const EngineSettings& settings) noexcept
    {
        inputGain.setTime (gainRampSeconds, sampleRate);
        outputGain.setTime (gainRampSeconds, sampleRate);
        bypassFade.setTime (bypassFadeSeconds, sampleRate);

        // Al preparar no hay nada que suavizar: se empieza directamente en los valores actuales.
        inputGain.snap (deepwater::dsp::dbToGain (settings.inputGainDb));
        outputGain.snap (deepwater::dsp::dbToGain (settings.outputGainDb));
        processing = ! settings.bypassed;
        bypassFade.snap (processing);

        for (auto& meter : inputMeters)
            meter.prepare (sampleRate);
        for (auto& meter : outputMeters)
            meter.prepare (sampleRate);
    }

    void setSettings (const EngineSettings& settings) noexcept
    {
        inputGain.setTarget (deepwater::dsp::dbToGain (settings.inputGainDb));
        outputGain.setTarget (deepwater::dsp::dbToGain (settings.outputGainDb));
        processing = ! settings.bypassed;
    }

    // Procesa en el sitio. Con más de maxChannels canales, los de más se dejan intactos.
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            // Las rampas avanzan una vez por muestra para todos los canales: L y R reciben exactamente la misma ganancia.
            const float gainIn = inputGain.next();
            const float gainOut = outputGain.next();
            const float wet = bypassFade.next (processing); // 1 = procesado, 0 = original

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float& sample = channels[ch][i];
                const float dry = sample;

                const float driven = dry * gainIn;
                inputMeters[static_cast<size_t> (ch)].push (driven);
                const float processed = driven * gainOut;

                // Fuera del fundido se copia la señal tal cual: con el bypass puesto la salida es la entrada bit a bit.
                const float out = wet == 1.0f ? processed : wet == 0.0f ? dry : dry + (processed - dry) * wet;
                outputMeters[static_cast<size_t> (ch)].push (out);
                sample = out;
            }
        }
    }

    [[nodiscard]] LevelMeter& inputMeter (int channel) noexcept { return inputMeters[static_cast<size_t> (channel)]; }
    [[nodiscard]] LevelMeter& outputMeter (int channel) noexcept { return outputMeters[static_cast<size_t> (channel)]; }

private:
    deepwater::dsp::LinearRamp inputGain;
    deepwater::dsp::LinearRamp outputGain;
    deepwater::dsp::EffectSwitch bypassFade;
    bool processing = true;

    std::array<LevelMeter, maxChannels> inputMeters;
    std::array<LevelMeter, maxChannels> outputMeters;
};

} // namespace riptide::dsp
