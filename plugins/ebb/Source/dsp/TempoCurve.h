#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "deepwater/dsp/BreakpointCurve.h"
#include "dsp/HostClock.h"

namespace ebb::dsp
{

// Fase 2: la curva de ducking del modo tempo, como funciones puras (sin estado): dónde cae cada golpe y cuánto se baja
// el volumen en cada momento. El motor (Engine.h) las evalúa muestra a muestra con la posición del reloj del host.

// Divisiones de la curva: cada cuánto se dispara, en negras. "1 bar" dura lo que dure el compás del host (4 negras en
// 4/4, 3 en 6/8). T = tresillo: tres golpes en el espacio de dos. El parámetro guarda el índice: solo añadir al final.
struct DuckDivision
{
    const char* name;
    double beats;
    bool wholeBar = false;
};

inline constexpr std::array<DuckDivision, 9> duckDivisions { {
    { "1 bar", 4.0, true },
    { "1/2", 2.0 },
    { "1/4", 1.0 },
    { "1/8", 0.5 },
    { "1/16", 0.25 },
    { "1/2 T", 4.0 / 3.0 },
    { "1/4 T", 2.0 / 3.0 },
    { "1/8 T", 1.0 / 3.0 },
    { "1/16 T", 1.0 / 6.0 },
} };

inline constexpr int defaultDuckDivision = 2; // 1/4: un golpe por tiempo, como el kick del dembow clásico

// Duración de un ciclo en negras, con el compás actual (solo importa para "1 bar").
[[nodiscard]] inline double divisionBeats (int index, TimeSignature signature) noexcept
{
    const auto& division = duckDivisions[static_cast<size_t> (std::clamp (index, 0, static_cast<int> (duckDivisions.size()) - 1))];
    if (! division.wholeBar)
        return division.beats;
    return signature.isValid() ? signature.quartersPerBar() : 4.0;
}

// Cuánto lleva el ciclo actual, en negras [0, period). La cuadrícula arranca en cada inicio de compás: así una división
// que no cabe justa en el compás (1/2 T en 3/4) se reinicia en el "1" en vez de ir corriéndose compás tras compás.
// Margen de 1e-9 negras: si el host entrega 0.9999999999 en lugar de 1, el golpe cae en esa muestra y no en la siguiente.
[[nodiscard]] inline double positionInCycle (double ppq, double barOrigin, double quartersPerBar, double period) noexcept
{
    constexpr double epsilon = 1.0e-9;
    double inBar = ppq - barOrigin;
    inBar -= std::floor (inBar / quartersPerBar + epsilon) * quartersPerBar;
    double inCycle = inBar - std::floor (inBar / period + epsilon) * period;
    return std::clamp (inCycle, 0.0, period);
}

// La curva completa en un punto del ciclo: 1 = el golpe (máxima atenuación), 0 = el volumen ya volvió. La forma la
// dibuja una curva por puntos (Fase 3, ver CurveShapes.h), estirada a lo largo de 'duration' (fracción del ciclo que
// tarda en volver, 0–1). Después de la duración se queda en el valor de su último punto hasta el siguiente golpe.
[[nodiscard]] inline float duckAt (const deepwater::dsp::BreakpointCurve& curve, double inCycle, double period, float duration) noexcept
{
    const double length = period * static_cast<double> (std::max (duration, 1.0e-3f));
    return 1.0f - curve.valueAt (static_cast<float> (inCycle / length));
}

} // namespace ebb::dsp
