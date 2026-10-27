#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "dsp/Band.h"

// Fase 12: de una curva deseada a bandas de EQ, sin JUCE (se prueba en tests/EngineTests.cpp). La usan las dos
// herramientas de la fase: IGUALAR (la curva es la diferencia entre el espectro de una referencia y el de la pista) y
// DIBUJAR (la curva la traza el usuario a mano).
//
// El problema: encontrar pocas bandas (campanas y shelves) cuya suma en dB se parezca a la curva. La respuesta de una
// banda no es lineal en sus parámetros (mover la frecuencia desplaza toda la campana), así que no hay una fórmula
// directa. Se resuelve en dos pasos, como se hace en el ajuste de curvas en general:
//  1. Codicioso: se busca dónde la curva todavía se aleja más de lo que ya suman las bandas (el "residuo") y se pone
//     ahí una banda con una primera estimación a ojo: la altura del residuo como ganancia y su ancho a media altura
//     como Q (la definición del Q de una campana); en los extremos se prueba también un shelf.
//  2. Afinar con Levenberg-Marquardt: el método clásico de mínimos cuadrados no lineales. En cada paso se mide cuánto
//     cambia la curva al mover un poco cada parámetro (la jacobiana, por diferencias finitas sobre la respuesta EXACTA
//     de la banda: los mismos coeficientes que suenan), se resuelve el sistema lineal que dice hacia dónde moverlos y se
//     acepta el paso solo si el error baja. Su parámetro λ mezcla dos estrategias: chico = salto de Gauss-Newton
//     (rápido cerca de la solución), grande = pasito cuesta abajo (seguro lejos de ella).
// Se repite hasta llegar al máximo de bandas, a la tolerancia o a que otra banda ya no mejore casi nada.
//
// Los parámetros se ajustan en el dominio en que se oyen: frecuencia en octavas (log2 Hz), ganancia en dB y Q en log2.
namespace fathom::edit
{

// La grilla de las curvas: 1/24 de octava de 20 Hz a 20 kHz (241 puntos, casi 10 octavas). Más fina que cualquier
// banda que tenga sentido (Q 12 ≈ 1/8 de octava) y barata de ajustar.
struct CurveGrid
{
    static constexpr int points = 241;
    static constexpr double minHz = 20.0;
    static constexpr double maxHz = 20000.0;

    static double hz (int i) noexcept { return minHz * std::pow (maxHz / minHz, static_cast<double> (i) / (points - 1)); }

    // La posición (con decimales) de una frecuencia en la grilla.
    static double position (double hz) noexcept { return std::log (hz / minHz) / std::log (maxHz / minHz) * (points - 1); }
};

using CurveValues = std::array<float, CurveGrid::points>;

// Lo que deben sumar las bandas nuevas (dB por punto) y cuánto importa cada punto (0 = nada: ahí no se sabe qué hacer,
// como donde la referencia no tiene energía).
struct CurveTarget
{
    CurveValues db {};
    CurveValues weight {};
};

struct FitOptions
{
    double sampleRate = 44100.0;
    bool analog = true;        // la respuesta de las bandas (Fase 7): la que van a tener al crearlas
    int maxBands = 8;
    float toleranceDb = 0.3f;  // con el residuo por debajo de esto en todos los puntos, no se agregan más bandas
    float maxGainDb = dsp::BandSettings::maxGainDb;
};

struct FitResult
{
    std::vector<dsp::BandSettings> bands; // ordenadas por frecuencia, encendidas, estéreo
    float startErrorDb = 0.0f;            // el mayor alejamiento (con su peso) antes de las bandas
    float maxErrorDb = 0.0f;              // y después
};

namespace detail
{
class Fitter
{
public:
    // Límites: el Q de las campanas (de casi 7 octavas a 1/8 de octava) y el de los shelves (sin el rebote que dan los
    // Q altos: un shelf de Q 2 tiene una "panza" antes del escalón que el ajuste usaría como campana escondida).
    static constexpr double minBellQ = 0.15, maxBellQ = 12.0;
    static constexpr double minShelfQ = 0.35, maxShelfQ = 1.0;
    static constexpr double shelfQ = 0.71;

    Fitter (const CurveTarget& t, const FitOptions& o) : target (t), options (o)
    {
        minLogHz = std::log2 (CurveGrid::minHz);
        maxLogHz = std::log2 (std::min (CurveGrid::maxHz, 0.45 * o.sampleRate));
        for (int i = 0; i < points; ++i)
        {
            hz[index (i)] = CurveGrid::hz (i);
            // Encima de Nyquist no hay nada que ajustar (no pasa a 44.1 kHz o más, pero la cuenta no depende de eso).
            weight[index (i)] = hz[index (i)] < 0.49 * o.sampleRate ? std::max (0.0, static_cast<double> (t.weight[index (i)])) : 0.0;
        }
    }

    FitResult run()
    {
        std::vector<Param> bands;
        Curve model {};
        FitResult result;
        result.startErrorDb = static_cast<float> (maxError (model));
        const int maxBands = std::clamp (options.maxBands, 0, 24);
        while (static_cast<int> (bands.size()) < maxBands)
        {
            const auto residual = residualOf (model);
            int peak = -1;
            double peakError = 0.0;
            for (int i = 0; i < points; ++i)
                if (const double e = weight[index (i)] * std::abs (residual[index (i)]); e > peakError)
                {
                    peakError = e;
                    peak = i;
                }
            if (peak < 0 || peakError < static_cast<double> (options.toleranceDb))
                break;

            // Cada candidato se afina solo (las bandas que ya están quedan quietas) y gana el que deja menos error.
            const double before = cost (model);
            double bestCost = before;
            std::vector<Param> best;
            for (const auto& candidate : candidatesFor (residual, peak))
            {
                auto trial = bands;
                trial.push_back (candidate);
                const double trialCost = refine (trial, bands.size(), 15);
                if (trialCost < bestCost)
                {
                    bestCost = trialCost;
                    best = std::move (trial);
                }
            }
            // Una banda que mejora menos de un 2 % es una banda de más (y el usuario tendría que lidiar con ella).
            if (best.empty() || bestCost > 0.98 * before)
                break;
            bands = std::move (best);
            refine (bands, 0, 8); // todas juntas: la nueva cambia lo que conviene a las demás
            model = modelOf (bands);
        }
        refine (bands, 0, 40);

        for (const auto& band : bands)
            if (std::abs (band.gainDb) >= 0.05) // una banda que terminó en 0 dB no hace nada
                result.bands.push_back (settingsFor (band));
        std::sort (result.bands.begin(), result.bands.end(), [] (const auto& a, const auto& b) { return a.frequency < b.frequency; });
        result.maxErrorDb = static_cast<float> (maxError (modelOf (bands)));
        return result;
    }

private:
    static constexpr int points = CurveGrid::points;
    using Curve = std::array<double, CurveGrid::points>;

    struct Param
    {
        dsp::Shape shape = dsp::Shape::bell;
        double logHz = 10.0, gainDb = 0.0, logQ = 0.0;

        double& operator[] (int p) noexcept { return p == 0 ? logHz : p == 1 ? gainDb : logQ; }
        double operator[] (int p) const noexcept { return p == 0 ? logHz : p == 1 ? gainDb : logQ; }
    };

    static size_t index (int i) noexcept { return static_cast<size_t> (i); }

    dsp::BandSettings settingsFor (const Param& p) const noexcept
    {
        dsp::BandSettings s;
        s.enabled = true;
        s.shape = p.shape;
        s.frequency = static_cast<float> (std::exp2 (p.logHz));
        s.gainDb = static_cast<float> (p.gainDb);
        s.q = static_cast<float> (std::exp2 (p.logQ));
        s.analog = options.analog;
        return s;
    }

    // La curva de una banda en la grilla, con los mismos coeficientes que el audio.
    Curve curveOf (const Param& p) const noexcept
    {
        const auto cascade = dsp::Band::coefficientsFor (settingsFor (p), options.sampleRate);
        Curve curve {};
        for (int i = 0; i < points; ++i)
            if (weight[index (i)] > 0.0)
                curve[index (i)] = cascade.magnitudeDb (hz[index (i)], options.sampleRate);
        return curve;
    }

    Curve modelOf (const std::vector<Param>& bands) const noexcept
    {
        Curve model {};
        for (const auto& band : bands)
        {
            const auto curve = curveOf (band);
            for (size_t i = 0; i < model.size(); ++i)
                model[i] += curve[i];
        }
        return model;
    }

    Curve residualOf (const Curve& model) const noexcept
    {
        Curve r {};
        for (int i = 0; i < points; ++i)
            r[index (i)] = static_cast<double> (target.db[index (i)]) - model[index (i)];
        return r;
    }

    double cost (const Curve& model) const noexcept
    {
        double sum = 0.0;
        for (int i = 0; i < points; ++i)
        {
            const double e = weight[index (i)] * (static_cast<double> (target.db[index (i)]) - model[index (i)]);
            sum += e * e;
        }
        return sum;
    }

    double maxError (const Curve& model) const noexcept
    {
        double worst = 0.0;
        for (int i = 0; i < points; ++i)
            worst = std::max (worst, weight[index (i)] * std::abs (static_cast<double> (target.db[index (i)]) - model[index (i)]));
        return worst;
    }

    void clamp (Param& p) const noexcept
    {
        const bool shelf = p.shape != dsp::Shape::bell;
        p.logHz = std::clamp (p.logHz, minLogHz, maxLogHz);
        p.gainDb = std::clamp (p.gainDb, -static_cast<double> (options.maxGainDb), static_cast<double> (options.maxGainDb));
        p.logQ = std::clamp (p.logQ, std::log2 (shelf ? minShelfQ : minBellQ), std::log2 (shelf ? maxShelfQ : maxBellQ));
    }

    // Las primeras estimaciones para el residuo con su mayor alejamiento en 'peak'.
    std::vector<Param> candidatesFor (const Curve& r, int peak) const
    {
        std::vector<Param> candidates;
        const double height = r[index (peak)];
        const double sign = height < 0.0 ? -1.0 : 1.0;

        // Campana: centrada en el pico, con la altura del pico y el ancho donde el residuo cae a la mitad (en dB). Ese es
        // el ancho que define el Q de una campana: N octavas ↔ Q = 1 / (2·sinh(N·ln2 / 2)).
        int left = peak, right = peak;
        while (left > 0 && weight[index (left - 1)] > 0.0 && sign * r[index (left - 1)] > 0.5 * std::abs (height))
            --left;
        while (right < points - 1 && weight[index (right + 1)] > 0.0 && sign * r[index (right + 1)] > 0.5 * std::abs (height))
            ++right;
        const double octaves = std::max (1.0, static_cast<double> (right - left + 1)) / 24.0;
        Param bell { dsp::Shape::bell, std::log2 (hz[index (peak)]), height, std::log2 (1.0 / (2.0 * std::sinh (octaves * std::log (2.0) * 0.5))) };
        clamp (bell);
        candidates.push_back (bell);

        // Shelves: desde cada extremo (el primer y el último punto que importan), con la altura del extremo y la
        // frecuencia donde el residuo cae a la mitad (el punto medio de un shelf está en su frecuencia).
        const auto shelfFrom = [&] (int edge, int step, dsp::Shape shape) {
            const double level = r[index (edge)];
            if (std::abs (level) < static_cast<double> (options.toleranceDb))
                return;
            const double levelSign = level < 0.0 ? -1.0 : 1.0;
            int i = edge;
            while (i + step >= 0 && i + step < points && weight[index (i + step)] > 0.0 && levelSign * r[index (i + step)] > 0.5 * std::abs (level))
                i += step;
            Param shelf { shape, std::log2 (hz[index (i)]), level, std::log2 (shelfQ) };
            clamp (shelf);
            candidates.push_back (shelf);
        };
        int first = 0, last = points - 1;
        while (first < points - 1 && weight[index (first)] <= 0.0)
            ++first;
        while (last > 0 && weight[index (last)] <= 0.0)
            --last;
        shelfFrom (first, 1, dsp::Shape::lowShelf);
        shelfFrom (last, -1, dsp::Shape::highShelf);
        return candidates;
    }

    // Levenberg-Marquardt sobre las bandas desde 'first' (las anteriores quedan fijas). Devuelve el error final (suma de
    // los cuadrados con su peso).
    double refine (std::vector<Param>& bands, size_t first, int iterations) const
    {
        const size_t numFree = bands.size() - first;
        if (numFree == 0)
            return cost (modelOf (bands));
        const size_t numParams = 3 * numFree;

        std::vector<Curve> curves (bands.size());
        Curve model {};
        for (size_t b = 0; b < bands.size(); ++b)
        {
            curves[b] = curveOf (bands[b]);
            for (size_t i = 0; i < model.size(); ++i)
                model[i] += curves[b][i];
        }
        double current = cost (model);

        // Pasos de las diferencias finitas: chicos frente a lo que se oye (una milésima de octava, de dB, de Q en log).
        static constexpr std::array<double, 3> steps { 1.0e-3, 1.0e-3, 1.0e-3 };
        std::vector<double> jacobian (numParams * points), normal (numParams * numParams), gradient (numParams), delta (numParams);
        double lambda = 1.0e-2;
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            // La jacobiana: cuánto cambia la curva (con su peso) por unidad de cada parámetro.
            for (size_t b = 0; b < numFree; ++b)
                for (int p = 0; p < 3; ++p)
                {
                    auto moved = bands[first + b];
                    moved[p] += steps[static_cast<size_t> (p)];
                    double step = steps[static_cast<size_t> (p)];
                    auto clamped = moved;
                    clamp (clamped);
                    if (clamped[p] != moved[p]) // en el límite de arriba: hacia el otro lado
                    {
                        moved = bands[first + b];
                        moved[p] -= step;
                        step = -step;
                    }
                    const auto curve = curveOf (moved);
                    double* column = &jacobian[(3 * b + static_cast<size_t> (p)) * points];
                    for (int i = 0; i < points; ++i)
                        column[i] = weight[index (i)] * (curve[index (i)] - curves[first + b][index (i)]) / step;
                }
            // Las ecuaciones normales: (JᵀJ) δ = Jᵀe.
            for (size_t a = 0; a < numParams; ++a)
            {
                const double* ja = &jacobian[a * points];
                double g = 0.0;
                for (int i = 0; i < points; ++i)
                    g += ja[i] * weight[index (i)] * (static_cast<double> (target.db[index (i)]) - model[index (i)]);
                gradient[a] = g;
                for (size_t c = a; c < numParams; ++c)
                {
                    const double* jc = &jacobian[c * points];
                    double sum = 0.0;
                    for (int i = 0; i < points; ++i)
                        sum += ja[i] * jc[i];
                    normal[a * numParams + c] = normal[c * numParams + a] = sum;
                }
            }

            bool improved = false;
            while (lambda < 1.0e7)
            {
                // λ suma a la diagonal (escalada por ella misma: así cada parámetro se frena en sus propias unidades).
                auto damped = normal;
                for (size_t d = 0; d < numParams; ++d)
                    damped[d * numParams + d] += lambda * (normal[d * numParams + d] + 1.0e-9);
                if (! solve (damped, gradient, delta, numParams))
                {
                    lambda *= 10.0;
                    continue;
                }
                auto trial = bands;
                for (size_t b = 0; b < numFree; ++b)
                {
                    for (int p = 0; p < 3; ++p)
                        trial[first + b][p] += delta[3 * b + static_cast<size_t> (p)];
                    clamp (trial[first + b]);
                }
                Curve trialModel {};
                std::vector<Curve> trialCurves (curves);
                for (size_t b = 0; b < bands.size(); ++b)
                {
                    if (b >= first)
                        trialCurves[b] = curveOf (trial[b]);
                    for (size_t i = 0; i < trialModel.size(); ++i)
                        trialModel[i] += trialCurves[b][i];
                }
                if (const double trialCost = cost (trialModel); trialCost < current)
                {
                    const double gain = current - trialCost;
                    bands = std::move (trial);
                    curves = std::move (trialCurves);
                    model = trialModel;
                    lambda = std::max (lambda / 3.0, 1.0e-7);
                    improved = gain > 1.0e-7 * std::max (current, 1.0e-12);
                    current = trialCost;
                    break;
                }
                lambda *= 4.0;
            }
            if (! improved)
                break;
        }
        return current;
    }

    // Resuelve A x = b (A simétrica definida positiva, n × n) por Cholesky. false si A no lo es (λ crece y se reintenta).
    static bool solve (std::vector<double>& a, const std::vector<double>& b, std::vector<double>& x, size_t n)
    {
        for (size_t j = 0; j < n; ++j)
        {
            double diagonal = a[j * n + j];
            for (size_t k = 0; k < j; ++k)
                diagonal -= a[j * n + k] * a[j * n + k];
            if (! (diagonal > 1.0e-300))
                return false;
            const double root = std::sqrt (diagonal);
            a[j * n + j] = root;
            for (size_t i = j + 1; i < n; ++i)
            {
                double sum = a[i * n + j];
                for (size_t k = 0; k < j; ++k)
                    sum -= a[i * n + k] * a[j * n + k];
                a[i * n + j] = sum / root;
            }
        }
        for (size_t i = 0; i < n; ++i) // L y = b
        {
            double sum = b[i];
            for (size_t k = 0; k < i; ++k)
                sum -= a[i * n + k] * x[k];
            x[i] = sum / a[i * n + i];
        }
        for (size_t i = n; i-- > 0;) // Lᵀ x = y
        {
            double sum = x[i];
            for (size_t k = i + 1; k < n; ++k)
                sum -= a[k * n + i] * x[k];
            x[i] = sum / a[i * n + i];
        }
        return true;
    }

    const CurveTarget& target;
    const FitOptions& options;
    Curve hz {}, weight {};
    double minLogHz = 0.0, maxLogHz = 0.0;
};
} // namespace detail

// Las bandas (como mucho options.maxBands) cuya suma se parece más a target.db donde target.weight lo pide.
inline FitResult fitBands (const CurveTarget& target, const FitOptions& options)
{
    return detail::Fitter (target, options).run();
}

} // namespace fathom::edit
