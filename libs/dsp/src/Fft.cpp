#include "deepwater/dsp/Fft.h"

#include <cassert>
#include <cmath>
#include <numbers>
#include <utility>

namespace deepwater::dsp
{

void fft (std::vector<std::complex<double>>& data, bool inverse)
{
    const size_t n = data.size();
    assert (n > 0 && (n & (n - 1)) == 0 && "el tamaño debe ser potencia de 2");

    // 1) Reordenar por índice con los bits invertidos: deja juntas las muestras que se combinan
    //    en la primera etapa de mariposas.
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;

        if (i < j)
            std::swap (data[i], data[j]);
    }

    // 2) Mariposas: en cada etapa se fusionan DFT de tamaño len/2 en DFT de tamaño len.
    const double sign = inverse ? 1.0 : -1.0;
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const size_t half = len / 2;
        for (size_t k = 0; k < half; ++k)
        {
            // El "twiddle" depende solo de k: se calcula una vez por k y no por cada mariposa.
            const auto twiddle = std::polar (1.0, sign * 2.0 * std::numbers::pi * static_cast<double> (k)
                                                      / static_cast<double> (len));
            for (size_t start = 0; start < n; start += len)
            {
                const auto even = data[start + k];
                const auto odd = data[start + k + half] * twiddle;
                data[start + k] = even + odd;
                data[start + k + half] = even - odd;
            }
        }
    }

    if (inverse)
        for (auto& value : data)
            value /= static_cast<double> (n);
}

} // namespace deepwater::dsp
