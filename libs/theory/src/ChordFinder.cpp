#include "deepwater/theory/ChordFinder.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace deepwater::theory
{
namespace
{
// La escritura de la fundamental que deje menos alteraciones en el acorde (las dobles pesan mucho). Empate (F♯ / G♭):
// sostenidos, como FL.
SpelledNote plainRootSpelling (PitchClass root, ChordTypeId type)
{
    std::array<SpelledNote, 2> candidates {};
    const int count = spellingCandidates (root, candidates);
    SpelledNote best = candidates[0];
    int bestWeight = 1 << 30;
    for (int i = 0; i < count; ++i)
    {
        int weight = 0;
        for (const auto& tone : Chord { candidates[static_cast<std::size_t> (i)], type }.tones())
        {
            const int a = tone.accidental < 0 ? -tone.accidental : tone.accidental;
            weight += a * (a > 1 ? 10 : 1);
        }
        if (weight < bestWeight)
        {
            bestWeight = weight;
            best = candidates[static_cast<std::size_t> (i)];
        }
    }
    return best;
}

std::vector<ChordReading> find (std::span<const int> midiNotes, const Key* key)
{
    if (midiNotes.empty())
        return {};

    std::uint16_t mask = 0;
    int lowest = midiNotes[0];
    for (const int note : midiNotes)
    {
        mask = static_cast<std::uint16_t> (mask | (1u << wrap12 (note)));
        lowest = std::min (lowest, note);
    }
    const PitchClass bass = wrap12 (lowest);
    const int distinct = std::popcount (mask);
    if (distinct < 2)
        return {};

    struct Candidate
    {
        int score;
        ChordReading reading;
    };
    std::vector<Candidate> candidates;

    for (PitchClass root = 0; root < 12; ++root)
    {
        if ((mask & (1u << root)) == 0)
            continue;
        for (int t = 0; t < chordTypeCount; ++t)
        {
            const auto type = static_cast<ChordTypeId> (t);
            const auto typeMask = chordMask (root, type);
            const bool exact = typeMask == mask;
            bool omitsFifth = false;
            if (! exact && distinct >= 3)
            {
                const auto& info = chordTypeInfo (type);
                const bool hasFifth = std::any_of (info.tones.begin(), info.tones.begin() + info.toneCount,
                                                   [] (Interval i) { return i.number == 5 && i.semitones == 7; });
                omitsFifth = hasFifth && static_cast<std::uint16_t> (typeMask & ~(1u << wrap12 (root + 7))) == mask;
            }
            if (! exact && ! omitsFifth)
                continue;

            const auto rootSpelled = key != nullptr ? spellInKey (root, *key) : plainRootSpelling (root, type);
            Chord chord { rootSpelled, type };
            if (bass != root)
            {
                const auto tones = chord.tones();
                for (std::size_t i = 0; i < tones.size(); ++i)
                    if (tones[i].pitchClass() == bass)
                        chord = chord.withInversion (static_cast<int> (i));
            }
            const int score = (omitsFifth ? 1000 : 0) + (bass == root ? 0 : 100) + t;
            candidates.push_back ({ score, { chord, omitsFifth } });
        }
    }

    std::stable_sort (candidates.begin(), candidates.end(), [] (const Candidate& a, const Candidate& b) { return a.score < b.score; });
    std::vector<ChordReading> result;
    result.reserve (candidates.size());
    for (const auto& c : candidates)
        result.push_back (c.reading);
    return result;
}
} // namespace

std::vector<ChordReading> findChords (std::span<const int> midiNotes)
{
    return find (midiNotes, nullptr);
}

std::vector<ChordReading> findChords (std::span<const int> midiNotes, const Key& key)
{
    return find (midiNotes, &key);
}

} // namespace deepwater::theory
