#pragma once

#include <complex>
#include <vector>

namespace deepwater::dsp
{

// FFT radix-2 (Cooley-Tukey iterativa), en el sitio. Solo se usa FUERA del hilo de audio
// (al construir wavetables, en analizadores fuera del audio y en los tests), así que prioriza claridad sobre velocidad.
// El tamaño debe ser potencia de 2.
//
//   inverse = false:  X[k] = Σ x[n] · e^(-i·2π·k·n/N)            (tiempo -> frecuencia)
//   inverse = true:   x[n] = (1/N) · Σ X[k] · e^(+i·2π·k·n/N)    (frecuencia -> tiempo)
//
// La idea: una DFT de N puntos se parte en dos DFT de N/2 (muestras pares e impares) que se
// combinan con una "mariposa". Repetido log2(N) veces cuesta N·log2(N) en vez de N².
void fft (std::vector<std::complex<double>>& data, bool inverse);

} // namespace deepwater::dsp
