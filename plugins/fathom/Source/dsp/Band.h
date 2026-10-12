#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "deepwater/dsp/EqFilter.h"
#include "deepwater/dsp/Smoothing.h"

namespace fathom::dsp
{

// Las formas de una banda. El orden es el del parámetro "Shape" (se guarda como índice en los proyectos): agregar al
// final, nunca reordenar.
enum class Shape
{
    bell,
    lowShelf,
    highShelf,
    tiltShelf,
    flatTilt,
    lowCut,
    highCut,
    notch,
    bandPass,
    allPass,
};
inline constexpr int numShapes = 10;

// Pendientes de los cortes en dB/oct (6 por polo). También es el orden del parámetro "Slope".
inline constexpr std::array<int, 9> slopes { 6, 12, 18, 24, 30, 36, 48, 72, 96 };

// Qué ajustes usa cada forma (los demás no cambian el sonido; la GUI los atenúa).
constexpr bool usesGain (Shape shape) noexcept
{
    return shape == Shape::bell || shape == Shape::lowShelf || shape == Shape::highShelf || shape == Shape::tiltShelf
           || shape == Shape::flatTilt;
}
constexpr bool usesSlope (Shape shape) noexcept { return shape == Shape::lowCut || shape == Shape::highCut; }
constexpr bool usesQ (Shape shape, int slopeIndex) noexcept
{
    return shape != Shape::flatTilt && ! (usesSlope (shape) && slopes[static_cast<size_t> (slopeIndex)] == 6);
}

// Los ajustes de una banda tal como llegan de los parámetros.
struct BandSettings
{
    bool enabled = false;
    float frequency = 1000.0f; // Hz
    float gainDb = 0.0f;
    float q = 1.0f;
    Shape shape = Shape::bell;
    int slopeIndex = 1; // 12 dB/oct

    static constexpr float minFrequency = 10.0f;
    static constexpr float maxFrequency = 30000.0f; // encima de Nyquist el filtro se limita a 0.49·fs
    static constexpr float minGainDb = -30.0f;
    static constexpr float maxGainDb = 30.0f;
    static constexpr float minQ = 0.025f;
    static constexpr float maxQ = 40.0f;
    // Flat tilt: la ganancia se lee como pendiente, 10 dB = 1 dB/oct (de 20 Hz a 20 kHz hay casi 10 octavas: la
    // ganancia es, más o menos, cuánto cambia de un extremo al otro).
    static constexpr float flatTiltDbPerOctavePerDb = 0.1f;
};

// Una banda del EQ, estéreo, con los parámetros suavizados.
//
// Mover un filtro de golpe es un clic: los coeficientes saltan y el estado del filtro (lo que "recuerda" de las
// muestras anteriores) de pronto no corresponde a la nueva respuesta. Por eso no se suavizan los coeficientes sino los
// PARÁMETROS, cada uno en el dominio en que se oye: la frecuencia en octavas (log2 Hz: de 100 a 200 Hz tarda lo mismo
// que de 5 a 10 kHz), la ganancia en dB y el Q en log (de 0.5 a 1 lo mismo que de 4 a 8). Mientras alguna rampa se
// mueve, los coeficientes se recalculan en cada muestra (el SVF TPT es estable aunque cambien en cada muestra). Quietos,
// no se recalcula nada.
//
// Encender, apagar o cambiar de forma o de pendiente no se puede suavizar así (un corte no "se convierte" en un shelf
// ni 4 etapas en 2). Para eso la banda tiene un fundido 'mix': salida = entrada + mix·(filtrada − entrada), que es
// interpolar la respuesta entre "nada" (1) y la banda (H). Apagar = mix baja a 0 en 20 ms; cambiar de forma = baja a 0,
// se cambia la forma con el filtro en limpio y vuelve a subir. Con mix en 0 y apagada, la banda no procesa nada.
class Band
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double rampSeconds = 0.02;

    void prepare (double newSampleRate, const BandSettings& settings) noexcept
    {
        sampleRate = newSampleRate;
        flatTilt.prepare (sampleRate);
        for (auto* ramp : { &octaves, &gainDb, &logQ, &mix })
            ramp->setTime (rampSeconds, sampleRate);
        pending = settings;
        mix.snap (settings.enabled ? 1.0f : 0.0f);
        startShape();
    }

    void setSettings (const BandSettings& settings) noexcept
    {
        pending = settings;
        if (isIdle())
        {
            // Apagada y en silencio: no hay nada que suavizar. Si se enciende, arranca desde cero con el fundido.
            if (settings.enabled)
                startShape();
            else
                return;
        }
        const auto targets = targetsFor (settings);
        octaves.setTarget (targets[0]);
        gainDb.setTarget (targets[1]);
        logQ.setTarget (targets[2]);
        mix.setTarget (settings.enabled && ! needsSwitch() ? 1.0f : 0.0f);
    }

    // True si la banda no hace nada (apagada y con el fundido terminado): el motor se la salta.
    [[nodiscard]] bool isIdle() const noexcept { return mix.value == 0.0f && mix.isSteady() && ! pending.enabled; }

    // Una vez por muestra, antes de process(): avanza las rampas.
    void advance() noexcept
    {
        // El fundido de salida terminó y hay una forma o una pendiente nueva esperando: se cambia ahora, en silencio.
        if (mix.value == 0.0f && mix.isSteady() && needsSwitch())
        {
            startShape();
            mix.setTarget (pending.enabled ? 1.0f : 0.0f);
        }
        mix.next();
        if (octaves.isSteady() && gainDb.isSteady() && logQ.isSteady())
            return;
        octaves.next();
        gainDb.next();
        logQ.next();
        updateCoefficients();
    }

    [[nodiscard]] float process (int channel, float x) noexcept
    {
        const float wet = filters[static_cast<size_t> (channel)].process (x, cascade);
        // Fuera del fundido no se mezcla: en 1 sale la banda tal cual (una campana en 0 dB = la entrada bit a bit).
        const float m = mix.value;
        return m == 1.0f ? wet : m == 0.0f ? x : x + (wet - x) * m;
    }

    // La respuesta de una banda con estos ajustes, ya quieta (apagada = nada). La GUI la usa para dibujar la curva: son
    // los MISMOS números con los que termina sonando el audio.
    static deepwater::dsp::EqCascade coefficientsFor (const BandSettings& settings, double sampleRate) noexcept
    {
        if (! settings.enabled)
            return {};
        deepwater::dsp::FlatTiltDesign design;
        if (settings.shape == Shape::flatTilt)
            design.prepare (sampleRate);
        const auto targets = targetsFor (settings);
        return makeCascade (settings.shape, settings.slopeIndex, targets[0], targets[1], targets[2], sampleRate, design);
    }

    [[nodiscard]] const deepwater::dsp::EqCascade& getCascade() const noexcept { return cascade; }
    [[nodiscard]] Shape getShape() const noexcept { return shape; }

private:
    // {octavas, dB, log2 Q}.
    static std::array<float, 3> targetsFor (const BandSettings& s) noexcept
    {
        const float hz = std::clamp (s.frequency, BandSettings::minFrequency, BandSettings::maxFrequency);
        const float db = std::clamp (s.gainDb, BandSettings::minGainDb, BandSettings::maxGainDb);
        const float q = std::clamp (s.q, BandSettings::minQ, BandSettings::maxQ);
        return { std::log2 (hz), db, std::log2 (q) };
    }

    static int clampSlope (int slopeIndex) noexcept { return std::clamp (slopeIndex, 0, static_cast<int> (slopes.size()) - 1); }

    static deepwater::dsp::EqCascade makeCascade (Shape shape, int slopeIndex, float octave, float db, float logQ, double sampleRate,
                                                  const deepwater::dsp::FlatTiltDesign& design) noexcept
    {
        using deepwater::dsp::EqCascade;
        using deepwater::dsp::EqCoefficients;
        const double hz = std::exp2 (static_cast<double> (octave));
        const double q = std::exp2 (static_cast<double> (logQ));
        const auto gain = static_cast<double> (db);
        const int order = slopes[static_cast<size_t> (clampSlope (slopeIndex))] / 6;
        switch (shape)
        {
            case Shape::lowShelf: return EqCascade::single (EqCoefficients::lowShelf (hz, q, gain, sampleRate));
            case Shape::highShelf: return EqCascade::single (EqCoefficients::highShelf (hz, q, gain, sampleRate));
            case Shape::tiltShelf: return EqCascade::single (EqCoefficients::tiltShelf (hz, q, gain, sampleRate));
            case Shape::flatTilt: return design.make (hz, gain * static_cast<double> (BandSettings::flatTiltDbPerOctavePerDb));
            case Shape::lowCut: return EqCascade::butterworthHighPass (hz, order, q, sampleRate);
            case Shape::highCut: return EqCascade::butterworthLowPass (hz, order, q, sampleRate);
            case Shape::notch: return EqCascade::single (EqCoefficients::notch (hz, q, sampleRate));
            case Shape::bandPass: return EqCascade::single (EqCoefficients::bandPassFilter (hz, q, sampleRate));
            case Shape::allPass: return EqCascade::single (EqCoefficients::allPass (hz, q, sampleRate));
            case Shape::bell: break;
        }
        return EqCascade::single (EqCoefficients::bell (hz, q, gain, sampleRate));
    }

    // ¿La forma (o la pendiente de un corte) pedida es distinta de la que suena?
    [[nodiscard]] bool needsSwitch() const noexcept
    {
        return pending.shape != shape || (usesSlope (shape) && clampSlope (pending.slopeIndex) != slopeIndex);
    }

    // Empieza a sonar la forma pedida, con los ajustes ya en su sitio (sin rampas) y el filtro en limpio. Solo se llama
    // con mix en 0: no se oye.
    void startShape() noexcept
    {
        shape = pending.shape;
        slopeIndex = clampSlope (pending.slopeIndex);
        const auto targets = targetsFor (pending);
        octaves.snap (targets[0]);
        gainDb.snap (targets[1]);
        logQ.snap (targets[2]);
        for (auto& filter : filters)
            filter.reset();
        updateCoefficients();
    }

    void updateCoefficients() noexcept
    {
        cascade = makeCascade (shape, slopeIndex, octaves.value, gainDb.value, logQ.value, sampleRate, flatTilt);
    }

    double sampleRate = 44100.0;
    BandSettings pending;           // lo último que pidieron los parámetros
    Shape shape = Shape::bell;      // la forma que suena ahora
    int slopeIndex = 1;
    deepwater::dsp::LinearRamp octaves, gainDb, logQ, mix;
    deepwater::dsp::FlatTiltDesign flatTilt;
    deepwater::dsp::EqCascade cascade;
    std::array<deepwater::dsp::EqCascadeFilter, maxChannels> filters;
};

} // namespace fathom::dsp
