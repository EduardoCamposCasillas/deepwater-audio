#pragma once

#include <array>

#include "dsp/Bands.h"
#include "dsp/Saturator.h"

namespace riptide::gui
{

// Lo que los visores necesitan saber de las bandas, ya resuelto por el editor en cada cuadro (modo "Global" cambiado
// por el modo global, drives con la modulación sumada, solo/mute convertidos en "se oye o no").
struct BandDisplay
{
    dsp::Mode mode = dsp::Mode::tape;
    float driveDb = 0.0f; // drive total que llega a la curva de esta banda: Input + drive de la banda (+ modulación)
    float bandDriveDb = 0.0f; // solo el drive propio de la banda (lo que se escribe en su etiqueta)
    bool audible = true;
    bool operator== (const BandDisplay&) const = default;
};

struct DisplayState
{
    int numBands = 1;
    std::array<BandDisplay, dsp::maxBands> bands {};
    std::array<float, dsp::maxCrossovers> crossoverHz = dsp::defaultCrossoverHz; // los que suenan (ya separados)
    bool operator== (const DisplayState&) const = default; // Fase 10: los visores solo se repintan si algo cambió
};

} // namespace riptide::gui
