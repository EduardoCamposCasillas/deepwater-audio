#pragma once

#include <array>

namespace deepwater::theory
{

// Clase de altura: la nota sin la octava, 0 = Do ... 11 = Si. Todas las Do (C3, C5, C7) son la clase 0. Las cuentas de
// armonía (¿qué notas tiene este acorde?, ¿está en la escala?) se hacen aquí, módulo 12.
using PitchClass = int;

[[nodiscard]] constexpr int wrap12 (int value) noexcept
{
    return ((value % 12) + 12) % 12;
}

// La clase de altura de cada letra natural: C D E F G A B.
inline constexpr std::array<int, 7> naturalPitchClass { 0, 2, 4, 5, 7, 9, 11 };

// Intervalo con nombre: el número (1 = unísono, 3 = tercera, 9 = novena...) dice cuántas LETRAS se sube, y los semitonos
// cuánto se sube de verdad. Hacen falta los dos: Do→Mi♭ (3.ª menor: 3 letras, 3 semitonos) y Do→Re♯ (2.ª aumentada:
// 2 letras, 3 semitonos) suenan igual en un piano pero se escriben distinto, y el número decide cómo se escribe.
struct Interval
{
    int number = 1;
    int semitones = 0;

    constexpr bool operator== (const Interval&) const = default;
};

// Semitonos de cada número en la escala mayor (1..7): la referencia para decir si un intervalo es mayor, menor, justo,
// aumentado o disminuido, y para los grados con ♭/♯ ("♭3", "♭VI").
inline constexpr std::array<int, 7> majorScaleSemitones { 0, 2, 4, 5, 7, 9, 11 };

// Cuánto se aparta un intervalo del de la escala mayor con su mismo número (−1 = ♭, +1 = ♯). Para 9, 11 y 13 se compara
// con 2, 4 y 6 una octava más arriba.
[[nodiscard]] constexpr int alterationFromMajor (Interval interval) noexcept
{
    const int index = (interval.number - 1) % 7;
    const int octaves = (interval.number - 1) / 7;
    int diff = interval.semitones - (majorScaleSemitones[static_cast<std::size_t> (index)] + 12 * octaves);
    diff = wrap12 (diff);
    return diff > 6 ? diff - 12 : diff;
}

// Nota escrita: una letra (0 = C ... 6 = B) y una alteración (−2 = 𝄫, −1 = ♭, 0 = natural, +1 = ♯, +2 = 𝄪). Es lo que
// hace que en Fa mayor la cuarta nota sea Si♭ y no La♯: la misma tecla, distinta letra.
struct SpelledNote
{
    int letter = 0;
    int accidental = 0;

    [[nodiscard]] constexpr PitchClass pitchClass() const noexcept
    {
        return wrap12 (naturalPitchClass[static_cast<std::size_t> (letter)] + accidental);
    }

    constexpr bool operator== (const SpelledNote&) const = default;
};

namespace notes
{
inline constexpr SpelledNote C { 0, 0 }, D { 1, 0 }, E { 2, 0 }, F { 3, 0 }, G { 4, 0 }, A { 5, 0 }, B { 6, 0 };
}

[[nodiscard]] constexpr SpelledNote sharp (SpelledNote note) noexcept { return { note.letter, note.accidental + 1 }; }
[[nodiscard]] constexpr SpelledNote flat (SpelledNote note) noexcept { return { note.letter, note.accidental - 1 }; }

// Subir un intervalo desde una nota escrita: la letra avanza (número − 1) posiciones y la alteración es la que haga
// falta para llegar a los semitonos pedidos. Do + 3.ª menor = Mi♭; Re + 3.ª mayor = Fa♯; Si♭ + 5.ª justa = Fa.
[[nodiscard]] constexpr SpelledNote transpose (SpelledNote from, Interval interval) noexcept
{
    const int letter = (from.letter + interval.number - 1) % 7;
    int diff = wrap12 (from.pitchClass() + interval.semitones - naturalPitchClass[static_cast<std::size_t> (letter)]);
    if (diff > 6)
        diff -= 12;
    return { letter, diff };
}

// El intervalo (dentro de una octava) de una nota escrita a otra: Do→Mi♭ = {3, 3}, La→Fa = {6, 8}.
[[nodiscard]] constexpr Interval intervalBetween (SpelledNote from, SpelledNote to) noexcept
{
    return { ((to.letter - from.letter) % 7 + 7) % 7 + 1, wrap12 (to.pitchClass() - from.pitchClass()) };
}

// Una tecla sin contexto: con sostenidos (C, C#, D...) o con bemoles (C, Db, D...).
[[nodiscard]] constexpr SpelledNote spellPitchClass (PitchClass pc, bool preferFlats = false) noexcept
{
    constexpr std::array<SpelledNote, 12> sharps { notes::C, sharp (notes::C), notes::D, sharp (notes::D), notes::E, notes::F,
                                                   sharp (notes::F), notes::G, sharp (notes::G), notes::A, sharp (notes::A), notes::B };
    constexpr std::array<SpelledNote, 12> flats { notes::C, flat (notes::D), notes::D, flat (notes::E), notes::E, notes::F,
                                                  flat (notes::G), notes::G, flat (notes::A), notes::A, flat (notes::B), notes::B };
    const auto index = static_cast<std::size_t> (wrap12 (pc));
    return preferFlats ? flats[index] : sharps[index];
}

// Las formas razonables de escribir una tecla (sin dobles alteraciones): las blancas, una; las negras, dos (C♯ / D♭).
// Sirve para elegir la escritura con menos alteraciones de una tonalidad o de un acorde.
[[nodiscard]] constexpr int spellingCandidates (PitchClass pc, std::array<SpelledNote, 2>& out) noexcept
{
    const auto withSharp = spellPitchClass (pc, false);
    const auto withFlat = spellPitchClass (pc, true);
    out[0] = withSharp;
    if (withFlat == withSharp)
        return 1;
    out[1] = withFlat;
    return 2;
}

// Cómo se escriben las notas como texto. FL Studio llama C5 a la nota MIDI 60 (el Do central) y A5 a la 69 (La 440 Hz);
// otros programas usan C4 = 60 o C3 = 60, por eso es configurable.
struct Notation
{
    bool unicode = false; // ♭ ♯ en vez de b #
    int middleCOctave = 5;
};

} // namespace deepwater::theory
