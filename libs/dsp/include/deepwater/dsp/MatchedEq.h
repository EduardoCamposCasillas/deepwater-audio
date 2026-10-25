#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>

#include "deepwater/dsp/EqFilter.h"

namespace deepwater::dsp
{

// --- Respuesta analógica sin latencia: filtros de EQ "matched" sobre el SVF -------------------------------------------
//
// El problema. El SVF TPT es la transformada bilineal del filtro analógico: el eje de frecuencias entero (0 a ∞) se
// "dobla" para caber entre 0 y Nyquist (f_digital = (fs/π)·atan(...)). Lejos de Nyquist no se nota; cerca, todo se
// aprieta: una campana en 12 kHz a 44.1 kHz sale más angosta que la analógica y, como la bilineal vale lo mismo en
// Nyquist que el analógico en el infinito, su lado de arriba cae a 0 dB justo en 22 kHz (la "campana aplastada",
// "cramping"). Un shelf de agudos aprieta su subida (empieza tarde y llega antes a la meseta); un corte de agudos se
// hunde a −∞ en Nyquist.
//
// Dos soluciones. (1) Sobremuestrear: a ×4 Nyquist queda lejos y la bilineal ya no aprieta en lo audible, pero cuesta
// ×4 de CPU en 24 bandas y los filtros del sobremuestreo agregan latencia o fase. (2) Diseñar el filtro digital para
// que su MAGNITUD coincida con la analógica, a la frecuencia de muestreo original: es la idea de M. Vicanek, "Matched
// Second Order Digital Filters" (2016). Fathom usa (2): latencia 0 y el mismo costo por muestra.
//
// La idea matemática (Vicanek):
//   - Los POLOS se copian del analógico por invarianza al impulso: z = e^(s·T). Así la resonancia (dónde y cuánto
//     resuena) es la analógica, sin doblar el eje. Funciona mientras los polos estén bajo Nyquist; en los shelves que
//     los pondrían encima se diseña el inverso (ver abajo).
//   - Los CEROS se eligen para que |H|² digital coincida con el analógico en unos pocos puntos. Con φ1 = sin²(ω/2),
//     φ0 = 1 − φ1, φ2 = 4·φ0·φ1, el |H|² de un biquad es una razón de dos funciones LINEALES en (φ0, φ1, φ2):
//         |B(e^jω)|² = B0·φ0 + B1·φ1 + B2·φ2,   B0 = (b0+b1+b2)², B1 = (b0−b1+b2)², B2 = −4·b0·b2
//     Tres números B0, B1, B2 = tres condiciones: p. ej. igual en DC, en la frecuencia de la banda y en Nyquist (un
//     sistema 3×3 que se resuelve a mano). De B0, B1, B2 se vuelve a b0, b1, b2 (la raíz de fase mínima).
//   - La campana usa otro trío: igual en DC, igual en el pico y la derivada 0 en el pico (que el pico no se corra).
//   - Los que necesitan un cero exacto lo fijan: el notch en su frecuencia, los pasa altos en DC, el all-pass con los
//     ceros espejo de los polos (magnitud 1 exacta).
//   - Dos ajustes propios, medidos contra el analógico (ver los tests): los shelves con Q alto copian también sus
//     ceros por invarianza al impulso, y las etapas de los cortes de agudos igualan la banda que PASA (en una cascada
//     de 8 etapas los errores se suman).
//
// Lo que se ganó (error máximo contra el analógico de 20 Hz a 20 kHz, Q 0.3-2, ±30 dB): a 96 kHz la campana pasa de
// 2.4 dB (bilineal) a 0.06 dB y los shelves de 3 dB a 0.3 dB; a 44.1 kHz, con la banda en 20 kHz (lo peor), de 13-17 dB
// a 2.5 dB. Un corte de agudos de 96 dB/oct en 15 kHz a 44.1 kHz ya no se hunde 120 dB de más en Nyquist.
//
// Cómo se traduce a código: el biquad resultante se escribe sobre el MISMO SVF (fromBiquad): cualquier par de polos
// estables es un SVF con algún g y k, y cualquier numerador es una mezcla de entrada, LP, BP y HP. Así se conserva
// todo lo del SVF (estable al modular en cada muestra, EqCoefficients::response() exacta para la curva de la GUI, 0 dB
// bit a bit) y solo cambia CÓMO se eligen los números. Se calcula en double, con las sumas en DC y Nyquist sacadas de
// los polos sin restas que pierdan precisión (en 10 Hz a 96 kHz, 1 + a1 + a2 es del orden de 10^-7).
//
// Lo que no se puede: encima de Nyquist no hay nada que igualar (la banda se limita a 0.49·fs, como en la bilineal), y
// la fase es la de un filtro de fase mínima con esa magnitud, parecida pero no idéntica a la analógica cerca de Nyquist.

// Un prototipo analógico de 2º orden: H(S) = (n2·S² + n1·S + n0) / (d2·S² + d1·S + d0), con S = j·f/hz.
// Son las formas de EqFilter.h escritas en Hz absolutos (la "verdad" contra la que se mide el filtro digital).
struct AnalogBiquad
{
    double hz = 1000.0;
    double n2 = 0.0, n1 = 0.0, n0 = 1.0;
    double d2 = 0.0, d1 = 0.0, d0 = 1.0;

    static double amplitude (double gainDb) noexcept { return std::pow (10.0, gainDb / 40.0); } // A: |H| = A² en la meseta

    static AnalogBiquad bell (double hz, double q, double gainDb) noexcept
    {
        const double a = amplitude (gainDb);
        return { hz, 1.0, a / q, 1.0, 1.0, 1.0 / (a * q), 1.0 };
    }
    // (s² + k·A·s + A²) / (s² + k·s + 1) con s = S·√A (el corte del SVF en hz/√A).
    static AnalogBiquad lowShelf (double hz, double q, double gainDb) noexcept
    {
        const double a = amplitude (gainDb), r = std::sqrt (a);
        return { hz, a, a * r / q, a * a, a, r / q, 1.0 };
    }
    // (A²·s² + k·A·s + 1) / (s² + k·s + 1) con s = S/√A.
    static AnalogBiquad highShelf (double hz, double q, double gainDb) noexcept
    {
        const double a = amplitude (gainDb), r = std::sqrt (a);
        return { hz, a, r / q, 1.0, 1.0 / a, 1.0 / (r * q), 1.0 };
    }
    static AnalogBiquad tiltShelf (double hz, double q, double gainDb) noexcept
    {
        auto shelf = highShelf (hz, q, gainDb);
        const double a = amplitude (gainDb);
        shelf.n2 /= a;
        shelf.n1 /= a;
        shelf.n0 /= a;
        return shelf;
    }
    static AnalogBiquad notch (double hz, double q) noexcept { return { hz, 1.0, 0.0, 1.0, 1.0, 1.0 / q, 1.0 }; }
    static AnalogBiquad bandPass (double hz, double q) noexcept { return { hz, 0.0, 1.0 / q, 0.0, 1.0, 1.0 / q, 1.0 }; }
    static AnalogBiquad allPass (double hz, double q) noexcept { return { hz, 1.0, -1.0 / q, 1.0, 1.0, 1.0 / q, 1.0 }; }
    static AnalogBiquad lowPass2 (double hz, double q) noexcept { return { hz, 0.0, 0.0, 1.0, 1.0, 1.0 / q, 1.0 }; }
    static AnalogBiquad highPass2 (double hz, double q) noexcept { return { hz, 1.0, 0.0, 0.0, 1.0, 1.0 / q, 1.0 }; }
    static AnalogBiquad lowPass1 (double hz) noexcept { return { hz, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0 }; }
    static AnalogBiquad highPass1 (double hz) noexcept { return { hz, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0 }; }

    [[nodiscard]] std::complex<double> response (double f) const noexcept
    {
        const std::complex<double> s { 0.0, f / hz };
        return ((n2 * s + n1) * s + n0) / ((d2 * s + d1) * s + d0);
    }
    [[nodiscard]] double power (double f) const noexcept { return std::norm (response (f)); }
};

// Una cascada de prototipos analógicos (los cortes de orden alto), con los mismos Q por etapa que EqCascade.
struct AnalogCascade
{
    std::array<AnalogBiquad, EqCascade::maxStages> stages {};
    int numStages = 0;

    static AnalogCascade butterworth (double hz, int order, double q, bool highPass) noexcept
    {
        order = std::clamp (order, 1, EqCascade::maxOrder);
        AnalogCascade cascade;
        if (order % 2 == 1)
            cascade.stages[static_cast<size_t> (cascade.numStages++)] = highPass ? AnalogBiquad::highPass1 (hz) : AnalogBiquad::lowPass1 (hz);
        for (int i = 1; i <= order / 2; ++i)
        {
            const double stageQ = EqCascade::butterworthStageQ (order, i, q);
            cascade.stages[static_cast<size_t> (cascade.numStages++)] = highPass ? AnalogBiquad::highPass2 (hz, stageQ)
                                                                                 : AnalogBiquad::lowPass2 (hz, stageQ);
        }
        return cascade;
    }

    [[nodiscard]] std::complex<double> response (double f) const noexcept
    {
        std::complex<double> total { 1.0, 0.0 };
        for (int i = 0; i < numStages; ++i)
            total *= stages[static_cast<size_t> (i)].response (f);
        return total;
    }
    [[nodiscard]] double magnitudeDb (double f) const noexcept { return 20.0 * std::log10 (std::max (std::abs (response (f)), 1.0e-12)); }
};

// Los diseños "matched". Misma interfaz que las formas de EqCoefficients y los cortes de EqCascade: Band elige uno u
// otro y el resto (SVF, mezcla, curva de la GUI) no cambia.
struct MatchedEq
{
    static EqCoefficients bell (double hz, double q, double gainDb, double sampleRate) noexcept
    {
        if (gainDb == 0.0)
            return EqCoefficients::bell (hz, q, 0.0, sampleRate); // la entrada intacta, bit a bit
        // Se diseña la campana que SUBE (sus polos resuenan con Q·A: los que la invarianza al impulso copia mejor) y la
        // que baja es su inversa exacta: +x y −x se siguen deshaciendo una a la otra, como en la analógica.
        hz = limit (hz, sampleRate);
        const auto boost = AnalogBiquad::bell (hz, q, std::abs (gainDb));
        const double w = omega (hz, sampleRate);
        const auto poles = Poles::second (w, boost.d1);
        auto biquad = Biquad { peak (poles, w, boost.power (hz)), poles.a };
        if (gainDb < 0.0)
            biquad = biquad.inverse();
        return fromBiquad (biquad, 1.0);
    }

    // Shelves: se igualan DC, la frecuencia de la banda (el punto medio de la subida) y Nyquist. Los polos del shelf
    // están en hz/√A (el de graves que sube) o en hz·√A (el de agudos que sube): este último puede caer encima de
    // Nyquist, donde la invarianza al impulso no sirve. Por eso siempre se diseña la versión con los polos ABAJO (el
    // de graves que sube, el de agudos que baja) y la otra es su inversa.
    static EqCoefficients lowShelf (double hz, double q, double gainDb, double sampleRate) noexcept
    {
        if (gainDb == 0.0)
            return EqCoefficients::lowShelf (hz, q, 0.0, sampleRate);
        hz = limit (hz, sampleRate);
        auto biquad = shelf (AnalogBiquad::lowShelf (hz, q, std::abs (gainDb)), sampleRate);
        if (gainDb < 0.0)
            biquad = biquad.inverse();
        return fromBiquad (biquad, 1.0);
    }

    static EqCoefficients highShelf (double hz, double q, double gainDb, double sampleRate) noexcept
    {
        if (gainDb == 0.0)
            return EqCoefficients::highShelf (hz, q, 0.0, sampleRate);
        return fromBiquad (highShelfBiquad (hz, q, gainDb, sampleRate), 1.0);
    }

    // El shelf de agudos × 1/A (como en EqFilter.h): gira alrededor de 'hz'.
    static EqCoefficients tiltShelf (double hz, double q, double gainDb, double sampleRate) noexcept
    {
        if (gainDb == 0.0)
            return EqCoefficients::tiltShelf (hz, q, 0.0, sampleRate);
        auto biquad = highShelfBiquad (hz, q, gainDb, sampleRate);
        biquad.b = biquad.b.scaled (1.0 / AnalogBiquad::amplitude (gainDb));
        return fromBiquad (biquad, 1.0);
    }

    // Notch: los ceros justo en el círculo, en la frecuencia de la banda (el hueco es infinito donde se pide); la
    // escala deja 0 dB en DC. El ancho lo dan los polos analógicos.
    static EqCoefficients notch (double hz, double q, double sampleRate) noexcept
    {
        hz = limit (hz, sampleRate);
        const double w = omega (hz, sampleRate);
        const auto poles = Poles::second (w, 1.0 / q);
        const double x = sinSquared (w);
        const double c = poles.a.atDc / (4.0 * x);
        return fromBiquad ({ { c, -2.0 * c * std::cos (w), c, poles.a.atDc, 4.0 * c * (1.0 - x), 0.0 }, poles.a }, 1.0);
    }

    // Pasa banda: un cero en DC (como el analógico) y la magnitud igual en el centro y en Nyquist.
    static EqCoefficients bandPassFilter (double hz, double q, double sampleRate) noexcept
    {
        hz = limit (hz, sampleRate);
        return fromBiquad (threePoint (AnalogBiquad::bandPass (hz, q), hz, sampleRate), 0.0);
    }

    // All-pass: los ceros son el espejo de los polos (numerador = denominador al revés): |H| = 1 exacto en todas las
    // frecuencias, y la fase, la de los polos analógicos.
    static EqCoefficients allPass (double hz, double q, double sampleRate) noexcept
    {
        hz = limit (hz, sampleRate);
        const auto poles = Poles::second (omega (hz, sampleRate), 1.0 / q);
        const auto& a = poles.a;
        return fromBiquad ({ { a.c2, a.c1, a.c0, a.atDc, a.atNyquist, -a.outer }, a }, 1.0);
    }

    static EqCascade butterworthHighPass (double hz, int order, double q, double sampleRate) noexcept
    {
        return butterworth (hz, order, q, sampleRate, true);
    }

    static EqCascade butterworthLowPass (double hz, int order, double q, double sampleRate) noexcept
    {
        return butterworth (hz, order, q, sampleRate, false);
    }

    // --- Piezas (públicas para los tests) --------------------------------------------------------------------------

    // c0 + c1·z⁻¹ + c2·z⁻², con su valor en DC (z = 1), en Nyquist (z = −1) y c0 − c2 guardados aparte: calculados
    // de los polos o de B0, B1, B2 sin restas entre números casi iguales.
    struct Poly
    {
        double c0 = 1.0, c1 = 0.0, c2 = 0.0;
        double atDc = 1.0, atNyquist = 1.0, outer = 1.0;

        [[nodiscard]] Poly scaled (double s) const noexcept { return { c0 * s, c1 * s, c2 * s, atDc * s, atNyquist * s, outer * s }; }
    };

    // H(z) = b(z) / a(z), con a.c0 = 1.
    struct Biquad
    {
        Poly b, a;

        // 1/H: los ceros pasan a ser polos. Solo con un numerador de fase mínima (ceros dentro del círculo), que es
        // lo que da numerator(): así la inversa es estable.
        [[nodiscard]] Biquad inverse() const noexcept { return { a.scaled (1.0 / b.c0), b.scaled (1.0 / b.c0) }; }

        [[nodiscard]] std::complex<double> response (double hz, double sampleRate) const noexcept
        {
            const auto z1 = std::polar (1.0, -2.0 * std::numbers::pi * hz / sampleRate);
            return (b.c0 + (b.c1 + b.c2 * z1) * z1) / (a.c0 + (a.c1 + a.c2 * z1) * z1);
        }
    };

    // Los polos digitales copiados del analógico s² + k·s + 1 (normalizado a su frecuencia, que en el digital es 'w'
    // radianes por muestra), y |a(e^jω)|² calculado como producto de distancias a cada polo (preciso aunque los polos
    // estén pegados a z = 1).
    struct Poles
    {
        Poly a;
        bool complex = true;
        double r = 0.0, theta = 0.0; // polos complejos: r·e^(±jθ)
        double p1 = 0.0, p2 = 0.0;   // polos reales
        double oneMinus1 = 1.0, oneMinus2 = 1.0; // 1 − r (o 1 − p1 y 1 − p2), con expm1: precisos con polos pegados a 1

        static Poles second (double w, double k) noexcept
        {
            Poles poles;
            const double zeta = 0.5 * k;
            if (zeta < 1.0)
            {
                poles.r = std::exp (-zeta * w);
                poles.theta = w * std::sqrt (1.0 - zeta * zeta);
                const double r = poles.r, s = sinSquared (poles.theta), oneMinusR = -std::expm1 (-zeta * w);
                poles.oneMinus1 = poles.oneMinus2 = oneMinusR;
                poles.a = { 1.0, -2.0 * r * std::cos (poles.theta), r * r, oneMinusR * oneMinusR + 4.0 * r * s,
                            (1.0 + r) * (1.0 + r) - 4.0 * r * s, -std::expm1 (-2.0 * zeta * w) };
                return poles;
            }
            // Amortiguado de más (Q < 0.5): dos polos reales, e^(−w·(ζ ∓ √(ζ² − 1))). La resta ζ − √(ζ² − 1) se
            // escribe como 1/(ζ + √(ζ² − 1)) para no perder precisión con ζ grande.
            poles.complex = false;
            const double root = std::sqrt (zeta * zeta - 1.0);
            const double slow = w / (zeta + root), fast = w * (zeta + root);
            poles.p1 = std::exp (-slow);
            poles.p2 = std::exp (-fast);
            poles.oneMinus1 = -std::expm1 (-slow);
            poles.oneMinus2 = -std::expm1 (-fast);
            poles.a = { 1.0, -(poles.p1 + poles.p2), poles.p1 * poles.p2, std::expm1 (-slow) * std::expm1 (-fast),
                        (1.0 + poles.p1) * (1.0 + poles.p2), -std::expm1 (-2.0 * zeta * w) };
            return poles;
        }

        // Un polo solo (1er orden, s + 1): e^(−w). El otro queda en z = 0 (no hace nada).
        static Poles first (double w) noexcept
        {
            Poles poles;
            poles.complex = false;
            poles.p1 = std::exp (-w);
            poles.oneMinus1 = -std::expm1 (-w);
            poles.p2 = 0.0;
            poles.oneMinus2 = 1.0;
            poles.a = { 1.0, -poles.p1, 0.0, -std::expm1 (-w), 1.0 + poles.p1, 1.0 };
            return poles;
        }

        // |a(e^jω)|² y su derivada respecto de x = sin²(ω/2) (para la condición del pico de la campana).
        // Cada polo p aporta |e^jω − p|² = (1 − |p|)² + 4·|p|·sin²((ω − arg p)/2).
        [[nodiscard]] double power (double w) const noexcept { return evaluate (w).first; }
        [[nodiscard]] double powerSlope (double w) const noexcept { return evaluate (w).second; }

    private:
        [[nodiscard]] std::pair<double, double> evaluate (double w) const noexcept
        {
            // Factor (1 − m)² + 4·m·sin²(φ/2) y su derivada respecto de ω: 2·m·sin φ.
            const auto factor = [] (double m, double oneMinusM, double phi) {
                return std::pair { oneMinusM * oneMinusM + 4.0 * m * sinSquared (phi), 2.0 * m * std::sin (phi) };
            };
            std::pair<double, double> f1, f2;
            if (complex)
            {
                f1 = factor (r, oneMinus1, w - theta);
                f2 = factor (r, oneMinus2, w + theta);
            }
            else
            {
                f1 = factor (p1, oneMinus1, w);
                f2 = factor (p2, oneMinus2, w);
            }
            const double value = f1.first * f2.first;
            const double dValue = f1.second * f2.first + f1.first * f2.second; // d/dω
            const double dx = 0.5 * std::sin (w);                               // dx/dω
            return { value, dx > 0.0 ? dValue / dx : 0.0 };
        }
    };

    // Vuelve de B0 = s0², B1 = s1², B2 a b0, b1, b2 (la raíz de fase mínima: |b2| ≤ b0). Si no hay ningún numerador
    // real con esos B (pasa en casos extremos), se acerca lo más posible: B2 se limita a −W² y queda un cero doble.
    static Poly numerator (double s0, double s1, double b2Power) noexcept
    {
        const double w = 0.5 * (s0 + s1);
        const double root = std::sqrt (std::max (w * w + b2Power, 0.0));
        return { 0.5 * (w + root), 0.5 * (s0 - s1), 0.5 * (w - root), s0, s1, root };
    }

    // Igualar |H|² en DC, en 'midHz' y en Nyquist con los polos dados.
    static Poly threePoint (const Poles& poles, const AnalogBiquad& analog, double midHz, double sampleRate) noexcept
    {
        const double s0 = poles.a.atDc * std::sqrt (analog.power (0.0));
        const double s1 = poles.a.atNyquist * std::sqrt (analog.power (0.5 * sampleRate));
        const double w = omega (midHz, sampleRate);
        const double x = sinSquared (w);
        const double b2 = (analog.power (midHz) * poles.power (w) - s0 * s0 * (1.0 - x) - s1 * s1 * x) / (4.0 * x * (1.0 - x));
        return numerator (s0, s1, b2);
    }

    // Igualar |H|² en DC y en dos puntos x_a < x_b (en x = sin²(ω/2)), dejando Nyquist libre. Con B0 fijo por DC:
    //     B1 + 4·B2·(1 − x) = (T(x)·D(x) − B0·(1 − x)) / x   en los dos puntos  →  dos ecuaciones lineales.
    static Poly lowMatch (const Poles& poles, const AnalogBiquad& analog, double xa, double xb, double sampleRate) noexcept
    {
        const double s0 = poles.a.atDc * std::sqrt (analog.power (0.0));
        const auto side = [&] (double x) {
            const double w = 2.0 * std::asin (std::sqrt (x));
            return (analog.power (w * sampleRate / (2.0 * std::numbers::pi)) * poles.power (w) - s0 * s0 * (1.0 - x)) / x;
        };
        const double ra = side (xa), rb = side (xb);
        const double b2 = (ra - rb) / (4.0 * (xb - xa));
        const double b1 = ra - 4.0 * b2 * (1.0 - xa);
        return numerator (s0, std::sqrt (std::max (b1, 0.0)), b2);
    }

    // La campana (Vicanek): igual en DC (1), igual en el pico (gain²) y con el pico en su sitio (derivada 0). Nyquist
    // queda libre: lo que dé la forma.
    static Poly peak (const Poles& poles, double w, double peakPower) noexcept
    {
        const double x = sinSquared (w);
        const double dcPower = poles.a.atDc * poles.a.atDc; // B0
        const double r1 = peakPower * poles.power (w);      // B·φ en el pico
        const double r2 = peakPower * poles.powerSlope (w); // su derivada respecto de x
        const double b2 = (r1 - r2 * x - dcPower) / (4.0 * x * x);
        const double b1 = r2 + dcPower + 4.0 * b2 * (2.0 * x - 1.0);
        return numerator (poles.a.atDc, std::sqrt (std::max (b1, 0.0)), b2);
    }

    // El biquad escrito sobre el SVF: y = dry·x + hp·HP + bp·BP + lp·LP. Con S(z) los integradores bilineales, el SVF
    // de corte g y amortiguación k tiene el denominador a(z) ∝ (1 + k·g + g²) + 2(g² − 1)·z⁻¹ + (1 − k·g + g²)·z⁻², y
    // de ahí g² = a(1)/a(−1) y k·g = 2·(1 − a2)/a(−1). La mezcla sale de mirar DC (solo pasa LP), Nyquist (solo HP) y
    // el resto (BP): lp = H(DC) − dry, hp = H(Nyquist) − dry.
    static EqCoefficients fromBiquad (const Biquad& biquad, double dry) noexcept
    {
        const auto& a = biquad.a;
        const auto& b = biquad.b;
        const double g = std::sqrt (a.atDc / a.atNyquist);
        const double k = 2.0 * a.outer / (a.atNyquist * g);
        return { EqCoefficients::svfFor (g, k), static_cast<float> (dry), static_cast<float> (2.0 * (b.outer - dry * a.outer) / (a.atNyquist * g)),
                 static_cast<float> (b.atDc / a.atDc - dry), static_cast<float> (b.atNyquist / a.atNyquist - dry) };
    }

private:
    static double limit (double hz, double sampleRate) noexcept { return std::clamp (hz, 1.0, 0.49 * sampleRate); }
    static double omega (double hz, double sampleRate) noexcept { return 2.0 * std::numbers::pi * hz / sampleRate; }
    static double sinSquared (double w) noexcept
    {
        const double s = std::sin (0.5 * w);
        return s * s;
    }

    // Igualar en DC, en la frecuencia de la banda y en Nyquist, con los polos del prototipo.
    static Biquad threePoint (const AnalogBiquad& analog, double midHz, double sampleRate) noexcept
    {
        const auto poles = analogPoles (analog, sampleRate);
        return { threePoint (poles, analog, midHz, sampleRate), poles.a };
    }

    // Los polos de un prototipo de 2º orden: d2·S² + d1·S + d0 = d2·(s² + k·s + 1) con s = S/√(d0/d2).
    static Poles analogPoles (const AnalogBiquad& analog, double sampleRate) noexcept
    {
        const double natural = std::sqrt (analog.d0 / analog.d2);
        return Poles::second (omega (analog.hz * natural, sampleRate), analog.d1 / std::sqrt (analog.d0 * analog.d2));
    }

    // El shelf con los polos abajo. Con Q bajo bastan los tres puntos; con Q alto el shelf tiene un rebote y un pozo
    // angostos (los ceros resuenan como los polos) que tres puntos no dibujan: ahí los CEROS también se copian por
    // invarianza al impulso (la "matched-Z" clásica: e^(s·T) para polos y ceros), escalados para que DC sea exacto.
    // Se mezcla de uno al otro entre Q 1 y Q 2, y se vuelve a los tres puntos si los ceros se acercan a Nyquist
    // (encima no se pueden copiar). Las dos versiones tienen los mismos polos y son de fase mínima: la mezcla también
    // (un biquad con los dos ceros dentro del círculo cumple tres desigualdades LINEALES en sus coeficientes).
    static Biquad shelf (const AnalogBiquad& analog, double sampleRate) noexcept
    {
        const auto poles = analogPoles (analog, sampleRate);
        auto b = threePoint (poles, analog, analog.hz, sampleRate);
        const double zeroHz = analog.hz * std::sqrt (analog.n0 / analog.n2);
        const double zeroK = analog.n1 / std::sqrt (analog.n0 * analog.n2);
        const double weight = std::clamp (-std::log2 (zeroK), 0.0, 1.0) * std::clamp ((0.45 - zeroHz / sampleRate) / 0.1, 0.0, 1.0);
        if (weight > 0.0)
        {
            const auto zeros = Poles::second (omega (zeroHz, sampleRate), zeroK);
            const auto mzt = zeros.a.scaled (std::sqrt (analog.power (0.0)) * poles.a.atDc / zeros.a.atDc);
            b = mix (b, mzt, weight);
        }
        return { b, poles.a };
    }

    static Poly mix (const Poly& x, const Poly& y, double t) noexcept
    {
        const auto lerp = [t] (double a, double b) { return a + (b - a) * t; };
        return { lerp (x.c0, y.c0), lerp (x.c1, y.c1), lerp (x.c2, y.c2), lerp (x.atDc, y.atDc), lerp (x.atNyquist, y.atNyquist),
                 lerp (x.outer, y.outer) };
    }

    static Biquad highShelfBiquad (double hz, double q, double gainDb, double sampleRate) noexcept
    {
        hz = limit (hz, sampleRate);
        const auto biquad = shelf (AnalogBiquad::highShelf (hz, q, -std::abs (gainDb)), sampleRate);
        return gainDb > 0.0 ? biquad.inverse() : biquad;
    }

    // Pasa altos de 2 polos: cero doble en DC (cae 12 dB/oct como el analógico) e igual en el corte.
    static Biquad highPass2 (double hz, double q, double sampleRate) noexcept
    {
        const double w = omega (hz, sampleRate);
        const auto poles = Poles::second (w, 1.0 / q);
        const double x = sinSquared (w);
        const double c = std::sqrt (AnalogBiquad::highPass2 (hz, q).power (hz) * poles.power (w)) / (4.0 * x);
        return { { c, -2.0 * c, c, 0.0, 4.0 * c, 0.0 }, poles.a };
    }

    // Pasa bajos (de 1 o 2 polos, una etapa de la cascada): igual en DC, en el corte y a "media altura" (x_c/4, cerca
    // de fc/2), con Nyquist libre. En una cascada los errores de cada etapa se SUMAN en dB: igualar en Nyquist dejaba
    // una ondulación de ±1 dB por etapa en la banda que pasa (8 etapas: +8 dB en 15 kHz); así la banda que pasa queda
    // fiel y lo que se aparta es lo que ya está cortado, cerca de Nyquist.
    static Biquad lowPassStage (const AnalogBiquad& analog, const Poles& poles, double sampleRate) noexcept
    {
        const double xc = sinSquared (omega (analog.hz, sampleRate));
        return { lowMatch (poles, analog, 0.25 * xc, xc, sampleRate), poles.a };
    }

    // Pasa altos de 1 polo: c·(1 − z⁻¹), un cero en DC como el analógico, e igual en el corte (−3 dB). (Con tres puntos,
    // cerca de Nyquist no hay numerador real que los cumpla.)
    static Biquad highPass1 (double hz, double sampleRate) noexcept
    {
        const double w = omega (hz, sampleRate);
        const auto poles = Poles::first (w);
        const double c = std::sqrt (0.5 * poles.power (w) / (4.0 * sinSquared (w)));
        return { { c, -c, 0.0, 0.0, 2.0 * c, c }, poles.a };
    }

    static EqCascade butterworth (double hz, int order, double q, double sampleRate, bool highPass) noexcept
    {
        order = std::clamp (order, 1, EqCascade::maxOrder);
        hz = limit (hz, sampleRate);
        EqCascade cascade;
        cascade.numStages = 0;
        const double w = omega (hz, sampleRate);
        if (order % 2 == 1)
            cascade.stages[static_cast<size_t> (cascade.numStages++)] =
                fromBiquad (highPass ? highPass1 (hz, sampleRate) : lowPassStage (AnalogBiquad::lowPass1 (hz), Poles::first (w), sampleRate), 0.0);
        for (int i = 1; i <= order / 2; ++i)
        {
            const double stageQ = EqCascade::butterworthStageQ (order, i, q);
            cascade.stages[static_cast<size_t> (cascade.numStages++)] =
                fromBiquad (highPass ? highPass2 (hz, stageQ, sampleRate)
                                     : lowPassStage (AnalogBiquad::lowPass2 (hz, stageQ), Poles::second (w, 1.0 / stageQ), sampleRate),
                            0.0);
        }
        return cascade;
    }
};

} // namespace deepwater::dsp
