#pragma once

// IDs de los parámetros. Son la "llave" con la que se guardan en el proyecto de FL y en los presets:
// una vez publicados NO deben cambiar, o los proyectos antiguos perderían sus valores.
namespace fathom::params
{
// Fase 1.
inline constexpr const char* bypass = "bypass";
inline constexpr const char* outputGain = "outputGain";

// Versión en la que apareció cada parámetro (la usa el VST3 para ordenar y reconocer parámetros nuevos).
inline constexpr int versionHintPhase1 = 1;
} // namespace fathom::params
