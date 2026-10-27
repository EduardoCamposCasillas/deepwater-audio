#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "deepwater/dsp/SpectrumAnalyzer.h"
#include "edit/CurveFit.h"

// Fase 12: igualar el espectro a una referencia. Sin JUCE (se prueba en tests/EngineTests.cpp); corre en el hilo de la
// GUI con lo que el audio deja en las tomas del analizador.
//
// El analizador de siempre muestra el AHORA (un trozo de 93 ms): sube y baja con cada nota y cada golpe. Para comparar el
// "color" de dos mezclas hace falta lo contrario: el espectro PROMEDIO de muchos segundos (en inglés, LTAS: long-term
// average spectrum), donde las notas se reparten y queda el balance tonal. Se promedia la POTENCIA (no los dB) de cada bin
// de la FFT a lo largo de los trozos: es la energía media en cada frecuencia, lo que el oído integra.
//
// La curva para igualar es la resta de los dos promedios en dB (referencia − pista): dónde a la pista le falta o le
// sobra. Antes de restar, cada espectro se suaviza en fracciones de octava (promediar la potencia de los bins dentro de
// una ventana de, por ejemplo, 1/3 de octava alrededor de cada punto): sin eso la curva copiaría cada nota y cada
// armónico de la referencia (un EQ que solo sirve para esa canción). A la resta se le quita su promedio entre 50 Hz y
// 10 kHz: igualar el COLOR, no el volumen (una referencia más fuerte no debe convertirse en +6 dB en todas partes).
namespace fathom::analysis
{

// El espectro promedio de una señal: la suma de la potencia de cada bin en cada trozo analizado, y cuántos trozos.
class AverageSpectrum
{
public:
    // Un trozo más suave que esto (RMS) no cuenta: los silencios entre partes no deben "oscurecer" el promedio.
    static constexpr float gateDb = -70.0f;

    // Una FFT de unos 186 ms (8192 puntos a 44.1/48 kHz, 16384 a 88.2/96): 5.4 Hz por bin, suficiente detalle en los
    // graves. Con otra frecuencia de muestreo, lo aprendido se borra (los bins ya no significan lo mismo).
    void prepare (double newSampleRate)
    {
        const int order = deepwater::dsp::SpectrumAnalyzer::orderForSampleRate (newSampleRate) + 1;
        if (newSampleRate == sampleRate && order == analyzer.getOrder())
            return;
        sampleRate = newSampleRate;
        analyzer.prepare (order);
        scratch.assign (static_cast<size_t> (analyzer.getSize()), 0.0f);
        sum.assign (static_cast<size_t> (analyzer.getSize() / 2 + 1), 0.0);
        clear();
    }

    void clear() noexcept
    {
        std::fill (sum.begin(), sum.end(), 0.0);
        frames = 0;
        countedSamples = 0;
    }

    // Lo nuevo de una toma (AnalyzerTap): si el audio escribió algo desde la última vez, se analizan las últimas
    // getSize() muestras. Devuelve true si se sumó un trozo.
    template <typename Tap>
    bool feed (const Tap& tap)
    {
        const auto written = tap.getWritten();
        if (scratch.empty() || written == lastWritten || ! tap.copyLatest (scratch.data(), scratch.size()))
            return false;
        const auto fresh = std::min<std::uint64_t> (written - lastWritten, scratch.size());
        lastWritten = written;
        return add (scratch.data(), fresh);
    }

    // Suma un trozo de getSize() muestras, de las que 'freshSamples' son nuevas (cuentan para los segundos aprendidos).
    bool add (const float* samples, std::uint64_t freshSamples)
    {
        if (sum.empty())
            return false;
        const auto size = static_cast<size_t> (analyzer.getSize());
        double energy = 0.0;
        for (size_t i = 0; i < size; ++i)
            energy += static_cast<double> (samples[i]) * static_cast<double> (samples[i]);
        if (10.0 * std::log10 (std::max (energy / static_cast<double> (size), 1.0e-30)) < static_cast<double> (gateDb))
            return false;
        analyzer.analyze (samples);
        const auto& bins = analyzer.binsDb();
        for (size_t k = 0; k < sum.size(); ++k)
            sum[k] += std::pow (10.0, 0.1 * static_cast<double> (bins[k]));
        ++frames;
        countedSamples += freshSamples;
        return true;
    }

    [[nodiscard]] bool hasData() const noexcept { return frames > 0; }
    [[nodiscard]] double seconds() const noexcept { return sampleRate > 0.0 ? static_cast<double> (countedSamples) / sampleRate : 0.0; }
    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }
    [[nodiscard]] int getSize() const noexcept { return analyzer.getSize(); }

    // El nivel promedio (dB, la escala del analizador: un seno de 0 dBFS en su bin = 0 dB) alrededor de 'hz', suavizado
    // en una ventana de 'octaves' octavas: la potencia media de los bins que caen dentro. Si la ventana es más angosta
    // que un bin (graves con poco suavizado), la potencia interpolada entre los dos bins vecinos.
    [[nodiscard]] float levelDb (double hz, double octaves) const noexcept
    {
        if (frames == 0 || sum.size() < 2)
            return deepwater::dsp::SpectrumAnalyzer::floorDb;
        const double binHz = sampleRate / static_cast<double> (analyzer.getSize());
        const double last = static_cast<double> (sum.size() - 1);
        const double half = std::exp2 (0.5 * octaves);
        const auto low = static_cast<size_t> (std::clamp (std::ceil (hz / half / binHz), 1.0, last));
        const auto high = static_cast<size_t> (std::clamp (std::floor (hz * half / binHz), 0.0, last));
        double power = 0.0;
        if (high >= low)
        {
            for (size_t k = low; k <= high; ++k)
                power += sum[k];
            power /= static_cast<double> (high - low + 1);
        }
        else
        {
            const double position = std::clamp (hz / binHz, 0.0, last);
            const auto below = static_cast<size_t> (position);
            const auto above = std::min (below + 1, sum.size() - 1);
            const double t = position - static_cast<double> (below);
            power = sum[below] + (sum[above] - sum[below]) * t;
        }
        return static_cast<float> (10.0 * std::log10 (std::max (power / static_cast<double> (frames), 1.0e-14)));
    }

private:
    deepwater::dsp::SpectrumAnalyzer analyzer;
    std::vector<double> sum;
    std::vector<float> scratch;
    std::uint64_t frames = 0, countedSamples = 0, lastWritten = 0;
    double sampleRate = 0.0;
};

// Los suavizados que se ofrecen (en octavas): 1 = solo el balance grueso; 1/6 = sigue detalles (más riesgo de copiar la
// canción de referencia en vez de su color).
inline constexpr std::array<double, 4> matchSmoothingOctaves { 1.0, 1.0 / 2.0, 1.0 / 3.0, 1.0 / 6.0 };
inline constexpr std::array<const char*, 4> matchSmoothingNames { "1 oct", "1/2 oct", "1/3 oct", "1/6 oct" };
inline constexpr int defaultMatchSmoothing = 2; // 1/3 de octava: lo que se usa para hablar del balance tonal

// La curva de EQ (dB por punto de la grilla, ya escalada por 'amount') que lleva 'current' hacia 'reference', y el peso
// de cada punto: 0 donde alguno de los dos no tiene energía de verdad (más de 80 dB bajo su punto más fuerte, o sobre
// 0.45·fs): ahí la resta sería ruido y no se le pide nada a las bandas. Sin datos en alguno, todo en 0.
inline edit::CurveTarget matchCurve (const AverageSpectrum& reference, const AverageSpectrum& current, double octaves, float amount)
{
    static constexpr float maxMatchDb = 24.0f;    // ni la mejor referencia justifica más
    static constexpr float usableRangeDb = 80.0f; // bajo el punto más fuerte de cada espectro
    edit::CurveTarget result;
    if (! reference.hasData() || ! current.hasData())
        return result;
    constexpr int points = edit::CurveGrid::points;
    std::array<float, points> ref {}, cur {};
    float refTop = deepwater::dsp::SpectrumAnalyzer::floorDb, curTop = refTop;
    for (int i = 0; i < points; ++i)
    {
        const double hz = edit::CurveGrid::hz (i);
        ref[static_cast<size_t> (i)] = reference.levelDb (hz, octaves);
        cur[static_cast<size_t> (i)] = current.levelDb (hz, octaves);
        refTop = std::max (refTop, ref[static_cast<size_t> (i)]);
        curTop = std::max (curTop, cur[static_cast<size_t> (i)]);
    }
    const double nyquistLimit = 0.45 * std::min (reference.getSampleRate(), current.getSampleRate());
    double offset = 0.0, everywhere = 0.0;
    int counted = 0, countedEverywhere = 0;
    for (int i = 0; i < points; ++i)
    {
        const auto at = static_cast<size_t> (i);
        const double hz = edit::CurveGrid::hz (i);
        const bool usable = hz < nyquistLimit && ref[at] > refTop - usableRangeDb && cur[at] > curTop - usableRangeDb;
        result.weight[at] = usable ? 1.0f : 0.0f;
        if (! usable)
            continue;
        const float difference = ref[at] - cur[at];
        result.db[at] = difference;
        everywhere += static_cast<double> (difference);
        ++countedEverywhere;
        if (hz >= 50.0 && hz <= 10000.0)
        {
            offset += static_cast<double> (difference);
            ++counted;
        }
    }
    if (countedEverywhere == 0)
        return result;
    offset = counted > 0 ? offset / counted : everywhere / countedEverywhere;
    for (size_t i = 0; i < result.db.size(); ++i)
        result.db[i] = result.weight[i] > 0.0f
                           ? std::clamp (result.db[i] - static_cast<float> (offset), -maxMatchDb, maxMatchDb) * amount
                           : 0.0f;
    return result;
}

} // namespace fathom::analysis
