#pragma once

// IDs de los parámetros. Son la "llave" con la que se guardan en el proyecto de FL y en los presets:
// una vez publicados NO deben cambiar, o los proyectos antiguos perderían sus valores.
namespace fathom::params
{
// Fase 1.
inline constexpr const char* bypass = "bypass";
inline constexpr const char* outputGain = "outputGain";

// Fase 2: la primera banda. Las de la Fase 3 siguen el mismo esquema, "band<N><Ajuste>" (band2Frequency, ...), así
// estos IDs ya son los definitivos de la banda 1.
inline constexpr const char* band1Enabled = "band1Enabled";
inline constexpr const char* band1Frequency = "band1Frequency";
inline constexpr const char* band1Gain = "band1Gain";
inline constexpr const char* band1Q = "band1Q";

// Versión en la que apareció cada parámetro (la usa el VST3 para ordenar y reconocer parámetros nuevos).
inline constexpr int versionHintPhase1 = 1;
inline constexpr int versionHintPhase2 = 2;
} // namespace fathom::params
