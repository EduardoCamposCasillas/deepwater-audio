#pragma once

// IDs de los parámetros. Son la "llave" con la que se guardan en el proyecto de FL y en los presets:
// una vez publicados NO deben cambiar, o los proyectos antiguos perderían sus valores.
namespace tidechart::params
{
// Fase 1.
inline constexpr const char* bypass = "bypass";

// Fase 3: la tonalidad y cómo se tocan los acordes.
inline constexpr const char* keyTonic = "keyTonic";     // 0..11 = C, C#/Db ... B (la escritura la decide el modo)
inline constexpr const char* keyMode = "keyMode";       // el orden de deepwater::theory::Mode
inline constexpr const char* chordSize = "chordSize";   // 0 = tríadas, 1 = séptimas
inline constexpr const char* keyMapping = "keyMapping"; // 0 = teclas blancas = grados, 1 = notas de la escala
inline constexpr const char* chordBass = "chordBass";   // agregar la fundamental una octava abajo

// Versión en la que apareció cada parámetro (VST3 la usa para ordenar los parámetros entre versiones del plugin).
inline constexpr int versionHintPhase1 = 1;
inline constexpr int versionHintPhase3 = 2;
} // namespace tidechart::params
