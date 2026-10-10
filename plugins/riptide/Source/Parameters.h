#pragma once

// IDs de los parámetros. Son la "llave" con la que se guardan en el proyecto de FL y en los presets:
// una vez publicados NO deben cambiar, o los proyectos antiguos perderían sus valores.
namespace riptide::params
{
// Fase 1.
inline constexpr const char* inputGain = "inputGain";
inline constexpr const char* outputGain = "outputGain";
inline constexpr const char* bypass = "bypass";

// Fase 2.
inline constexpr const char* oversampling = "oversampling";

// Fase 3.
inline constexpr const char* mode = "mode";

// Versión en la que apareció cada parámetro (la usa el VST3 para ordenar y reconocer parámetros nuevos).
inline constexpr int versionHint = 1;
inline constexpr int versionHintPhase2 = 2;
inline constexpr int versionHintPhase3 = 3;
} // namespace riptide::params
