#pragma once

#include <algorithm>
#include <array>

#include "deepwater/dsp/DcBlocker.h"
#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/LevelMeter.h"
#include "deepwater/dsp/Smoothing.h"
#include "dsp/AutoGain.h"
#include "dsp/Bands.h"
#include "dsp/Modulation.h"
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
    float mix = 1.0f;      // 0 = solo la señal original, 1 = solo la saturada
    bool autoGain = false; // igualar el volumen de la saturada con el de la original
    bool delta = false;    // escuchar solo lo que el plugin le agrega (o quita) a la original
    int numBands = 1;      // 1..maxBands
    std::array<float, maxCrossovers> crossoverHz = defaultCrossoverHz;
    std::array<BandSettings, maxBands> bands {};
    ModulationSettings modulation {}; // amount 0 en las dos fuentes = sin modulación (como la Fase 6)
};

// Fase 7: cadena de audio completa del saturador, sin JUCE (se prueba en tests/EngineTests.cpp):
//
//   entrada ─ |x| ─ envolvente ─┐
//                          LFO ─┴─ rutas (destino × amount) ─→ drive · drive de banda · emphasis · tilt · mix
//
//                                                         ┌─ banda 1: × drive ─ modo ─ × auto gain ─ × solo/mute ─┐
//   entrada ─┬─ × input (drive) ─ tono pre ─[IN]─ divisor ┼─ ...                                                  ┼─ Σ ─ tono post ─ filtro DC ── wet ─┐
//            │                                            └─ banda N ─────────────────────────────────────────────┘                                   │
//            ├─ retardo (= latencia) ─ filtro DC ─ divisor gemelo ─ × solo/mute por banda ─ Σ ──────────────────────────────────────────── dry ─┴─ mix ─ − delta·dry ─ × output ─┐
//            └─ retardo (= latencia) ─ original intacta ─────────────────────────────────────────────────────────────────────────────────────────────────────── fundido de bypass ───┴─ × fundido de estructura ─[OUT]
//
// Multibanda (Bands.h): el divisor Linkwitz-Riley parte lo que va a la curva en 1–4 bandas y cada una tiene su modo (o
// el global), su drive (sobre el Input), su auto gain y su solo/mute. Con 1 banda no hay divisor: la señal entera es la
// banda 1, como en la Fase 5. La original del mix y del delta pasa por un divisor idéntico con los mismos solo/mute:
// sin saturación las dos sumas son la misma señal (la entrada con la fase girada por los cruces) y el delta da
// silencio; con una banda en solo, mix y delta hablan solo de esa banda.
//
// Mix (mezcla en paralelo): dry + (wet − dry)·mix. Para mezclar dos versiones de la misma señal tienen que llegar
// ALINEADAS: si una va retrasada o desfasada respecto de la otra, al sumarlas unas frecuencias se refuerzan y otras se
// cancelan (filtro de peine). Tres compensaciones:
//  - latencia: la dry se retrasa exactamente la latencia del oversampling. Los halfband son FIR de fase lineal (todas
//    las frecuencias llegan con el mismo retraso entero), así que el retraso puro las alinea a todas;
//  - fase del filtro DC: el de la wet (5 Hz) adelanta la fase de los graves (6° en 50 Hz). La dry pasa por uno idéntico;
//  - fase de los cruces: la dry pasa por el divisor gemelo (el mismo giro de fase en cada cruce).
// Los cortes, el tilt y la campana de la cinta NO se compensan: son ecualización de la wet, y al mezclarla con la dry se
// oyen como en un procesamiento paralelo analógico (alrededor del corte de un Low Cut la suma pierde un poco).
//
// Auto gain (AutoGain.h) va POR BANDA: cada una mide su propio nivel y compensa su propio modo y drive, antes de sumar.
// Así el mix y el drive de cada banda cambian el carácter sin cambiar el volumen. Delta = mix·(wet − dry): solo la
// diferencia entre la salida y la original. Output se aplica después (también al delta, para poder subirlo y
// escucharlo). El bypass sigue usando la original intacta (sin el filtro DC ni el divisor): bit a bit.
//
// Input es el drive global: decide con qué nivel llega la señal a las curvas (ver Saturator.h). El tono (Tone.h) decide
// QUÉ frecuencias llegan y con cuánto nivel (cortes PRE y emphasis) y el color de lo que sale (de-emphasis, cortes POST y
// tilt): envuelve a todas las bandas. IN mide antes del divisor: lo que entra a las curvas sin el drive de cada banda.
// Cada modo calcula su curva con oversampling (etapas halfband de la librería) para que los armónicos que pasan de
// Nyquist no se reflejen como aliasing.
//
// Cambiar de modo sin clics: cada banda de cada canal tiene DOS saturadores. El activo suena; al pedir otro modo, el
// segundo se configura con él (vacío), procesa en silencio el tiempo que tarda su oversampler en llenarse (2 × latencia)
// y después los dos se cruzan en 20 ms. Las dos señales vienen de la misma entrada y salen alineadas, así que el cruce
// es suave. Solo durante el cambio se paga el doble de CPU. Los filtros retrasan la señal; el camino original del bypass
// se retrasa lo mismo para que los dos lleguen alineados al fundido (si no, el fundido mezclaría dos señales desfasadas:
// filtro de peine y clics).
//
// Modulación (Modulation.h, Fase 7): la envolvente (sigue el volumen de lo que entra) y el LFO mueven una perilla cada
// uno. Se calculan una vez por muestra, compartidas por los canales. Cada destino se mueve en el punto de la cadena donde
// actúa su perilla, alineado con el audio: lo que va ANTES de la curva (drive, drive de banda, emphasis) con la
// modulación de esta muestra; lo que va DESPUÉS (de-emphasis, tilt, mix) con la de hace 'latency' muestras, que es
// cuando entró a la curva el audio que sale ahora. Así un golpe que sube el drive y baja el mix lo hace sobre el mismo
// golpe. Con AUTO GAIN el drive modulado también se compensa (driveDelay lo guarda muestra a muestra): la envolvente
// cambia cuánto satura cada golpe, no su volumen.
//
// Cambiar el factor de oversampling o el número de bandas cambia la estructura (la latencia, qué entra a cada
// saturador) y borra la historia de los filtros: es un salto. Por eso se hace en silencio: la salida baja a cero en
// 10 ms, se cambia la estructura y vuelve a subir. Mover un cruce, en cambio, es continuo (sus frecuencias se suavizan).
class Engine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double gainRampSeconds = 0.02;   // 20 ms: girar la perilla no hace "zipper" y sigue siendo ágil
    static constexpr double bypassFadeSeconds = 0.02;
    static constexpr double factorFadeSeconds = 0.01; // fundido a silencio al cambiar el oversampling o las bandas
    static constexpr double modeFadeSeconds = 0.02;   // cruce entre el modo anterior y el nuevo
    static constexpr double switchFadeSeconds = 0.02; // encender o apagar auto gain, delta, solo y mute
    static constexpr double dcCutoffHz = 5.0;         // por debajo de lo audible; asienta en ~30 ms

    void prepare (double sampleRate, const EngineSettings& settings) noexcept
    {
        inputGain.setTime (gainRampSeconds, sampleRate);
        outputGain.setTime (gainRampSeconds, sampleRate);
        bypassFade.setTime (bypassFadeSeconds, sampleRate);
        structureFade.setTime (factorFadeSeconds, sampleRate);
        mix.setTime (gainRampSeconds, sampleRate);
        autoGainFade.setTime (switchFadeSeconds, sampleRate);
        deltaFade.setTime (switchFadeSeconds, sampleRate);
        tone.prepare (sampleRate, settings.tone);
        requestedBands = activeBands = clampBands (settings.numBands);
        crossoverHz = settings.crossoverHz;
        crossovers.prepare (sampleRate, crossoverHz, activeBands);
        modulator.prepare (sampleRate, settings.modulation);
        mixModDelay.fill (0.0f);
        tiltModDelay.fill (0.0f);

        // Al preparar no hay nada que suavizar: se empieza directamente en los valores actuales.
        inputGain.snap (deepwater::dsp::dbToGain (settings.inputGainDb));
        outputGain.snap (deepwater::dsp::dbToGain (settings.outputGainDb));
        processing = ! settings.bypassed;
        bypassFade.snap (processing);
        mix.snap (clampMix (settings.mix));
        autoGainOn = settings.autoGain;
        autoGainFade.snap (autoGainOn);
        deltaOn = settings.delta;
        deltaFade.snap (deltaOn);

        requestedStages = activeStages = clampStages (settings.oversampling);
        structureFade.snap (true);

        setBandSettings (settings);
        for (size_t b = 0; b < bands.size(); ++b)
        {
            auto& band = bands[b];
            band.drive.setTime (gainRampSeconds, sampleRate);
            band.drive.snap (band.drive.target);
            band.audible.setTime (switchFadeSeconds, sampleRate);
            band.audible.snap (band.audibleOn);
            band.modeFade.setTime (modeFadeSeconds, sampleRate);
            band.autoGain.prepare (sampleRate);
            band.activeMode = band.requestedMode;
            band.activeSlot = 0;
            band.switching = false;
            for (auto& channel : channels)
                for (auto& slot : channel.slots[b])
                    slot.prepare (sampleRate, activeStages, band.requestedMode);
            // Nunca 0: el auto gain divide por el drive.
            band.driveDelay.fill (inputGain.value * band.drive.value);
        }

        for (auto& channel : channels)
        {
            channel.tone.reset();
            channel.wetSplitter.reset();
            channel.drySplitter.reset();
            channel.dcBlocker.prepare (dcCutoffHz, sampleRate);
            channel.dryDcBlocker.prepare (dcCutoffHz, sampleRate);
            channel.dryDelay.fill (0.0f);
            channel.input.prepare (sampleRate);
            channel.output.prepare (sampleRate);
        }
        dryWriteIndex = 0;
        latency = channels[0].slots[0][0].getLatency();
    }

    void setSettings (const EngineSettings& settings) noexcept
    {
        inputGain.setTarget (deepwater::dsp::dbToGain (settings.inputGainDb));
        outputGain.setTarget (deepwater::dsp::dbToGain (settings.outputGainDb));
        processing = ! settings.bypassed;
        requestedStages = clampStages (settings.oversampling);
        requestedBands = clampBands (settings.numBands);
        tone.setSettings (settings.tone);
        crossoverHz = settings.crossoverHz;
        crossovers.setFrequencies (crossoverHz, activeBands); // separados según las bandas que suenan ahora
        mix.setTarget (clampMix (settings.mix));
        autoGainOn = settings.autoGain;
        deltaOn = settings.delta;
        setBandSettings (settings);
        modulator.setSettings (settings.modulation);
    }

    // Tempo y posición de la canción (para el LFO sincronizado). Una vez por bloque, después de setSettings.
    void setTransport (const Transport& transport) noexcept { modulator.setTransport (transport); }

    // Procesa en el sitio. Con más de maxChannels canales, los de más se dejan intactos.
    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            // El fundido de estructura baja mientras haya un cambio pendiente (factor o bandas); en silencio se aplica y
            // vuelve a subir.
            const bool structureSettled = requestedStages == activeStages && requestedBands == activeBands;
            const float level = structureFade.next (structureSettled);
            if (! structureSettled && level == 0.0f)
                applyRequestedStructure();

            // La modulación de esta muestra: la envolvente escucha la entrada tal cual llega (el mayor |x| de los canales).
            float inputPeak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                inputPeak = std::max (inputPeak, std::abs (audio[ch][i]));
            const auto& mod = modulator.advance (inputPeak);
            const size_t readIndex = (dryWriteIndex - static_cast<size_t> (latency)) & (dryDelaySize - 1);
            mixModDelay[dryWriteIndex] = mod[static_cast<size_t> (ModTarget::mix)];
            tiltModDelay[dryWriteIndex] = mod[static_cast<size_t> (ModTarget::tilt)];

            // Las rampas avanzan una vez por muestra para todos los canales: L y R reciben exactamente la misma ganancia.
            const float gainIn = modulateDrive (inputGain.next(), mod[static_cast<size_t> (ModTarget::drive)], driveLimit);
            const float gainOut = outputGain.next();
            const float wet = bypassFade.next (processing); // 1 = procesado, 0 = original
            float mixAmount = mix.next();
            if (const float offset = mixModDelay[readIndex]; offset != 0.0f)
                mixAmount = clampMix (mixAmount + offset);
            const float autoAmount = autoGainFade.next (autoGainOn);
            const float deltaAmount = deltaFade.next (deltaOn);
            tone.advance (mod[static_cast<size_t> (ModTarget::emphasis)], mod[static_cast<size_t> (ModTarget::emphasisFreq)],
                          tiltModDelay[readIndex], latency);
            crossovers.advance();
            const float inverseDrive = 1.0f / gainIn;

            // Lo de cada banda que comparten los canales.
            std::array<float, maxBands> bandDrive {}, blend {}, wetGain {}, audible {};
            std::array<double, maxBands> levelPower {};
            for (size_t b = 0; b < static_cast<size_t> (activeBands); ++b)
            {
                auto& band = bands[b];

                // Cruce de modos: 'blend' = cuánto se oye el saturador entrante (0 mientras se llena su oversampler).
                if (! band.switching && band.requestedMode != band.activeMode)
                    beginModeSwitch (band, b);
                if (band.switching)
                {
                    if (band.warmUpRemaining > 0)
                        --band.warmUpRemaining;
                    else
                        blend[b] = band.modeFade.next();
                }

                bandDrive[b] = modulateDrive (band.drive.next(), mod[static_cast<size_t> (bandDriveTarget (b))], bandDriveLimit);
                audible[b] = band.audible.next (band.audibleOn);

                // Lo que sale ahora del saturador entró hace 'latency' muestras: la compensación usa el drive de
                // entonces (si no, al bajar Input de golpe la salida subiría un instante). Se calcula siempre: la GUI
                // la muestra.
                band.driveDelay[dryWriteIndex] = gainIn * bandDrive[b];
                const Mode incoming = band.switching ? band.incomingMode : band.activeMode;
                const float compensation = band.autoGain.gain (band.driveDelay[readIndex], band.activeMode, incoming, blend[b]);
                wetGain[b] = autoAmount == 0.0f   ? 1.0f
                             : autoAmount == 1.0f ? compensation
                                                  : 1.0f + (compensation - 1.0f) * autoAmount;
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& channel = channels[static_cast<size_t> (ch)];
                float& sample = audio[ch][i];

                channel.dryDelay[dryWriteIndex] = sample;
                const float dry = channel.dryDelay[readIndex];
                // La dry, con la misma fase que la wet (filtro DC y cruces) y los mismos solo/mute.
                float alignedDry = channel.dryDcBlocker.process (dry);
                if (activeBands > 1)
                {
                    std::array<float, maxBands> dryBands {};
                    channel.drySplitter.process (alignedDry, crossovers.coefficients, activeBands, dryBands.data());
                    alignedDry = 0.0f;
                    for (size_t b = 0; b < static_cast<size_t> (activeBands); ++b)
                        alignedDry += dryBands[b] * audible[b];
                }
                else if (audible[0] != 1.0f)
                {
                    alignedDry *= audible[0];
                }

                const float driven = channel.tone.pre (sample * gainIn, tone);
                channel.input.push (driven);
                std::array<float, maxBands> bandSignals {};
                channel.wetSplitter.process (driven, crossovers.coefficients, activeBands, bandSignals.data());

                float saturatedSum = 0.0f;
                for (size_t b = 0; b < static_cast<size_t> (activeBands); ++b)
                {
                    auto& band = bands[b];
                    auto& slots = channel.slots[b];
                    const float x = bandSignals[b];

                    const float undriven = x * inverseDrive; // el tono pre y el divisor son lineales
                    levelPower[b] += static_cast<double> (undriven) * undriven;

                    const float bandInput = bandDrive[b] == 1.0f ? x : x * bandDrive[b];
                    float saturated = slots[band.activeSlot].process (bandInput);
                    if (band.switching)
                    {
                        const float incoming = slots[1 - band.activeSlot].process (bandInput);
                        saturated = blend[b] == 1.0f ? incoming : saturated + (incoming - saturated) * blend[b];
                    }
                    if (wetGain[b] != 1.0f)
                        saturated *= wetGain[b];
                    if (audible[b] != 1.0f)
                        saturated *= audible[b];
                    saturatedSum = b == 0 ? saturated : saturatedSum + saturated;
                }

                float processed = channel.dcBlocker.process (channel.tone.post (saturatedSum, tone));
                // Con mix al 100 % y sin delta la wet pasa tal cual: los valores por defecto suenan bit a bit como la Fase 5.
                if (mixAmount != 1.0f)
                    processed = alignedDry + (processed - alignedDry) * mixAmount;
                if (deltaAmount != 0.0f)
                    processed -= alignedDry * deltaAmount;
                processed *= gainOut;

                // Fuera de los fundidos se copia la señal tal cual: con el bypass puesto la salida es la entrada
                // retrasada, bit a bit.
                float out = wet == 1.0f ? processed : wet == 0.0f ? dry : dry + (processed - dry) * wet;
                if (level != 1.0f)
                    out *= level;
                channel.output.push (out);
                sample = out;
            }

            for (size_t b = 0; b < static_cast<size_t> (activeBands); ++b)
            {
                auto& band = bands[b];
                band.autoGain.push (levelPower[b] / numChannels);
                if (band.switching && blend[b] == 1.0f)
                    finishModeSwitch (band);
            }
            dryWriteIndex = (dryWriteIndex + 1) & (dryDelaySize - 1);
        }
    }

    // Latencia actual en muestras del host (la que hay que informar al host).
    [[nodiscard]] int getLatency() const noexcept { return latency; }
    [[nodiscard]] int getActiveOversampling() const noexcept { return activeStages; }
    [[nodiscard]] int getActiveBands() const noexcept { return activeBands; }
    [[nodiscard]] Mode getActiveMode (int band = 0) const noexcept { return bands[static_cast<size_t> (band)].activeMode; }
    [[nodiscard]] bool isSwitchingMode (int band = 0) const noexcept { return bands[static_cast<size_t> (band)].switching; }
    // La frecuencia de un cruce que suena ahora (ya separada de las vecinas y suavizada).
    [[nodiscard]] float getCrossoverHz (int index) const noexcept { return crossovers.getFrequency (index); }
    // La ganancia de auto gain de una banda que se aplica (o se aplicaría, si está apagado), en dB.
    [[nodiscard]] float getAutoGainDb (int band = 0) const noexcept
    {
        return deepwater::dsp::gainToDb (bands[static_cast<size_t> (band)].autoGain.getGain());
    }

    // Modulación, para la GUI: el valor de cada fuente y cuánto mueve cada destino (en sus unidades) en la última muestra.
    [[nodiscard]] float getEnvelopeValue() const noexcept { return modulator.getEnvelopeValue(); }
    [[nodiscard]] float getLfoValue() const noexcept { return modulator.getLfoValue(); }
    [[nodiscard]] float getModulation (ModTarget target) const noexcept
    {
        return modulator.getOffsets()[static_cast<size_t> (target)];
    }
    // El emphasis y el tilt que suenan ahora (perilla + modulación).
    [[nodiscard]] float getEmphasisHz() const noexcept { return tone.getEmphasisHz(); }
    [[nodiscard]] float getEmphasisDb() const noexcept { return tone.getEmphasisDb(); }
    [[nodiscard]] float getTiltDb() const noexcept { return tone.getTiltDb(); }

    [[nodiscard]] LevelMeter& inputMeter (int channel) noexcept { return channels[static_cast<size_t> (channel)].input; }
    [[nodiscard]] LevelMeter& outputMeter (int channel) noexcept { return channels[static_cast<size_t> (channel)].output; }

private:
    // La latencia máxima (×16 a frecuencias bajas, donde todas las etapas usan el filtro más largo) es de unas 140
    // muestras: el retardo del camino original cabe en 256.
    static constexpr size_t dryDelaySize = 256;
    static_assert (dryDelaySize <= ToneControls::historySize);

    // Lo que comparten los canales de una banda.
    struct Band
    {
        deepwater::dsp::LinearRamp drive;     // drive propio (lineal), sobre el Input global
        deepwater::dsp::EffectSwitch audible; // solo/mute con fundido
        bool audibleOn = true;
        Mode requestedMode = Mode::tape; // el propio o el global
        Mode activeMode = Mode::tape;
        Mode incomingMode = Mode::tape;  // el que entra durante un cruce de modos
        size_t activeSlot = 0;
        bool switching = false;
        int warmUpRemaining = 0;
        deepwater::dsp::LinearRamp modeFade;
        AutoGain autoGain;
        std::array<float, dryDelaySize> driveDelay {}; // el drive total de cada muestra, para alinearlo con la salida
    };

    struct Channel
    {
        // Por banda, el saturador activo y el que entra durante un cambio de modo.
        std::array<std::array<Saturator, 2>, maxBands> slots;
        Splitter wetSplitter, drySplitter; // el de la señal que se satura y su gemelo para la original
        ToneChannel tone;
        deepwater::dsp::DcBlocker dcBlocker;
        deepwater::dsp::DcBlocker dryDcBlocker; // el gemelo del de la wet, para la dry del mix y del delta
        std::array<float, dryDelaySize> dryDelay {};
        LevelMeter input, output;
    };

    static int clampStages (int index) noexcept { return std::clamp (index, 0, numOversamplingChoices - 1); }
    static int clampBands (int count) noexcept { return std::clamp (count, 1, maxBands); }
    static float clampMix (float amount) noexcept { return std::clamp (amount, 0.0f, 1.0f); }

    // Un drive (ganancia lineal) movido 'offsetDb' por la modulación, sin salirse del rango de su perilla (±limit, como
    // ganancia). Sin modulación devuelve el drive tal cual (bit a bit).
    static float modulateDrive (float drive, float offsetDb, float limit) noexcept
    {
        if (offsetDb == 0.0f)
            return drive;
        return std::clamp (drive * deepwater::dsp::dbToGain (offsetDb), 1.0f / limit, limit);
    }
    static inline const float driveLimit = deepwater::dsp::dbToGain (maxDriveDb);
    static inline const float bandDriveLimit = deepwater::dsp::dbToGain (maxBandDriveDb);

    void setBandSettings (const EngineSettings& settings) noexcept
    {
        // Con alguna banda en solo se oyen solo las que están en solo (el solo gana al mute); si no, todas menos las
        // muteadas. Solo cuentan las bandas que existen.
        bool anySolo = false;
        for (size_t b = 0; b < static_cast<size_t> (clampBands (settings.numBands)); ++b)
            anySolo = anySolo || settings.bands[b].solo;

        for (size_t b = 0; b < bands.size(); ++b)
        {
            const auto& wanted = settings.bands[b];
            auto& band = bands[b];
            band.drive.setTarget (deepwater::dsp::dbToGain (std::clamp (wanted.driveDb, -maxBandDriveDb, maxBandDriveDb)));
            band.audibleOn = anySolo ? wanted.solo : ! wanted.mute;
            band.requestedMode = wanted.ownMode ? wanted.mode : settings.mode;
        }
    }

    // Se llama en silencio (el fundido de estructura está en 0). Todas las bandas arrancan vacías con el modo pedido (si
    // había un cruce a medias, se termina de golpe) y, si cambió el número de bandas, también los divisores y la medida
    // del auto gain (cada banda ahora es otra señal) y los cruces saltan a su sitio (se separan según las bandas nuevas).
    void applyRequestedStructure() noexcept
    {
        const bool bandsChanged = activeBands != requestedBands;
        activeStages = requestedStages;
        activeBands = requestedBands;
        for (size_t b = 0; b < bands.size(); ++b)
        {
            auto& band = bands[b];
            band.switching = false;
            band.activeSlot = 0;
            band.activeMode = band.requestedMode;
            for (auto& channel : channels)
                for (auto& slot : channel.slots[b])
                {
                    slot.setNumStages (activeStages);
                    slot.setMode (band.activeMode);
                }
            if (bandsChanged)
                band.autoGain.reset();
        }
        if (bandsChanged)
        {
            crossovers.setFrequencies (crossoverHz, activeBands);
            crossovers.snap();
            for (auto& channel : channels)
            {
                channel.wetSplitter.reset();
                channel.drySplitter.reset();
            }
        }
        // El retardo del camino original conserva su historia: solo cambia desde dónde se lee.
        latency = channels[0].slots[0][0].getLatency();
    }

    void beginModeSwitch (Band& band, size_t index) noexcept
    {
        for (auto& channel : channels)
            channel.slots[index][1 - band.activeSlot].setMode (band.requestedMode); // vacío: arranca desde silencio
        band.incomingMode = band.requestedMode;
        // La respuesta al impulso de los filtros halfband dura unas 2 × latencia: pasado ese tiempo el saturador
        // entrante ya no "recuerda" que empezó vacío.
        band.warmUpRemaining = 2 * latency;
        band.modeFade.snap (0.0f);
        band.modeFade.setTarget (1.0f);
        band.switching = true;
    }

    static void finishModeSwitch (Band& band) noexcept
    {
        band.activeSlot = 1 - band.activeSlot;
        band.activeMode = band.incomingMode;
        band.switching = false;
    }

    deepwater::dsp::LinearRamp inputGain;
    deepwater::dsp::LinearRamp outputGain;
    deepwater::dsp::EffectSwitch bypassFade;
    deepwater::dsp::EffectSwitch structureFade;
    deepwater::dsp::LinearRamp mix;
    deepwater::dsp::EffectSwitch autoGainFade;
    deepwater::dsp::EffectSwitch deltaFade;
    ToneControls tone;
    CrossoverControls crossovers;
    Modulator modulator;
    // La modulación de los destinos que van después de la curva, retrasada la latencia (como la dry).
    std::array<float, dryDelaySize> mixModDelay {}, tiltModDelay {};
    std::array<float, maxCrossovers> crossoverHz = defaultCrossoverHz; // lo que piden las perillas
    bool autoGainOn = false;
    bool deltaOn = false;
    bool processing = true;
    int requestedStages = 0;
    int activeStages = 0;
    int requestedBands = 1;
    int activeBands = 1;
    int latency = 0;

    std::array<Band, maxBands> bands;
    std::array<Channel, maxChannels> channels;
    size_t dryWriteIndex = 0;
};

} // namespace riptide::dsp
