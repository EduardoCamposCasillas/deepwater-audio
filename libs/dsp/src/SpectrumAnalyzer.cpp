#include "deepwater/dsp/SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "deepwater/dsp/Fft.h"

namespace deepwater::dsp
{

void SpectrumAnalyzer::prepare (int newOrder)
{
    order = std::clamp (newOrder, 4, 16);
    const size_t size = size_t { 1 } << order;
    // Hann "periódica" (divide por N, no por N − 1): es la que hace exacta la calibración 4/N en los bins.
    window.resize (size);
    for (size_t n = 0; n < size; ++n)
        window[n] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (n) / static_cast<double> (size)));
    bins.assign (size, {});
    levels.assign (size / 2 + 1, floorDb);
}

int SpectrumAnalyzer::orderForSampleRate (double sampleRate) noexcept
{
    return sampleRate > 66000.0 ? 13 : 12;
}

void SpectrumAnalyzer::analyze (const float* samples)
{
    const size_t size = window.size();
    if (size == 0)
        return;
    for (size_t n = 0; n < size; ++n)
        bins[n] = { static_cast<double> (samples[n] * window[n]), 0.0 };
    fft (bins, false);

    // Amplitud = |X|·4/N (ver el header); en DC y en Nyquist el seno no se reparte entre frecuencias positivas y
    // negativas: ahí es |X|·2/N.
    const double scale = 4.0 / static_cast<double> (size);
    for (size_t k = 0; k < levels.size(); ++k)
    {
        const double amplitude = std::abs (bins[k]) * ((k == 0 || k == size / 2) ? 0.5 * scale : scale);
        levels[k] = amplitude > 0.0 ? std::max (floorDb, static_cast<float> (20.0 * std::log10 (amplitude))) : floorDb;
    }
}

float SpectrumAnalyzer::levelDb (double lowHz, double highHz, double sampleRate) const noexcept
{
    if (levels.empty() || sampleRate <= 0.0)
        return floorDb;
    const double binHz = sampleRate / static_cast<double> (window.size());
    const double last = static_cast<double> (levels.size() - 1);
    const double low = std::clamp (lowHz / binHz, 0.0, last);
    const double high = std::clamp (highHz / binHz, 0.0, last);

    // El nivel interpolado (lineal en dB) en el centro de la zona: lo que cuenta cuando la zona es más angosta que un
    // bin. Si dentro hay bins enteros, gana el más fuerte de ellos.
    const double centre = 0.5 * (low + high);
    const auto below = static_cast<size_t> (std::floor (centre));
    const size_t above = std::min (below + 1, levels.size() - 1);
    const auto fraction = static_cast<float> (centre - static_cast<double> (below));
    float strongest = levels[below] + (levels[above] - levels[below]) * fraction;
    for (auto k = static_cast<size_t> (std::ceil (low)); static_cast<double> (k) <= high; ++k)
        strongest = std::max (strongest, levels[k]);
    return strongest;
}

} // namespace deepwater::dsp
