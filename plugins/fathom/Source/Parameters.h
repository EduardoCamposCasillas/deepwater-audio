#pragma once

#include <string>

// IDs de los parámetros. Son la "llave" con la que se guardan en el proyecto de FL y en los presets:
// una vez publicados NO deben cambiar, o los proyectos antiguos perderían sus valores.
namespace fathom::params
{
// Fase 1.
inline constexpr const char* bypass = "bypass";
inline constexpr const char* outputGain = "outputGain";

// Las 24 bandas: "band<N><Ajuste>", con N de 1 a 24 (band1Frequency, band12Shape...). Los cuatro primeros ajustes
// nacieron en la Fase 2 con la banda 1; forma y pendiente, en la Fase 3.
inline constexpr int numBands = 24;
inline constexpr const char* enabled = "Enabled";
inline constexpr const char* frequency = "Frequency";
inline constexpr const char* gain = "Gain";
inline constexpr const char* q = "Q";
inline constexpr const char* shape = "Shape";
inline constexpr const char* slope = "Slope";

// El ID de un ajuste de una banda. 'band' empieza en 0 (la "Banda 1" de la GUI es band = 0).
inline std::string band (int band, const char* setting)
{
    return "band" + std::to_string (band + 1) + setting;
}

// Versión en la que apareció cada parámetro (la usa el VST3 para ordenar y reconocer parámetros nuevos).
inline constexpr int versionHintPhase1 = 1;
inline constexpr int versionHintPhase2 = 2; // band1Enabled, band1Frequency, band1Gain, band1Q
inline constexpr int versionHintPhase3 = 3; // las bandas 2-24 y la forma y la pendiente de todas
} // namespace fathom::params
