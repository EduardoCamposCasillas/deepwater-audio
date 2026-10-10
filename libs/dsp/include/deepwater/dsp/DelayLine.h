#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace deepwater::dsp
{

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

    [[nodiscard]] int getMaxDelay() const noexcept { return static_cast<int> (buffer.size()) - 4; }

private:
    std::vector<float> buffer = std::vector<float> (4, 0.0f);
    size_t mask = 3;
    size_t writeIndex = 0;
};

} // namespace deepwater::dsp
