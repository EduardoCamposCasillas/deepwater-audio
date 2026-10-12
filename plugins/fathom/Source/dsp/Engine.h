#pragma once

#include <algorithm>
#include <array>

#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/Smoothing.h"
#include "dsp/Band.h"

namespace fathom::dsp
{

// Cadena de audio completa del EQ, sin JUCE (se prueba en tests/EngineTests.cpp):
//
//   entrada ──┬── banda 1 ── banda 2 ── ... ── banda 24 ── × ganancia de salida ──┐
//             │                                                                   ├─ fundido de bypass ── salida
//             └──────────────────── señal original (dry) ─────────────────────────┘
//
// Las bandas van en serie: sus respuestas se multiplican (los dB se suman), que es lo que dibuja la curva. Las
// apagadas no procesan nada. La ganancia de salida va con una rampa lineal de 20 ms (LinearRamp llega exacto: en 0 dB
// la señal pasa bit a bit). El bypass también se funde en 20 ms: con bandas que suben o bajan 12 dB, conmutar de golpe
// sería un clic.
class Engine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int numBands = 24;
    static constexpr double bypassFadeSeconds = 0.02;
    static constexpr double gainRampSeconds = 0.02;
    static constexpr float minOutputDb = -24.0f;
    static constexpr float maxOutputDb = 24.0f;

    using BandArray = std::array<BandSettings, numBands>;

    void prepare (double sampleRate, bool bypassed, float outputDb, const BandArray& bandSettings = {}) noexcept
    {
        for (size_t b = 0; b < bands.size(); ++b)
            bands[b].prepare (sampleRate, bandSettings[b]);
        bypassFade.setTime (bypassFadeSeconds, sampleRate);
        processing = ! bypassed;
        bypassFade.snap (processing);
        outputGain.setTime (gainRampSeconds, sampleRate);
        outputGain.snap (deepwater::dsp::dbToGain (std::clamp (outputDb, minOutputDb, maxOutputDb)));
    }

    void setBypassed (bool bypassed) noexcept { processing = ! bypassed; }
    void setBand (int index, const BandSettings& settings) noexcept { bands[static_cast<size_t> (index)].setSettings (settings); }
    void setOutputDb (float db) noexcept { outputGain.setTarget (deepwater::dsp::dbToGain (std::clamp (db, minOutputDb, maxOutputDb))); }

    // Procesa en el sitio. Con más de maxChannels canales, los de más se dejan intactos.
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);

        // Por tramos de 'chunkSize' muestras: cada banda recorre el tramo entero antes de pasar a la siguiente (sus
        // coeficientes y su estado se quedan "a mano" en la caché), y la copia de la señal original cabe en un buffer
        // fijo (sin reservar memoria en el audio).
        for (int start = 0; start < numSamples; start += chunkSize)
        {
            const int count = std::min (chunkSize, numSamples - start);
            for (int ch = 0; ch < numChannels; ++ch)
                std::copy_n (channels[ch] + start, count, dry[static_cast<size_t> (ch)].begin());

            for (auto& band : bands)
            {
                if (band.isIdle())
                    continue;
                for (int i = 0; i < count; ++i)
                {
                    band.advance();
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        float& sample = channels[ch][start + i];
                        sample = band.process (ch, sample);
                    }
                }
            }

            for (int i = 0; i < count; ++i)
            {
                const float wet = bypassFade.next (processing); // 1 = procesado, 0 = original
                const float gain = outputGain.next();
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float& sample = channels[ch][start + i];
                    const float original = dry[static_cast<size_t> (ch)][static_cast<size_t> (i)];
                    const float processed = gain == 1.0f ? sample : sample * gain;

                    // Fuera del fundido se copia la señal tal cual: con o sin bypass, con las bandas en 0 dB (o
                    // apagadas) y la salida en 0 dB, la salida es la entrada bit a bit. En bypass las bandas siguen
                    // corriendo: al volver no arrancan con un estado viejo.
                    sample = wet == 1.0f ? processed : wet == 0.0f ? original : original + (processed - original) * wet;
                }
            }
        }
    }

    [[nodiscard]] const Band& getBand (int index) const noexcept { return bands[static_cast<size_t> (index)]; }

private:
    static constexpr int chunkSize = 64;

    std::array<Band, numBands> bands;
    std::array<std::array<float, chunkSize>, maxChannels> dry {};
    deepwater::dsp::EffectSwitch bypassFade;
    deepwater::dsp::LinearRamp outputGain;
    bool processing = true;
};

} // namespace fathom::dsp
