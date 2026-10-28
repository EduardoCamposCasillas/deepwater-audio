#include "deepwater/theory/KeyDetection.h"

#include <algorithm>
#include <map>
#include <vector>

namespace deepwater::theory
{

KeyEvidence keyEvidence (std::span<const NoteEvent> notes, double beatsPerPhrase)
{
    KeyEvidence evidence;

    // Duración: se parte el tiempo en tramos entre todos los inicios y finales de nota; en cada tramo, cada nota que
    // suena suma su largo y la más grave lo suma otra vez (bassWeight).
    std::vector<double> times;
    times.reserve (notes.size() * 2);
    for (const auto& n : notes)
        if (n.length > 0.0)
        {
            times.push_back (n.start);
            times.push_back (n.start + n.length);
        }
    std::sort (times.begin(), times.end());
    times.erase (std::unique (times.begin(), times.end()), times.end());

    for (std::size_t i = 0; i + 1 < times.size(); ++i)
    {
        const double from = times[i], to = times[i + 1], length = to - from;
        int lowest = 128;
        for (const auto& n : notes)
            if (n.length > 0.0 && n.start <= from && n.start + n.length >= to)
            {
                evidence.pitchWeight[static_cast<std::size_t> (wrap12 (n.note))] += length;
                lowest = std::min (lowest, n.note);
            }
        if (lowest < 128)
            evidence.pitchWeight[static_cast<std::size_t> (wrap12 (lowest))] += KeyEvidence::bassWeight * length;
    }

    // Votos de "casa": la nota más grave de las que empiezan en la ventana del inicio de cada frase.
    std::map<long long, int> lowestPerPhrase;
    for (const auto& n : notes)
        if (n.length > 0.0)
            if (const long long phrase = phraseCueIndex (n.start, beatsPerPhrase); phrase >= 0)
            {
                auto [it, inserted] = lowestPerPhrase.try_emplace (phrase, n.note);
                if (! inserted)
                    it->second = std::min (it->second, n.note);
            }
    for (const auto& [phrase, note] : lowestPerPhrase)
        evidence.tonicCue[static_cast<std::size_t> (wrap12 (note))] += 1.0;
    return evidence;
}

} // namespace deepwater::theory
