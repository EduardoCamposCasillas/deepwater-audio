#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "deepwater/theory/Pitch.h"

namespace deepwater::theory
{

// Modos y escalas de 7 notas. Los 7 modos "griegos" son la escala mayor empezando en cada una de sus notas; los otros
// tres son las escalas menores que alteran la natural para tener sensible (armónica, melódica) y el 5.º modo de la
// armónica, el frigio dominante (el sonido "flamenco" / árabe del ♭2 con tercera mayor).
enum class Mode : int
{
    ionian,          // mayor
    dorian,
    phrygian,
    lydian,
    mixolydian,
    aeolian,         // menor natural
    locrian,
    harmonicMinor,
    melodicMinor,    // la ascendente (la del jazz): menor con 6 y 7 mayores
    phrygianDominant,
};

inline constexpr int modeCount = 10;

struct ModeInfo
{
    std::string_view name; // en inglés, el que se lee y escribe como texto ("A minor", "E phrygian")
    std::array<int, 7> semitones;
};

inline constexpr std::array<ModeInfo, modeCount> modeTable { {
    { "major", { 0, 2, 4, 5, 7, 9, 11 } },
    { "dorian", { 0, 2, 3, 5, 7, 9, 10 } },
    { "phrygian", { 0, 1, 3, 5, 7, 8, 10 } },
    { "lydian", { 0, 2, 4, 6, 7, 9, 11 } },
    { "mixolydian", { 0, 2, 4, 5, 7, 9, 10 } },
    { "minor", { 0, 2, 3, 5, 7, 8, 10 } },
    { "locrian", { 0, 1, 3, 5, 6, 8, 10 } },
    { "harmonic minor", { 0, 2, 3, 5, 7, 8, 11 } },
    { "melodic minor", { 0, 2, 3, 5, 7, 9, 11 } },
    { "phrygian dominant", { 0, 1, 4, 5, 7, 8, 10 } },
} };

[[nodiscard]] constexpr const ModeInfo& modeInfo (Mode mode) noexcept
{
    return modeTable[static_cast<std::size_t> (mode)];
}

// Tonalidad: una tónica escrita y un modo. Las notas de la escala salen escritas (letras consecutivas): Fa mayor da
// F G A B♭ C D E, nunca A♯.
struct Key
{
    SpelledNote tonic = notes::C;
    Mode mode = Mode::ionian;

    // La nota del grado 0..6 (0 = la tónica).
    [[nodiscard]] constexpr SpelledNote degree (int index) const noexcept
    {
        const auto i = static_cast<std::size_t> (((index % 7) + 7) % 7);
        return transpose (tonic, { static_cast<int> (i) + 1, modeInfo (mode).semitones[i] });
    }

    // El grado (0..6) que ocupa esta clase de altura, o −1 si no está en la escala.
    [[nodiscard]] constexpr int degreeOf (PitchClass pc) const noexcept
    {
        const int offset = wrap12 (pc - tonic.pitchClass());
        for (int i = 0; i < 7; ++i)
            if (modeInfo (mode).semitones[static_cast<std::size_t> (i)] == offset)
                return i;
        return -1;
    }

    [[nodiscard]] constexpr bool contains (PitchClass pc) const noexcept { return degreeOf (pc) >= 0; }

    constexpr bool operator== (const Key&) const = default;
};

// Cuántas alteraciones tiene la escala escrita (las dobles pesan mucho: nadie escribe en Sol♯ mayor, con Fa𝄪).
[[nodiscard]] constexpr int accidentalWeight (const Key& key) noexcept
{
    int weight = 0;
    for (int i = 0; i < 7; ++i)
    {
        const int a = key.degree (i).accidental;
        weight += (a < 0 ? -a : a) * ((a > 1 || a < -1) ? 10 : 1);
    }
    return weight;
}

// La forma habitual de escribir una tonalidad dada solo la tecla de la tónica: la que tenga menos alteraciones (Re♭
// mayor con 5 ♭ antes que Do♯ mayor con 7 ♯; Sol♯ menor con 5 ♯ antes que La♭ menor con 7 ♭). Empate (Fa♯ / Sol♭ mayor,
// Re♯ / Mi♭ menor): sostenidos, como los nombra el Piano Roll de FL.
[[nodiscard]] constexpr Key standardKey (PitchClass tonic, Mode mode) noexcept
{
    std::array<SpelledNote, 2> candidates {};
    const int count = spellingCandidates (tonic, candidates);
    Key best { candidates[0], mode };
    for (int i = 1; i < count; ++i)
    {
        const Key other { candidates[static_cast<std::size_t> (i)], mode };
        if (accidentalWeight (other) < accidentalWeight (best))
            best = other;
    }
    return best;
}

// Cómo se escribe una tecla dentro de una tonalidad. Si está en la escala, como en la escala. Si no, como un grado
// alterado respecto de la escala MAYOR de la tónica, con las convenciones del pop y el jazz: ♭2, ♭3, ♯4, ♭6, ♭7
// (en La menor, la nota 1 semitono sobre La es Si♭ —el ♭2 del frigio—, no La♯).
[[nodiscard]] constexpr SpelledNote spellInKey (PitchClass pc, const Key& key) noexcept
{
    const int degree = key.degreeOf (pc);
    if (degree >= 0)
        return key.degree (degree);

    const int offset = wrap12 (pc - key.tonic.pitchClass());
    constexpr std::array<int, 12> numberForOffset { 1, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7 };
    return transpose (key.tonic, { numberForOffset[static_cast<std::size_t> (offset)], offset });
}

} // namespace deepwater::theory
