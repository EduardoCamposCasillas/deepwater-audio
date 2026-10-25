#pragma once

#include <algorithm>
#include <array>
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
// Todas las formas de un EQ de 2 polos son una MEZCLA de la entrada con esas salidas:
//     y = dry·x + hp·HP + bp·BP + lp·LP   →   H(s) = dry + (hp·s² + bp·s + lp) / D(s)
// Elegir la mezcla es elegir los ceros del filtro (dónde "tira hacia abajo" o "empuja hacia arriba"); el SVF pone los
// polos (dónde resuena y cuánto). Las formas con ganancia (campana, shelves) se escriben como "entrada + algo", así en
// 0 dB ese algo vale exactamente 0 y la señal pasa intacta bit a bit.
//
// La campana (peaking): sumarle a la entrada un poco de band-pass sube (o baja, si se resta) una zona alrededor del
// corte. Con A = 10^(dB/40) y la amortiguación k = 1/(Q·A):
//     H(s) = 1 + k·(A² − 1)·BP = (s² + (A/Q)·s + 1) / (s² + s/(A·Q) + 1)
// que es el prototipo analógico clásico de la campana (el del "Audio EQ Cookbook" de R. Bristow-Johnson): en el centro
// vale A² (la ganancia pedida), lejos vale 1, y +x dB y −x dB son inversas exactas (una deshace a la otra). Que k
// dependa de A hace que el ancho "se vea" igual al subir y al bajar.
//
// Los shelves (estantes) son los del mismo Cookbook escritos sobre el SVF (como los publica A. Simper, Cytomic): el
// truco es correr el corte del SVF √A veces (abajo en el grave, arriba en el agudo) para que en la frecuencia pedida la
// curva valga justo la MITAD de la ganancia en dB: esa frecuencia es el punto medio de la subida.
//
// Como el SVF es TPT (bilineal), la respuesta digital es la analógica evaluada en s = j·tan(π·f/fs)/g: response()
// la calcula con ESOS MISMOS coeficientes (float), así la curva que dibuja la GUI es la del audio, no una aproximación.
// (El precio de la bilineal: cerca de Nyquist la campana se "aplasta" respecto de la analógica. MatchedEq.h da las
// mismas formas con la respuesta analógica, sobre este mismo SVF.)
struct EqCoefficients
{
    SvfStage::Coefficients svf;
    float dry = 1.0f;       // cuánto de la entrada
    float bandPass = 0.0f;  // cuánto de la salida band-pass (s/D)
    float lowPass = 0.0f;   // cuánto de la salida low-pass (1/D)
    float highPass = 0.0f;  // cuánto de la salida high-pass (s²/D)

    // tan(π·f/fs) con la frecuencia limitada por debajo de Nyquist: la ganancia de los integradores para ese corte.
    static double warp (double hz, double sampleRate) noexcept
    {
        return std::tan (std::numbers::pi * std::clamp (hz, 1.0, 0.49 * sampleRate) / sampleRate);
    }

    static SvfStage::Coefficients svfFor (double g, double k) noexcept
    {
        return SvfStage::Coefficients::make (static_cast<float> (g), static_cast<float> (k));
    }

    // Campana centrada en 'hz', con su Q y su ganancia en dB. 0 dB da dry = 1 y bp = 0: la señal pasa intacta.
    static EqCoefficients bell (double hz, double q, double gainDb, double sampleRate) noexcept
    {
        const double a = std::pow (10.0, gainDb / 40.0);
        const double k = 1.0 / (q * a);
        const auto svf = SvfStage::Coefficients::forCutoff (hz, 1.0 / k, sampleRate);
        return { svf, 1.0f, static_cast<float> (k * (a * a - 1.0)), 0.0f };
    }

    // Shelf de graves: debajo de 'hz' todo sube (o baja) 'gainDb'; encima, nada. En 'hz', la mitad.
    //     H = 1 + k·(A − 1)·BP + (A² − 1)·LP = (s² + k·A·s + A²) / (s² + k·s + 1),  corte del SVF = hz/√A
    // Q 0.71 da la subida más suave sin rebote; Q más alto agrega un pequeño "contragolpe" justo antes de la subida.
    static EqCoefficients lowShelf (double hz, double q, double gainDb, double sampleRate) noexcept
    {
        const double a = std::pow (10.0, gainDb / 40.0);
        const double k = 1.0 / q;
        return { svfFor (warp (hz, sampleRate) / std::sqrt (a), k), 1.0f, static_cast<float> (k * (a - 1.0)),
                 static_cast<float> (a * a - 1.0) };
    }

    // Shelf de agudos: encima de 'hz' todo sube (o baja) 'gainDb'. El espejo del de graves:
    //     H = A² + k·A·(1 − A)·BP + (1 − A²)·LP = (A²·s² + k·A·s + 1) / (s² + k·s + 1),  corte del SVF = hz·√A
    static EqCoefficients highShelf (double hz, double q, double gainDb, double sampleRate) noexcept
    {
        const double a = std::pow (10.0, gainDb / 40.0);
        const double k = 1.0 / q;
        return { svfFor (warp (hz, sampleRate) * std::sqrt (a), k), static_cast<float> (a * a),
                 static_cast<float> (k * a * (1.0 - a)), static_cast<float> (1.0 - a * a) };
    }

    // Tilt shelf: un shelf de agudos de 'gainDb' bajado a la mitad, así gira alrededor de 'hz': los agudos suben
    // gainDb/2 y los graves bajan gainDb/2 (o al revés). En 'hz' vale 0 dB.
    //     H = (A·s² + k·s + 1/A) / (s² + k·s + 1) = A + k·(1 − A)·BP + (1/A − A)·LP
    static EqCoefficients tiltShelf (double hz, double q, double gainDb, double sampleRate) noexcept
    {
        const double a = std::pow (10.0, gainDb / 40.0);
        const double k = 1.0 / q;
        return { svfFor (warp (hz, sampleRate) * std::sqrt (a), k), static_cast<float> (a), static_cast<float> (k * (1.0 - a)),
                 static_cast<float> (1.0 / a - a) };
    }

    // Notch: quita UNA frecuencia por completo (un cero justo en el eje): H = 1 − k·BP = (s² + 1) / (s² + k·s + 1).
    // El Q dice lo angosto del hueco.
    static EqCoefficients notch (double hz, double q, double sampleRate) noexcept
    {
        const double k = 1.0 / q;
        return { svfFor (warp (hz, sampleRate), k), 1.0f, static_cast<float> (-k), 0.0f };
    }

    // Band-pass: deja solo la zona alrededor de 'hz' (0 dB en el centro): H = k·BP = k·s / (s² + k·s + 1).
    static EqCoefficients bandPassFilter (double hz, double q, double sampleRate) noexcept
    {
        const double k = 1.0 / q;
        return { svfFor (warp (hz, sampleRate), k), 0.0f, static_cast<float> (k), 0.0f };
    }

    // All-pass: no cambia el volumen de NINGUNA frecuencia, solo su fase (el retardo de cada una): los ceros son el
    // espejo de los polos.  H = 1 − 2k·BP = (s² − k·s + 1) / (s² + k·s + 1).
    static EqCoefficients allPass (double hz, double q, double sampleRate) noexcept
    {
        const double k = 1.0 / q;
        return { svfFor (warp (hz, sampleRate), k), 1.0f, static_cast<float> (-2.0 * k), 0.0f };
    }

    // Low-pass y high-pass de 2 polos (12 dB/oct), ya con 'g' calculado (los cortes de orden alto comparten un g).
    static EqCoefficients lowPass2 (double g, double q) noexcept { return { svfFor (g, 1.0 / q), 0.0f, 0.0f, 1.0f, 0.0f }; }
    static EqCoefficients highPass2 (double g, double q) noexcept { return { svfFor (g, 1.0 / q), 0.0f, 0.0f, 0.0f, 1.0f }; }

    // Low-pass y high-pass de 1 polo (6 dB/oct) con el mismo SVF: con k = 2 sus dos polos caen juntos en s = −1, y la
    // mezcla pone un cero encima de uno de ellos, que se cancela:
    //     BP + LP = (s + 1)/(s + 1)² = 1/(s + 1)         HP + BP = s·(s + 1)/(s + 1)² = s/(s + 1)
    // El polo "escondido" es estable (está dentro, como el otro): cualquier error de redondeo se apaga solo.
    static EqCoefficients lowPass1 (double g) noexcept { return { svfFor (g, 2.0), 0.0f, 1.0f, 1.0f, 0.0f }; }
    static EqCoefficients highPass1 (double g) noexcept { return { svfFor (g, 2.0), 0.0f, 1.0f, 0.0f, 1.0f }; }

    // La misma respuesta multiplicada por una ganancia fija.
    [[nodiscard]] EqCoefficients scaled (float gain) const noexcept
    {
        return { svf, dry * gain, bandPass * gain, lowPass * gain, highPass * gain };
    }

    // Un punto intermedio entre dos juegos de coeficientes (t = 0 → 'from', 1 → 'to'): se interpolan g, k y la mezcla.
    // El SVF es estable con cualquier g > 0 y k > 0, así que el camino también lo es; sirve para pasar de un diseño a
    // otro (bilineal ↔ analógico) en unos milisegundos sin clic.
    static EqCoefficients blend (const EqCoefficients& from, const EqCoefficients& to, float t) noexcept
    {
        const auto mix = [t] (float a, float b) { return a + (b - a) * t; };
        return { SvfStage::Coefficients::make (mix (from.svf.g, to.svf.g), mix (from.svf.k, to.svf.k)), mix (from.dry, to.dry),
                 mix (from.bandPass, to.bandPass), mix (from.lowPass, to.lowPass), mix (from.highPass, to.highPass) };
    }

    // Respuesta en frecuencia (compleja: magnitud y fase) en 'hz'. Exacta para estos coeficientes.
    [[nodiscard]] std::complex<double> response (double hz, double sampleRate) const noexcept
    {
        // En Nyquist o encima, tan() se dispara: s → ∞, BP y LP valen 0 y HP vale 1.
        if (hz >= 0.5 * sampleRate)
            return { static_cast<double> (dry) + static_cast<double> (highPass), 0.0 };
        const std::complex<double> s { 0.0, std::tan (std::numbers::pi * hz / sampleRate) / static_cast<double> (svf.g) };
        const auto denominator = s * s + static_cast<double> (svf.k) * s + 1.0;
        const auto numerator = (static_cast<double> (highPass) * s + static_cast<double> (bandPass)) * s + static_cast<double> (lowPass);
        return static_cast<double> (dry) + numerator / denominator;
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
        // Con dry = 1 y lo demás en 0 la suma es x + 0 + 0 + 0 = x: una banda en 0 dB deja la señal intacta bit a bit.
        return c.dry * x + c.bandPass * out.bandPass + c.lowPass * out.lowPass + c.highPass * out.highPass;
    }

private:
    SvfStage stage;
};

// --- Cascadas: filtros de orden alto como varias etapas de 2 polos en serie -----------------------------------------
//
// Un filtro de 2 polos cae a 12 dB/oct. Para cortes más empinados se ponen etapas en serie (las respuestas se
// multiplican, los dB se suman): 4 etapas = 8 polos = 48 dB/oct. Hasta 8 etapas: 16 polos, 96 dB/oct.
struct EqCascade
{
    static constexpr int maxStages = 8;
    static constexpr int maxOrder = 2 * maxStages; // en polos: 6 dB/oct cada uno

    std::array<EqCoefficients, maxStages> stages {}; // las que sobran son identidad (dry = 1)
    int numStages = 1;

    static EqCascade single (const EqCoefficients& c) noexcept
    {
        EqCascade cascade;
        cascade.stages[0] = c;
        return cascade;
    }

    // Butterworth de orden 'order' (1–16 polos, 6 dB/oct cada uno): lo más plano posible en la banda que pasa y −3 dB
    // exactos en el corte, para cualquier orden. Sus polos están repartidos en un semicírculo; agrupados de a dos, cada
    // etapa es un SVF con su propio Q:  Q_i = 1 / (2·sin((2i − 1)·π / (2n))),  i = 1..n/2  (n impar: más un polo solo).
    //
    // 'q' es la resonancia: 1/√2 (0.71) = Butterworth puro. Multiplica el Q de la etapa más resonante (la i = 1, la
    // que forma el "codo"): más alto = un pico justo antes del corte (el "golpe" de un filtro analógico); más bajo = un
    // codo más suave y redondo. Con 1 polo no hay resonancia posible.
    static EqCascade butterworthHighPass (double hz, int order, double q, double sampleRate) noexcept
    {
        return butterworth (hz, order, q, sampleRate, true);
    }

    static EqCascade butterworthLowPass (double hz, int order, double q, double sampleRate) noexcept
    {
        return butterworth (hz, order, q, sampleRate, false);
    }

    // El Q de la etapa de 2 polos 'i' (1..order/2) de un Butterworth de 'order' polos, con la resonancia 'q' aplicada a
    // la primera (ver abajo).
    static double butterworthStageQ (int order, int i, double q) noexcept
    {
        double stageQ = 1.0 / (2.0 * std::sin ((2.0 * i - 1.0) * std::numbers::pi / (2.0 * order)));
        if (i == 1)
            stageQ *= q * std::numbers::sqrt2;
        return stageQ;
    }

    // Etapa por etapa (las dos cascadas tienen que tener la misma estructura). Ver EqCoefficients::blend.
    static EqCascade blend (const EqCascade& from, const EqCascade& to, float t) noexcept
    {
        EqCascade cascade;
        cascade.numStages = std::max (from.numStages, to.numStages);
        for (size_t i = 0; i < cascade.stages.size(); ++i)
            cascade.stages[i] = EqCoefficients::blend (from.stages[i], to.stages[i], t);
        return cascade;
    }

    [[nodiscard]] std::complex<double> response (double hz, double sampleRate) const noexcept
    {
        std::complex<double> total { 1.0, 0.0 };
        for (int i = 0; i < numStages; ++i)
            total *= stages[static_cast<size_t> (i)].response (hz, sampleRate);
        return total;
    }

    [[nodiscard]] double magnitudeDb (double hz, double sampleRate) const noexcept
    {
        return 20.0 * std::log10 (std::max (std::abs (response (hz, sampleRate)), 1.0e-12));
    }

private:
    static EqCascade butterworth (double hz, int order, double q, double sampleRate, bool highPass) noexcept
    {
        order = std::clamp (order, 1, maxOrder);
        const double g = EqCoefficients::warp (hz, sampleRate); // todas las etapas comparten el corte
        EqCascade cascade;
        cascade.numStages = 0;
        if (order % 2 == 1)
            cascade.stages[static_cast<size_t> (cascade.numStages++)] = highPass ? EqCoefficients::highPass1 (g) : EqCoefficients::lowPass1 (g);
        for (int i = 1; i <= order / 2; ++i)
        {
            const double stageQ = butterworthStageQ (order, i, q);
            cascade.stages[static_cast<size_t> (cascade.numStages++)] = highPass ? EqCoefficients::highPass2 (g, stageQ)
                                                                                 : EqCoefficients::lowPass2 (g, stageQ);
        }
        return cascade;
    }
};

// --- Flat tilt: una pendiente constante en dB/oct en todo el espectro -------------------------------------------------
//
// Un shelf cambia el volumen en una zona y se queda plano; una pendiente constante (p. ej. +1 dB por octava, de 20 Hz
// a 20 kHz) no la da ningún filtro de pocos polos: un polo solo ya cae 6 dB/oct. El truco clásico (el de los filtros
// que convierten ruido blanco en rosa) es una ESCALERA de pares polo-cero de 1er orden: cada par (s + z)/(s + p) es un
// shelf diminuto que sube o baja un escalón de 20·log10(p/z) dB. Repartidos a 1 octava de distancia, los escalones se
// funden en una rampa: S dB/oct con escalones de S dB, y la ondulación que queda es de centésimas de dB.
//
// Aquí: 16 pares (8 etapas de SVF de dos polos reales), uno por octava desde 0.4·fs hacia abajo (cubren de ~1 Hz a casi
// Nyquist). Cada par se centra en su frecuencia (pre-deformada con tan, como el SVF) y la razón polo/cero se aplica en
// ese mismo eje: cambiar la pendiente no mueve los centros, solo la altura de los escalones (sin tan() por muestra).
// Al final se escala todo para que en 'pivotHz' valga 0 dB: la frecuencia de la banda es el eje alrededor del que gira.
// Cerca de Nyquist la bilineal "aprieta" los escalones: la recta se curva un poco (≈0.2 dB por cada dB/oct a 20 kHz
// con 44.1 kHz; la curva de la GUI lo muestra tal cual).
class FlatTiltDesign
{
public:
    static constexpr int numPairs = 2 * EqCascade::maxStages;
    static constexpr double spacingOctaves = 1.0;
    static constexpr double topCentre = 0.4; // el par más agudo, en fracción de fs

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        for (int stage = 0; stage < EqCascade::maxStages; ++stage)
        {
            // Los dos pares de cada etapa (a y b, una octava aparte) son los dos polos reales del SVF:
            // (s + pa)(s + pb) = s² + (pa + pb)·s + pa·pb  →  corte √(pa·pb), k = (pa + pb)/√(pa·pb).
            const double a = std::tan (std::numbers::pi * topCentre * std::exp2 (-spacingOctaves * (2 * stage)));
            const double b = std::tan (std::numbers::pi * topCentre * std::exp2 (-spacingOctaves * (2 * stage + 1)));
            centre[static_cast<size_t> (stage)] = std::sqrt (a * b);
            damping[static_cast<size_t> (stage)] = (a + b) / std::sqrt (a * b);
        }
    }

    // La pendiente en dB por octava (+ = los agudos suben y los graves bajan), con 0 dB en 'pivotHz'.
    [[nodiscard]] EqCascade make (double pivotHz, double dbPerOctave) const noexcept
    {
        // Cada par: polo en c/√r y cero en c·√r, con r = 10^(−S·d/20) (el escalón de S·d dB). En una etapa, con s
        // normalizada a su corte g = c/√r, eso queda (s² + r·k·s + r²) / (s² + k·s + 1): la forma del shelf de graves,
        // dry = 1, bp = k·(r − 1), lp = r² − 1. Con pendiente 0, r = 1 y la etapa es la entrada intacta.
        const double r = std::pow (10.0, -dbPerOctave * spacingOctaves / 20.0);
        const double sqrtR = std::sqrt (r);
        EqCascade cascade;
        cascade.numStages = EqCascade::maxStages;
        for (size_t i = 0; i < cascade.stages.size(); ++i)
            cascade.stages[i] = { EqCoefficients::svfFor (centre[i] / sqrtR, damping[i]), 1.0f, static_cast<float> (damping[i] * (r - 1.0)),
                                  static_cast<float> (r * r - 1.0) };

        const double atPivot = std::abs (cascade.response (std::clamp (pivotHz, 1.0, 0.49 * sampleRate), sampleRate));
        cascade.stages[0] = cascade.stages[0].scaled (static_cast<float> (1.0 / atPivot));
        return cascade;
    }

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }

private:
    double sampleRate = 0.0;
    std::array<double, EqCascade::maxStages> centre {}, damping {};
};

// La cascada de filtros para UN canal. Solo corren las etapas que usa la cascada.
class EqCascadeFilter
{
public:
    void reset() noexcept
    {
        for (auto& stage : stages)
            stage.reset();
    }

    [[nodiscard]] float process (float x, const EqCascade& cascade) noexcept
    {
        for (int i = 0; i < cascade.numStages; ++i)
            x = stages[static_cast<size_t> (i)].process (x, cascade.stages[static_cast<size_t> (i)]);
        return x;
    }

private:
    std::array<EqFilter, EqCascade::maxStages> stages;
};

} // namespace deepwater::dsp
