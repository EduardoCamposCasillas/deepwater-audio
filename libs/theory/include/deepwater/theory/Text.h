#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "deepwater/theory/Chord.h"
#include "deepwater/theory/Harmony.h"
#include "deepwater/theory/Pitch.h"
#include "deepwater/theory/Scale.h"

// Texto ida y vuelta: todo lo que se escribe se puede volver a leer y da lo mismo (lo comprueban los tests). Al leer se
// aceptan ♭/♯ o b/#, °/o, ø/h, Δ/^ y –/-. Reserva memoria (std::string): fuera del hilo de audio.
namespace deepwater::theory
{

// --- Notas -----------------------------------------------------------------------------------------------------------
[[nodiscard]] std::string accidentalText (int accidental, const Notation& notation = {});
[[nodiscard]] std::string noteName (SpelledNote note, const Notation& notation = {});               // "Bb", "F♯"
[[nodiscard]] std::string noteName (int midiNote, SpelledNote spelling, const Notation& notation = {}); // "Bb4": la octava sigue a la letra (C♭6 = B5)
[[nodiscard]] std::string midiNoteName (int midiNote, const Notation& notation = {}, bool preferFlats = false); // 60 → "C5"
[[nodiscard]] std::string midiNoteName (int midiNote, const Key& key, const Notation& notation = {});           // escrita según la tonalidad
[[nodiscard]] std::optional<SpelledNote> parseNote (std::string_view text);                       // "Bb", "C#", "F♯", "Ebb"
[[nodiscard]] std::optional<int> parseMidiNote (std::string_view text, const Notation& notation = {}); // "C5" → 60

// --- Intervalos ------------------------------------------------------------------------------------------------------
[[nodiscard]] std::string intervalName (Interval interval);                            // "m3", "P5", "A4", "M9"
[[nodiscard]] std::string degreeLabel (Interval interval, const Notation& notation = {}); // "b3", "5", "#11" (como en "1 ♭3 5 ♭7")

// --- Escalas y tonalidades -------------------------------------------------------------------------------------------
[[nodiscard]] std::string_view modeName (Mode mode);                               // "minor", "phrygian dominant"
[[nodiscard]] std::optional<Mode> parseMode (std::string_view text);               // también "ionian", "aeolian", "natural minor"
[[nodiscard]] std::string keyName (const Key& key, const Notation& notation = {}); // "A minor", "F# dorian"
[[nodiscard]] std::optional<Key> parseKey (std::string_view text);                  // "A minor", "Am", "Bb", "E phrygian"

// --- Acordes ---------------------------------------------------------------------------------------------------------
[[nodiscard]] std::string chordSuffix (ChordTypeId type, const Notation& notation = {});
[[nodiscard]] std::string chordSymbol (const Chord& chord, const Notation& notation = {});   // "Cm7b5", "F/A", "Bbadd9"
[[nodiscard]] std::optional<Chord> parseChordSymbol (std::string_view text);

// --- Números romanos y progresiones ------------------------------------------------------------------------------------
[[nodiscard]] std::string romanNumeralText (const RomanNumeral& numeral, const Notation& notation = {}); // "bVI", "viiø7"
[[nodiscard]] std::optional<RomanNumeral> parseRomanNumeral (std::string_view text);
[[nodiscard]] std::string progressionText (std::span<const RomanNumeral> progression, const Notation& notation = {}); // "i–♭VI–♭III–♭VII"
[[nodiscard]] std::optional<std::vector<RomanNumeral>> parseProgression (std::string_view text); // separadas por –, -, espacios, | o ,

// Pasa a la forma interna en la que se comparan los textos: ♭ → b, ♯ → #, ° → o, ø → h, Δ → ^, – → -, 𝄪 → x, 𝄫 → bb.
[[nodiscard]] std::string normalizeSymbols (std::string_view text);

} // namespace deepwater::theory
