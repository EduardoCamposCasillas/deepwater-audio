#include "deepwater/theory/Text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <utility>

namespace deepwater::theory
{
namespace
{
constexpr std::array<char, 7> letterNames { 'C', 'D', 'E', 'F', 'G', 'A', 'B' };
constexpr std::array<std::string_view, 7> upperNumerals { "I", "II", "III", "IV", "V", "VI", "VII" };

std::string_view trim (std::string_view text)
{
    while (! text.empty() && std::isspace (static_cast<unsigned char> (text.front())))
        text.remove_prefix (1);
    while (! text.empty() && std::isspace (static_cast<unsigned char> (text.back())))
        text.remove_suffix (1);
    return text;
}

std::string lowercase (std::string_view text)
{
    std::string result (text);
    for (auto& c : result)
        c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return result;
}

// Lo canónico en ASCII → lo que se muestra: b y # delante de un número pasan a ♭ y ♯ (en "m7b5", "7#9"), y en los
// romanos o → ° y h → ø.
std::string decorate (std::string_view text, const Notation& notation, bool roman)
{
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        const bool beforeDigit = i + 1 < text.size() && std::isdigit (static_cast<unsigned char> (text[i + 1]));
        if (notation.unicode && c == 'b' && beforeDigit)
            result += "\xe2\x99\xad";
        else if (notation.unicode && c == '#')
            result += "\xe2\x99\xaf";
        else if (roman && c == 'o')
            result += "\xc2\xb0";
        else if (roman && c == 'h')
            result += "\xc3\xb8";
        else
            result += c;
    }
    return result;
}

// Lee la letra y las alteraciones del principio de 'text' (ya normalizado). 'consumed' = cuántos caracteres usó.
std::optional<SpelledNote> parseNotePrefix (std::string_view text, bool allowLowercase, std::size_t& consumed)
{
    if (text.empty())
        return std::nullopt;
    const char first = allowLowercase ? static_cast<char> (std::toupper (static_cast<unsigned char> (text[0]))) : text[0];
    const auto letter = std::find (letterNames.begin(), letterNames.end(), first);
    if (letter == letterNames.end())
        return std::nullopt;

    SpelledNote note { static_cast<int> (letter - letterNames.begin()), 0 };
    std::size_t i = 1;
    for (; i < text.size(); ++i)
    {
        if (text[i] == '#')
            ++note.accidental;
        else if (text[i] == 'b')
            --note.accidental;
        else if (text[i] == 'x')
            note.accidental += 2;
        else
            break;
    }
    if (note.accidental < -3 || note.accidental > 3) // hasta triples: B♯aug tiene, en teoría, Fa♯♯♯
        return std::nullopt;
    consumed = i;
    return note;
}

int floorDiv12 (int value)
{
    return value >= 0 ? value / 12 : -((-value + 11) / 12);
}

// Todas las formas de escribir el sufijo de cada tipo, de la más larga a la más corta: al leer "Cmaj7" hay que probar
// "maj7" antes que "m" (si no, sobraría "aj7").
const std::vector<std::pair<std::string, ChordTypeId>>& suffixTable()
{
    static const auto table = [] {
        std::vector<std::pair<std::string, ChordTypeId>> result;
        for (int t = 0; t < chordTypeCount; ++t)
        {
            const auto& info = chordTypes[static_cast<std::size_t> (t)];
            result.emplace_back (std::string (info.symbol), static_cast<ChordTypeId> (t));
            for (const auto alias : info.aliases)
                if (! alias.empty())
                    result.emplace_back (std::string (alias), static_cast<ChordTypeId> (t));
        }
        std::stable_sort (result.begin(), result.end(), [] (const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
        return result;
    }();
    return table;
}
} // namespace

std::string normalizeSymbols (std::string_view text)
{
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 12> replacements { {
        { "\xe2\x99\xad", "b" },          // ♭
        { "\xe2\x99\xaf", "#" },          // ♯
        { "\xe2\x99\xae", "" },           // ♮
        { "\xf0\x9d\x84\xaa", "x" },      // 𝄪
        { "\xf0\x9d\x84\xab", "bb" },     // 𝄫
        { "\xc2\xb0", "o" },              // °
        { "\xc2\xba", "o" },              // º (el ordinal, que se teclea en vez de °)
        { "\xc3\xb8", "h" },              // ø
        { "\xc3\x98", "h" },              // Ø
        { "\xce\x94", "^" },              // Δ
        { "\xe2\x88\x86", "^" },          // ∆
        { "\xe2\x80\x93", "-" },          // –
    } };

    std::string result;
    for (std::size_t i = 0; i < text.size();)
    {
        bool replaced = false;
        for (const auto& [from, to] : replacements)
            if (text.substr (i, from.size()) == from)
            {
                result += to;
                i += from.size();
                replaced = true;
                break;
            }
        if (text.substr (i, 3) == "\xe2\x80\x94") // —
        {
            result += '-';
            i += 3;
            replaced = true;
        }
        if (! replaced)
            result += text[i++];
    }
    return result;
}

// --- Notas -----------------------------------------------------------------------------------------------------------
std::string accidentalText (int accidental, const Notation& notation)
{
    std::string result;
    for (int i = 0; i < accidental; ++i)
        result += notation.unicode ? "\xe2\x99\xaf" : "#";
    for (int i = 0; i > accidental; --i)
        result += notation.unicode ? "\xe2\x99\xad" : "b";
    return result;
}

std::string noteName (SpelledNote note, const Notation& notation)
{
    return std::string (1, letterNames[static_cast<std::size_t> (note.letter)]) + accidentalText (note.accidental, notation);
}

std::string noteName (int midiNote, SpelledNote spelling, const Notation& notation)
{
    const int octave = floorDiv12 (midiNote - spelling.accidental) + notation.middleCOctave - 5;
    return noteName (spelling, notation) + std::to_string (octave);
}

std::string midiNoteName (int midiNote, const Notation& notation, bool preferFlats)
{
    return noteName (midiNote, spellPitchClass (midiNote, preferFlats), notation);
}

std::string midiNoteName (int midiNote, const Key& key, const Notation& notation)
{
    return noteName (midiNote, spellInKey (midiNote, key), notation);
}

std::optional<SpelledNote> parseNote (std::string_view text)
{
    const auto normalized = normalizeSymbols (trim (text));
    std::size_t consumed = 0;
    const auto note = parseNotePrefix (normalized, true, consumed);
    if (! note || consumed != normalized.size())
        return std::nullopt;
    return note;
}

std::optional<int> parseMidiNote (std::string_view text, const Notation& notation)
{
    const auto normalized = normalizeSymbols (trim (text));
    std::size_t consumed = 0;
    const auto note = parseNotePrefix (normalized, true, consumed);
    if (! note)
        return std::nullopt;

    std::string_view rest = std::string_view (normalized).substr (consumed);
    bool negative = false;
    if (! rest.empty() && rest.front() == '-')
    {
        negative = true;
        rest.remove_prefix (1);
    }
    if (rest.empty() || rest.size() > 2 || ! std::all_of (rest.begin(), rest.end(), [] (char c) { return std::isdigit (static_cast<unsigned char> (c)); }))
        return std::nullopt;
    int octave = std::stoi (std::string (rest));
    if (negative)
        octave = -octave;

    const int midi = (octave - notation.middleCOctave + 5) * 12 + naturalPitchClass[static_cast<std::size_t> (note->letter)] + note->accidental;
    if (midi < 0 || midi > 127)
        return std::nullopt;
    return midi;
}

// --- Intervalos ------------------------------------------------------------------------------------------------------
std::string intervalName (Interval interval)
{
    const int index = (interval.number - 1) % 7;
    const bool perfect = index == 0 || index == 3 || index == 4;
    const int alteration = alterationFromMajor (interval);
    std::string quality;
    if (perfect)
        quality = alteration == 0 ? "P" : alteration < 0 ? std::string (static_cast<std::size_t> (-alteration), 'd')
                                                         : std::string (static_cast<std::size_t> (alteration), 'A');
    else if (alteration == 0)
        quality = "M";
    else if (alteration == -1)
        quality = "m";
    else
        quality = alteration < 0 ? std::string (static_cast<std::size_t> (-alteration - 1), 'd')
                                 : std::string (static_cast<std::size_t> (alteration), 'A');
    return quality + std::to_string (interval.number);
}

std::string degreeLabel (Interval interval, const Notation& notation)
{
    return accidentalText (alterationFromMajor (interval), notation) + std::to_string (interval.number);
}

// --- Escalas y tonalidades -------------------------------------------------------------------------------------------
std::string_view modeName (Mode mode)
{
    return modeInfo (mode).name;
}

std::optional<Mode> parseMode (std::string_view text)
{
    // Minúsculas y un solo espacio entre palabras ("Harmonic   Minor" = "harmonic minor").
    std::string words;
    for (const char c : lowercase (trim (text)))
    {
        if (std::isspace (static_cast<unsigned char> (c)) || c == '-' || c == '_')
        {
            if (! words.empty() && words.back() != ' ')
                words += ' ';
        }
        else
            words += c;
    }

    for (int m = 0; m < modeCount; ++m)
        if (modeTable[static_cast<std::size_t> (m)].name == words)
            return static_cast<Mode> (m);

    static constexpr std::array<std::pair<std::string_view, Mode>, 7> aliases { {
        { "ionian", Mode::ionian },
        { "maj", Mode::ionian },
        { "aeolian", Mode::aeolian },
        { "natural minor", Mode::aeolian },
        { "min", Mode::aeolian },
        { "jazz minor", Mode::melodicMinor },
        { "spanish phrygian", Mode::phrygianDominant },
    } };
    for (const auto& [alias, mode] : aliases)
        if (alias == words)
            return mode;
    return std::nullopt;
}

std::string keyName (const Key& key, const Notation& notation)
{
    return noteName (key.tonic, notation) + " " + std::string (modeName (key.mode));
}

std::optional<Key> parseKey (std::string_view text)
{
    const auto normalized = normalizeSymbols (trim (text));
    std::size_t consumed = 0;
    const auto tonic = parseNotePrefix (normalized, true, consumed);
    if (! tonic)
        return std::nullopt;

    const auto rest = trim (std::string_view (normalized).substr (consumed));
    if (rest.empty())
        return Key { *tonic, Mode::ionian };
    if (rest == "m")
        return Key { *tonic, Mode::aeolian };
    if (consumed < normalized.size() && ! std::isspace (static_cast<unsigned char> (normalized[consumed])))
        return std::nullopt; // "Cmajor" sin espacio: ambiguo con un acorde; se pide "C major"
    if (const auto mode = parseMode (rest))
        return Key { *tonic, *mode };
    return std::nullopt;
}

// --- Acordes ---------------------------------------------------------------------------------------------------------
std::string chordSuffix (ChordTypeId type, const Notation& notation)
{
    return decorate (chordTypeInfo (type).symbol, notation, false);
}

std::string chordSymbol (const Chord& chord, const Notation& notation)
{
    auto text = noteName (chord.root, notation) + chordSuffix (chord.type, notation);
    if (chord.hasBass)
        text += "/" + noteName (chord.bass, notation);
    return text;
}

std::optional<Chord> parseChordSymbol (std::string_view text)
{
    const auto normalized = normalizeSymbols (trim (text));
    std::size_t consumed = 0;
    const auto root = parseNotePrefix (normalized, false, consumed);
    if (! root)
        return std::nullopt;

    const auto rest = std::string_view (normalized).substr (consumed);
    for (const auto& [suffix, type] : suffixTable())
    {
        if (rest.substr (0, suffix.size()) != suffix)
            continue;
        const auto remainder = rest.substr (suffix.size());
        const Chord chord { *root, type };
        if (remainder.empty())
            return chord;
        if (remainder.front() == '/')
        {
            std::size_t bassConsumed = 0;
            const auto bass = parseNotePrefix (remainder.substr (1), false, bassConsumed);
            if (bass && bassConsumed == remainder.size() - 1)
                return chord.withBass (*bass);
        }
    }
    return std::nullopt;
}

// --- Números romanos y progresiones ------------------------------------------------------------------------------------
std::string romanNumeralText (const RomanNumeral& numeral, const Notation& notation)
{
    const auto& info = chordTypeInfo (numeral.type);
    std::string text = accidentalText (numeral.accidental, notation);
    std::string digits (upperNumerals[static_cast<std::size_t> ((numeral.degree - 1) % 7)]);
    if (! info.romanUpper)
        digits = lowercase (digits);
    return text + digits + decorate (info.romanSuffix, notation, true);
}

std::optional<RomanNumeral> parseRomanNumeral (std::string_view text)
{
    const auto normalized = normalizeSymbols (trim (text));
    std::string_view rest = normalized;

    RomanNumeral numeral;
    while (! rest.empty() && (rest.front() == 'b' || rest.front() == '#'))
    {
        numeral.accidental += rest.front() == '#' ? 1 : -1;
        rest.remove_prefix (1);
    }

    // El número más largo que encaje (VII antes que V), todo en mayúsculas o todo en minúsculas.
    bool found = false;
    bool upper = true;
    for (int d = 7; d >= 1 && ! found; --d)
    {
        const auto digits = upperNumerals[static_cast<std::size_t> (d - 1)];
        for (const bool tryUpper : { true, false })
        {
            const auto candidate = tryUpper ? std::string (digits) : lowercase (digits);
            if (rest.substr (0, candidate.size()) == candidate)
            {
                numeral.degree = d;
                upper = tryUpper;
                rest.remove_prefix (candidate.size());
                found = true;
                break;
            }
        }
    }
    if (! found)
        return std::nullopt;

    // Primero el sufijo de números romanos ("7", "o", "h7", "maj7"); si no, un sufijo de acorde que concuerde con la
    // mayúscula/minúscula ("iim7", "viim7b5", "viidim").
    for (int t = 0; t < chordTypeCount; ++t)
    {
        const auto& info = chordTypes[static_cast<std::size_t> (t)];
        if (info.romanUpper == upper && info.romanSuffix == rest)
        {
            numeral.type = static_cast<ChordTypeId> (t);
            return numeral;
        }
    }
    for (const auto& [suffix, type] : suffixTable())
        if (suffix == rest && chordTypeInfo (type).romanUpper == upper)
        {
            numeral.type = type;
            return numeral;
        }
    return std::nullopt;
}

std::string progressionText (std::span<const RomanNumeral> progression, const Notation& notation)
{
    std::string text;
    for (std::size_t i = 0; i < progression.size(); ++i)
    {
        if (i > 0)
            text += notation.unicode ? "\xe2\x80\x93" : "-";
        text += romanNumeralText (progression[i], notation);
    }
    return text;
}

std::optional<std::vector<RomanNumeral>> parseProgression (std::string_view text)
{
    const auto normalized = normalizeSymbols (text);
    std::vector<RomanNumeral> result;
    std::string token;
    const auto flush = [&] {
        if (token.empty())
            return true;
        const auto numeral = parseRomanNumeral (token);
        token.clear();
        if (! numeral)
            return false;
        result.push_back (*numeral);
        return true;
    };

    for (const char c : normalized)
    {
        if (c == '-' || c == '|' || c == ',' || std::isspace (static_cast<unsigned char> (c)))
        {
            if (! flush())
                return std::nullopt;
        }
        else
            token += c;
    }
    if (! flush() || result.empty())
        return std::nullopt;
    return result;
}

} // namespace deepwater::theory
