#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace deepwater::dsp
{

// --- Filtro de estado variable (SVF) con topología TPT / ZDF -----------------------------------
//
// Un filtro analógico es un circuito con integradores (condensadores) conectados en lazo de
// realimentación. La forma "clásica" de digitalizarlo mete un retardo de una muestra en ese lazo,
// y ese retardo desafina el corte y vuelve el filtro inestable con resonancia alta o al modular
// el cutoff rápido. TPT (Topology-Preserving Transform, Zavalishin) conserva el circuito tal cual
// y sustituye cada integrador por uno trapezoidal. El lazo queda "sin retardo" (Zero-Delay Feedback):
// en cada muestra se resuelve una ecuación lineal pequeña en vez de usar el valor de la muestra anterior.
//
// Resultado: la respuesta es la del filtro analógico con el eje de frecuencias "doblado" (bilineal),
// el corte cae exactamente donde se pide gracias a g = tan(π·fc/fs), y es estable con cualquier
// cutoff y resonancia, incluso si cambian en cada muestra (imprescindible para modular el filtro).

// Una etapa SVF de 2 polos (12 dB/oct). Da a la vez las salidas low-pass, band-pass y high-pass.
class SvfStage
{
public:
    struct Coefficients
    {
        float g = 0.0f;  // tan(π·fc/fs): la ganancia de cada integrador ya "prewarpeada"
        float k = 1.0f;  // 1/Q: amortiguación. Cuanto menor, más resonancia
        float a1 = 1.0f; // 1 / (1 + g·(g + k)): la solución de la ecuación del lazo sin retardo

        static Coefficients make (float g, float k) noexcept { return { g, k, 1.0f / (1.0f + g * (g + k)) }; }

        // Coeficientes para un corte en Hz y un Q (0.7071 = Butterworth: sin pico). Llama a tan(): no conviene
        // hacerlo en cada muestra, solo cuando el cutoff cambia. El corte se limita por debajo de Nyquist (en
        // Nyquist tan() se dispara a infinito).
        static Coefficients forCutoff (double cutoffHz, double q, double sampleRate) noexcept
        {
            const double cutoff = std::clamp (cutoffHz, 1.0, 0.49 * sampleRate);
            const auto g = static_cast<float> (std::tan (std::numbers::pi * cutoff / sampleRate));
            return make (g, static_cast<float> (1.0 / q));
        }
    };

    struct Outputs
    {
        float lowPass, bandPass, highPass;
    };

    void reset() noexcept { s1 = s2 = 0.0f; }

    [[nodiscard]] Outputs process (float x, const Coefficients& c) noexcept
    {
        // s1 y s2 son los "condensadores" (estado de los dos integradores).
        // Se despeja el high-pass de golpe (ZDF) y de él salen las otras dos salidas.
        const float highPass = (x - (c.g + c.k) * s1 - s2) * c.a1;
        const float v1 = c.g * highPass;
        const float bandPass = v1 + s1;
        s1 = bandPass + v1;
        const float v2 = c.g * bandPass;
        const float lowPass = v2 + s2;
        s2 = lowPass + v2;
        return { lowPass, bandPass, highPass };
    }

private:
    float s1 = 0.0f;
    float s2 = 0.0f;
};

} // namespace deepwater::dsp
