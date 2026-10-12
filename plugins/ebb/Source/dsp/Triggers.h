#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace ebb::dsp
{

// Qué dispara la curva. El parámetro "mode" guarda el índice: solo añadir al final (la Fase 5 añade el modo audio).
enum class TriggerMode : std::uint8_t
{
    tempo, // la cuadrícula de la división, desde la posición del host (Fase 2)
    midi   // cada nota MIDI recibida (Fase 4): sigue el patrón real del kick
};

inline constexpr std::array<const char*, 2> triggerModeNames { "Tempo", "MIDI" };

[[nodiscard]] inline TriggerMode triggerModeFromIndex (int index) noexcept
{
    return index == 1 ? TriggerMode::midi : TriggerMode::tempo;
}

// Un disparo dentro del bloque actual: en qué muestra cae y con qué velocidad (0–1). El procesador traduce las notas
// MIDI a esto (ya filtradas por nota) y el motor reinicia la curva en esa muestra exacta.
struct Trigger
{
    int offset = 0;
    float velocity = 1.0f;
};

// Filtro de nota: el parámetro "midiNote" guarda 0 = todas las notas, 1–128 = la nota MIDI 0–127.
inline constexpr int midiNoteFilterCount = 129;

[[nodiscard]] inline bool noteFilterAccepts (int filterIndex, int note) noexcept
{
    return filterIndex <= 0 || filterIndex - 1 == note;
}

// Nombre de una nota como lo escribe FL Studio: la nota 60 (do central) es C5 y la 69 (440 Hz) es A5, una octava más
// que la convención "científica" (C4) que usan otros programas. Se añade el número MIDI para no confundirse.
[[nodiscard]] inline std::string flNoteName (int note)
{
    static constexpr std::array<const char*, 12> names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    note = std::clamp (note, 0, 127);
    return std::string (names[static_cast<size_t> (note % 12)]) + std::to_string (note / 12) + " (" + std::to_string (note) + ")";
}

} // namespace ebb::dsp
