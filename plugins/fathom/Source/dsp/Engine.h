#pragma once

#include <algorithm>
#include <array>
#include <optional>

#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/Smoothing.h"
#include "dsp/AutoGain.h"
#include "dsp/Band.h"
#include "dsp/Solo.h"

namespace fathom::dsp
{

// Cadena de audio completa del EQ, sin JUCE (se prueba en tests/EngineTests.cpp):
//
//   entrada ──┬── banda 1 ── ... ── banda 24 ── × salida × auto gain × balance × polaridad ──┐
//             │                                                                              ├─ solo ─┐
//             ├────────────── zona de la banda en solo (filtro sobre la original) ───────────┘        ├─ bypass ── salida
//             └──────────────────────────── señal original (dry) ─────────────────────────────────────┘
//
// Las bandas van en serie: sus respuestas se multiplican (los dB se suman), que es lo que dibuja la curva. Las
// apagadas no procesan nada. La escala de ganancia multiplica la ganancia de todas antes de que lleguen a las bandas
// (que ya la suavizan en dB). Todo lo que va después de las bandas es un solo multiplicador por canal hecho de rampas
// lineales de 20 ms (LinearRamp llega exacto: si todas valen 1, la señal pasa bit a bit). El solo y el bypass se funden
// en 20 ms: con bandas que suben o bajan 12 dB, conmutar de golpe sería un clic.
//
// Cada banda actúa en su canal (Fase 8: estéreo, L, R, mid o side; ver Band.h). Siguen en serie: una banda mid después
// de una izquierda filtra el centro de lo que ya dejó la izquierda.
class Engine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int numBands = 24;
    static constexpr double bypassFadeSeconds = 0.02;
    static constexpr double gainRampSeconds = 0.02;
    static constexpr double soloFadeSeconds = 0.02;
    static constexpr float minOutputDb = -24.0f;
    static constexpr float maxOutputDb = 24.0f;
    static constexpr float maxGainScale = 2.0f; // 200 %

    using BandArray = std::array<BandSettings, numBands>;

    // Los ajustes de salida que se pidieron antes (setGainScale, setPan...) arrancan ya en su sitio, sin rampa.
    void prepare (double newSampleRate, bool bypassed, float outputDb, const BandArray& bandSettings = {}) noexcept
    {
        sampleRate = newSampleRate;
        autoGain.prepare (sampleRate);
        for (size_t b = 0; b < bands.size(); ++b)
        {
            sounding[b] = scaled (bandSettings[b]);
            bands[b].prepare (sampleRate, sounding[b]);
        }
        bypassFade.setTime (bypassFadeSeconds, sampleRate);
        processing = ! bypassed;
        bypassFade.snap (processing);
        for (auto* ramp : { &outputGain, &autoGainRamp, &panLeft, &panRight, &polarity })
            ramp->setTime (gainRampSeconds, sampleRate);
        outputGain.snap (deepwater::dsp::dbToGain (std::clamp (outputDb, minOutputDb, maxOutputDb)));
        autoGainRamp.snap (autoGainTarget());
        panLeft.snap (std::min (1.0f, 1.0f - pan));
        panRight.snap (std::min (1.0f, 1.0f + pan));
        polarity.snap (inverted ? -1.0f : 1.0f);
        soloFade.setTime (soloFadeSeconds, sampleRate);
        soloFade.snap (false);
        soloOn = false;
        soloPlaying = -1;
    }

    void setBypassed (bool bypassed) noexcept { processing = ! bypassed; }
    void setOutputDb (float db) noexcept { outputGain.setTarget (deepwater::dsp::dbToGain (std::clamp (db, minOutputDb, maxOutputDb))); }

    // Los ajustes de una banda tal como los piden los parámetros (sin la escala de ganancia: la pone el motor).
    void setBand (int index, const BandSettings& settings) noexcept
    {
        const auto b = static_cast<size_t> (index);
        sounding[b] = scaled (settings);
        bands[b].setSettings (sounding[b]);
    }

    // 1 = las ganancias tal cual, 0.5 = la mitad de los dB, 0 = plano, 2 = el doble. Se aplica en la siguiente llamada a
    // setBand (el procesador lo pide antes de pasar las bandas).
    void setGainScale (float scale) noexcept { gainScale = std::clamp (scale, 0.0f, maxGainScale); }

    // Balance de −1 (izquierda) a +1 (derecha): el centro deja los dos lados en 0 dB y al girar solo baja el contrario
    // (en una pista estéreo "panear" es eso; un paneo de potencia constante subiría el lado al que se gira). En mono no
    // hace nada.
    void setPan (float newPan) noexcept
    {
        pan = std::clamp (newPan, -1.0f, 1.0f);
        panLeft.setTarget (std::min (1.0f, 1.0f - pan));
        panRight.setTarget (std::min (1.0f, 1.0f + pan));
    }

    // Invertir la polaridad (× −1). Sin clic: el factor pasa de +1 a −1 en 20 ms (por 0: un hueco cortísimo).
    void setPhaseInverted (bool shouldInvert) noexcept
    {
        inverted = shouldInvert;
        polarity.setTarget (inverted ? -1.0f : 1.0f);
    }

    void setAutoGain (bool on) noexcept { autoGainOn = on; }

    // La banda (0-23) cuya zona se escucha, o −1 = sin solo. Las formas sin zona (los tilts) no hacen solo.
    void setSolo (int band) noexcept { soloRequest = band >= 0 && band < numBands ? band : -1; }

    // Lo último que compensó el auto gain (0 si está apagado). Para mostrarlo en la GUI.
    [[nodiscard]] float getAutoGainDb() const noexcept { return autoGainOn ? autoGainDb : 0.0f; }

    // Procesa en el sitio. Con más de maxChannels canales, los de más se dejan intactos.
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);
        prepareBlock (numChannels);

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
                if (numChannels == 2)
                    for (int i = start; i < start + count; ++i)
                    {
                        band.advance();
                        band.processStereo (channels[0][i], channels[1][i]);
                    }
                else
                    for (int i = start; i < start + count; ++i)
                    {
                        band.advance();
                        band.processMono (channels[0][i]);
                    }
            }

            for (int i = 0; i < count; ++i)
            {
                const float common = outputGain.next() * autoGainRamp.next() * polarity.next();
                const std::array<float, maxChannels> gains { numChannels == 2 ? common * panLeft.next() : common,
                                                             numChannels == 2 ? common * panRight.next() : common };
                const float solo = advanceSolo();
                const float wet = bypassFade.next (processing); // 1 = procesado, 0 = original
                const auto at = static_cast<size_t> (i);
                // La zona del solo, en el canal de la banda (una banda side se escucha solo en el side).
                std::array<float, maxChannels> zone {};
                if (solo > 0.0f)
                {
                    if (numChannels == 2)
                        zone = soloBand.zoneStereo (dry[0][at], dry[1][at]);
                    else
                        zone[0] = soloBand.zoneMono (dry[0][at]);
                }
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float& sample = channels[ch][start + i];
                    const float original = dry[static_cast<size_t> (ch)][at];
                    const float gain = gains[static_cast<size_t> (ch)];
                    float processed = gain == 1.0f ? sample : sample * gain;
                    if (solo > 0.0f)
                    {
                        const float z = zone[static_cast<size_t> (ch)];
                        processed = solo == 1.0f ? z : processed + (z - processed) * solo;
                    }

                    // Fuera del fundido se copia la señal tal cual: con o sin bypass, con las bandas en 0 dB (o
                    // apagadas) y la salida en 0 dB, la salida es la entrada bit a bit. En bypass las bandas siguen
                    // corriendo: al volver no arrancan con un estado viejo.
                    sample = wet == 1.0f ? processed : wet == 0.0f ? original : original + (processed - original) * wet;
                }
            }
        }
    }

    [[nodiscard]] const Band& getBand (int index) const noexcept { return bands[static_cast<size_t> (index)]; }

    // True mientras se oye (o se está fundiendo) la zona de una banda.
    [[nodiscard]] bool isSoloing() const noexcept { return ! soloFade.isSilent (soloOn); }

private:
    static constexpr int chunkSize = 64;

    [[nodiscard]] BandSettings scaled (BandSettings settings) const noexcept
    {
        settings.gainDb *= gainScale;
        return settings;
    }

    [[nodiscard]] float autoGainTarget() noexcept
    {
        if (! autoGainOn)
            return 1.0f;
        for (int b = 0; b < numBands; ++b)
            autoGain.setBand (b, sounding[static_cast<size_t> (b)]);
        autoGainDb = autoGain.compensationDb();
        return deepwater::dsp::dbToGain (autoGainDb); // 0 dB → 1 exacto
    }

    // Una vez por bloque: el auto gain con los ajustes nuevos y qué zona debería sonar en solo.
    void prepareBlock (int numChannels) noexcept
    {
        autoGain.setMono (numChannels == 1);
        autoGainRamp.setTarget (autoGainTarget());

        soloNext.reset();
        if (soloRequest >= 0)
            soloNext = soloZoneFor (sounding[static_cast<size_t> (soloRequest)]);
        // La misma banda y la misma clase de filtro: sus ajustes se suavizan solos (la banda del solo tiene sus rampas).
        // Otra banda, otra clase (pasa banda → pasa bajos) u otro canal: fundido a la mezcla, cambio en silencio y fundido al solo.
        if (soloNext && soloPlaying == soloRequest && soloNext->shape == soloShape && soloNext->slopeIndex == soloSlope
            && soloNext->placement == soloPlacement)
        {
            soloBand.setSettings (*soloNext);
            soloOn = true;
        }
        else
            soloOn = false;
    }

    // Una vez por muestra: cuánto del solo se oye (0 = la mezcla, 1 = solo la zona).
    float advanceSolo() noexcept
    {
        if (! soloOn && soloNext && soloFade.isSilent (false))
        {
            // Lo de antes ya se fundió: arranca la zona nueva con el filtro en limpio y sus ajustes en su sitio.
            soloBand.prepare (sampleRate, *soloNext);
            soloPlaying = soloRequest;
            soloShape = soloNext->shape;
            soloSlope = soloNext->slopeIndex;
            soloPlacement = soloNext->placement;
            soloOn = true;
        }
        const float amount = soloFade.next (soloOn);
        if (amount > 0.0f)
            soloBand.advance();
        else if (! soloOn)
            soloPlaying = -1;
        return amount;
    }

    double sampleRate = 44100.0;
    std::array<Band, numBands> bands;
    BandArray sounding {}; // los ajustes con que suena cada banda (con la escala de ganancia)
    std::array<std::array<float, chunkSize>, maxChannels> dry {};
    deepwater::dsp::EffectSwitch bypassFade;
    deepwater::dsp::LinearRamp outputGain, autoGainRamp, panLeft, panRight, polarity;
    bool processing = true;
    float gainScale = 1.0f, pan = 0.0f;
    bool inverted = false;

    AutoGain autoGain;
    bool autoGainOn = false;
    float autoGainDb = 0.0f;

    Band soloBand; // la zona en solo: una banda más (pasa banda, pasa bajos o pasa altos) con sus rampas
    deepwater::dsp::EffectSwitch soloFade;
    std::optional<BandSettings> soloNext;
    int soloRequest = -1, soloPlaying = -1;
    Shape soloShape = Shape::bandPass;
    int soloSlope = 1;
    Placement soloPlacement = Placement::stereo;
    bool soloOn = false;
};

} // namespace fathom::dsp
