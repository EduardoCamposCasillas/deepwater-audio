#pragma once

#include <string>

// IDs de los parámetros. Son la "llave" con la que se guardan en el proyecto de FL y en los presets:
// una vez publicados NO deben cambiar, o los proyectos antiguos perderían sus valores.
namespace fathom::params
{
// Fase 1.
inline constexpr const char* bypass = "bypass";
inline constexpr const char* outputGain = "outputGain";

// Fase 6: escuchar y comparar.
inline constexpr const char* gainScale = "gainScale";     // % de la ganancia de todas las bandas (100 = tal cual)
inline constexpr const char* outputPan = "outputPan";     // balance de salida, -100 (izquierda) a +100 (derecha)
inline constexpr const char* phaseInvert = "phaseInvert"; // invertir la polaridad de la salida
inline constexpr const char* autoGain = "autoGain";       // compensar el volumen que cambia el EQ

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
inline constexpr int versionHintPhase6 = 6; // escala, balance, polaridad y auto gain
} // namespace fathom::params
