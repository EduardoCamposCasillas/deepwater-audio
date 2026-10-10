#pragma once

#include <algorithm>

namespace undertow::dsp
{

// tanh rápida: la fracción continua de Lambert, tanh x = x / (1 + x²/(3 + x²/(5 + x²/(7 + ...)))), cortada en el
// término 21 y escrita como un cociente de dos polinomios en x² (5 multiplicaciones-suma arriba, 5 abajo y una
// división). std::tanh cuesta varias veces más porque calcula exponenciales con precisión completa en todo el rango.
//
// Error máximo medido en float: 6·10⁻⁷ (−124 dB respecto a la señal saturada), menos que el ruido de cuantificación
// de un archivo de 24 bits. Por encima de |x| = 7.6 tanh ya vale 1 a menos de 5·10⁻⁷; ahí se limita la entrada (si
// no, los polinomios de grado alto se desbordarían con entradas enormes).
[[nodiscard]] inline float fastTanh (float x) noexcept
{
    x = std::clamp (x, -7.6f, 7.6f);
    const float y = x * x;
    const float p = ((((7.2730919e-11f * y + 1.5600782e-07f) * y + 4.9142464e-05f) * y + 4.6783626e-03f) * y + 0.14285714f) * y + 1.0f;
    const float q = ((((4.8002407e-09f * y + 3.2761643e-06f) * y + 5.5039560e-04f) * y + 3.0075188e-02f) * y + 0.47619048f) * y + 1.0f;
    return x * p / q;
}

} // namespace undertow::dsp
