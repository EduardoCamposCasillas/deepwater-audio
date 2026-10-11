#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace deepwater::dsp
{

// Interpolación de Lagrange de 3.er orden: el polinomio cúbico que pasa por 4 muestras vecinas, evaluado en el
// retardo pedido. Comparada con Catmull-Rom (readCubic) tiene una propiedad que importa en un lazo afinado: en graves
// su retardo es EXACTAMENTE el pedido (un polinomio de grado 3 se reproduce sin error), así que no desafina el
// fundamental. Es un FIR (sin estado): el retardo se puede mover en cada muestra sin transitorios.
//
// La fracción se mantiene en [1, 2) respecto a la primera toma (el punto pedido queda entre las dos centrales): ahí la
// respuesta nunca pasa de 1 (segura dentro de una realimentación) y la pérdida de agudos es la menor posible.
// Precio: con fracción 1.5 atenúa los agudos (cero en Nyquist); con fracción 1 no toca nada.
struct LagrangeTaps
{
    int first = 0;                              // retardo entero de la primera toma
    std::array<float, 4> weights { 0, 1, 0, 0 }; // pesos de las tomas first .. first + 3
    double fraction = 1.0;                      // retardo dentro de las 4 tomas, en [1, 2)

    // Retardo total ≥ 1 muestra (en double: con retardos de miles de muestras un float perdería la fracción).
    [[nodiscard]] static LagrangeTaps forDelay (double delay) noexcept
    {
        LagrangeTaps taps;
        delay = std::max (delay, 1.0);
        taps.first = static_cast<int> (std::floor (delay)) - 1;
        taps.fraction = delay - static_cast<double> (taps.first);
        const double d = taps.fraction;
        taps.weights = { static_cast<float> (-(d - 1.0) * (d - 2.0) * (d - 3.0) / 6.0),
                         static_cast<float> (d * (d - 2.0) * (d - 3.0) / 2.0),
                         static_cast<float> (-d * (d - 1.0) * (d - 3.0) / 2.0),
                         static_cast<float> (d * (d - 1.0) * (d - 2.0) / 6.0) };
        return taps;
    }

    // Respuesta en frecuencia de las 4 tomas (sin contar 'first') a la frecuencia angular 'omega' (radianes por muestra).
    [[nodiscard]] std::complex<double> response (double omega) const noexcept
    {
        std::complex<double> sum {};
        for (size_t k = 0; k < weights.size(); ++k)
            sum += static_cast<double> (weights[k]) * std::polar (1.0, -omega * static_cast<double> (k));
        return sum;
    }

    // Retardo de fase total (en muestras) a 'omega' > 0. En graves es el retardo pedido; hacia los agudos se aparta un
    // poco: un lazo afinado lo compensa con esto.
    [[nodiscard]] double phaseDelay (double omega) const noexcept
    {
        return static_cast<double> (first) - std::arg (response (omega)) / omega;
    }

    // Ganancia a 'omega' (≤ 1 con la fracción en [1, 2)).
    [[nodiscard]] double magnitude (double omega) const noexcept { return std::abs (response (omega)); }
};

// Línea de retardo: un buffer circular donde se escribe la señal y se lee "desde el pasado". Es la pieza de la
// que salen el chorus, el delay y la reverb (y el lookahead de un compresor). Sin dependencias de JUCE.
//
// El tamaño se reserva UNA vez en prepare (fuera del hilo de audio); escribir y leer no reservan nada.
// El tamaño es potencia de 2: "dar la vuelta" al buffer es un AND de bits en vez de una división.
class DelayLine
{
public:
    // Se llama fuera del hilo de audio. Reserva sitio para 'maxDelaySamples' más el margen de la interpolación.
    void prepare (int maxDelaySamples)
    {
        size_t size = 1;
        while (size < static_cast<size_t> (maxDelaySamples) + 4)
            size <<= 1;
        buffer.assign (size, 0.0f);
        mask = size - 1;
        writeIndex = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    void push (float sample) noexcept
    {
        writeIndex = (writeIndex + 1) & mask;
        buffer[writeIndex] = sample;
    }

    // Muestra de hace exactamente 'delay' muestras (0 = la última escrita).
    [[nodiscard]] float read (int delay) const noexcept
    {
        return buffer[(writeIndex - static_cast<size_t> (delay)) & mask];
    }

    // Retardo fraccionario con interpolación lineal: barata y nunca amplifica (segura dentro de una realimentación).
    // Atenúa un poco los agudos cuando la fracción está cerca de 0.5.
    [[nodiscard]] float readLinear (float delay) const noexcept
    {
        const auto whole = static_cast<int> (delay);
        const float fraction = delay - static_cast<float> (whole);
        const float a = read (whole);
        const float b = read (whole + 1);
        return a + (b - a) * fraction;
    }

    // Retardo fraccionario con interpolación cúbica (Catmull-Rom, la misma idea que en el oscilador de la Fase 3):
    // conserva los agudos. Necesita 'delay' ≥ 1.
    [[nodiscard]] float readCubic (float delay) const noexcept
    {
        const auto whole = static_cast<int> (delay);
        const float t = delay - static_cast<float> (whole);
        const float ym1 = read (whole - 1);
        const float y0 = read (whole);
        const float y1 = read (whole + 1);
        const float y2 = read (whole + 2);
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    // Retardo fraccionario con Lagrange de 3.er orden (ver LagrangeTaps). Los coeficientes se calculan aparte para
    // reutilizarlos en cada muestra mientras el retardo no cambie (en una cuerda de Karplus-Strong, casi siempre).
    [[nodiscard]] float readLagrange (const LagrangeTaps& taps) const noexcept
    {
        return taps.weights[0] * read (taps.first) + taps.weights[1] * read (taps.first + 1)
               + taps.weights[2] * read (taps.first + 2) + taps.weights[3] * read (taps.first + 3);
    }

    [[nodiscard]] int getMaxDelay() const noexcept { return static_cast<int> (buffer.size()) - 4; }

private:
    std::vector<float> buffer = std::vector<float> (4, 0.0f);
    size_t mask = 3;
    size_t writeIndex = 0;
};

} // namespace deepwater::dsp
