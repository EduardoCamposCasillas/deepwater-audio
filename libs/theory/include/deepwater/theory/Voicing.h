#pragma once

#include <array>
#include <cstddef>

#include "deepwater/theory/Chord.h"
#include "deepwater/theory/StaticVector.h"

namespace deepwater::theory
{

// --- Voicings y conducción de voces (Fase 4 de Tidechart) ------------------------------------------------------------
// Un acorde dice QUÉ notas suenan; el voicing dice en qué octava y en qué orden. C se puede tocar C E G, E G C, G C E,
// abierto, en otra octava... En estado fundamental, una progresión salta: cada voz recorre una 3.ª o una 4.ª al cambiar
// de acorde. La conducción de voces elige, para cada acorde, el voicing que obliga a las voces a moverse lo MENOS
// posible desde el anterior: las notas comunes se quedan quietas y las demás van a la más cercana. Es lo que haría un
// pianista (o un coro), y de ahí salen solas las inversiones: C → F se toca C F A (2.ª inversión, el Do quieto) y G,
// B D G (1.ª).
enum class VoicingStyle
{
    rootPosition, // posición cerrada con la fundamental abajo (el voicing de la Fase 3)
    voiceLed,     // la inversión y la octava que menos mueven las voces, en posición cerrada
    open,         // lo mismo, en drop 2 (la 2.ª voz desde arriba baja una octava): más ancho, de pad
};

inline constexpr int voicingStyleCount = 3;

// Las notas MIDI de un voicing, de grave a agudo.
using Voicing = StaticVector<int, 8>;

// Cuánto se puede alejar el centro de un voicing del centro pedido, en semitonos (hacia cada lado). Sin un límite, un
// acorde tras otro "bajando un poquito" acabaría en el sótano del teclado.
inline constexpr int voicingWindow = 7;

namespace detail
{
constexpr int absolute (int value) noexcept { return value < 0 ? -value : value; }

constexpr void sortAscending (Voicing& voicing) noexcept
{
    for (std::size_t i = 1; i < voicing.size(); ++i)
        for (std::size_t j = i; j > 0 && voicing[j - 1] > voicing[j]; --j)
        {
            const int t = voicing[j];
            voicing[j] = voicing[j - 1];
            voicing[j - 1] = t;
        }
}
} // namespace detail

// Cuántos semitonos recorren en total las voces al pasar de un voicing a otro (los dos de grave a agudo). Con el mismo
// número de notas, cada voz va a la del mismo lugar (la de abajo con la de abajo), como se canta un coral. Con distinto
// número, cada nota se compara con la más cercana del otro acorde, en las dos direcciones.
[[nodiscard]] constexpr int voiceMovement (const Voicing& from, const Voicing& to) noexcept
{
    if (from.empty() || to.empty())
        return 0;
    int total = 0;
    if (from.size() == to.size())
    {
        for (std::size_t i = 0; i < from.size(); ++i)
            total += detail::absolute (from[i] - to[i]);
        return total;
    }
    const auto nearest = [] (int note, const Voicing& in) {
        int best = 1 << 20;
        for (const int other : in)
            best = detail::absolute (other - note) < best ? detail::absolute (other - note) : best;
        return best;
    };
    for (const int note : to)
        total += nearest (note, from);
    for (const int note : from)
        total += nearest (note, to);
    return total;
}

// Drop 2: la segunda voz desde arriba baja una octava. C E G → E3 C G (una décima entre las voces de fuera).
[[nodiscard]] constexpr Voicing dropTwo (Voicing voicing) noexcept
{
    if (voicing.size() >= 3)
    {
        voicing[voicing.size() - 2] -= 12;
        detail::sortAscending (voicing);
    }
    return voicing;
}

// El voicing de un acorde (solo fundamental y tipo: el bajo lo agrega quien llama).
//   rootNote: la tecla de la fundamental en estado fundamental (rootPosition devuelve Chord::midiNotes desde ahí).
//   centre2:  dónde debe quedar el centro del voicing, como grave + agudo (en medios semitonos, para no usar
//             decimales); p. ej. el de la tríada de tónica: 2 · tónica + 7.
//   previous: el voicing anterior, o nullptr si es el primero (entonces solo cuenta el centro).
//   floor:    ninguna voz puede quedar en esta nota o por debajo (el bajo, si lo hay); −1 = sin límite.
// Se prueban todas las inversiones en 5 octavas; gana la que menos mueve las voces (×4) más lo que se aleja del
// centro. En un empate, la primera: estado fundamental y octava de rootNote. Sin reservar memoria (hilo de audio).
[[nodiscard]] constexpr Voicing voiceChord (const Chord& chord, int rootNote, VoicingStyle style, int centre2,
                                            const Voicing* previous = nullptr, int floor = -1) noexcept
{
    const Chord plain { chord.root, chord.type };
    const Voicing rootPosition = plain.midiNotes (rootNote);
    if (style == VoicingStyle::rootPosition)
        return rootPosition;

    const auto& info = plain.info();
    constexpr std::array<int, 5> shifts { 0, -12, 12, -24, 24 };
    Voicing best;
    int bestCost = 1 << 30;
    for (int inversion = 0; inversion < info.toneCount; ++inversion)
        for (const int shift : shifts)
        {
            // Posición cerrada con el tono 'inversion' abajo: los tonos que quedarían por debajo suben una octava.
            Voicing candidate;
            const int bassOffset = info.tones[static_cast<std::size_t> (inversion)].semitones;
            for (int i = 0; i < info.toneCount; ++i)
            {
                int offset = info.tones[static_cast<std::size_t> (i)].semitones;
                while (offset < bassOffset)
                    offset += 12;
                candidate.push_back (rootNote + shift + offset);
            }
            detail::sortAscending (candidate);
            if (style == VoicingStyle::open)
                candidate = dropTwo (candidate);

            if (candidate.empty() || candidate[0] < 0 || candidate[candidate.size() - 1] > 127 || candidate[0] <= floor)
                continue;
            const int distance = detail::absolute (candidate[0] + candidate[candidate.size() - 1] - centre2);
            if (distance > 2 * voicingWindow)
                continue;
            const int cost = previous != nullptr && ! previous->empty() ? 4 * voiceMovement (*previous, candidate) + distance : distance;
            if (cost < bestCost)
            {
                bestCost = cost;
                best = candidate;
            }
        }
    return best.empty() ? rootPosition : best;
}

} // namespace deepwater::theory
