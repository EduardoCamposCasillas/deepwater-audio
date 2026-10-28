// Tests de la librería de teoría musical (deepwater::theory). No dependen de JUCE: se compilan como un ejecutable normal
// y se lanzan con ctest. Si algo falla, imprime el archivo, la línea y la condición (y, en los de texto, lo que salió).
//
// Los ejemplos salen del contexto de Tidechart: menor natural y frigio, cadencia andaluza, préstamo modal, séptimas y
// novenas, además del pop de referencia.

#include <array>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "deepwater/theory/Chord.h"
#include "deepwater/theory/ChordFinder.h"
#include "deepwater/theory/Harmony.h"
#include "deepwater/theory/Pitch.h"
#include "deepwater/theory/Progression.h"
#include "deepwater/theory/Rhythm.h"
#include "deepwater/theory/Scale.h"
#include "deepwater/theory/Text.h"
#include "deepwater/theory/Voicing.h"

namespace
{
int failures = 0;

#define CHECK(condition)                                                                   \
    do                                                                                     \
    {                                                                                      \
        if (! (condition))                                                                 \
        {                                                                                  \
            std::printf ("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #condition);           \
            ++failures;                                                                    \
        }                                                                                  \
    } while (false)

// Igualdad de textos que, si falla, dice qué salió.
#define CHECK_TEXT(actual, expected)                                                                          \
    do                                                                                                        \
    {                                                                                                         \
        const std::string a_ = (actual);                                                                      \
        const std::string e_ = (expected);                                                                    \
        if (a_ != e_)                                                                                         \
        {                                                                                                     \
            std::printf ("  FALLO %s:%d: %s\n    salió \"%s\", se esperaba \"%s\"\n", __FILE__, __LINE__, #actual, \
                         a_.c_str(), e_.c_str());                                                             \
            ++failures;                                                                                       \
        }                                                                                                     \
    } while (false)

using namespace deepwater::theory;

const Notation ascii {};
const Notation pretty { true, 5 };

Key key (const char* text)
{
    const auto parsed = parseKey (text);
    CHECK (parsed.has_value());
    return parsed.value_or (Key {});
}

Chord chord (const char* text)
{
    const auto parsed = parseChordSymbol (text);
    if (! parsed)
        std::printf ("  (no se pudo leer el acorde \"%s\")\n", text);
    CHECK (parsed.has_value());
    return parsed.value_or (Chord {});
}

std::string tonesText (const Chord& c, const Notation& notation = ascii)
{
    std::string text;
    for (const auto& tone : c.tones())
        text += (text.empty() ? "" : " ") + noteName (tone, notation);
    return text;
}

std::string scaleText (const Key& k)
{
    std::string text;
    for (int i = 0; i < 7; ++i)
        text += (i == 0 ? "" : " ") + noteName (k.degree (i));
    return text;
}

std::vector<int> midi (const Chord& c, int rootNote)
{
    const auto notes = c.midiNotes (rootNote);
    return { notes.begin(), notes.end() };
}

// Los acordes de los 7 grados de una tonalidad como números romanos, separados por espacios.
std::string diatonicText (const Key& k, ChordSize size)
{
    std::string text;
    for (int d = 0; d < 7; ++d)
    {
        const auto c = diatonicChord (k, d, size);
        CHECK (c.has_value());
        if (c)
            text += (d == 0 ? "" : " ") + romanNumeralText (romanNumeralFor (*c, k), pretty);
    }
    return text;
}

// Una progresión en romanos llevada a una tonalidad, como símbolos de acorde.
std::string realize (const char* progression, const Key& k)
{
    const auto numerals = parseProgression (progression);
    CHECK (numerals.has_value());
    std::string text;
    if (numerals)
        for (const auto& n : *numerals)
            text += (text.empty() ? "" : " ") + chordSymbol (chordFor (n, k));
    return text;
}

std::string bestChord (std::vector<int> notes)
{
    const auto readings = findChords (notes);
    return readings.empty() ? std::string ("(nada)") : chordSymbol (readings.front().chord);
}

// --- Notas e intervalos ----------------------------------------------------------------------------------------------
void testSpelledNotes()
{
    std::printf ("Notas escritas e intervalos\n");
    CHECK (transpose (notes::C, { 3, 3 }) == flat (notes::E));               // Do + 3.ª menor = Mi♭
    CHECK (transpose (notes::D, { 3, 4 }) == sharp (notes::F));              // Re + 3.ª mayor = Fa♯
    CHECK (transpose (flat (notes::B), { 5, 7 }) == notes::F);
    CHECK (transpose (notes::C, { 2, 3 }) == sharp (notes::D));              // 2.ª aumentada: misma tecla que Mi♭
    CHECK (transpose (notes::C, { 9, 14 }) == notes::D);
    CHECK (transpose (sharp (notes::G), { 3, 4 }) == sharp (notes::B));      // Sol♯ mayor: Si♯, no Do
    CHECK ((transpose (notes::C, { 7, 9 }) == SpelledNote { 6, -2 }));      // 7.ª disminuida de Do: Si𝄫

    CHECK ((intervalBetween (notes::C, flat (notes::E)) == Interval { 3, 3 }));
    CHECK ((intervalBetween (notes::A, notes::F) == Interval { 6, 8 }));
    CHECK ((intervalBetween (notes::E, notes::C) == Interval { 6, 8 }));
    CHECK (alterationFromMajor ({ 3, 3 }) == -1);
    CHECK (alterationFromMajor ({ 5, 7 }) == 0);
    CHECK (alterationFromMajor ({ 11, 18 }) == 1);
    CHECK (alterationFromMajor ({ 9, 13 }) == -1);

    CHECK_TEXT (intervalName ({ 3, 3 }), "m3");
    CHECK_TEXT (intervalName ({ 3, 4 }), "M3");
    CHECK_TEXT (intervalName ({ 5, 7 }), "P5");
    CHECK_TEXT (intervalName ({ 5, 6 }), "d5");
    CHECK_TEXT (intervalName ({ 4, 6 }), "A4");
    CHECK_TEXT (intervalName ({ 7, 9 }), "d7");
    CHECK_TEXT (intervalName ({ 9, 13 }), "m9");
    CHECK_TEXT (intervalName ({ 1, 0 }), "P1");
    CHECK_TEXT (degreeLabel ({ 3, 3 }), "b3");
    CHECK_TEXT (degreeLabel ({ 11, 18 }, pretty), "\xe2\x99\xaf" "11");
    CHECK_TEXT (degreeLabel ({ 7, 9 }), "bb7");
}

void testNoteNames()
{
    std::printf ("Nombres de nota (FL: C5 = 60) y su lectura\n");
    CHECK_TEXT (midiNoteName (60), "C5");
    CHECK_TEXT (midiNoteName (69), "A5");
    CHECK_TEXT (midiNoteName (0), "C0");
    CHECK_TEXT (midiNoteName (127), "G10");
    CHECK_TEXT (midiNoteName (61), "C#5");
    CHECK_TEXT (midiNoteName (61, ascii, true), "Db5");
    CHECK_TEXT (midiNoteName (61, pretty, true), "D\xe2\x99\xad" "5");
    CHECK_TEXT (midiNoteName (60, Notation { false, 4 }), "C4");
    CHECK_TEXT (midiNoteName (0, Notation { false, 4 }), "C-1");

    // La octava sigue a la letra: Si♯ suena como el Do de arriba, Do♭ como el Si de abajo.
    CHECK_TEXT (noteName (72, sharp (notes::B)), "B#5");
    CHECK_TEXT (noteName (71, flat (notes::C)), "Cb6");

    // Según la tonalidad: en Fa mayor la tecla 70 es Si♭; en Mi mayor (fuera de la escala) es el ♯4, La♯.
    CHECK_TEXT (midiNoteName (70, key ("F major")), "Bb5");
    CHECK_TEXT (midiNoteName (70, key ("E major")), "A#5");
    CHECK_TEXT (midiNoteName (61, key ("A minor")), "C#5");
    CHECK_TEXT (midiNoteName (70, key ("A minor")), "Bb5"); // el ♭2 del frigio

    // Ida y vuelta de las 128 notas, en las dos convenciones y con sostenidos y bemoles.
    for (const int middleC : { 5, 4, 3 })
        for (const bool flats : { false, true })
            for (const bool unicode : { false, true })
                for (int n = 0; n < 128; ++n)
                {
                    const Notation notation { unicode, middleC };
                    CHECK (parseMidiNote (midiNoteName (n, notation, flats), notation) == n);
                }

    CHECK (parseNote ("Bb") == flat (notes::B));
    CHECK (parseNote ("F\xe2\x99\xaf") == sharp (notes::F));
    CHECK (parseNote ("Ebb") == (SpelledNote { 2, -2 }));
    CHECK (parseNote ("Fx") == (SpelledNote { 3, 2 }));
    CHECK (parseNote ("bb") == flat (notes::B));
    CHECK (! parseNote ("H").has_value());
    CHECK (! parseNote ("C#5").has_value());
    CHECK (! parseMidiNote ("C11").has_value());
    CHECK (! parseMidiNote ("C").has_value());
}

// --- Escalas y tonalidades -------------------------------------------------------------------------------------------
void testScales()
{
    std::printf ("Escalas, modos y tonalidades\n");
    CHECK_TEXT (scaleText (key ("C major")), "C D E F G A B");
    CHECK_TEXT (scaleText (key ("F major")), "F G A Bb C D E");
    CHECK_TEXT (scaleText (key ("A minor")), "A B C D E F G");
    CHECK_TEXT (scaleText (key ("D dorian")), "D E F G A B C");
    CHECK_TEXT (scaleText (key ("E phrygian")), "E F G A B C D");
    CHECK_TEXT (scaleText (key ("F lydian")), "F G A B C D E");
    CHECK_TEXT (scaleText (key ("G mixolydian")), "G A B C D E F");
    CHECK_TEXT (scaleText (key ("B locrian")), "B C D E F G A");
    CHECK_TEXT (scaleText (key ("A harmonic minor")), "A B C D E F G#");
    CHECK_TEXT (scaleText (key ("A melodic minor")), "A B C D E F# G#");
    CHECK_TEXT (scaleText (key ("E phrygian dominant")), "E F G# A B C D");
    CHECK_TEXT (scaleText (key ("C# minor")), "C# D# E F# G# A B");
    CHECK_TEXT (scaleText (key ("Eb minor")), "Eb F Gb Ab Bb Cb Db");
    CHECK_TEXT (scaleText (key ("G# major")), "G# A# B# C# D# E# F##");

    // En toda tonalidad y todo modo: letras consecutivas y las clases de altura de la tabla.
    for (int pc = 0; pc < 12; ++pc)
        for (int m = 0; m < modeCount; ++m)
        {
            const auto k = standardKey (pc, static_cast<Mode> (m));
            CHECK (k.tonic.pitchClass() == pc);
            for (int d = 0; d < 7; ++d)
            {
                CHECK (k.degree (d).letter == (k.tonic.letter + d) % 7);
                CHECK (k.degree (d).pitchClass() == wrap12 (pc + modeInfo (k.mode).semitones[static_cast<std::size_t> (d)]));
                CHECK (k.degreeOf (k.degree (d).pitchClass()) == d);
            }
            int inScale = 0;
            for (int other = 0; other < 12; ++other)
                inScale += k.contains (other) ? 1 : 0;
            CHECK (inScale == 7);
        }

    // La escritura habitual: la de menos alteraciones; empate, sostenidos.
    const auto tonicName = [] (int pc, Mode mode) { return noteName (standardKey (pc, mode).tonic); };
    CHECK_TEXT (tonicName (1, Mode::ionian), "Db");
    CHECK_TEXT (tonicName (3, Mode::ionian), "Eb");
    CHECK_TEXT (tonicName (6, Mode::ionian), "F#");
    CHECK_TEXT (tonicName (8, Mode::ionian), "Ab");
    CHECK_TEXT (tonicName (10, Mode::ionian), "Bb");
    CHECK_TEXT (tonicName (1, Mode::aeolian), "C#");
    CHECK_TEXT (tonicName (3, Mode::aeolian), "D#");
    CHECK_TEXT (tonicName (8, Mode::aeolian), "G#");
    CHECK_TEXT (tonicName (10, Mode::aeolian), "Bb");
    CHECK_TEXT (tonicName (1, Mode::phrygian), "C#"); // C♯ frigio = escala de La mayor (3 ♯); D♭ frigio tendría 𝄫

    // Notas fuera de la escala: ♭2, ♭3, ♯4, ♭6, ♭7 respecto de la mayor de la tónica.
    const auto c = key ("C major");
    CHECK_TEXT (noteName (spellInKey (1, c)), "Db");
    CHECK_TEXT (noteName (spellInKey (3, c)), "Eb");
    CHECK_TEXT (noteName (spellInKey (6, c)), "F#");
    CHECK_TEXT (noteName (spellInKey (8, c)), "Ab");
    CHECK_TEXT (noteName (spellInKey (10, c)), "Bb");

    // Nombres de tonalidad ida y vuelta.
    CHECK_TEXT (keyName (key ("Am")), "A minor");
    CHECK_TEXT (keyName (key ("Bb")), "Bb major");
    CHECK_TEXT (keyName (key ("f# Dorian")), "F# dorian");
    CHECK_TEXT (keyName (key ("E aeolian")), "E minor");
    CHECK_TEXT (keyName (key ("E  Phrygian   Dominant"), pretty), "E phrygian dominant");
    CHECK (! parseKey ("Cmajor").has_value());
    CHECK (! parseKey ("C blues").has_value());
    for (int pc = 0; pc < 12; ++pc)
        for (int m = 0; m < modeCount; ++m)
            for (const bool unicode : { false, true })
            {
                const auto k = standardKey (pc, static_cast<Mode> (m));
                CHECK (parseKey (keyName (k, Notation { unicode, 5 })) == k);
            }
}

// --- Acordes -----------------------------------------------------------------------------------------------------------
void testChordCatalog()
{
    std::printf ("Catálogo de acordes\n");
    std::set<std::string> suffixes;
    std::set<std::pair<bool, std::string>> romans;
    std::set<std::uint16_t> masks;
    for (int t = 0; t < chordTypeCount; ++t)
    {
        const auto& info = chordTypes[static_cast<std::size_t> (t)];
        CHECK (info.toneCount >= 2 && info.toneCount <= 7);
        CHECK ((info.tones[0] == Interval { 1, 0 }));
        for (int i = 1; i < info.toneCount; ++i)
            CHECK (info.tones[static_cast<std::size_t> (i)].semitones > info.tones[static_cast<std::size_t> (i - 1)].semitones);

        // Ninguna forma de escribir el sufijo se repite entre tipos, ni el sufijo romano (con su mayúscula/minúscula),
        // ni el conjunto de notas sobre Do (si dos tipos sonaran igual, el reconocedor no podría distinguirlos).
        CHECK (suffixes.insert (std::string (info.symbol)).second);
        for (const auto alias : info.aliases)
            if (! alias.empty())
                CHECK (suffixes.insert (normalizeSymbols (alias)).second);
        CHECK (romans.insert ({ info.romanUpper, std::string (info.romanSuffix) }).second);
        CHECK (masks.insert (chordMask (0, static_cast<ChordTypeId> (t))).second);
        CHECK (findChordType (info.symbol) == t);

        // La mayúscula del romano dice la tercera: minúscula = tercera menor.
        bool minorThird = false;
        for (int i = 0; i < info.toneCount; ++i)
            minorThird = minorThird || info.tones[static_cast<std::size_t> (i)] == Interval { 3, 3 };
        CHECK (info.romanUpper == ! minorThird);
    }
    CHECK (findChordType ("m13#11") == -1);
}

void testChordTones()
{
    std::printf ("Tonos de los acordes, bien escritos\n");
    CHECK_TEXT (tonesText (chord ("C")), "C E G");
    CHECK_TEXT (tonesText (chord ("Cm7b5")), "C Eb Gb Bb");
    CHECK_TEXT (tonesText (chord ("F#7")), "F# A# C# E");
    CHECK_TEXT (tonesText (chord ("Bbadd9")), "Bb D F C");
    CHECK_TEXT (tonesText (chord ("Ebmaj7")), "Eb G Bb D");
    CHECK_TEXT (tonesText (chord ("Bdim7")), "B D F Ab");
    CHECK_TEXT (tonesText (chord ("Abm9")), "Ab Cb Eb Gb Bb");
    CHECK_TEXT (tonesText (chord ("C#m11")), "C# E G# B D# F#");
    CHECK_TEXT (tonesText (chord ("E7#9")), "E G# B D F##");
    CHECK_TEXT (tonesText (chord ("Fmaj7#11")), "F A C E B");
    CHECK_TEXT (tonesText (chord ("Dsus2")), "D E A");
    CHECK_TEXT (tonesText (chord ("Gsus4")), "G C D");
    CHECK_TEXT (tonesText (chord ("Am7b9")), "A C E G Bb");
    CHECK_TEXT (tonesText (chord ("Cm7b5"), pretty), "C E\xe2\x99\xad G\xe2\x99\xad B\xe2\x99\xad");
}

void testChordSymbols()
{
    std::printf ("Símbolos de acorde: lectura, alias e ida y vuelta\n");
    CHECK_TEXT (chordSymbol (chord ("Cm7\xe2\x99\xad" "5")), "Cm7b5");
    CHECK_TEXT (chordSymbol (chord ("Cm7b5"), pretty), "Cm7\xe2\x99\xad" "5");
    CHECK_TEXT (chordSymbol (chord ("F/A")), "F/A");
    CHECK_TEXT (chordSymbol (chord ("Bbadd9"), pretty), "B\xe2\x99\xad" "add9");
    CHECK_TEXT (chordSymbol (chord ("C\xce\x94" "7")), "Cmaj7");
    CHECK_TEXT (chordSymbol (chord ("C\xe2\x88\x86")), "Cmaj7");
    CHECK_TEXT (chordSymbol (chord ("C-7")), "Cm7");
    CHECK_TEXT (chordSymbol (chord ("Cmin")), "Cm");
    CHECK_TEXT (chordSymbol (chord ("CM7")), "Cmaj7");
    CHECK_TEXT (chordSymbol (chord ("C\xc3\xb8" "7")), "Cm7b5");
    CHECK_TEXT (chordSymbol (chord ("C\xc3\xb8")), "Cm7b5");
    CHECK_TEXT (chordSymbol (chord ("C\xc2\xb0")), "Cdim");
    CHECK_TEXT (chordSymbol (chord ("C\xc2\xb0" "7")), "Cdim7");
    CHECK_TEXT (chordSymbol (chord ("C+")), "Caug");
    CHECK_TEXT (chordSymbol (chord ("Csus")), "Csus4");
    CHECK_TEXT (chordSymbol (chord ("C6/9")), "C6/9");
    CHECK_TEXT (chordSymbol (chord ("C69/E")), "C6/9/E");
    CHECK_TEXT (chordSymbol (chord ("C6/E")), "C6/E");
    CHECK_TEXT (chordSymbol (chord ("Am(add9)")), "Amadd9");
    CHECK_TEXT (chordSymbol (chord (" Ebm7 ")), "Ebm7");
    CHECK_TEXT (chordSymbol (chord ("C/C")), "C");
    CHECK (! parseChordSymbol ("H7").has_value());
    CHECK (! parseChordSymbol ("cm").has_value());       // la fundamental va en mayúscula ("b" sería ambiguo)
    CHECK (! parseChordSymbol ("Cxyz").has_value());
    CHECK (! parseChordSymbol ("C/H").has_value());
    CHECK (! parseChordSymbol ("C/").has_value());
    CHECK (! parseChordSymbol ("").has_value());

    // Ida y vuelta: todos los tipos, sobre las 21 escrituras de fundamental (7 letras × ♭ natural ♯), en estado
    // fundamental, en cada inversión y con un bajo ajeno, en ASCII y en Unicode.
    int checked = 0;
    for (int t = 0; t < chordTypeCount; ++t)
        for (int letter = 0; letter < 7; ++letter)
            for (int accidental = -1; accidental <= 1; ++accidental)
            {
                const Chord base { { letter, accidental }, static_cast<ChordTypeId> (t) };
                std::vector<Chord> variants { base, base.withBass (transpose (base.root, { 2, 2 })) };
                for (int inv = 1; inv < base.toneCount(); ++inv)
                    variants.push_back (base.withInversion (inv));
                for (const auto& c : variants)
                    for (const bool unicode : { false, true })
                    {
                        const auto text = chordSymbol (c, Notation { unicode, 5 });
                        const auto back = parseChordSymbol (text);
                        CHECK (back.has_value() && *back == c);
                        if (! back || ! (*back == c))
                            std::printf ("    ida y vuelta: \"%s\"\n", text.c_str());
                        ++checked;
                    }
            }
    CHECK (checked > 3000);
}

void testInversionsAndVoicing()
{
    std::printf ("Inversiones y notas MIDI\n");
    const auto c = chord ("C");
    CHECK ((midi (c, 60) == std::vector<int> { 60, 64, 67 }));
    CHECK ((midi (c.withInversion (1), 60) == std::vector<int> { 64, 67, 72 }));
    CHECK ((midi (c.withInversion (2), 60) == std::vector<int> { 67, 72, 76 }));
    CHECK_TEXT (chordSymbol (c.withInversion (1)), "C/E");
    CHECK_TEXT (chordSymbol (c.withInversion (2)), "C/G");
    CHECK (c.withInversion (1).inversion() == 1);
    CHECK (c.withInversion (0).inversion() == 0);
    CHECK (c.withInversion (5).inversion() == 0); // fuera de rango = estado fundamental

    // 3.ª inversión de una séptima: la 7.ª abajo (el bajo que baja por grados en "Cmaj7/B").
    const auto cmaj7 = chord ("Cmaj7/B");
    CHECK (cmaj7.inversion() == 3);
    CHECK ((midi (cmaj7, 60) == std::vector<int> { 71, 72, 76, 79 }));

    // Novena en 1.ª inversión: los tonos por debajo del bajo suben una octava y la 9.ª sigue siendo 9.ª.
    CHECK ((midi (chord ("C9/E"), 60) == std::vector<int> { 64, 67, 70, 72, 74 }));

    // Bajo ajeno (pedal): va debajo de todo.
    const auto pedal = chord ("F/G");
    CHECK (pedal.inversion() == -1);
    CHECK ((midi (pedal, 65) == std::vector<int> { 55, 65, 69, 72 }));
    CHECK ((midi (chord ("C/D"), 60) == std::vector<int> { 50, 60, 64, 67 }));
    CHECK ((midi (chord ("C/C#"), 60) == std::vector<int> { 49, 60, 64, 67 }));

    // Lo que se sale de 0..127 se omite (no se dobla ni se envuelve).
    CHECK ((midi (c, 120) == std::vector<int> { 120, 124, 127 }));
    CHECK ((midi (chord ("C13"), 120) == std::vector<int> { 120, 124, 127 }));
    CHECK ((midi (chord ("C/B"), 0) == std::vector<int> { 0, 4, 7 })); // el bajo ajeno caería en −1: se omite
}

// --- Armonía ---------------------------------------------------------------------------------------------------------
void testDiatonicChords()
{
    std::printf ("Acordes de cada modo (grados en romanos)\n");
    CHECK_TEXT (diatonicText (key ("C major"), ChordSize::triad), "I ii iii IV V vi vii°");
    CHECK_TEXT (diatonicText (key ("A minor"), ChordSize::triad), "i ii° ♭III iv v ♭VI ♭VII");
    CHECK_TEXT (diatonicText (key ("E phrygian"), ChordSize::triad), "i ♭II ♭III iv v° ♭VI ♭vii");
    CHECK_TEXT (diatonicText (key ("D dorian"), ChordSize::triad), "i ii ♭III IV v vi° ♭VII");
    CHECK_TEXT (diatonicText (key ("F lydian"), ChordSize::triad), "I II iii ♯iv° V vi vii");
    CHECK_TEXT (diatonicText (key ("G mixolydian"), ChordSize::triad), "I ii iii° IV v vi ♭VII");
    CHECK_TEXT (diatonicText (key ("B locrian"), ChordSize::triad), "i° ♭II ♭iii iv ♭V ♭VI ♭vii");
    CHECK_TEXT (diatonicText (key ("A harmonic minor"), ChordSize::triad), "i ii° ♭III+ iv V ♭VI vii°");
    CHECK_TEXT (diatonicText (key ("A melodic minor"), ChordSize::triad), "i ii ♭III+ IV V vi° vii°");
    CHECK_TEXT (diatonicText (key ("E phrygian dominant"), ChordSize::triad), "I ♭II iii° iv v° ♭VI+ ♭vii");

    CHECK_TEXT (diatonicText (key ("C major"), ChordSize::seventh), "Imaj7 ii7 iii7 IVmaj7 V7 vi7 viiø7");
    CHECK_TEXT (diatonicText (key ("A minor"), ChordSize::seventh), "i7 iiø7 ♭IIImaj7 iv7 v7 ♭VImaj7 ♭VII7");
    CHECK_TEXT (diatonicText (key ("A harmonic minor"), ChordSize::seventh), "imaj7 iiø7 ♭III+maj7 iv7 V7 ♭VImaj7 vii°7");

    // Todas las escalas dan tríadas y séptimas del catálogo en sus 7 grados (desde las 12 tónicas).
    for (int pc = 0; pc < 12; ++pc)
        for (int m = 0; m < modeCount; ++m)
            for (int d = 0; d < 7; ++d)
                for (const auto size : { ChordSize::triad, ChordSize::seventh })
                {
                    const auto k = standardKey (pc, static_cast<Mode> (m));
                    const auto c = diatonicChord (k, d, size);
                    CHECK (c.has_value());
                    if (c)
                        CHECK (isDiatonic (*c, k));
                }

    // Con nombres: los acordes de La menor.
    std::string names;
    for (int d = 0; d < 7; ++d)
        names += (d == 0 ? "" : " ") + chordSymbol (*diatonicChord (key ("A minor"), d));
    CHECK_TEXT (names, "Am Bdim C Dm Em F G");
}

void testRomanNumerals()
{
    std::printf ("Números romanos y progresiones\n");
    // Las del contexto de Tidechart.
    CHECK_TEXT (realize ("i–♭VI–♭III–♭VII", key ("A minor")), "Am F C G");
    CHECK_TEXT (realize ("i–♭VI–♭III–♭VII", Key { flat (notes::E), Mode::aeolian }), "Ebm Cb Gb Db");
    CHECK_TEXT (realize ("i bVII bVI V", key ("A minor")), "Am G F E");          // cadencia andaluza
    CHECK_TEXT (realize ("i-bII", key ("E phrygian")), "Em F");                   // el ♭II frigio
    CHECK_TEXT (realize ("I V vi IV", key ("C major")), "C G Am F");              // el pop de referencia
    CHECK_TEXT (realize ("I-bVI-bVII-I", key ("C major")), "C Ab Bb C");          // préstamo modal (el "Mario cadence")
    CHECK_TEXT (realize ("ii7 | V7 | Imaj7", key ("Bb major")), "Cm7 F7 Bbmaj7");
    CHECK_TEXT (realize ("i7, iv9, bVImaj7, V7b9", key ("C# minor")), "C#m7 F#m9 Amaj7 G#7b9");
    CHECK_TEXT (realize ("viiø7 vii°7 iiø iim7 viidim", key ("C major")), "Bm7b5 Bdim7 Dm7b5 Dm7 Bdim");

    // Escribir.
    const auto parsed = parseProgression ("i - bVI - bIII - bVII");
    CHECK (parsed.has_value());
    if (parsed)
    {
        CHECK_TEXT (progressionText (*parsed, pretty), "i\xe2\x80\x93\xe2\x99\xadVI\xe2\x80\x93\xe2\x99\xadIII\xe2\x80\x93\xe2\x99\xadVII");
        CHECK_TEXT (progressionText (*parsed), "i-bVI-bIII-bVII");
    }
    CHECK (! parseProgression ("").has_value());
    CHECK (! parseProgression ("I V x IV").has_value());
    CHECK (! parseRomanNumeral ("IIII").has_value());
    CHECK (! parseRomanNumeral ("Iv").has_value());
    CHECK (! parseRomanNumeral ("vmaj9x").has_value());

    // Ida y vuelta: cada grado, alteración y tipo, en texto y en una tonalidad.
    const std::array<Key, 4> keys { key ("C major"), key ("A minor"), key ("Eb dorian"), key ("F# phrygian") };
    for (int degree = 1; degree <= 7; ++degree)
        for (int accidental = -1; accidental <= 1; ++accidental)
            for (int t = 0; t < chordTypeCount; ++t)
            {
                const RomanNumeral numeral { degree, accidental, static_cast<ChordTypeId> (t) };
                for (const bool unicode : { false, true })
                {
                    const auto text = romanNumeralText (numeral, Notation { unicode, 5 });
                    const auto back = parseRomanNumeral (text);
                    CHECK (back == numeral);
                    if (back != numeral)
                        std::printf ("    ida y vuelta: \"%s\"\n", text.c_str());
                }
                for (const auto& k : keys)
                    CHECK (romanNumeralFor (chordFor (numeral, k), k) == numeral);
            }
}

void testHarmonicFunctions()
{
    std::printf ("Funciones armónicas y préstamo modal\n");
    const auto function = [] (const char* numeral) { return harmonicFunction (*parseRomanNumeral (numeral)); };
    CHECK (function ("I") == HarmonicFunction::tonic);
    CHECK (function ("ii") == HarmonicFunction::subdominant);
    CHECK (function ("iii") == HarmonicFunction::tonic);
    CHECK (function ("IV") == HarmonicFunction::subdominant);
    CHECK (function ("V7") == HarmonicFunction::dominant);
    CHECK (function ("vi") == HarmonicFunction::tonic);
    CHECK (function ("vii°") == HarmonicFunction::dominant);
    CHECK (function ("i") == HarmonicFunction::tonic);
    CHECK (function ("bIII") == HarmonicFunction::tonic);
    CHECK (function ("iv") == HarmonicFunction::subdominant);
    CHECK (function ("bVI") == HarmonicFunction::subdominant);
    CHECK (function ("bVII") == HarmonicFunction::dominant);
    CHECK (function ("bII") == HarmonicFunction::subdominant);

    // Préstamo modal en Do mayor: de qué modos de Do viene cada acorde.
    const auto from = [] (const char* symbol) {
        std::string text;
        for (const auto mode : modesContaining (*parseChordSymbol (symbol), notes::C))
            text += (text.empty() ? "" : ", ") + std::string (modeName (mode));
        return text;
    };
    CHECK_TEXT (from ("Bb"), "dorian, mixolydian, minor");                 // ♭VII
    CHECK_TEXT (from ("Ab"), "phrygian, minor, locrian, harmonic minor");  // ♭VI
    CHECK_TEXT (from ("Fm"), "phrygian, minor, locrian, harmonic minor");  // iv
    CHECK_TEXT (from ("Db"), "phrygian, locrian");                         // ♭II napolitano
    CHECK_TEXT (from ("G"), "major, lydian, harmonic minor, melodic minor");
    CHECK_TEXT (from ("C#"), "phrygian, locrian");                         // se compara por teclas: C♯ = D♭
    CHECK_TEXT (from ("E"), "");                                           // ningún modo de Do tiene Mi y Sol♯ a la vez

    CHECK (isDiatonic (chord ("Csus2"), key ("C major")));
    CHECK (isDiatonic (chord ("Dm7"), key ("C major")));
    CHECK (isDiatonic (chord ("G/F"), key ("C major")));
    CHECK (! isDiatonic (chord ("G/F#"), key ("C major")));
    CHECK (! isDiatonic (chord ("E"), key ("A minor")));
    CHECK (isDiatonic (chord ("E"), key ("A harmonic minor")));
}

// --- Reconocer acordes ------------------------------------------------------------------------------------------------
void testChordFinder()
{
    std::printf ("Reconocer acordes por sus notas\n");
    CHECK_TEXT (bestChord ({ 60, 64, 67 }), "C");
    CHECK_TEXT (bestChord ({ 64, 67, 72 }), "C/E");
    CHECK_TEXT (bestChord ({ 55, 60, 64 }), "C/G");
    CHECK_TEXT (bestChord ({ 48, 64, 67, 72, 76 }), "C");          // octavas y duplicaciones no cambian nada
    CHECK_TEXT (bestChord ({ 60, 64, 67, 69 }), "C6");             // con Do abajo...
    CHECK_TEXT (bestChord ({ 57, 60, 64, 67 }), "Am7");            // ...y con La abajo, las mismas notas son Am7
    CHECK_TEXT (bestChord ({ 60, 64, 70 }), "C7");                 // sin 5.ª
    CHECK_TEXT (bestChord ({ 60, 67 }), "C5");
    CHECK_TEXT (bestChord ({ 60, 64 }), "(nada)");                 // dos notas: un intervalo, no un acorde
    CHECK_TEXT (bestChord ({ 60 }), "(nada)");
    CHECK_TEXT (bestChord ({}), "(nada)");
    CHECK_TEXT (bestChord ({ 65, 69, 72, 75 }), "F7");
    CHECK_TEXT (bestChord ({ 60, 63, 66, 70 }), "Cm7b5");
    CHECK_TEXT (bestChord ({ 62, 67, 69 }), "Dsus4");
    CHECK_TEXT (bestChord ({ 57, 60, 64, 71 }), "Amadd9");
    CHECK_TEXT (bestChord ({ 52, 59, 62, 65, 67 }), "Em7b9");      // el acorde frigio
    CHECK_TEXT (bestChord ({ 60, 64, 67, 71, 74 }), "Cmaj9");
    // F/G (F con Sol en el bajo): la lectura exacta es Fadd9 con su 9.ª abajo; G9sus4 sin 5.ª va después.
    CHECK_TEXT (bestChord ({ 43, 65, 69, 72 }), "Fadd9/G");
    // Escritura sin tonalidad: la de menos alteraciones.
    CHECK_TEXT (bestChord ({ 63, 67, 70 }), "Eb");
    CHECK_TEXT (bestChord ({ 61, 64, 68 }), "C#m");
    CHECK_TEXT (bestChord ({ 58, 61, 65 }), "Bbm");
    CHECK_TEXT (bestChord ({ 68, 72, 75 }), "Ab");
    CHECK_TEXT (bestChord ({ 66, 70, 73 }), "F#");

    // Con tonalidad: como se escribe en ella.
    const std::vector<int> gFlat { 66, 70, 73 };
    const auto inDb = findChords (gFlat, key ("Db major"));
    CHECK (! inDb.empty() && chordSymbol (inDb.front().chord) == "Gb");
    const std::vector<int> aSharpMinor { 58, 61, 65 };
    const auto inDSharp = findChords (aSharpMinor, key ("D# minor"));
    CHECK (! inDSharp.empty() && chordSymbol (inDSharp.front().chord) == "A#m");

    // Sin 5.ª se marca, y la exacta va antes.
    const std::vector<int> c7NoFifth { 60, 64, 70 };
    const auto readings = findChords (c7NoFifth);
    CHECK (! readings.empty() && readings.front().omitsFifth);

    // Todo tipo del catálogo, sobre las 12 fundamentales y en sus inversiones, se reconoce a sí mismo como 1.ª lectura
    // (salvo los que suenan como otro tipo más común con la misma nota en el bajo: lo comprueba que la 1.ª lectura
    // tenga las mismas clases de altura y el mismo bajo).
    for (int t = 0; t < chordTypeCount; ++t)
        for (int root = 0; root < 12; ++root)
        {
            const Chord base { spellPitchClass (root), static_cast<ChordTypeId> (t) };
            for (int inv = 0; inv < base.toneCount(); ++inv)
            {
                const auto c = base.withInversion (inv);
                const auto notes = c.midiNotes (48 + root);
                const std::vector<int> list (notes.begin(), notes.end());
                const auto found = findChords (list);
                CHECK (! found.empty());
                if (found.empty())
                    continue;
                const auto& best = found.front().chord;
                CHECK (! found.front().omitsFifth);
                CHECK (chordMask (best.root.pitchClass(), best.type) == chordMask (root, c.type));
                CHECK (best.bassNote().pitchClass() == c.bassNote().pitchClass());
                if (inv == 0)
                    CHECK (best.type == c.type && best.root.pitchClass() == root);
            }
        }
}
// --- Fase 4: voicings, ritmos, progresiones y préstamo modal ---------------------------------------------------------

std::vector<int> notesOf (const Voicing& v)
{
    return { v.begin(), v.end() };
}

Progression progressionOf (const char* text, std::vector<int> beats = {})
{
    Progression p;
    const auto numerals = parseProgression (text);
    CHECK (numerals.has_value());
    if (numerals)
        for (std::size_t i = 0; i < numerals->size(); ++i)
            p.steps.push_back ({ (*numerals)[i], i < beats.size() ? beats[i] : 4 });
    return p;
}

void testVoiceLeading()
{
    std::printf ("Conduccion de voces: I-IV-V-I con las inversiones de libro, notas comunes quietas, drop 2, piso del bajo\n");

    const Chord cMajor = chord ("C"), fMajor = chord ("F"), gMajor = chord ("G");
    constexpr int centre = 2 * 60 + 7; // la tríada de Do desde C5
    CHECK (notesOf (voiceChord (cMajor, 60, VoicingStyle::rootPosition, centre)) == (std::vector<int> { 60, 64, 67 }));

    // El primero (sin anterior) queda en el centro: estado fundamental. F, con el Do quieto: C F A (2.ª inversión);
    // G: B D G (1.ª); y de vuelta a C E G. Cada voz se mueve 2 semitonos como mucho.
    const auto i = voiceChord (cMajor, 60, VoicingStyle::voiceLed, centre);
    const auto iv = voiceChord (fMajor, 65, VoicingStyle::voiceLed, centre, &i);
    const auto v = voiceChord (gMajor, 67, VoicingStyle::voiceLed, centre, &iv);
    const auto back = voiceChord (cMajor, 60, VoicingStyle::voiceLed, centre, &v);
    CHECK (notesOf (i) == (std::vector<int> { 60, 64, 67 }));
    CHECK (notesOf (iv) == (std::vector<int> { 60, 65, 69 }));
    CHECK (notesOf (v) == (std::vector<int> { 59, 62, 67 }));
    CHECK (notesOf (back) == (std::vector<int> { 60, 64, 67 }));
    CHECK (voiceMovement (i, iv) == 3 && voiceMovement (iv, v) == 6 && voiceMovement (v, back) == 3);
    // En estado fundamental, las mismas voces recorren 15 y 6.
    CHECK (voiceMovement (i, fMajor.midiNotes (65)) == 15);
    CHECK (voiceMovement (fMajor.midiNotes (65), gMajor.midiNotes (67)) == 6);

    // Am → F: comparten A y C, que no se mueven; solo E sube a F.
    const auto am = voiceChord (chord ("Am"), 57, VoicingStyle::voiceLed, 2 * 57 + 7);
    CHECK (notesOf (voiceChord (fMajor, 65, VoicingStyle::voiceLed, 2 * 57 + 7, &am)) == (std::vector<int> { 57, 60, 65 }));

    // Distinto número de notas: cada nota con la más cercana del otro acorde.
    Voicing triad, seventh;
    for (const int n : { 60, 64, 67 })
        triad.push_back (n);
    for (const int n : { 60, 64, 67, 70 })
        seventh.push_back (n);
    CHECK (voiceMovement (triad, seventh) == 3);

    // Drop 2 y el estilo abierto: más de una octava entre las voces de fuera.
    CHECK (notesOf (dropTwo (triad)) == (std::vector<int> { 52, 60, 67 }));
    const auto open = voiceChord (cMajor, 60, VoicingStyle::open, centre);
    CHECK (open.size() == 3 && open[2] - open[0] > 12);

    // El piso: con bajo en C4 (48), ninguna voz baja hasta él.
    const auto aboveBass = voiceChord (cMajor, 60, VoicingStyle::voiceLed, 2 * 50 + 7, nullptr, 48);
    CHECK (! aboveBass.empty() && aboveBass[0] > 48);

    // Todas las tonalidades, modos y grados (tríadas y séptimas), en los tres estilos y encadenados: las notas son las
    // del acorde (las mismas clases de altura, todas), de grave a agudo, y el centro no se aleja más de la ventana.
    bool ok = true;
    for (int tonic = 0; tonic < 12; ++tonic)
        for (int m = 0; m < modeCount; ++m)
            for (const auto size : { ChordSize::triad, ChordSize::seventh })
                for (int style = 0; style < voicingStyleCount; ++style)
                {
                    const Key k = standardKey (tonic, static_cast<Mode> (m));
                    const int tonicNote = tonicNoteNear (k, 60);
                    Voicing previous;
                    for (int d = 0; d < 7; ++d)
                    {
                        const auto c = diatonicChord (k, d, size);
                        const int root = tonicNote + modeInfo (k.mode).semitones[static_cast<std::size_t> (d)];
                        const auto voiced = voiceChord (*c, root, static_cast<VoicingStyle> (style), tonicCentre2 (tonicNote),
                                                        previous.empty() ? nullptr : &previous);
                        std::uint16_t mask = 0;
                        for (std::size_t n = 0; n < voiced.size(); ++n)
                        {
                            mask = static_cast<std::uint16_t> (mask | (1u << (voiced[n] % 12)));
                            ok = ok && (n == 0 || voiced[n - 1] < voiced[n]);
                        }
                        ok = ok && mask == chordMask (c->root.pitchClass(), c->type) && static_cast<int> (voiced.size()) == c->toneCount();
                        if (style != 0)
                        {
                            const int distance = voiced[0] + voiced[voiced.size() - 1] - tonicCentre2 (tonicNote);
                            ok = ok && distance <= 2 * voicingWindow && distance >= -2 * voicingWindow;
                        }
                        previous = voiced;
                    }
                }
    CHECK (ok);
}

void testRhythmPatterns()
{
    std::printf ("Patrones ritmicos: golpes dentro del patron y sin solaparse, el dembow 3+3+2 y sus variantes\n");

    CHECK (rhythmPattern (0).steps == 0 && rhythmPattern (0).name == "sustain");
    CHECK (&rhythmPattern (99) == &rhythmPattern (0));
    std::set<std::string> names;
    for (int i = 0; i < rhythmPatternCount; ++i)
    {
        const auto& pattern = rhythmPattern (i);
        names.insert (std::string (pattern.name));
        int end = 0;
        for (int h = 0; h < pattern.hitCount; ++h)
        {
            const auto& hit = pattern.hits[static_cast<std::size_t> (h)];
            CHECK (hit.start >= end && hit.length >= 1 && hit.start + hit.length <= pattern.steps);
            CHECK (hit.accent >= 1 && hit.accent <= 127);
            end = hit.start + hit.length;
            CHECK (hitAt (pattern, hit.start) == &hit);
        }
    }
    CHECK (static_cast<int> (names.size()) == rhythmPatternCount);

    const auto starts = [] (const char* name) {
        std::vector<int> result;
        for (const auto& pattern : rhythmPatterns)
            if (pattern.name == name)
                for (int h = 0; h < pattern.hitCount; ++h)
                    result.push_back (pattern.hits[static_cast<std::size_t> (h)].start);
        return result;
    };
    // 3+3+2 dos veces por compás; en corcheas, una vez.
    CHECK (starts ("dembow") == (std::vector<int> { 0, 3, 6, 8, 11, 14 }));
    CHECK (starts ("dembow long") == (std::vector<int> { 0, 6, 12 }));
    CHECK (starts ("dembow offbeat") == (std::vector<int> { 3, 6, 11, 14 }));
    CHECK (starts ("dembow shifted") == (std::vector<int> { 0, 2, 5, 8, 10, 13 }));

    const auto& dembow = rhythmPattern (4);
    CHECK (dembow.name == "dembow");
    CHECK (hitAt (dembow, 1) == &dembow.hits[0]);
    CHECK (hitAt (dembow, 2) == nullptr);
    CHECK (patternStep (dembow, 35) == 3);
    CHECK (patternStep (dembow, -1) == 15);
}

void testProgressions()
{
    std::printf ("Progresiones: pasos en el tiempo, la vuelta, voicing de toda la progresion y sus notas en el tiempo\n");

    const auto reggaeton = progressionOf ("i bVI bIII bVII");
    CHECK (reggaeton.totalBeats() == 16);
    CHECK (reggaeton.stepAt (0) == 0 && reggaeton.stepAt (15) == 0 && reggaeton.stepAt (16) == 1 && reggaeton.stepAt (63) == 3);
    CHECK (reggaeton.stepAt (64) == 0 && reggaeton.stepAt (-1) == 3);
    const auto uneven = progressionOf ("i bII i bVII", { 4, 2, 2, 8 });
    CHECK (uneven.totalBeats() == 16 && uneven.stepStart (2) == 24 && uneven.stepAt (23) == 1 && uneven.stepAt (24) == 2);
    CHECK (Progression {}.stepAt (5) == -1 && Progression {}.totalBeats() == 0);
    CHECK (Progression::clampBeats (0) == 1 && Progression::clampBeats (99) == maxStepBeats);

    const Key aMinor = key ("A minor");
    CHECK (tonicNoteNear (aMinor, 60) == 57);
    CHECK (tonicNoteNear (standardKey (6, Mode::ionian), 60) == 54);
    CHECK (rootNoteFor ({ 6, -1, 0 }, 57) == 65); // ♭VI desde A4 = F5
    CHECK (rootNoteFor ({ 1, 0, 0 }, 57) == 57);

    // La del reggaetón en La menor con conducción de voces: Am, F con A y C quietas, C/G, G; la vuelta G → Am también es
    // corta. En estado fundamental, cada cambio salta.
    const auto voiced = voiceProgression (reggaeton, aMinor, VoicingStyle::voiceLed, 57, false);
    CHECK (notesOf (voiced[0].upper) == (std::vector<int> { 57, 60, 64 }));
    CHECK (notesOf (voiced[1].upper) == (std::vector<int> { 57, 60, 65 }));
    CHECK (notesOf (voiced[2].upper) == (std::vector<int> { 55, 60, 64 }));
    CHECK (notesOf (voiced[3].upper) == (std::vector<int> { 55, 59, 62 }));
    CHECK (chordSymbol (voiced[1].chord) == "F" && voiced[1].rootNote == 65);
    const auto plain = voiceProgression (reggaeton, aMinor, VoicingStyle::rootPosition, 57, true);
    CHECK (notesOf (plain[1].upper) == (std::vector<int> { 65, 69, 72 }) && plain[1].bassNote == 53);
    const auto withBass = plain[1].notes();
    CHECK ((std::vector<int> (withBass.begin(), withBass.end()) == std::vector<int> { 53, 65, 69, 72 }));
    int ledMovement = 0, rootMovement = 0;
    for (std::size_t s = 0; s < 4; ++s)
    {
        ledMovement += voiceMovement (voiced[s].upper, voiced[(s + 1) % 4].upper);
        rootMovement += voiceMovement (plain[s].upper, plain[(s + 1) % 4].upper);
    }
    CHECK (ledMovement == 12 && rootMovement > 2 * ledMovement);
    // Con bajo, las voces nunca bajan hasta él.
    const auto ledBass = voiceProgression (reggaeton, aMinor, VoicingStyle::voiceLed, 57, true);
    for (std::size_t s = 0; s < 4; ++s)
        CHECK (ledBass[s].bassNote >= 0 && ledBass[s].upper[0] > ledBass[s].bassNote);

    // Las notas en el tiempo. Sostenido: un acorde por paso.
    RenderOptions options;
    auto events = renderProgression (reggaeton, aMinor, options);
    CHECK (events.size() == 12);
    CHECK (events.size() == 12 && events[0] == (NoteEvent { 57, 0.0, 4.0, 100 }) && events[3] == (NoteEvent { 65, 4.0, 4.0, 100 }));
    // Dembow: 6 golpes de media negra por compás, con sus acentos.
    options.rhythm = 4;
    options.velocity = 110;
    events = renderProgression (reggaeton, aMinor, options);
    CHECK (events.size() == 4 * 6 * 3);
    std::vector<double> firstBar;
    for (const auto& e : events)
        if (e.note == 57 && e.start < 4.0)
        {
            firstBar.push_back (e.start);
            CHECK (e.length == 0.5);
        }
    CHECK ((firstBar == std::vector<double> { 0.0, 0.75, 1.5, 2.0, 2.75, 3.5 }));
    CHECK (events.size() > 3 && events[0].velocity == 110 && events[3].velocity == accentedVelocity (110, 85));
    // Un cambio de paso dentro de un golpe ataca el acorde nuevo ahí mismo: i (1 tiempo) y ♭VI (3), dembow largo (golpe
    // de 5 semicorcheas en 0): Am de 0 a 1, F de 1 a 1.25.
    options.rhythm = 5;
    options.velocity = 100;
    events = renderProgression (progressionOf ("i bVI", { 1, 3 }), aMinor, options);
    CHECK (events.size() >= 6 && events[0] == (NoteEvent { 57, 0.0, 1.0, 100 }) && events[3] == (NoteEvent { 65, 1.0, 0.25, 100 }));
    // Dos vueltas = el doble; vacía = nada.
    options = {};
    options.loops = 2;
    CHECK (renderProgression (reggaeton, aMinor, options).size() == 24);
    CHECK (renderProgression ({}, aMinor, options).empty());
}

void testBorrowedChords()
{
    std::printf ("Prestamo modal: los acordes de los modos paralelos que no estan en la tonalidad, en orden de uso\n");

    const auto symbols = [] (const Key& k, int count) {
        std::string text;
        const auto borrowed = borrowedChords (k);
        for (int i = 0; i < count && i < static_cast<int> (borrowed.size()); ++i)
            text += (text.empty() ? "" : " ") + chordSymbol (borrowed[static_cast<std::size_t> (i)].chord) + "="
                    + romanNumeralText (borrowed[static_cast<std::size_t> (i)].numeral);
        return text;
    };
    // Do mayor: lo del menor paralelo primero (i, ♭III, iv, v, ♭VI, ♭VII).
    CHECK_TEXT (symbols (key ("C major"), 6), "Cm=i Eb=bIII Fm=iv Gm=v Ab=bVI Bb=bVII");
    CHECK (borrowedChords (key ("C major"))[0].source == Mode::aeolian);
    // La menor: ii y IV del dórico, ♭II y ♭vii del frigio, el V de la armónica.
    CHECK_TEXT (symbols (key ("A minor"), 5), "Bm=ii D=IV Bb=bII Gm=bvii E=V");
    CHECK (borrowedChords (key ("A minor"))[2].source == Mode::phrygian);

    // En todas las tonalidades y modos: ninguno es de la tonalidad, sin repetidos, cada uno es de su modo de origen y su
    // número romano lleva de vuelta al acorde.
    bool ok = true;
    for (int tonic = 0; tonic < 12; ++tonic)
        for (int m = 0; m < modeCount; ++m)
            for (const auto size : { ChordSize::triad, ChordSize::seventh })
            {
                const Key k = standardKey (tonic, static_cast<Mode> (m));
                const auto borrowed = borrowedChords (k, size);
                ok = ok && ! borrowed.empty();
                for (std::size_t i = 0; i < borrowed.size(); ++i)
                {
                    const auto& b = borrowed[i];
                    ok = ok && ! isDiatonic (b.chord, k) && isDiatonic (b.chord, Key { k.tonic, b.source }) && b.source != k.mode;
                    ok = ok && chordFor (b.numeral, k).root.pitchClass() == b.chord.root.pitchClass() && chordFor (b.numeral, k).type == b.chord.type;
                    for (std::size_t j = 0; j < i; ++j)
                        ok = ok && ! (borrowed[j].chord.root.pitchClass() == b.chord.root.pitchClass() && borrowed[j].chord.type == b.chord.type);
                }
            }
    CHECK (ok);
}
} // namespace

int main()
{
    testSpelledNotes();
    testNoteNames();
    testScales();
    testChordCatalog();
    testChordTones();
    testChordSymbols();
    testInversionsAndVoicing();
    testDiatonicChords();
    testRomanNumerals();
    testHarmonicFunctions();
    testChordFinder();
    testVoiceLeading();
    testRhythmPatterns();
    testProgressions();
    testBorrowedChords();

    if (failures == 0)
        std::printf ("\nTodos los tests de teoria pasaron.\n");
    else
        std::printf ("\n%d fallos.\n", failures);
    return failures == 0 ? 0 : 1;
}
