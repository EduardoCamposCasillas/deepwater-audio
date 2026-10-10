#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace deepwater::dsp
{

// --- Filtro de 1 polo TPT (6 dB/oct) -----------------------------------------------------------
//
// Es la transformada bilineal del filtro RC analógico w / (s + w): un solo integrador trapezoidal, con la misma idea
// TPT/ZDF que el SVF (ver Svf.h). Da a la vez el low-pass y su complemento, el high-pass = x − low-pass (los dos
// suman exactamente la entrada).
//
// El coeficiente se pasa en cada llamada (como en SvfStage): L y R comparten uno solo y, si el corte se modula, se
// calcula una vez por muestra para todos los canales.
class OnePole
{
public:
    // G = g / (1 + g), con g = tan(π·fc/fs) (el corte "prewarpeado": cae exactamente donde se pide).
    [[nodiscard]] static float coefficient (float g) noexcept { return g / (1.0f + g); }

    // Llama a tan(): solo cuando el corte cambia. Se limita por debajo de Nyquist (ahí tan() se dispara).
    [[nodiscard]] static float coefficientForCutoff (double cutoffHz, double sampleRate) noexcept
    {
        const double cutoff = std::clamp (cutoffHz, 1.0, 0.49 * sampleRate);
        return coefficient (static_cast<float> (std::tan (std::numbers::pi * cutoff / sampleRate)));
    }

    void reset() noexcept { state = 0.0f; }

    [[nodiscard]] float lowPass (float x, float G) noexcept
    {
        const float v = (x - state) * G;
        const float low = v + state;
        state = low + v;
        return low;
    }

    [[nodiscard]] float highPass (float x, float G) noexcept { return x - lowPass (x, G); }

private:
    float state = 0.0f;
};

// --- Tilt EQ ------------------------------------------------------------------------------------
//
// Inclina todo el espectro alrededor de un pivote: sube los agudos y baja los graves la misma cantidad (o al revés).
// Es un solo polo: H(s) = (A·s + w) / (s + A·w) = A·HP(s) + LP(s)/A, con el polo en A·w.
//   - en 0 Hz vale 1/A, en agudos A, y en el pivote w exactamente 1 (|jA + 1| = |j + A|): 0 dB;
//   - la transición es la de un filtro de 1 polo, muy suave (6 dB/oct): "tono" más que "ecualización".
// Con tilt = 0 dB (A = 1) es HP + LP = x: la señal pasa intacta (process() la copia tal cual).
class TiltFilter
{
public:
    struct Coefficients
    {
        float pole = 0.0f; // coeficiente del 1 polo (en A·pivote)
        float high = 1.0f; // A: ganancia en agudos
        float low = 1.0f;  // 1/A: ganancia en graves

        // tiltDb = ganancia en agudos (los graves reciben −tiltDb). Llama a tan() y pow().
        static Coefficients forTilt (double pivotHz, float tiltDb, double sampleRate) noexcept
        {
            const double a = std::pow (10.0, tiltDb / 20.0);
            return { OnePole::coefficientForCutoff (pivotHz * a, sampleRate), static_cast<float> (a), static_cast<float> (1.0 / a) };
        }

        [[nodiscard]] bool isFlat() const noexcept { return high == 1.0f; }
    };

    void reset() noexcept { filter.reset(); }

    // El filtro procesa siempre (su estado sigue a la señal); plano devuelve la entrada bit a bit.
    [[nodiscard]] float process (float x, const Coefficients& c) noexcept
    {
        const float low = filter.lowPass (x, c.pole);
        return c.isFlat() ? x : c.high * (x - low) + c.low * low;
    }

private:
    OnePole filter;
};

} // namespace deepwater::dsp
