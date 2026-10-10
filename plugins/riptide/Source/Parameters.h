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

// Fase 4.
inline constexpr const char* lowCut = "lowCut";
inline constexpr const char* lowCutPosition = "lowCutPosition";
inline constexpr const char* highCut = "highCut";
inline constexpr const char* highCutPosition = "highCutPosition";
inline constexpr const char* emphasis = "emphasis";
inline constexpr const char* emphasisFreq = "emphasisFreq";
inline constexpr const char* tilt = "tilt";

// Fase 5.
inline constexpr const char* mix = "mix";
inline constexpr const char* autoGain = "autoGain";
inline constexpr const char* delta = "delta";

// Versión en la que apareció cada parámetro (la usa el VST3 para ordenar y reconocer parámetros nuevos).
inline constexpr int versionHint = 1;
inline constexpr int versionHintPhase2 = 2;
inline constexpr int versionHintPhase3 = 3;
inline constexpr int versionHintPhase4 = 4;
inline constexpr int versionHintPhase5 = 5;
} // namespace riptide::params
