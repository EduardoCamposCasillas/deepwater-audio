#pragma once

#include <algorithm>
#include <cmath>

namespace deepwater::dsp
{

// Suavizado de parámetros sin JUCE. Un parámetro que salta de golpe (automatización por bloques, un clic en la GUI)
// produce un escalón en la señal: se oye como clic o, si se repite cada bloque, como "zipper noise". Suavizarlo es
// pasarlo por un low-pass muy lento antes de usarlo.

// Suavizado de un parámetro con un filtro de 1 polo: se acerca a su objetivo un porcentaje fijo por muestra.
struct SmoothedParameter
{
    float value = 0.0f;
    float coef = 1.0f;

    void setTime (double seconds, double sampleRate) noexcept
    {
        coef = static_cast<float> (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
    }

    // Devuelve true si el valor cambió (para recalcular coeficientes solo cuando hace falta).
    bool approach (float goal) noexcept
    {
        if (value == goal)
            return false;
        value += (goal - value) * coef;
        if (std::abs (goal - value) < 1.0e-6f * std::max (1.0f, std::abs (goal)))
            value = goal;
        return true;
    }
};

// Rampa lineal hacia un objetivo en un tiempo fijo. A diferencia de SmoothedParameter llega EXACTAMENTE al objetivo
// (la última muestra de la rampa copia el objetivo) y después queda quieta: con una perilla de ganancia de vuelta en
// 0 dB la señal pasa intacta bit a bit, y el código puede saltarse cálculos mientras isSteady(). Si el objetivo cambia
// a mitad de rampa, empieza una rampa nueva desde el valor actual (sin saltos).
struct LinearRamp
{
    float value = 0.0f;
    float target = 0.0f;
    float step = 0.0f;
    int rampSamples = 1;
    int remaining = 0;

    void setTime (double seconds, double sampleRate) noexcept
    {
        rampSamples = std::max (1, static_cast<int> (std::lround (seconds * sampleRate)));
    }

    void snap (float newValue) noexcept
    {
        value = target = newValue;
        remaining = 0;
    }

    void setTarget (float goal) noexcept
    {
        if (goal == target)
            return;
        target = goal;
        remaining = rampSamples;
        step = (target - value) / static_cast<float> (rampSamples);
    }

    float next() noexcept
    {
        if (remaining > 0)
            value = --remaining == 0 ? target : value + step;
        return value;
    }

    [[nodiscard]] bool isSteady() const noexcept { return remaining == 0; }
};

// Fundido lineal de encendido/apagado de un efecto. Apagado y con el fundido terminado = no se procesa.
struct EffectSwitch
{
    float gain = 0.0f;
    float step = 1.0f;

    void setTime (double seconds, double sampleRate) noexcept { step = static_cast<float> (1.0 / (seconds * sampleRate)); }
    void snap (bool on) noexcept { gain = on ? 1.0f : 0.0f; }
    [[nodiscard]] bool isSilent (bool on) const noexcept { return ! on && gain == 0.0f; }
    [[nodiscard]] bool isSteady (bool on) const noexcept { return gain == (on ? 1.0f : 0.0f); }

    float next (bool on) noexcept
    {
        gain = on ? std::min (1.0f, gain + step) : std::max (0.0f, gain - step);
        return gain;
    }
};

} // namespace deepwater::dsp
