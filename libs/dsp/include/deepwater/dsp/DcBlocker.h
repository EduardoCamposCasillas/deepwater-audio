#pragma once

#include <cmath>
#include <numbers>

namespace deepwater::dsp
{

// Filtro DC: quita la componente de 0 Hz (un desplazamiento constante de la onda hacia arriba o hacia abajo).
// Una curva asimétrica (un bulbo, un rectificador) la genera: la onda deja de estar centrada en 0. No se oye, pero
// gasta headroom, hace que el siguiente saturador trabaje distinto en cada semiciclo y produce clics al cortar.
//
// Es un high-pass de 1 polo muy bajo:   y[n] = x[n] − x[n−1] + R · y[n−1]
// La resta x[n] − x[n−1] (un cero en 0 Hz) mata la DC; el polo en R, muy cerca de 1, devuelve todo lo demás casi
// intacto. R = e^(−2π·fc/sr): cuanto más cerca de 1, más bajo el corte (y más tarda en asentarse).
class DcBlocker
{
public:
    // Fuera del hilo de audio (calcula una exponencial).
    void prepare (double cutoffHz, double sampleRate) noexcept
    {
        pole = static_cast<float> (std::exp (-2.0 * std::numbers::pi * cutoffHz / sampleRate));
        reset();
    }

    void reset() noexcept { previousInput = previousOutput = 0.0f; }

    [[nodiscard]] float process (float x) noexcept
    {
        const float y = x - previousInput + pole * previousOutput;
        previousInput = x;
        previousOutput = y;
        return y;
    }

private:
    float pole = 0.999f;
    float previousInput = 0.0f;
    float previousOutput = 0.0f;
};

} // namespace deepwater::dsp
