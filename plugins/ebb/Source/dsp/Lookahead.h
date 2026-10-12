#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "deepwater/dsp/DelayLine.h"

namespace ebb::dsp
{

// Lookahead (Fase 5): retrasa la señal que se va a atenuar mientras el disparo (sidechain, MIDI o la cuadrícula) llega
// sin retraso. Así la curva empieza 'delay' muestras ANTES de que suene el golpe y el volumen ya está abajo cuando llega.
// El procesador informa ese retraso al host como latencia, y FL atrasa las demás pistas para que todo siga alineado.
//
// Cambiar el retraso de golpe sería un salto en la señal (un clic): se hace un fundido cruzado de 20 ms entre la lectura
// vieja y la nueva. Si llega otro cambio durante el fundido, espera a que termine. Con retraso 0 la salida es la entrada
// bit a bit (read (0) es la muestra recién escrita).
class LookaheadDelay
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double fadeSeconds = 0.02;

    // Fuera del hilo de audio: reserva las líneas de retardo.
    void prepare (double sampleRate, int maxDelaySamples, int initialDelay)
    {
        maxDelay = std::max (0, maxDelaySamples);
        for (auto& line : lines)
            line.prepare (maxDelay);
        fadeSamples = std::max (1, static_cast<int> (std::lround (fadeSeconds * sampleRate)));
        current = next = target = std::clamp (initialDelay, 0, maxDelay);
        fadePosition = 0;
        fading = false;
    }

    void setDelay (int samples) noexcept { target = std::clamp (samples, 0, maxDelay); }

    // Una vez por muestra, antes de process() en cada canal.
    void advance() noexcept
    {
        if (fading && ++fadePosition >= fadeSamples)
        {
            current = next;
            fading = false;
        }
        if (! fading && target != current)
        {
            next = target;
            fadePosition = 0;
            fading = true;
        }
    }

    [[nodiscard]] float process (int channel, float input) noexcept
    {
        auto& line = lines[static_cast<size_t> (channel)];
        line.push (input);
        const float now = line.read (current);
        if (! fading)
            return now;
        const float t = static_cast<float> (fadePosition) / static_cast<float> (fadeSamples);
        return now + (line.read (next) - now) * t;
    }

private:
    std::array<deepwater::dsp::DelayLine, maxChannels> lines;
    int maxDelay = 0;
    int current = 0, next = 0, target = 0;
    int fadeSamples = 1;
    int fadePosition = 0;
    bool fading = false;
};

} // namespace ebb::dsp
