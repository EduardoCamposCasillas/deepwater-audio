#include "deepwater/theory/Progression.h"

namespace deepwater::theory
{

std::vector<NoteEvent> renderProgression (const Progression& progression, const Key& key, const RenderOptions& options)
{
    std::vector<NoteEvent> events;
    const long long length = static_cast<long long> (progression.totalBeats()) * sixteenthsPerQuarter * (options.loops < 1 ? 1 : options.loops);
    if (length <= 0)
        return events;

    const auto voiced = voiceProgression (progression, key, options.style, options.tonicNote, options.bass);
    const auto& pattern = rhythmPattern (options.rhythm);

    // El acorde abierto (el que suena): desde qué semicorchea, con qué paso y velocidad.
    int openStep = -1;
    long long openStart = 0;
    int openVelocity = 0;
    const auto close = [&] (long long at) {
        if (openStep < 0)
            return;
        for (const int note : voiced[static_cast<std::size_t> (openStep)].notes())
            events.push_back ({ note, static_cast<double> (openStart) / sixteenthsPerQuarter,
                                static_cast<double> (at - openStart) / sixteenthsPerQuarter, openVelocity });
        openStep = -1;
    };

    for (long long t = 0; t < length; ++t)
    {
        const int step = progression.stepAt (t);
        bool gate = true, hitStart = false;
        int accent = 100;
        if (pattern.steps > 0)
        {
            const int p = patternStep (pattern, t);
            const auto* hit = hitAt (pattern, p);
            gate = hit != nullptr;
            hitStart = gate && hit->start == p;
            accent = gate ? hit->accent : 100;
        }
        const bool attack = gate && (openStep < 0 || step != openStep || hitStart);
        if (openStep >= 0 && (! gate || attack))
            close (t);
        if (attack)
        {
            openStep = step;
            openStart = t;
            openVelocity = accentedVelocity (options.velocity, accent);
        }
    }
    close (length);
    return events;
}

} // namespace deepwater::theory
