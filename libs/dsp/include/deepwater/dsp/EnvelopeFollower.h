#pragma once

#include <algorithm>
#include <cmath>

namespace deepwater::dsp
{

// Seguidor de envolvente (envelope follower): convierte una señal que oscila cientos de veces por segundo en una curva
// lenta que dice "qué tan fuerte suena ahora". Es la pieza de detección de un compresor, de un gate o de un auto-wah.
//
// Detector de picos "desacoplado" (Giannoulis, Massberg y Reiss, "Digital Dynamic Range Compressor Design", JAES 2012),
// en dos etapas:
//   1. pico con relajación: salta al instante a cualquier pico nuevo y, si no llega otro, cae exponencialmente:
//          peak = max(x, peak + (x − peak)·relajación)
//      En dB la caída es una recta de 8.7 dB por cada τ de relajación (se oye pareja en todo el rango).
//   2. ataque: un filtro de 1 polo sobre ese pico decide cuánto tarda la envolvente en subir: llega al 63 % en τ.
//          env += (peak − env)·ataque          coef = 1 − e^(−1 / (τ · sr))
//
// Por qué no la versión de una etapa (env se acerca a |x| con el coeficiente de ataque si sube y el de relajación si
// baja): con un tono sostenido |x| solo supera a env cerca de cada cresta, y con un ataque de varios ms no le da tiempo
// a llegar; se equilibra por debajo del pico (con un seno de 1 kHz y 5 ms de ataque, 2 dB abajo, y distinto en cada
// frecuencia). Separando las etapas el pico se mide entero y el ataque solo decide la velocidad.
//
// Un ataque corto sigue los transitorios; una relajación corta sigue cada ciclo de un bajo (ondula); larga, suaviza
// pero tarda en soltar. El estado va en double: con relajaciones de segundos a 96 kHz el coeficiente es ~5·10⁻⁶ y en
// float el paso se perdería por redondeo.
class EnvelopeFollower
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        reset();
    }

    void setTimes (double attackSeconds, double releaseSeconds) noexcept
    {
        attackCoef = coefficient (attackSeconds);
        releaseCoef = coefficient (releaseSeconds);
    }

    void reset (double value = 0.0) noexcept { peak = envelope = value; }

    // level = nivel instantáneo, ya rectificado (|x|, o el mayor |x| de los canales para que todos vean lo mismo).
    double process (double level) noexcept
    {
        peak = std::max (level, peak + (level - peak) * releaseCoef);
        envelope += (peak - envelope) * attackCoef;
        return envelope;
    }

    [[nodiscard]] double getValue() const noexcept { return envelope; }

private:
    [[nodiscard]] double coefficient (double seconds) const noexcept
    {
        return seconds > 0.0 ? 1.0 - std::exp (-1.0 / (seconds * sampleRate)) : 1.0;
    }

    double sampleRate = 44100.0;
    double attackCoef = 1.0, releaseCoef = 1.0;
    double peak = 0.0, envelope = 0.0;
};

} // namespace deepwater::dsp
