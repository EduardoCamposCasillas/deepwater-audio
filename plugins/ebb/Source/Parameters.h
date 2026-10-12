#pragma once

// IDs de los parámetros. Son la "llave" con la que se guardan en el proyecto de FL y en los presets:
// una vez publicados NO deben cambiar, o los proyectos antiguos perderían sus valores.
namespace ebb::params
{
// Fase 1.
inline constexpr const char* bypass = "bypass";

// Fase 2: modo tempo.
inline constexpr const char* division = "division";   // cada cuánto se dispara la curva (índice de duckDivisions)
inline constexpr const char* depth = "depth";         // %: cuánto baja el volumen en el golpe
inline constexpr const char* duration = "duration";   // %: fracción del ciclo que tarda en volver
inline constexpr const char* smoothing = "smoothing"; // ms: suavizado final de la ganancia

// Fase 4: modo MIDI trigger.
inline constexpr const char* mode = "mode";           // qué dispara la curva (índice de triggerModeNames)
inline constexpr const char* midiNote = "midiNote";   // 0 = todas las notas, 1–128 = solo la nota MIDI 0–127
inline constexpr const char* velocity = "velocity";   // la velocidad de la nota escala la profundidad

// Versión en la que apareció cada parámetro (la usa el VST3 para ordenar y reconocer parámetros nuevos).
inline constexpr int versionHintPhase1 = 1;
inline constexpr int versionHintPhase2 = 2;
inline constexpr int versionHintPhase4 = 4;
} // namespace ebb::params
