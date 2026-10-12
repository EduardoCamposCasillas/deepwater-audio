#pragma once

#include <algorithm>

#include "deepwater/dsp/Smoothing.h"

namespace ebb::dsp
{

// Fase 1: cadena de audio completa del ducker, sin JUCE (se prueba en tests/EngineTests.cpp):
//
//   entrada ──┬── (Fase 2: × curva de ducking) ──┐
//             │                                  ├─ fundido de bypass ── salida
//             └────── señal original (dry) ──────┘
//
// Por ahora la señal pasa intacta. El bypass ya hace el fundido de 20 ms que hará falta cuando haya curva: conmutar de
// golpe entre una señal atenuada y la original sería un salto en la forma de onda (un clic).
class Engine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double bypassFadeSeconds = 0.02;

    void prepare (double sampleRate, bool bypassed) noexcept
    {
        bypassFade.setTime (bypassFadeSeconds, sampleRate);
        processing = ! bypassed;
        bypassFade.snap (processing);
    }

    void setBypassed (bool bypassed) noexcept { processing = ! bypassed; }

    // Procesa en el sitio. Con más de maxChannels canales, los de más se dejan intactos.
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            const float wet = bypassFade.next (processing); // 1 = procesado, 0 = original

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float& sample = channels[ch][i];
                const float dry = sample;
                const float processed = dry; // Fase 2: aquí se multiplicará por la curva

                // Fuera del fundido se copia la señal tal cual: con o sin bypass, la salida es la entrada bit a bit.
                sample = wet == 1.0f ? processed : wet == 0.0f ? dry : dry + (processed - dry) * wet;
            }
        }
    }

private:
    deepwater::dsp::EffectSwitch bypassFade;
    bool processing = true;
};

} // namespace ebb::dsp
