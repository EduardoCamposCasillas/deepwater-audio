#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <optional>

#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/Smoothing.h"
#include "dsp/AnalogColor.h"
#include "dsp/AutoGain.h"
#include "dsp/Band.h"
#include "dsp/Dynamics.h"
#include "dsp/PhaseStage.h"
#include "dsp/Solo.h"
#include "dsp/SpectralDynamics.h"

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
//
// Las bandas dinámicas (Fase 9) tienen además su detector (Dynamics.h), que escucha lo que llega a la banda (lo que ya
// dejaron las anteriores) o el sidechain, y le mueve la ganancia. El auto gain y el solo miran la ganancia propia de cada
// banda, no la dinámica: compensar cada movimiento de la banda haría "respirar" el volumen.
//
// Los modos de fase (Fase 10, PhaseStage.h) van entre las bandas y la salida: con cero latencia no hacen nada; con fase
// natural, un FIR corto corrige lo que dejaron las bandas; con fase lineal, las bandas no corren y un FIR largo filtra la
// entrada. La señal "original" del bypass, del solo y de la mezcla sale de ahí retrasada lo mismo que la procesada.
// Con fase lineal los detectores dinámicos escuchan la entrada alineada con el centro del FIR (no lo que deja cada banda).
//
// Las bandas espectrales (Fase 11, SpectralDynamics.h) dejan su parte estática en las bandas (o en el FIR) y su dinámica
// va a una etapa STFT después de los modos de fase: baja cada frecuencia de su zona según su propio exceso. Mientras
// haya alguna, retrasa todo (también la original) N muestras más.
//
// El color analógico (Fase 13, AnalogColor.h) va al final de lo procesado, antes de la salida: una curva suave con
// sobremuestreo, como los amplificadores de un EQ de hardware (subir una banda también empuja el color). En LIMPIO no
// hace nada; con color retrasa todo (también la original) la latencia del sobremuestreo.
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

    // Los ajustes de salida que se pidieron antes (setGainScale, setPan, setPhaseMode...) arrancan ya en su sitio, sin
    // rampa. Reserva la memoria de los modos de fase: fuera del hilo de audio.
    void prepare (double newSampleRate, bool bypassed, float outputDb, const BandArray& bandSettings = {})
    {
        sampleRate = newSampleRate;
        autoGain.prepare (sampleRate);
        for (size_t b = 0; b < bands.size(); ++b)
        {
            sounding[b] = scaled (bandSettings[b]);
            bands[b].prepare (sampleRate, sounding[b]);
            dynamics[b].prepare (sampleRate, sounding[b]);
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
        // Los modos de fase: con la memoria reservada si ya se usan (o ya se usaron), si este modo la necesita o si es
        // un render offline (ahí no hay hilo de mensajes a tiempo: todo tiene que estar listo y dar siempre lo mismo).
        offered.reset();
        offerState.store (offerIdle);
        memoryWanted.store (false);
        const bool isRealtime = realtime.load (std::memory_order_relaxed);
        const bool withMemory = phase->hasMemory() || phaseMode != PhaseMode::zeroLatency || ! isRealtime;
        phase->prepare (sampleRate, phaseMode, linearQuality, isRealtime, sounding, withMemory);
        spectral->prepare (sampleRate, sounding);
        color.prepare (sampleRate, colorMode);
    }

    // Hilo de mensajes (el procesador lo llama con un temporizador): si se eligió fase natural o lineal y la memoria no
    // estaba reservada, prepara una etapa nueva con memoria y se la ofrece al audio, que la cambia por la suya en el
    // próximo bloque (las dos están en cero latencia: no se nota); la vieja vuelve aquí y se destruye aquí. Sin nada
    // pendiente, no hace nada.
    void providePhaseMemory()
    {
        if (offerState.load (std::memory_order_acquire) == offerReturned)
        {
            offered.reset();
            offerState.store (offerIdle, std::memory_order_release);
        }
        if (! memoryWanted.load (std::memory_order_acquire) || offerState.load (std::memory_order_acquire) != offerIdle)
            return;
        auto fresh = std::make_unique<PhaseStage>();
        fresh->prepare (sampleRate, PhaseMode::zeroLatency, defaultLinearQuality, realtime.load (std::memory_order_relaxed), {}, true);
        offered = std::move (fresh);
        memoryWanted.store (false, std::memory_order_relaxed);
        offerState.store (offerMade, std::memory_order_release);
    }

    void setBypassed (bool bypassed) noexcept { processing = ! bypassed; }
    void setOutputDb (float db) noexcept { outputGain.setTarget (deepwater::dsp::dbToGain (std::clamp (db, minOutputDb, maxOutputDb))); }

    // Los ajustes de una banda tal como los piden los parámetros (sin la escala de ganancia: la pone el motor).
    void setBand (int index, const BandSettings& settings) noexcept
    {
        const auto b = static_cast<size_t> (index);
        sounding[b] = scaled (settings);
        bands[b].setSettings (sounding[b]);
        dynamics[b].setSettings (sounding[b]);
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

    // Fase 10: el modo de fase y la calidad de la fase lineal (0-4). Se aplica en las bandas siguientes (la natural usa
    // siempre la respuesta analógica) y, si cambia el modo, empieza la transición (ver PhaseStage.h).
    void setPhaseMode (PhaseMode mode, int quality) noexcept
    {
        phaseMode = mode;
        linearQuality = std::clamp (quality, 0, numLinearQualities - 1);
        phase->setTarget (phaseMode, linearQuality);
    }

    // true = tiempo real: los FIR se diseñan en un hilo aparte (lo que se pida antes de prepare decide si se arranca).
    // false = offline (render, tests): se diseñan en el audio y el resultado no depende del tiempo.
    void setRealtime (bool isRealtime) noexcept
    {
        realtime.store (isRealtime, std::memory_order_relaxed);
        phase->setRealtime (isRealtime);
    }

    // Fase 13: el color analógico (se aplica en el bloque siguiente; antes de prepare, decide con qué se arranca).
    void setColor (ColorMode mode) noexcept
    {
        colorMode = mode;
        color.setMode (mode);
    }

    // La latencia del modo pedido, para informarla al host (0 con cero latencia), más la de la etapa espectral si alguna
    // banda la usa (Fase 11) y la del color analógico (Fase 13).
    [[nodiscard]] int getLatencySamples() const noexcept
    {
        return phase->getLatencySamples() + spectral->getLatencySamples() + color.getLatencySamples();
    }

    // La latencia que agrega cualquier color a la frecuencia preparada (para la GUI).
    [[nodiscard]] int getColorLatency() const noexcept { return color.getColorLatency(); }

    // Hilo de audio (o tests): si la memoria de los modos de fase ya está reservada.
    [[nodiscard]] bool hasPhaseMemory() const noexcept { return phase->hasMemory(); }

    // La banda (0-23) cuya zona se escucha, o −1 = sin solo. Las formas sin zona (los tilts) no hacen solo.
    void setSolo (int band) noexcept { soloRequest = band >= 0 && band < numBands ? band : -1; }

    // Lo último que compensó el auto gain (0 si está apagado). Para mostrarlo en la GUI.
    [[nodiscard]] float getAutoGainDb() const noexcept { return autoGainOn ? autoGainDb : 0.0f; }

    // Procesa en el sitio. Con más de maxChannels canales, los de más se dejan intactos. 'sidechain' (opcional, solo se
    // lee) es lo que escuchan las bandas dinámicas con el detector externo; sin él, escuchan silencio.
    void process (float* const* channels, int numChannels, int numSamples, const float* const* sidechain = nullptr,
                  int sidechainChannels = 0) noexcept
    {
        if (sidechain == nullptr)
            sidechainChannels = 0;
        numChannels = std::min (numChannels, maxChannels);
        takeOfferedPhase();
        prepareBlock (numChannels);

        // Por tramos de 'chunkSize' muestras: cada banda recorre el tramo entero antes de pasar a la siguiente (sus
        // coeficientes y su estado se quedan "a mano" en la caché), y la copia de la señal original cabe en un buffer
        // fijo (sin reservar memoria en el audio).
        for (int start = 0; start < numSamples; start += chunkSize)
        {
            const int count = std::min (chunkSize, numSamples - start);
            for (int ch = 0; ch < numChannels; ++ch)
                std::copy_n (channels[ch] + start, count, dry[static_cast<size_t> (ch)].begin());

            // Las bandas IIR vuelven después de la fase lineal: arrancan en limpio, con sus ajustes en su sitio (el
            // modo nuevo no se oye hasta que pasa su transitorio).
            auto& stage = *phase;
            if (stage.takeIirRestart())
                for (size_t b = 0; b < bands.size(); ++b)
                {
                    bands[b].prepare (sampleRate, sounding[b]);
                    bands[b].setDynamicGain (dynamics[b].getGainDb());
                }
            const bool aligned = stage.alignedDetectors();
            if (stage.needsIir())
                processBands (channels, numChannels, start, count, sidechain, sidechainChannels, ! aligned);

            const std::array<const float*, maxChannels> dryPointers { dry[0].data(), dry[1].data() };
            const std::array<float*, maxChannels> io { channels[0] + start, channels[numChannels - 1] + start };
            std::array<const float*, maxChannels> sidechainChunk {};
            for (int ch = 0; ch < sidechainChannels; ++ch)
                sidechainChunk[static_cast<size_t> (ch)] = sidechain[ch] + start;
            stage.process (dryPointers.data(), io.data(), numChannels, count, sidechainChunk.data(), sidechainChannels);
            if (aligned)
                feedAlignedDetectors (numChannels, count, sidechainChannels);
            // Fase 11: la dinámica espectral, sobre lo que sale de los modos de fase (y la original, retrasada igual).
            const std::array<const float*, maxChannels> phaseDry { stage.dryOutput (0), stage.dryOutput (numChannels - 1) };
            spectral->process (io.data(), phaseDry.data(), numChannels, count, sidechainChunk.data(), sidechainChannels,
                               stage.getAudibleLatency());
            const std::array<const float*, maxChannels> spectralDry { spectral->dryOutput (0), spectral->dryOutput (numChannels - 1) };
            // Fase 13: el color analógico, sobre todo lo procesado (y la original, retrasada igual).
            color.process (io.data(), spectralDry.data(), numChannels, count);
            const std::array<const float*, maxChannels> original { color.dryOutput (0), color.dryOutput (numChannels - 1) };

            for (int i = 0; i < count; ++i)
            {
                const float common = outputGain.next() * autoGainRamp.next() * polarity.next();
                const std::array<float, maxChannels> gains { numChannels == 2 ? common * panLeft.next() : common,
                                                             numChannels == 2 ? common * panRight.next() : common };
                const float solo = advanceSolo();
                const float wet = bypassFade.next (processing); // 1 = procesado, 0 = original
                // La zona del solo, en el canal de la banda (una banda side se escucha solo en el side), sobre la
                // original ya retrasada como la salida.
                std::array<float, maxChannels> zone {};
                if (solo > 0.0f)
                {
                    if (numChannels == 2)
                        zone = soloBand.zoneStereo (original[0][i], original[1][i]);
                    else
                        zone[0] = soloBand.zoneMono (original[0][i]);
                }
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float& sample = channels[ch][start + i];
                    const float originalSample = original[static_cast<size_t> (ch)][i];
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
                    sample = wet == 1.0f   ? processed
                             : wet == 0.0f ? originalSample
                                           : originalSample + (processed - originalSample) * wet;
                }
            }
        }
    }

    [[nodiscard]] const Band& getBand (int index) const noexcept { return bands[static_cast<size_t> (index)]; }

    // Fase 9, para la GUI: lo que la dinámica le suma ahora a la ganancia de una banda (dB, ya con la escala), el nivel
    // de su zona y el umbral que usa (con el automático, el nivel típico).
    [[nodiscard]] const BandDynamics& getDynamics (int index) const noexcept { return dynamics[static_cast<size_t> (index)]; }

    // Lo mismo para el medidor, venga de la dinámica normal o de la espectral (Fase 11: el bin que más se mueve, el más
    // fuerte del centro de la zona y el umbral).
    [[nodiscard]] SpectralStage::Readout getDynamicReadout (int index) const noexcept
    {
        const auto b = static_cast<size_t> (index);
        if (isSpectral (sounding[b]))
            return spectral->getReadout (index);
        return { dynamics[b].getGainDb(), dynamics[b].getLevelDb(), dynamics[b].getThresholdDb() };
    }

    // Fase 11, hilo de la GUI (un solo lector): lo que mueve ahora cada banda espectral, en la grilla de la GUI.
    bool readSpectralSnapshot (SpectralStage::Snapshot& out) noexcept { return spectral->readSnapshot (out); }

    // True mientras se oye (o se está fundiendo) la zona de una banda.
    [[nodiscard]] bool isSoloing() const noexcept { return ! soloFade.isSilent (soloOn); }

private:
    static constexpr int chunkSize = 64;

    // La escala de ganancia también escala el rango dinámico: al 50 % la banda se mueve la mitad.
    // Con fase natural las bandas son siempre las analógicas (matched): el FIR corrige desde ahí.
    [[nodiscard]] BandSettings scaled (BandSettings settings) const noexcept
    {
        settings.gainDb *= gainScale;
        settings.dynamics.rangeDb *= gainScale;
        settings.analog = settings.analog || phaseMode == PhaseMode::natural;
        return settings;
    }

    // Las 24 bandas IIR en un tramo, en el sitio. 'detect' = los detectores dinámicos escuchan lo que llega a cada
    // banda (con fase lineal los alimenta feedAlignedDetectors y aquí solo se usa su ganancia).
    void processBands (float* const* channels, int numChannels, int start, int count, const float* const* sidechain,
                       int sidechainChannels, bool detect) noexcept
    {
        for (size_t b = 0; b < bands.size(); ++b)
        {
            auto& band = bands[b];
            auto& dynamic = dynamics[b];
            if (band.isIdle())
            {
                if (dynamic.isRunning())
                {
                    dynamic.stop();
                    band.setDynamicGain (0.0f);
                }
                continue;
            }
            if (dynamic.isRunning() && detect)
                processDynamic (band, dynamic, channels, numChannels, start, count, sidechain, sidechainChannels);
            else
            {
                if (dynamic.isRunning())
                    band.setDynamicGain (dynamic.getGainDb());
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
        }
    }

    // Una muestra a un detector: la de la banda o la del sidechain (uno mono va a los dos lados; con la pista mono, el
    // promedio de los dos lados del sidechain). Devuelve true cuando tiene una ganancia nueva.
    static bool feed (BandDynamics& dynamic, int numChannels, float left, float right, const float* const* sidechain,
                      int sidechainChannels, int i) noexcept
    {
        if (dynamic.listensToSidechain())
        {
            const int last = std::max (0, sidechainChannels - 1);
            if (numChannels == 2)
                return sidechainChannels > 0 ? dynamic.feedStereo (sidechain[0][i], sidechain[last][i]) : dynamic.feedStereo (0.0f, 0.0f);
            return dynamic.feedMono (sidechainChannels == 0   ? 0.0f
                                     : sidechainChannels == 1 ? sidechain[0][i]
                                                              : 0.5f * (sidechain[0][i] + sidechain[1][i]));
        }
        return numChannels == 2 ? dynamic.feedStereo (left, right) : dynamic.feedMono (left);
    }

    // Una banda dinámica en un tramo: el detector escucha la muestra que llega a la banda (o la del sidechain) ANTES de
    // que la banda la procese, y cada controlInterval muestras le pasa la ganancia nueva.
    static void processDynamic (Band& band, BandDynamics& dynamic, float* const* channels, int numChannels, int start, int count,
                                const float* const* sidechain, int sidechainChannels) noexcept
    {
        for (int i = start; i < start + count; ++i)
        {
            band.advance();
            if (feed (dynamic, numChannels, channels[0][i], channels[numChannels - 1][i], sidechain, sidechainChannels, i))
                band.setDynamicGain (dynamic.getGainDb());
            if (numChannels == 2)
                band.processStereo (channels[0][i], channels[1][i]);
            else
                band.processMono (channels[0][i]);
        }
    }

    // Fase lineal: los detectores escuchan la entrada (y el sidechain) retrasados como el centro del FIR; su ganancia
    // entra al diseño del FIR siguiente (prepareBlock).
    void feedAlignedDetectors (int numChannels, int count, int sidechainChannels) noexcept
    {
        const auto& stage = *phase;
        const std::array<const float*, maxChannels> sidechain { stage.alignedSidechainInput (0), stage.alignedSidechainInput (1) };
        const float* left = stage.alignedInput (0);
        const float* right = stage.alignedInput (numChannels - 1);
        for (size_t b = 0; b < dynamics.size(); ++b)
        {
            auto& dynamic = dynamics[b];
            if (! dynamic.isRunning())
                continue;
            if (! sounding[b].enabled)
            {
                dynamic.stop();
                continue;
            }
            for (int i = 0; i < count; ++i)
                feed (dynamic, numChannels, left[i], right[i], sidechain.data(), sidechainChannels, i);
        }
    }

    // La etapa con memoria que preparó el hilo de mensajes (providePhaseMemory): se cambia por la de ahora si esta está
    // quieta en cero latencia, y la vieja vuelve para que la destruya ese hilo (aquí no se libera memoria).
    void takeOfferedPhase() noexcept
    {
        if (offerState.load (std::memory_order_acquire) != offerMade || ! phase->isQuiet())
            return;
        std::swap (phase, offered);
        phase->setTarget (phaseMode, linearQuality);
        phase->setRealtime (realtime.load (std::memory_order_relaxed));
        offerState.store (offerReturned, std::memory_order_release);
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

        // Los modos de fase: la transición, si hay una, y el FIR de los ajustes de ahora (con la dinámica de ahora).
        std::array<float, numBands> dynamicDb {};
        for (size_t b = 0; b < dynamics.size(); ++b)
            dynamicDb[b] = dynamics[b].getGainDb();
        phase->beginBlock (sounding, dynamicDb, numChannels == 1);
        spectral->beginBlock (sounding, numChannels == 1);
        if (phase->wantsMemory())
            memoryWanted.store (true, std::memory_order_relaxed);

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
    std::array<BandDynamics, numBands> dynamics; // Fase 9: el detector de cada banda (quieto si no es dinámica)
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

    // Fase 10. 'offered' pasa entre el hilo de mensajes y el de audio según 'offerState' (solo lo toca el dueño del
    // estado: libre/devuelta = mensajes, ofrecida = audio).
    std::unique_ptr<PhaseStage> phase = std::make_unique<PhaseStage>();
    std::unique_ptr<PhaseStage> offered;
    static constexpr int offerIdle = 0, offerMade = 1, offerReturned = 2;
    std::atomic<int> offerState { offerIdle };
    std::atomic<bool> memoryWanted { false };
    PhaseMode phaseMode = PhaseMode::zeroLatency;
    int linearQuality = defaultLinearQuality;
    std::atomic<bool> realtime { false };

    std::unique_ptr<SpectralStage> spectral = std::make_unique<SpectralStage>(); // Fase 11 (en el heap: es grande)

    ColorStage color; // Fase 13
    ColorMode colorMode = ColorMode::clean;
};

} // namespace fathom::dsp
