#pragma once

#include <array>

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

// Fase 6. Las bandas van de la más grave (1) a la más aguda (4).
inline constexpr const char* bands = "bands";
inline constexpr std::array<const char*, 3> crossover { "crossover1", "crossover2", "crossover3" };
inline constexpr std::array<const char*, 4> bandMode { "band1Mode", "band2Mode", "band3Mode", "band4Mode" };
inline constexpr std::array<const char*, 4> bandDrive { "band1Drive", "band2Drive", "band3Drive", "band4Drive" };
inline constexpr std::array<const char*, 4> bandSolo { "band1Solo", "band2Solo", "band3Solo", "band4Solo" };
inline constexpr std::array<const char*, 4> bandMute { "band1Mute", "band2Mute", "band3Mute", "band4Mute" };

// Versión en la que apareció cada parámetro (la usa el VST3 para ordenar y reconocer parámetros nuevos).
inline constexpr int versionHint = 1;
inline constexpr int versionHintPhase2 = 2;
inline constexpr int versionHintPhase3 = 3;
inline constexpr int versionHintPhase4 = 4;
inline constexpr int versionHintPhase5 = 5;
inline constexpr int versionHintPhase6 = 6;
} // namespace riptide::params
