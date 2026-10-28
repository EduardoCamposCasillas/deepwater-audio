#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "deepwater/theory/Pitch.h"
#include "deepwater/theory/StaticVector.h"

namespace deepwater::theory
{

// --- Tipos de acorde -----------------------------------------------------------------------------------------------
// Un tipo de acorde es una lista de intervalos desde la fundamental: la tríada mayor es 1–3–5 ({1,0} {3,4} {5,7}), la
// séptima de dominante le suma la 7.ª menor ({7,10}), la novena la 9.ª mayor ({9,14}). Con los intervalos escritos, los
// tonos salen bien escritos desde cualquier fundamental (Cm7♭5 = C E♭ G♭ B♭; F♯7 = F♯ A♯ C♯ E).
//
// El orden del catálogo importa: va de lo más común a lo menos, y al reconocer un acorde por sus notas, si dos
// lecturas empatan (C6 = Am7 con otro bajo) gana la más común. Por eso NO reordenar sin mirar los tests de ChordFinder.
// Tampoco quitar ni insertar en medio: el índice se guardará en el estado de los proyectos (Fase 4); lo nuevo, al final.

using ChordTypeId = std::uint8_t;

struct ChordTypeInfo
{
    std::string_view symbol;          // sufijo canónico en ASCII ("m7b5"); en Unicode, b/# antes de un número → ♭/♯
    std::array<std::string_view, 6> aliases; // otras formas de escribirlo que se aceptan al leer (normalizadas: ° → o, ø → h, Δ → ^)
    std::array<Interval, 7> tones;
    int toneCount;
    bool romanUpper;                  // en números romanos: mayúscula si la tercera es mayor (o no hay tercera)
    std::string_view romanSuffix;     // lo que va tras el número romano (la tercera ya la dice la mayúscula/minúscula)
};

namespace detail
{
inline constexpr Interval R { 1, 0 }, m2 { 2, 1 }, M2 { 2, 2 }, m3 { 3, 3 }, M3 { 3, 4 }, P4 { 4, 5 }, A4 { 4, 6 }, d5 { 5, 6 },
    P5 { 5, 7 }, A5 { 5, 8 }, M6 { 6, 9 }, d7 { 7, 9 }, m7 { 7, 10 }, M7 { 7, 11 }, m9 { 9, 13 }, M9 { 9, 14 }, A9 { 9, 15 },
    P11 { 11, 17 }, A11 { 11, 18 }, M13 { 13, 21 };
} // namespace detail

inline constexpr std::array<ChordTypeInfo, 38> chordTypes { {
    // Tríadas (y el "power chord", que no tiene tercera)
    { "", { "maj", "M" }, { detail::R, detail::M3, detail::P5 }, 3, true, "" },
    { "m", { "min", "mi", "-" }, { detail::R, detail::m3, detail::P5 }, 3, false, "" },
    { "5", {}, { detail::R, detail::P5 }, 2, true, "5" },
    { "sus4", { "sus" }, { detail::R, detail::P4, detail::P5 }, 3, true, "sus4" },
    { "sus2", {}, { detail::R, detail::M2, detail::P5 }, 3, true, "sus2" },
    { "dim", { "o", "mb5", "m(b5)" }, { detail::R, detail::m3, detail::d5 }, 3, false, "o" },
    { "aug", { "+", "(#5)", "+5" }, { detail::R, detail::M3, detail::A5 }, 3, true, "+" },
    // Séptimas
    { "7", { "dom7" }, { detail::R, detail::M3, detail::P5, detail::m7 }, 4, true, "7" },
    { "maj7", { "M7", "^7", "^", "ma7", "Maj7" }, { detail::R, detail::M3, detail::P5, detail::M7 }, 4, true, "maj7" },
    { "m7", { "min7", "mi7", "-7" }, { detail::R, detail::m3, detail::P5, detail::m7 }, 4, false, "7" },
    { "m7b5", { "h", "h7", "m7(b5)", "-7b5", "min7b5" }, { detail::R, detail::m3, detail::d5, detail::m7 }, 4, false, "h7" },
    { "dim7", { "o7" }, { detail::R, detail::m3, detail::d5, detail::d7 }, 4, false, "o7" },
    { "6", { "maj6", "M6" }, { detail::R, detail::M3, detail::P5, detail::M6 }, 4, true, "6" },
    { "m6", { "min6", "-6" }, { detail::R, detail::m3, detail::P5, detail::M6 }, 4, false, "6" },
    { "add9", { "add2", "(add9)" }, { detail::R, detail::M3, detail::P5, detail::M9 }, 4, true, "add9" },
    { "madd9", { "m(add9)", "madd2", "-add9" }, { detail::R, detail::m3, detail::P5, detail::M9 }, 4, false, "add9" },
    { "7sus4", { "7sus" }, { detail::R, detail::P4, detail::P5, detail::m7 }, 4, true, "7sus4" },
    { "m(maj7)", { "mMaj7", "mM7", "m(M7)", "mmaj7", "-^7" }, { detail::R, detail::m3, detail::P5, detail::M7 }, 4, false, "maj7" },
    // Novenas, oncenas y trecenas (el color del R&B y el neo-soul)
    { "9", {}, { detail::R, detail::M3, detail::P5, detail::m7, detail::M9 }, 5, true, "9" },
    { "maj9", { "M9", "^9", "ma9" }, { detail::R, detail::M3, detail::P5, detail::M7, detail::M9 }, 5, true, "maj9" },
    { "m9", { "min9", "mi9", "-9" }, { detail::R, detail::m3, detail::P5, detail::m7, detail::M9 }, 5, false, "9" },
    { "m11", { "min11", "-11" }, { detail::R, detail::m3, detail::P5, detail::m7, detail::M9, detail::P11 }, 6, false, "11" },
    { "6/9", { "69", "6add9" }, { detail::R, detail::M3, detail::P5, detail::M6, detail::M9 }, 5, true, "6/9" },
    { "m6/9", { "m69", "-69" }, { detail::R, detail::m3, detail::P5, detail::M6, detail::M9 }, 5, false, "6/9" },
    { "13", {}, { detail::R, detail::M3, detail::P5, detail::m7, detail::M9, detail::M13 }, 6, true, "13" },
    { "maj13", { "M13", "^13" }, { detail::R, detail::M3, detail::P5, detail::M7, detail::M9, detail::M13 }, 6, true, "maj13" },
    { "m13", { "min13", "-13" }, { detail::R, detail::m3, detail::P5, detail::m7, detail::M9, detail::M13 }, 6, false, "13" },
    { "11", {}, { detail::R, detail::M3, detail::P5, detail::m7, detail::M9, detail::P11 }, 6, true, "11" },
    { "9sus4", { "9sus" }, { detail::R, detail::P4, detail::P5, detail::m7, detail::M9 }, 5, true, "9sus4" },
    { "add11", { "add4" }, { detail::R, detail::M3, detail::P5, detail::P11 }, 4, true, "add11" },
    { "madd11", { "m(add11)", "madd4" }, { detail::R, detail::m3, detail::P5, detail::P11 }, 4, false, "add11" },
    // Alteradas: la tensión de la dominante (♭9, ♯9, ♯5) y el color lidio (♯11)
    { "7b9", { "7(b9)" }, { detail::R, detail::M3, detail::P5, detail::m7, detail::m9 }, 5, true, "7b9" },
    { "7#9", { "7(#9)" }, { detail::R, detail::M3, detail::P5, detail::m7, detail::A9 }, 5, true, "7#9" },
    { "7#5", { "aug7", "+7", "7(#5)", "7+" }, { detail::R, detail::M3, detail::A5, detail::m7 }, 4, true, "+7" },
    { "maj7#5", { "maj7(#5)", "+maj7", "augmaj7", "^7#5" }, { detail::R, detail::M3, detail::A5, detail::M7 }, 4, true, "+maj7" },
    { "maj7#11", { "maj7(#11)", "^7#11", "M7#11" }, { detail::R, detail::M3, detail::P5, detail::M7, detail::A11 }, 5, true, "maj7#11" },
    { "7b5", { "7(b5)" }, { detail::R, detail::M3, detail::d5, detail::m7 }, 4, true, "7b5" },
    // El acorde frigio: menor con ♭9 (el ♭2 del frigio encima de la tónica menor)
    { "m7b9", { "m7(b9)", "-7b9" }, { detail::R, detail::m3, detail::P5, detail::m7, detail::m9 }, 5, false, "7b9" },
} };

inline constexpr int chordTypeCount = static_cast<int> (chordTypes.size());

[[nodiscard]] constexpr const ChordTypeInfo& chordTypeInfo (ChordTypeId type) noexcept
{
    return chordTypes[type < chordTypes.size() ? type : 0];
}

// El tipo por su símbolo canónico ("m7", "maj7"...), para escribir tests y tablas legibles; −1 si no existe.
[[nodiscard]] constexpr int findChordType (std::string_view symbol) noexcept
{
    for (int i = 0; i < chordTypeCount; ++i)
        if (chordTypes[static_cast<std::size_t> (i)].symbol == symbol)
            return i;
    return -1;
}

[[nodiscard]] constexpr ChordTypeId chordType (std::string_view symbol) noexcept
{
    const int index = findChordType (symbol);
    return static_cast<ChordTypeId> (index < 0 ? 0 : index);
}

// Las clases de altura de un tipo de acorde sobre una fundamental, como máscara de 12 bits (bit 0 = Do).
[[nodiscard]] constexpr std::uint16_t chordMask (PitchClass root, ChordTypeId type) noexcept
{
    const auto& info = chordTypeInfo (type);
    std::uint16_t mask = 0;
    for (int i = 0; i < info.toneCount; ++i)
        mask = static_cast<std::uint16_t> (mask | (1u << wrap12 (root + info.tones[static_cast<std::size_t> (i)].semitones)));
    return mask;
}

// --- Acorde ---------------------------------------------------------------------------------------------------------
// Fundamental escrita + tipo + bajo opcional. El bajo decide la inversión: si es un tono del acorde, el acorde está
// invertido (C/E = 1.ª inversión); si no, es un acorde "slash" con un bajo ajeno (C/D, el sonido del pedal en el bajo).
struct Chord
{
    SpelledNote root = notes::C;
    ChordTypeId type = 0;
    bool hasBass = false;
    SpelledNote bass {};

    [[nodiscard]] constexpr const ChordTypeInfo& info() const noexcept { return chordTypeInfo (type); }
    [[nodiscard]] constexpr int toneCount() const noexcept { return info().toneCount; }

    // Los tonos escritos en orden de terceras (1, 3, 5, 7, 9...).
    [[nodiscard]] constexpr StaticVector<SpelledNote, 7> tones() const noexcept
    {
        StaticVector<SpelledNote, 7> result;
        for (int i = 0; i < toneCount(); ++i)
            result.push_back (transpose (root, info().tones[static_cast<std::size_t> (i)]));
        return result;
    }

    [[nodiscard]] constexpr SpelledNote bassNote() const noexcept { return hasBass ? bass : root; }

    // 0 = fundamental en el bajo, 1 = 1.ª inversión (la 3.ª abajo), 2 = 2.ª (la 5.ª), 3 = 3.ª (la 7.ª)...; −1 si el
    // bajo no es un tono del acorde.
    [[nodiscard]] constexpr int inversion() const noexcept
    {
        if (! hasBass)
            return 0;
        const auto all = tones();
        for (std::size_t i = 0; i < all.size(); ++i)
            if (all[i].pitchClass() == bass.pitchClass())
                return static_cast<int> (i);
        return -1;
    }

    // El mismo acorde con el tono 'index' en el bajo (0 = estado fundamental).
    [[nodiscard]] constexpr Chord withInversion (int index) const noexcept
    {
        Chord result = *this;
        if (index <= 0 || index >= toneCount())
        {
            result.hasBass = false;
            return result;
        }
        result.hasBass = true;
        result.bass = transpose (root, info().tones[static_cast<std::size_t> (index)]);
        return result;
    }

    [[nodiscard]] constexpr Chord withBass (SpelledNote note) const noexcept
    {
        Chord result = *this;
        result.hasBass = note.pitchClass() != root.pitchClass();
        result.bass = note;
        return result.hasBass ? result : Chord { root, type };
    }

    // Las notas MIDI en posición cerrada, de grave a agudo, con la fundamental en 'rootNote' (la tecla que dispara el
    // acorde). En una inversión, los tonos por debajo del bajo suben una octava (C/E desde C5: E5 G5 C6); un bajo ajeno
    // va por debajo de todo (C/D desde C5: D4 C5 E5 G5). Lo que caiga fuera de 0..127 se omite. Sin reservar memoria:
    // se puede llamar en el hilo de audio.
    [[nodiscard]] constexpr StaticVector<int, 8> midiNotes (int rootNote) const noexcept
    {
        std::array<int, 8> pitches {};
        int count = 0;
        const int inv = inversion();
        const int bassOffset = inv > 0 ? info().tones[static_cast<std::size_t> (inv)].semitones : 0;
        for (int i = 0; i < toneCount(); ++i)
        {
            int offset = info().tones[static_cast<std::size_t> (i)].semitones;
            while (offset < bassOffset)
                offset += 12;
            pitches[static_cast<std::size_t> (count++)] = rootNote + offset;
        }
        if (inv < 0)
        {
            int bassNote = rootNote - wrap12 (rootNote - bass.pitchClass());
            if (bassNote == rootNote)
                bassNote -= 12;
            pitches[static_cast<std::size_t> (count++)] = bassNote;
        }

        // Orden ascendente (inserción: son 8 como mucho).
        for (int i = 1; i < count; ++i)
            for (int j = i; j > 0 && pitches[static_cast<std::size_t> (j - 1)] > pitches[static_cast<std::size_t> (j)]; --j)
            {
                const int t = pitches[static_cast<std::size_t> (j)];
                pitches[static_cast<std::size_t> (j)] = pitches[static_cast<std::size_t> (j - 1)];
                pitches[static_cast<std::size_t> (j - 1)] = t;
            }

        StaticVector<int, 8> result;
        for (int i = 0; i < count; ++i)
            if (pitches[static_cast<std::size_t> (i)] >= 0 && pitches[static_cast<std::size_t> (i)] <= 127)
                result.push_back (pitches[static_cast<std::size_t> (i)]);
        return result;
    }

    // Igualdad de escritura (C/E ≠ C/F♭ aunque suenen igual).
    [[nodiscard]] constexpr bool operator== (const Chord& other) const noexcept
    {
        return root == other.root && type == other.type && hasBass == other.hasBass && (! hasBass || bass == other.bass);
    }
};

} // namespace deepwater::theory
