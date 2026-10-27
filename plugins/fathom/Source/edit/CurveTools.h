#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "analysis/SpectrumMatch.h"
#include "edit/CurveFit.h"

// Fase 12: el estado de las herramientas IGUALAR y DIBUJAR, sin JUCE. Vive en el procesador (como el historial de
// deshacer) y lo usa solo el hilo de la GUI: lo aprendido y lo dibujado sobreviven a cerrar y abrir la ventana. No se
// guarda en el proyecto: son pasos intermedios; lo que queda son las bandas que se crean.
namespace fathom::edit
{

struct CurveTools
{
    // IGUALAR: los dos espectros promedio y si se están aprendiendo ahora.
    analysis::AverageSpectrum reference, current;
    bool learningReference = false, learningCurrent = false;
    int referenceSource = 0; // 0 = el sidechain (otra pista), 1 = la entrada (la misma pista, en otro momento)
    float amount = 1.0f;     // cuánto de la diferencia se aplica (0-1)
    int smoothingIndex = analysis::defaultMatchSmoothing;

    // DIBUJAR: lo que deben sumar TODAS las bandas en cada punto donde se dibujó (dB, como suena: con la escala).
    CurveValues drawnDb {};
    std::array<bool, CurveGrid::points> drawn {};

    // Las dos: cuántas bandas como mucho y si reemplazan a las que hay (si no, se suman a ellas).
    int maxBands = 8;
    bool replace = false;

    static constexpr int maxBandsLimit = 24;

    [[nodiscard]] bool hasDrawing() const noexcept { return std::any_of (drawn.begin(), drawn.end(), [] (bool d) { return d; }); }

    void clearDrawing() noexcept
    {
        drawn.fill (false);
        drawnDb.fill (0.0f);
    }

    // Un trazo del mouse de (hzA, dbA) a (hzB, dbB): todos los puntos de la grilla entre los dos, con la recta que los
    // une (en log de frecuencia, como se ve). Así un movimiento rápido no deja huecos entre dos eventos del mouse.
    void draw (double hzA, float dbA, double hzB, float dbB) noexcept
    {
        const double a = CurveGrid::position (hzA), b = CurveGrid::position (hzB);
        const double lowPosition = std::min (a, b), highPosition = std::max (a, b);
        const int first = std::max (0, static_cast<int> (std::round (lowPosition)));
        const int last = std::min (CurveGrid::points - 1, static_cast<int> (std::round (highPosition)));
        for (int i = first; i <= last; ++i)
        {
            const double t = highPosition - lowPosition < 1.0e-9 ? 0.0 : (static_cast<double> (i) - a) / (b - a);
            const auto at = static_cast<size_t> (i);
            drawnDb[at] = static_cast<float> (static_cast<double> (dbA) + (static_cast<double> (dbB) - static_cast<double> (dbA)) * std::clamp (t, 0.0, 1.0));
            drawn[at] = true;
        }
    }

    // Borrar lo dibujado entre dos frecuencias (vuelve a "dejar como está").
    void erase (double hzA, double hzB) noexcept
    {
        const double a = CurveGrid::position (std::min (hzA, hzB)), b = CurveGrid::position (std::max (hzA, hzB));
        for (int i = std::max (0, static_cast<int> (std::round (a))); i <= std::min (CurveGrid::points - 1, static_cast<int> (std::round (b))); ++i)
            drawn[static_cast<size_t> (i)] = false;
    }
};

// Lo que deben sumar las bandas NUEVAS, dado lo que se quiere en total ('wanted') y lo que ya suman las bandas que hay
// ('existing'). Si se reemplazan, las de ahora se borran: las nuevas cargan con todo.
inline CurveTarget newBandsTarget (const CurveTarget& wanted, const CurveValues& existing, bool replace) noexcept
{
    CurveTarget target = wanted;
    if (! replace)
        for (size_t i = 0; i < target.db.size(); ++i)
            target.db[i] = target.weight[i] > 0.0f ? wanted.db[i] - existing[i] : 0.0f;
    return target;
}

// Lo que se quiere en total al dibujar: lo dibujado donde se dibujó y, en el resto, la curva de ahora (lo que no se
// toca se deja como está; también al reemplazar: las bandas nuevas reconstruyen esa parte). Todo pesa lo mismo. Sin
// nada dibujado no se pide nada.
inline CurveTarget drawnTarget (const CurveTools& tools, const CurveValues& existing) noexcept
{
    CurveTarget wanted;
    if (! tools.hasDrawing())
        return wanted;
    for (size_t i = 0; i < wanted.db.size(); ++i)
    {
        wanted.db[i] = tools.drawn[i] ? tools.drawnDb[i] : existing[i];
        wanted.weight[i] = 1.0f;
    }
    return wanted;
}

} // namespace fathom::edit
