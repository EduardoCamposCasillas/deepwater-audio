#pragma once

#include <algorithm>

#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/Smoothing.h"

namespace fathom::dsp
{

// Fase 1: cadena de audio completa del EQ, sin JUCE (se prueba en tests/EngineTests.cpp):
//
//   entrada ──┬── (Fase 2: bandas del EQ) ── × ganancia de salida ──┐
//             │                                                   ├─ fundido de bypass ── salida
//             └──────────────── señal original (dry) ─────────────┘
//
// La ganancia de salida va con una rampa lineal de 20 ms (LinearRamp llega exacto: en 0 dB la señal pasa bit a bit).
// El bypass también se funde en 20 ms: con bandas que suben o bajan 12 dB, conmutar de golpe sería un clic.
class Engine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double bypassFadeSeconds = 0.02;
    static constexpr double gainRampSeconds = 0.02;
    static constexpr float minOutputDb = -24.0f;
    static constexpr float maxOutputDb = 24.0f;

    void prepare (double sampleRate, bool bypassed, float outputDb) noexcept
    {
        bypassFade.setTime (bypassFadeSeconds, sampleRate);
        processing = ! bypassed;
        bypassFade.snap (processing);
        outputGain.setTime (gainRampSeconds, sampleRate);
        outputGain.snap (deepwater::dsp::dbToGain (std::clamp (outputDb, minOutputDb, maxOutputDb)));
    }

    void setBypassed (bool bypassed) noexcept { processing = ! bypassed; }
    void setOutputDb (float db) noexcept { outputGain.setTarget (deepwater::dsp::dbToGain (std::clamp (db, minOutputDb, maxOutputDb))); }

    // Procesa en el sitio. Con más de maxChannels canales, los de más se dejan intactos.
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            const float wet = bypassFade.next (processing); // 1 = procesado, 0 = original
            const float gain = outputGain.next();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float& sample = channels[ch][i];
                const float dry = sample;
                const float processed = gain == 1.0f ? dry : dry * gain; // Fase 2: antes, las bandas

                // Fuera del fundido se copia la señal tal cual: con o sin bypass, sin bandas y en 0 dB, la salida es la
                // entrada bit a bit.
                sample = wet == 1.0f ? processed : wet == 0.0f ? dry : dry + (processed - dry) * wet;
            }
        }
    }

private:
    deepwater::dsp::EffectSwitch bypassFade;
    deepwater::dsp::LinearRamp outputGain;
    bool processing = true;
};

} // namespace fathom::dsp
