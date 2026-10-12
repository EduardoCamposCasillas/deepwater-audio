#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

#include "deepwater/dsp/Svf.h"

namespace deepwater::dsp
{

// --- Filtros de EQ construidos sobre el SVF ---------------------------------------------------------------------------
//
// Un SVF da a la vez tres salidas de la misma resonancia: low-pass, band-pass y high-pass. Con s la frecuencia
// normalizada al corte (ver Svf.h) y D(s) = s² + k·s + 1, sus funciones de transferencia son
//     LP = 1/D        BP = s/D        HP = s²/D
// Casi todas las formas de un EQ son una MEZCLA de la entrada con esas salidas:  y = dry·x + bp·BP + lp·LP.
//
// La campana (peaking): sumarle a la entrada un poco de band-pass sube (o baja, si se resta) una zona alrededor del
// corte. Con A = 10^(dB/40) y la amortiguación k = 1/(Q·A):
//     H(s) = 1 + k·(A² − 1)·BP = (s² + (A/Q)·s + 1) / (s² + s/(A·Q) + 1)
// que es el prototipo analógico clásico de la campana (el del "Audio EQ Cookbook" de R. Bristow-Johnson): en el centro
// vale A² (la ganancia pedida), lejos vale 1, y +x dB y −x dB son inversas exactas (una deshace a la otra). Que k
// dependa de A hace que el ancho "se vea" igual al subir y al bajar.
//
// Como el SVF es TPT (bilineal), la respuesta digital es la analógica evaluada en s = j·tan(π·f/fs)/g: response()
// la calcula con ESOS MISMOS coeficientes (float), así la curva que dibuja la GUI es la del audio, no una aproximación.
// (El precio de la bilineal: cerca de Nyquist la campana se "aplasta" respecto de la analógica. Fathom lo corrige en
// su Fase 7.)
struct EqCoefficients
{
    SvfStage::Coefficients svf;
    float dry = 1.0f;      // cuánto de la entrada
    float bandPass = 0.0f; // cuánto de la salida band-pass (s/D)
    float lowPass = 0.0f;  // cuánto de la salida low-pass (1/D)

    // Campana centrada en 'hz', con su Q y su ganancia en dB. 0 dB da dry = 1 y bp = 0: la señal pasa intacta.
    static EqCoefficients bell (double hz, double q, double gainDb, double sampleRate) noexcept
    {
        const double a = std::pow (10.0, gainDb / 40.0);
        const double k = 1.0 / (q * a);
        const auto svf = SvfStage::Coefficients::forCutoff (hz, 1.0 / k, sampleRate);
        return { svf, 1.0f, static_cast<float> (k * (a * a - 1.0)), 0.0f };
    }

    // Respuesta en frecuencia (compleja: magnitud y fase) en 'hz'. Exacta para estos coeficientes.
    [[nodiscard]] std::complex<double> response (double hz, double sampleRate) const noexcept
    {
        // En Nyquist o encima, tan() se dispara: s → ∞, y BP y LP valen 0 (solo queda la parte "dry").
        if (hz >= 0.5 * sampleRate)
            return { static_cast<double> (dry), 0.0 };
        const std::complex<double> s { 0.0, std::tan (std::numbers::pi * hz / sampleRate) / static_cast<double> (svf.g) };
        const auto denominator = s * s + static_cast<double> (svf.k) * s + 1.0;
        return static_cast<double> (dry) + (static_cast<double> (bandPass) * s + static_cast<double> (lowPass)) / denominator;
    }

    [[nodiscard]] double magnitudeDb (double hz, double sampleRate) const noexcept
    {
        return 20.0 * std::log10 (std::max (std::abs (response (hz, sampleRate)), 1.0e-12));
    }
};

// Un filtro de EQ de 2 polos para UN canal: el SVF + la mezcla de sus salidas. El SVF corre siempre (aunque la mezcla
// lo ignore), así al pasar de 0 dB a otra ganancia su estado ya está "al día" y no hay transitorio.
class EqFilter
{
public:
    void reset() noexcept { stage.reset(); }

    [[nodiscard]] float process (float x, const EqCoefficients& c) noexcept
    {
        const auto out = stage.process (x, c.svf);
        // Con dry = 1 y bp = lp = 0 la suma es x + 0 + 0 = x: una banda en 0 dB deja la señal intacta bit a bit.
        return c.dry * x + c.bandPass * out.bandPass + c.lowPass * out.lowPass;
    }

private:
    SvfStage stage;
};

} // namespace deepwater::dsp
