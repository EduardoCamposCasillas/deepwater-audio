#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "deepwater/theory/Chord.h"
#include "deepwater/theory/Scale.h"
#include "deepwater/theory/StaticVector.h"

namespace deepwater::theory
{

// --- Acordes diatónicos ---------------------------------------------------------------------------------------------
// Apilar terceras DENTRO de la escala: desde el grado d, las notas d, d+2, d+4 (y d+6 para la séptima). Así salen los
// acordes "de la tonalidad": en Do mayor I ii iii IV V vi vii°, en La menor i ii° III iv v VI VII.
enum class ChordSize
{
    triad,
    seventh,
};

// El tipo del catálogo cuyas notas (dentro de una octava) son exactamente estas; −1 si ninguno.
[[nodiscard]] constexpr int chordTypeWithSemitones (const StaticVector<int, 7>& semitones) noexcept
{
    for (int t = 0; t < chordTypeCount; ++t)
    {
        const auto& info = chordTypes[static_cast<std::size_t> (t)];
        if (info.toneCount != static_cast<int> (semitones.size()))
            continue;
        bool same = true;
        for (int i = 0; i < info.toneCount && same; ++i)
            same = info.tones[static_cast<std::size_t> (i)].semitones == semitones[static_cast<std::size_t> (i)];
        if (same)
            return t;
    }
    return -1;
}

// El acorde del grado 'degree' (0..6) de la tonalidad. Siempre existe para las escalas de modeTable (lo comprueban los
// tests), pero se devuelve optional por si se agrega una escala cuyas terceras apiladas no estén en el catálogo.
[[nodiscard]] constexpr std::optional<Chord> diatonicChord (const Key& key, int degree, ChordSize size = ChordSize::triad) noexcept
{
    const int notesInChord = size == ChordSize::triad ? 3 : 4;
    const auto& scale = modeInfo (key.mode).semitones;
    StaticVector<int, 7> semitones;
    for (int i = 0; i < notesInChord; ++i)
    {
        const int index = degree + 2 * i;
        const int fromTonic = scale[static_cast<std::size_t> (index % 7)] + 12 * (index / 7);
        semitones.push_back (fromTonic - scale[static_cast<std::size_t> (degree % 7)]);
    }
    const int type = chordTypeWithSemitones (semitones);
    if (type < 0)
        return std::nullopt;
    return Chord { key.degree (degree), static_cast<ChordTypeId> (type) };
}

// ¿Todas las notas del acorde (y el bajo) están en la escala? Sirve para la paleta: qué sus, add y novenas "caben" en
// la tonalidad sin salirse de ella.
[[nodiscard]] constexpr bool isDiatonic (const Chord& chord, const Key& key) noexcept
{
    for (const auto& tone : chord.tones())
        if (! key.contains (tone.pitchClass()))
            return false;
    return ! chord.hasBass || key.contains (chord.bass.pitchClass());
}

// Préstamo modal: de qué modos con la MISMA tónica es diatónico un acorde. En Do mayor, A♭ (♭VI) no está en la escala
// pero sí en Do menor (eólico) y en Do frigio: se "tomó prestado" de ahí. Solo los 7 modos griegos y las dos menores
// con sensible, en el orden de modeTable. Compara teclas, no escritura (C♯ cuenta como D♭): lo que llega por MIDI no
// trae escritura.
[[nodiscard]] constexpr StaticVector<Mode, 10> modesContaining (const Chord& chord, SpelledNote tonic) noexcept
{
    StaticVector<Mode, 10> result;
    for (int m = 0; m < static_cast<int> (Mode::phrygianDominant); ++m)
        if (isDiatonic (chord, Key { tonic, static_cast<Mode> (m) }))
            result.push_back (static_cast<Mode> (m));
    return result;
}

// --- Números romanos -----------------------------------------------------------------------------------------------
// Un acorde descrito por su grado en la tonalidad en vez de por su nombre: "i–♭VI–♭III–♭VII" es la misma progresión en
// La menor (Am F C G) que en Mi♭ menor (E♭m C♭ G♭ D♭). La alteración se mide contra la escala MAYOR de la tónica (la
// convención del pop y el jazz): en menor, el acorde sobre la 6.ª nota es ♭VI.
struct RomanNumeral
{
    int degree = 1;     // 1..7
    int accidental = 0; // −1 = ♭, +1 = ♯ (respecto de la escala mayor)
    ChordTypeId type = 0;

    constexpr bool operator== (const RomanNumeral&) const = default;
};

[[nodiscard]] constexpr RomanNumeral romanNumeralFor (const Chord& chord, const Key& key) noexcept
{
    const auto interval = intervalBetween (key.tonic, chord.root);
    return { interval.number, alterationFromMajor (interval), chord.type };
}

[[nodiscard]] constexpr Chord chordFor (const RomanNumeral& numeral, const Key& key) noexcept
{
    const int semitones = majorScaleSemitones[static_cast<std::size_t> ((numeral.degree - 1) % 7)] + numeral.accidental;
    return { transpose (key.tonic, { numeral.degree, semitones }), numeral.type };
}

// --- Funciones armónicas ---------------------------------------------------------------------------------------------
// El papel de un acorde en la historia de la progresión: TÓNICA (reposo, "en casa"), SUBDOMINANTE (alejarse, preparar)
// y DOMINANTE (tensión que pide volver a la tónica). Es una simplificación por grado de la fundamental, la que se usa
// en el pop y en la música modal:
//   I, iii, vi (y ♭III)      → tónica (comparten 2 notas con el I)
//   ii, IV, ♭II, ♭VI          → subdominante (en menor, ♭VI y el ♭II frigio/napolitano preparan la vuelta)
//   V, vii°, ♭VII             → dominante (♭VII–i es la "cadencia modal": la dominante sin sensible del eólico)
enum class HarmonicFunction
{
    tonic,
    subdominant,
    dominant,
};

[[nodiscard]] constexpr HarmonicFunction harmonicFunction (const RomanNumeral& numeral) noexcept
{
    switch (numeral.degree)
    {
        case 1: case 3: return HarmonicFunction::tonic;
        case 2: case 4: return HarmonicFunction::subdominant;
        case 6: return numeral.accidental < 0 ? HarmonicFunction::subdominant : HarmonicFunction::tonic;
        default: return HarmonicFunction::dominant; // 5 y 7
    }
}

// --- Acordes prestados (Fase 4 de Tidechart) -------------------------------------------------------------------------
// Los acordes de los modos paralelos (misma tónica) que NO están en la tonalidad, listos para usar en una progresión.
// Cada uno con su grado en la tonalidad y el modo del que se tomó. Ordenados por lo habitual del préstamo: primero los
// modos de los que más se toma (para una tonalidad mayor, el menor natural: iv, ♭VI, ♭VII, ♭III; para una menor, el
// dórico, el frigio y la menor armónica: IV, ♭II, V), y dentro de cada modo por grado; los disminuidos y aumentados,
// más difíciles de usar, al final. Sin repetidos: un acorde que está en varios modos aparece con el primero.
struct BorrowedChord
{
    Chord chord;
    RomanNumeral numeral;
    Mode source = Mode::aeolian;
};

[[nodiscard]] constexpr StaticVector<BorrowedChord, 64> borrowedChords (const Key& key, ChordSize size = ChordSize::triad) noexcept
{
    // La tonalidad es "mayor" si su 3.ª es mayor.
    const bool majorFamily = modeInfo (key.mode).semitones[2] == 4;
    constexpr std::array<Mode, 9> fromMajor { Mode::aeolian, Mode::mixolydian, Mode::dorian, Mode::phrygian, Mode::harmonicMinor,
                                              Mode::lydian, Mode::melodicMinor, Mode::locrian, Mode::ionian };
    constexpr std::array<Mode, 9> fromMinor { Mode::dorian, Mode::phrygian, Mode::harmonicMinor, Mode::ionian, Mode::mixolydian,
                                              Mode::melodicMinor, Mode::lydian, Mode::locrian, Mode::aeolian };
    const auto& order = majorFamily ? fromMajor : fromMinor;

    const auto unstable = [] (const Chord& c) {
        for (const auto& tone : c.info().tones)
            if (tone.number == 5 && tone.semitones != 7)
                return true; // 5.ª disminuida o aumentada
        return false;
    };

    StaticVector<BorrowedChord, 64> result;
    for (int pass = 0; pass < 2; ++pass)
        for (const Mode source : order)
        {
            if (source == key.mode)
                continue;
            const Key parallel { key.tonic, source };
            for (int degree = 0; degree < 7; ++degree)
            {
                const auto chord = diatonicChord (parallel, degree, size);
                if (! chord || isDiatonic (*chord, key) || unstable (*chord) != (pass == 1))
                    continue;
                bool seen = false;
                for (const auto& other : result)
                    seen = seen || (other.chord.root.pitchClass() == chord->root.pitchClass() && other.chord.type == chord->type);
                if (! seen)
                    result.push_back ({ *chord, romanNumeralFor (*chord, key), source });
            }
        }
    return result;
}

} // namespace deepwater::theory
