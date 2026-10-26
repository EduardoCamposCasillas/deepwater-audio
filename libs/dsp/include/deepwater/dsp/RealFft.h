#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace deepwater::dsp
{

// FFT de una señal REAL, pensada para el hilo de audio (nació en Fathom, Fase 10, para la convolución de la fase
// lineal; la reverb la va a reutilizar). A diferencia de fft() en Fft.h (clara pero lenta, solo fuera del audio):
// las tablas (giros y orden de bits invertidos) se calculan una vez en prepare() y transformar no reserva memoria.
//
// La idea: una señal real de N muestras tiene un espectro simétrico (X[N−k] = conj(X[k])), así que basta con N/2 + 1
// bins y con media FFT compleja. Se empaquetan las muestras pares como parte real y las impares como imaginaria,
// z[n] = x[2n] + j·x[2n+1], se hace la FFT compleja de N/2 puntos y se separan las dos mitades:
//     E[k] = (Z[k] + conj(Z[N/2−k])) / 2          (el espectro de las pares)
//     O[k] = (Z[k] − conj(Z[N/2−k])) / (2j)       (el de las impares)
//     X[k] = E[k] + e^(−j·2π·k/N) · O[k]          (una última "mariposa")
// La inversa deshace los mismos pasos al revés. Cuesta la mitad que una FFT compleja de N puntos.
//
// Formato: el espectro va en dos arrays separados, real e imaginario (N/2 + 1 valores cada uno): así los productos
// bin a bin de la convolución se vectorizan solos. forward() no escala; inverse() divide por N (inverse(forward(x)) = x).
// T = float (audio) o double (diseño de filtros largos, donde los errores de redondeo se acumularían).
template <typename T>
class RealFft
{
public:
    // Fuera del hilo de audio. 'newSize' = potencia de 2, al menos 4.
    void prepare (int newSize)
    {
        assert (newSize >= 4 && (newSize & (newSize - 1)) == 0 && "el tamaño debe ser potencia de 2 (>= 4)");
        size = newSize;
        half = size / 2;

        work.assign (static_cast<size_t> (2 * half), T (0));
        // Giros de la FFT compleja de N/2 puntos: e^(−j·2π·m/(N/2)), m < N/4 (cada etapa usa un subconjunto).
        twiddleRe.resize (static_cast<size_t> (std::max (1, half / 2)));
        twiddleIm.resize (twiddleRe.size());
        for (size_t m = 0; m < twiddleRe.size(); ++m)
        {
            const double angle = -2.0 * std::numbers::pi * static_cast<double> (m) / half;
            twiddleRe[m] = static_cast<T> (std::cos (angle));
            twiddleIm[m] = static_cast<T> (std::sin (angle));
        }
        // La mariposa final: e^(−j·2π·k/N), k = 0..N/2.
        splitRe.resize (static_cast<size_t> (half + 1));
        splitIm.resize (splitRe.size());
        for (size_t k = 0; k < splitRe.size(); ++k)
        {
            const double angle = -2.0 * std::numbers::pi * static_cast<double> (k) / size;
            splitRe[k] = static_cast<T> (std::cos (angle));
            splitIm[k] = static_cast<T> (std::sin (angle));
        }
        // Pares (i, j) con j = i con los bits invertidos: el reordenamiento previo a las mariposas.
        swaps.clear();
        for (int i = 1, j = 0; i < half; ++i)
        {
            int bit = half >> 1;
            for (; (j & bit) != 0; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                swaps.push_back ({ i, j });
        }
    }

    [[nodiscard]] int getSize() const noexcept { return size; }
    [[nodiscard]] int getNumBins() const noexcept { return half + 1; }

    // 'input': N muestras. 're'/'im': N/2 + 1 bins (im[0] e im[N/2] salen en 0).
    void forward (const T* input, T* re, T* im) noexcept
    {
        for (int i = 0; i < 2 * half; ++i)
            work[static_cast<size_t> (i)] = input[i]; // pares = parte real, impares = imaginaria
        transform (false);
        for (int k = 0; k <= half; ++k)
        {
            const auto a = static_cast<size_t> (k % half), b = static_cast<size_t> ((half - k) % half);
            const T zr = work[2 * a], zi = work[2 * a + 1];
            const T cr = work[2 * b], ci = -work[2 * b + 1]; // conj(Z[N/2 − k])
            const T er = T (0.5) * (zr + cr), ei = T (0.5) * (zi + ci);
            // O = (Z − conj(Z')) / (2j) = −j·(Z − conj(Z'))/2
            const T orr = T (0.5) * (zi - ci), oi = T (-0.5) * (zr - cr);
            const T wr = splitRe[static_cast<size_t> (k)], wi = splitIm[static_cast<size_t> (k)];
            re[k] = er + wr * orr - wi * oi;
            im[k] = ei + wr * oi + wi * orr;
        }
        im[0] = T (0);
        im[half] = T (0);
    }

    // 're'/'im': N/2 + 1 bins de un espectro de señal real (las partes imaginarias de DC y Nyquist se ignoran).
    // 'output': N muestras.
    void inverse (const T* re, const T* im, T* output) noexcept
    {
        for (int k = 0; k < half; ++k)
        {
            const T xr = re[k], xi = k == 0 ? T (0) : im[k];
            const T cr = re[half - k], ci = half - k == half ? T (0) : -im[half - k]; // conj(X[N/2 − k])
            const T er = T (0.5) * (xr + cr), ei = T (0.5) * (xi + ci);
            // O = (X − conj(X')) / 2 · e^(+j·2π·k/N)
            const T dr = T (0.5) * (xr - cr), di = T (0.5) * (xi - ci);
            const T wr = splitRe[static_cast<size_t> (k)], wi = -splitIm[static_cast<size_t> (k)];
            const T orr = dr * wr - di * wi, oi = dr * wi + di * wr;
            // Z = E + j·O
            work[2 * static_cast<size_t> (k)] = er - oi;
            work[2 * static_cast<size_t> (k) + 1] = ei + orr;
        }
        transform (true);
        const T scale = T (1) / static_cast<T> (half);
        for (int i = 0; i < 2 * half; ++i)
            output[i] = work[static_cast<size_t> (i)] * scale;
    }

private:
    struct Swap
    {
        int a, b;
    };

    // FFT compleja de N/2 puntos sobre 'work' (re, im intercalados), en el sitio y sin escalar.
    void transform (bool inverse) noexcept
    {
        T* data = work.data();
        for (const auto& s : swaps)
        {
            std::swap (data[2 * s.a], data[2 * s.b]);
            std::swap (data[2 * s.a + 1], data[2 * s.b + 1]);
        }
        const T sign = inverse ? T (-1) : T (1); // la inversa usa los giros conjugados
        for (int length = 2; length <= half; length <<= 1)
        {
            const int halfLength = length / 2;
            const int stride = half / length; // el giro k de esta etapa es el k·stride de la tabla
            for (int start = 0; start < half; start += length)
                for (int k = 0; k < halfLength; ++k)
                {
                    const T wr = twiddleRe[static_cast<size_t> (k * stride)], wi = sign * twiddleIm[static_cast<size_t> (k * stride)];
                    T* even = data + 2 * (start + k);
                    T* odd = data + 2 * (start + k + halfLength);
                    const T tr = odd[0] * wr - odd[1] * wi, ti = odd[0] * wi + odd[1] * wr;
                    odd[0] = even[0] - tr;
                    odd[1] = even[1] - ti;
                    even[0] += tr;
                    even[1] += ti;
                }
        }
    }

    int size = 0, half = 0;
    std::vector<T> work, twiddleRe, twiddleIm, splitRe, splitIm;
    std::vector<Swap> swaps;
};

} // namespace deepwater::dsp
