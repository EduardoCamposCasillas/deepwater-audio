#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/OnePole.h"
#include "deepwater/dsp/Smoothing.h"
#include "deepwater/dsp/Svf.h"

namespace riptide::dsp
{

// --- Rangos de los controles de tono (los usa también el procesador para crear los parámetros) -----------------------
inline constexpr float lowCutOffHz = 20.0f;    // Low Cut al mínimo = apagado
inline constexpr float lowCutMaxHz = 2000.0f;
inline constexpr float highCutMinHz = 500.0f;
inline constexpr float highCutOffHz = 20000.0f; // High Cut al máximo = apagado
inline constexpr float maxTiltDb = 6.0f;
inline constexpr float maxEmphasisDb = 12.0f;
inline constexpr float emphasisMinHz = 50.0f;
inline constexpr float emphasisMaxHz = 10000.0f;

// A menos de un 1 % del extremo la perilla se considera apagada (y la GUI muestra "Off"): la automatización del host
// no siempre llega exactamente al extremo.
inline constexpr float cutOffTolerance = 0.01f;
[[nodiscard]] inline bool isCutOff (float hz, float offHz) noexcept { return std::abs (hz - offHz) <= cutOffTolerance * offHz; }

struct ToneSettings
{
    float lowCutHz = lowCutOffHz;
    bool lowCutPost = false;  // false = antes de la curva (pre), true = después (post)
    float highCutHz = highCutOffHz;
    bool highCutPost = true;
    float tiltDb = 0.0f;      // ganancia en agudos; los graves reciben la contraria
    float emphasisDb = 0.0f;  // cuánto sube (o baja) la zona de emphasis antes de la curva
    float emphasisHz = 1000.0f;
};

// Fase 4: el tono del saturador. La curva NO es lineal, así que el orden importa:
//  - lo que va ANTES decide qué entra a la curva: qué se satura y cuánto;
//  - lo que va DESPUÉS solo cambia el color de lo que sale (incluidos los armónicos que la curva creó).
//
//   pre:  [Low Cut]  →  [High Cut]  →  emphasis (campana, +A)  ──→  curva
//   post: de-emphasis (la inversa exacta)  →  [Low Cut]  →  [High Cut]  →  tilt
//
// Cada corte existe dos veces (pre y post) y su interruptor PRE/POST cruza las dos copias en 20 ms: los dos filtros
// procesan siempre (su estado está "caliente"), solo cambia cuál se oye. Los coeficientes (con tan()) se recalculan
// solo mientras una perilla se mueve, una vez por muestra para todos los canales (ToneControls); cada canal guarda solo
// el estado de sus filtros (ToneChannel).
//
// Con todo en su posición neutra (cortes apagados, emphasis y tilt a 0 dB) la señal pasa bit a bit: Riptide suena igual
// que en la Fase 3.
//
// Fase 7: el emphasis y el tilt pueden moverse en cada muestra (envolvente y LFO). La de-emphasis tiene que deshacer el
// emphasis que la señal recibió AL ENTRAR a la curva, y lo que sale de la curva ahora entró hace 'latency' muestras (el
// oversampling la retrasa). Por eso los valores del emphasis se guardan en un historial y la de-emphasis usa los de hace
// 'latency' muestras: con el emphasis quieto las dos son inversas exactas, y moviéndose siguen siéndolo (sin el retraso,
// un LFO rápido sobre Emph Freq dejaría una ondulación de EQ en la salida).
class ToneControls
{
public:
    static constexpr double rampSeconds = 0.02;
    static constexpr size_t historySize = 256; // potencia de 2, mayor que la latencia máxima del oversampling
    static constexpr double cutQ = 0.70710678; // Butterworth: 12 dB/oct sin pico en el corte
    static constexpr double emphasisQ = 0.7;    // campana ancha (unas 2 octavas): "zona", no una frecuencia exacta
    static constexpr double tiltPivotHz = 1000.0;

    // Una campana del SVF y su inversa exacta. Campana: H(s) = (s² + A·k·w·s + w²) / (s² + k·w·s + w²) = 1 + (A − 1)·k·BP.
    // Su inversa es otra campana con ganancia 1/A y amortiguación A·k: 1 + (1/A − 1)·(A·k)·BP' = 1 + (1 − A)·k·BP'. Con el
    // mismo g (misma bilineal) las dos son inversas exactas también en digital.
    struct Emphasis
    {
        deepwater::dsp::SvfStage::Coefficients pre, post;
        float preAmount = 0.0f;
        float postAmount = 0.0f;
    };

    void prepare (double newSampleRate, const ToneSettings& settings) noexcept
    {
        sampleRate = newSampleRate;
        for (auto* ramp : { &lowCutOctaves, &highCutOctaves, &emphasisOctaves, &emphasisDb, &tiltDb })
            ramp->setTime (rampSeconds, sampleRate);
        for (auto* fade : { &lowCutPreFade, &lowCutPostFade, &highCutPreFade, &highCutPostFade })
            fade->setTime (rampSeconds, sampleRate);

        // Al preparar no hay nada que suavizar: todo empieza en su valor actual.
        setSettings (settings);
        for (auto* ramp : { &lowCutOctaves, &highCutOctaves, &emphasisOctaves, &emphasisDb, &tiltDb })
            ramp->snap (ramp->target);
        lowCutPreFade.snap (lowCutPreOn());
        lowCutPostFade.snap (lowCutPostOn());
        highCutPreFade.snap (highCutPreOn());
        highCutPostFade.snap (highCutPostOn());
        updateLowCut();
        updateHighCut();
        preEmphasis = { emphasisOctaves.value, emphasisDb.value };
        postEmphasis = preEmphasis;
        emphasisHistory.fill (preEmphasis);
        historyIndex = 0;
        updateEmphasis (preEmphasis, emphasis.pre, emphasis.preAmount, false);
        updateEmphasis (postEmphasis, emphasis.post, emphasis.postAmount, true);
        tiltValue = tiltDb.value;
        updateTilt();
        advanceFades();
    }

    void setSettings (const ToneSettings& settings) noexcept
    {
        // Las frecuencias se suavizan en octavas (log2): la perilla barre igual de rápido en graves que en agudos.
        lowCutOctaves.setTarget (std::log2 (std::clamp (settings.lowCutHz, lowCutOffHz, lowCutMaxHz)));
        highCutOctaves.setTarget (std::log2 (std::clamp (settings.highCutHz, highCutMinHz, highCutOffHz)));
        emphasisOctaves.setTarget (std::log2 (std::clamp (settings.emphasisHz, emphasisMinHz, emphasisMaxHz)));
        emphasisDb.setTarget (std::clamp (settings.emphasisDb, -maxEmphasisDb, maxEmphasisDb));
        tiltDb.setTarget (std::clamp (settings.tiltDb, -maxTiltDb, maxTiltDb));
        lowCutOn = ! isCutOff (settings.lowCutHz, lowCutOffHz);
        highCutOn = ! isCutOff (settings.highCutHz, highCutOffHz);
        lowCutPost = settings.lowCutPost;
        highCutPost = settings.highCutPost;
    }

    // Una vez por muestra, antes de procesar los canales. Los offsets son la modulación de esta muestra (0 = ninguna):
    // emphasis en dB y en octavas, tilt en dB (ya alineado con lo que sale de la curva). 'latency' = la del oversampling.
    void advance (float emphasisDbOffset, float emphasisOctaveOffset, float tiltDbOffset, int latency) noexcept
    {
        if (! lowCutOctaves.isSteady())
        {
            lowCutOctaves.next();
            updateLowCut();
        }
        if (! highCutOctaves.isSteady())
        {
            highCutOctaves.next();
            updateHighCut();
        }

        // Emphasis: los coeficientes solo se recalculan (tan()) cuando el valor cambia. Con offset 0 el valor es el de la
        // perilla tal cual.
        emphasisOctaves.next();
        emphasisDb.next();
        const EmphasisValue current { std::clamp (emphasisOctaves.value + emphasisOctaveOffset, minEmphasisOctave, maxEmphasisOctave),
                                      std::clamp (emphasisDb.value + emphasisDbOffset, -maxEmphasisDb, maxEmphasisDb) };
        if (current != preEmphasis)
        {
            preEmphasis = current;
            updateEmphasis (preEmphasis, emphasis.pre, emphasis.preAmount, false);
        }
        emphasisHistory[historyIndex] = current;
        const auto& delayed = emphasisHistory[(historyIndex - static_cast<size_t> (latency)) & (historySize - 1)];
        historyIndex = (historyIndex + 1) & (historySize - 1);
        if (delayed != postEmphasis)
        {
            postEmphasis = delayed;
            updateEmphasis (postEmphasis, emphasis.post, emphasis.postAmount, true);
        }

        tiltDb.next();
        if (const float tiltNow = std::clamp (tiltDb.value + tiltDbOffset, -maxTiltDb, maxTiltDb); tiltNow != tiltValue)
        {
            tiltValue = tiltNow;
            updateTilt();
        }
        advanceFades();
    }

    // Los valores que suenan ahora (perilla + modulación), para la GUI y los tests.
    [[nodiscard]] float getEmphasisHz() const noexcept { return std::exp2 (preEmphasis.octave); }
    [[nodiscard]] float getEmphasisDb() const noexcept { return preEmphasis.db; }
    [[nodiscard]] float getTiltDb() const noexcept { return tiltValue; }

    // Lo que leen los canales en la muestra actual.
    deepwater::dsp::SvfStage::Coefficients lowCut, highCut;
    Emphasis emphasis;
    deepwater::dsp::TiltFilter::Coefficients tilt;
    float lowCutPreMix = 0.0f, lowCutPostMix = 0.0f, highCutPreMix = 0.0f, highCutPostMix = 0.0f;

private:
    struct EmphasisValue
    {
        float octave = 0.0f; // log2 de la frecuencia
        float db = 0.0f;
        bool operator== (const EmphasisValue&) const = default;
    };

    static inline const float minEmphasisOctave = std::log2 (emphasisMinHz);
    static inline const float maxEmphasisOctave = std::log2 (emphasisMaxHz);

    [[nodiscard]] bool lowCutPreOn() const noexcept { return lowCutOn && ! lowCutPost; }
    [[nodiscard]] bool lowCutPostOn() const noexcept { return lowCutOn && lowCutPost; }
    [[nodiscard]] bool highCutPreOn() const noexcept { return highCutOn && ! highCutPost; }
    [[nodiscard]] bool highCutPostOn() const noexcept { return highCutOn && highCutPost; }

    void advanceFades() noexcept
    {
        lowCutPreMix = lowCutPreFade.next (lowCutPreOn());
        lowCutPostMix = lowCutPostFade.next (lowCutPostOn());
        highCutPreMix = highCutPreFade.next (highCutPreOn());
        highCutPostMix = highCutPostFade.next (highCutPostOn());
    }

    void updateLowCut() noexcept
    {
        lowCut = deepwater::dsp::SvfStage::Coefficients::forCutoff (std::exp2 (lowCutOctaves.value), cutQ, sampleRate);
    }

    void updateHighCut() noexcept
    {
        highCut = deepwater::dsp::SvfStage::Coefficients::forCutoff (std::exp2 (highCutOctaves.value), cutQ, sampleRate);
    }

    // La campana del emphasis (antes de la curva) o su inversa (después), para unos valores dados.
    void updateEmphasis (const EmphasisValue& value, deepwater::dsp::SvfStage::Coefficients& coefficients, float& amount,
                         bool inverse) const noexcept
    {
        using deepwater::dsp::SvfStage;
        const auto bell = SvfStage::Coefficients::forCutoff (std::exp2 (value.octave), emphasisQ, sampleRate);
        const float a = deepwater::dsp::dbToGain (value.db); // 0 dB = 1 exacto: las dos campanas no tocan nada
        coefficients = inverse ? SvfStage::Coefficients::make (bell.g, a * bell.k) : bell;
        amount = inverse ? (1.0f - a) * bell.k : (a - 1.0f) * bell.k;
    }

    void updateTilt() noexcept
    {
        tilt = deepwater::dsp::TiltFilter::Coefficients::forTilt (tiltPivotHz, tiltValue, sampleRate);
    }

    double sampleRate = 44100.0;
    deepwater::dsp::LinearRamp lowCutOctaves, highCutOctaves, emphasisOctaves, emphasisDb, tiltDb;
    deepwater::dsp::EffectSwitch lowCutPreFade, lowCutPostFade, highCutPreFade, highCutPostFade;
    bool lowCutOn = false, highCutOn = false, lowCutPost = false, highCutPost = true;
    EmphasisValue preEmphasis, postEmphasis;      // los que suenan antes y después de la curva
    std::array<EmphasisValue, historySize> emphasisHistory {};
    size_t historyIndex = 0;
    float tiltValue = 0.0f;
};

// Los filtros de tono de un canal (solo su estado; los coeficientes vienen de ToneControls).
class ToneChannel
{
public:
    void reset() noexcept
    {
        for (auto* filter : { &lowCutPre, &highCutPre, &emphasisPre, &emphasisPost, &lowCutPost, &highCutPost })
            filter->reset();
        tiltFilter.reset();
    }

    // Antes de la curva: cortes en posición PRE y emphasis.
    [[nodiscard]] float pre (float x, const ToneControls& c) noexcept
    {
        x = mix (x, lowCutPre.process (x, c.lowCut).highPass, c.lowCutPreMix);
        x = mix (x, highCutPre.process (x, c.highCut).lowPass, c.highCutPreMix);
        return x + c.emphasis.preAmount * emphasisPre.process (x, c.emphasis.pre).bandPass;
    }

    // Después de la curva: de-emphasis, cortes en posición POST y tilt.
    [[nodiscard]] float post (float x, const ToneControls& c) noexcept
    {
        x += c.emphasis.postAmount * emphasisPost.process (x, c.emphasis.post).bandPass;
        x = mix (x, lowCutPost.process (x, c.lowCut).highPass, c.lowCutPostMix);
        x = mix (x, highCutPost.process (x, c.highCut).lowPass, c.highCutPostMix);
        return tiltFilter.process (x, c.tilt);
    }

private:
    // Fuera de los fundidos se devuelve una de las dos señales tal cual (apagado = bit a bit).
    [[nodiscard]] static float mix (float dry, float filtered, float amount) noexcept
    {
        return amount == 0.0f ? dry : amount == 1.0f ? filtered : dry + (filtered - dry) * amount;
    }

    deepwater::dsp::SvfStage lowCutPre, highCutPre, emphasisPre, emphasisPost, lowCutPost, highCutPost;
    deepwater::dsp::TiltFilter tiltFilter;
};

} // namespace riptide::dsp
