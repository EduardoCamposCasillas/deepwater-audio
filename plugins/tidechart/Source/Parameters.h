#pragma once

// IDs de los parámetros. Son la "llave" con la que se guardan en el proyecto de FL y en los presets:
// una vez publicados NO deben cambiar, o los proyectos antiguos perderían sus valores.
namespace tidechart::params
{
// Fase 1.
inline constexpr const char* bypass = "bypass";

// Versión en la que apareció cada parámetro (VST3 la usa para ordenar los parámetros entre versiones del plugin).
inline constexpr int versionHintPhase1 = 1;
} // namespace tidechart::params
