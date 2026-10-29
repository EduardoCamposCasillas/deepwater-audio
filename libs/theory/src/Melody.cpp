#include "deepwater/theory/Melody.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace deepwater::theory
{

namespace
{
// Generador propio y repetible (splitmix64): misma semilla = misma secuencia en cualquier compilador. Solo sumas,
// multiplicaciones y desplazamientos de enteros; unit() convierte 53 bits a [0, 1) sin funciones trascendentes.
// 'stream' separa las secuencias de la melodía, el bajo y las variaciones aunque compartan semilla.
class SeededRandom
{
public:
    SeededRandom (std::uint32_t seed, std::uint64_t stream) noexcept
        : state (static_cast<std::uint64_t> (seed) * 0x9E3779B97F4A7C15ull ^ (stream * 0xD1B54A32D192ED03ull))
    {
    }

    std::uint64_t next() noexcept
    {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    double unit() noexcept { return static_cast<double> (next() >> 11) * (1.0 / 9007199254740992.0); }
    int below (int n) noexcept { return n <= 1 ? 0 : static_cast<int> (next() % static_cast<std::uint64_t> (n)); }
    bool chance (double p) noexcept { return unit() < p; }

    // Un índice al azar con probabilidad proporcional a su peso (los pesos ≤ 0 nunca salen); −1 si todos son 0.
    template <std::size_t N>
    int weighted (const std::array<double, N>& weights) noexcept
    {
        double total = 0.0;
        for (const double w : weights)
            total += w > 0.0 ? w : 0.0;
        if (total <= 0.0)
            return -1;
        double r = unit() * total;
        int last = -1;
        for (std::size_t i = 0; i < N; ++i)
            if (weights[i] > 0.0)
            {
                last = static_cast<int> (i);
                if (r < weights[i])
                    return last;
                r -= weights[i];
            }
        return last;
    }

private:
    std::uint64_t state;
};

struct Range
{
    int low = 0, high = 127;
};

// Rango ordenado, dentro de 0..127 y de al menos una octava (así cualquier acorde tiene todas sus notas en él).
Range sanitize (int low, int high) noexcept
{
    low = std::clamp (low, 0, 127);
    high = std::clamp (high, 0, 127);
    if (high < low)
        std::swap (low, high);
    if (high - low < 12)
    {
        high = std::min (127, low + 12);
        low = high - 12;
    }
    return { low, high };
}

bool inMask (std::uint16_t mask, int note) noexcept
{
    return ((mask >> wrap12 (note)) & 1u) != 0;
}

// Grados de escala entre dos notas (con signo): cuántas notas de la máscara se cruzan al ir de una a otra.
int scaleSteps (std::uint16_t mask, int from, int to) noexcept
{
    int steps = 0;
    if (to > from)
    {
        for (int n = from + 1; n <= to; ++n)
            steps += inMask (mask, n) ? 1 : 0;
    }
    else
        for (int n = to; n < from; ++n)
            steps -= inMask (mask, n) ? 1 : 0;
    return steps;
}

// La nota de la escala vecina (dir = +1 arriba, −1 abajo).
int stepFrom (std::uint16_t mask, int note, int dir) noexcept
{
    if (mask == 0)
        return note + dir;
    int n = note + dir;
    while (! inMask (mask, n))
        n += dir;
    return n;
}

// La nota de la máscara más cercana a 'note' dentro del rango (en un empate, hacia 'preferDir'); 'skipSelf' la excluye.
int nearestInMask (std::uint16_t mask, int note, Range range, int preferDir = 1, bool skipSelf = false) noexcept
{
    for (int d = skipSelf ? 1 : 0; d <= 24; ++d)
        for (const int dir : { preferDir, -preferDir })
        {
            const int n = note + dir * d;
            if (n >= range.low && n <= range.high && inMask (mask, n))
                return n;
            if (d == 0)
                break;
        }
    return std::clamp (note, range.low, range.high);
}

// Nota "a evitar" en un apoyo: un semitono por encima de una nota del acorde (Fa sobre Do mayor, Si♭ sobre La menor);
// sostenida, choca con la nota de abajo. Las demás notas de la escala que no son del acorde son tensiones que suenan
// bien (la 9.ª, la 11.ª sobre un menor, la 13.ª).
bool isAvoid (int note, std::uint16_t chordBits) noexcept
{
    return inMask (chordBits, note - 1);
}

struct Harmony
{
    std::uint16_t chordBits = 0; // notas del acorde
    std::uint16_t scale = 0;     // la escala del acorde (incluye sus notas)
    int rootPc = 0;
    int step = -1;               // el paso de la progresión (para saber si cambió el acorde)
};

Harmony harmonyAt (const Progression& progression, const Key& key, long long sixteenth) noexcept
{
    Harmony h;
    if (const auto chord = chordAt (progression, key, sixteenth))
    {
        h.chordBits = chordMask (chord->root.pitchClass(), chord->type);
        h.scale = static_cast<std::uint16_t> (chordScaleMask (*chord, key) | h.chordBits);
        h.rootPc = chord->root.pitchClass();
        h.step = progression.stepAt (sixteenth);
    }
    else
    {
        h.scale = scaleMask (key);
        h.rootPc = key.tonic.pitchClass();
    }
    return h;
}

LineNote clampedNote (int start, int length, int note, int velocity, int lineLen) noexcept
{
    start = std::clamp (start, 0, lineLen - 1);
    length = std::clamp (length, 1, lineLen - start);
    return { start, length, std::clamp (note, 0, 127), std::clamp (velocity, 1, 127) };
}

Line toLine (std::vector<LineNote> notes, int length)
{
    std::sort (notes.begin(), notes.end(), [] (const LineNote& a, const LineNote& b) {
        return a.start != b.start ? a.start < b.start : a.note < b.note;
    });
    notes.erase (std::unique (notes.begin(), notes.end(), [] (const LineNote& a, const LineNote& b) {
                     return a.start == b.start && a.note == b.note;
                 }),
                 notes.end());
    Line line;
    line.length = length;
    for (const auto& n : notes)
        line.notes.push_back (n);
    return line;
}

// --- Melodía ---------------------------------------------------------------------------------------------------------

// Dónde caen las notas dentro del compás (16 semicorcheas), por estilo: el peso de cada semicorchea para elegir el
// ritmo del motivo. El gancho vive en el 3+3+2 del dembow (0 3 6 | 8 11 14); la cantada, en los tiempos; el R&B, en las
// síncopas (lo que cae justo antes de un tiempo).
constexpr std::array<std::array<double, 16>, melodyStyleCount> onsetWeights { {
    { 1.0, 0.1, 0.5, 0.9, 0.3, 0.1, 0.85, 0.3, 0.95, 0.1, 0.5, 0.9, 0.3, 0.1, 0.8, 0.35 },
    { 1.0, 0.05, 0.35, 0.05, 0.8, 0.05, 0.35, 0.05, 0.9, 0.05, 0.35, 0.05, 0.75, 0.05, 0.4, 0.1 },
    { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 },
    { 0.9, 0.2, 0.5, 0.7, 0.5, 0.2, 0.7, 0.6, 0.7, 0.2, 0.6, 0.65, 0.55, 0.25, 0.7, 0.5 },
} };

// Notas por compás con densidad 0 y 1.
constexpr std::array<std::array<int, 2>, melodyStyleCount> onsetsPerBar { { { 3, 9 }, { 2, 6 }, { 4, 16 }, { 3, 10 } } };

// Lo más que dura una nota normal y la nota larga del final de la frase, en semicorcheas.
constexpr std::array<int, melodyStyleCount> noteCap { 3, 8, 4, 4 };
constexpr std::array<int, melodyStyleCount> cadenceCap { 6, 8, 8, 6 };

// Cómo elige las alturas cada estilo (costos: menos es mejor).
struct PitchRules
{
    double repeat;      // repetir la misma nota
    double colorStrong; // una tensión (nota de la escala que no es del acorde) en un apoyo
    double avoidStrong; // una nota a evitar en un apoyo
    double leapScale;   // cuánto pesan los saltos
    double temperature; // ruido: cuánto puede ganar una opción algo peor (variedad entre semillas)
};

constexpr std::array<PitchRules, melodyStyleCount> pitchRules { {
    { 0.2, 4.0, 6.0, 1.3, 1.6 }, // gancho: repite mucho, salta poco
    { 2.0, 3.5, 6.0, 1.0, 1.4 }, // cantada: grados conjuntos
    { 4.0, 50.0, 50.0, 0.3, 0.0 }, // arpegio (no se usa: tiene su propio camino)
    { 1.0, 0.8, 4.0, 1.0, 1.6 }, // R&B: las tensiones en los apoyos son bienvenidas
} };

double leapCost (int leap, const PitchRules& rules) noexcept
{
    double cost = 0.0;
    switch (leap)
    {
        case 0: return rules.repeat;
        case 1: case 2: cost = 0.0; break;
        case 3: case 4: cost = 0.8; break;
        case 5: cost = 1.5; break;
        case 6: cost = 2.5; break; // el tritono, difícil de cantar
        case 7: cost = 2.0; break;
        case 8: case 9: cost = 3.5; break;
        case 12: cost = 4.0; break;
        default: cost = 5.0; break; // 10, 11
    }
    return cost * rules.leapScale;
}

using BarRhythm = std::array<bool, 16>;

// El ritmo del motivo (un compás): 'count' semicorcheas elegidas al azar según sus pesos, sin repetir.
BarRhythm motifRhythm (MelodyStyle style, double density, SeededRandom& rng)
{
    BarRhythm on {};
    const auto index = static_cast<std::size_t> (style);
    if (style == MelodyStyle::arpeggio)
    {
        const int grid = density < 1.0 / 3.0 ? 4 : (density < 2.0 / 3.0 ? 2 : 1);
        for (int s = 0; s < 16; s += grid)
            on[static_cast<std::size_t> (s)] = true;
        return on;
    }
    const auto [lo, hi] = onsetsPerBar[index];
    const int count = std::clamp (static_cast<int> (lo + density * (hi - lo) + 0.5), 1, 16);
    auto weights = onsetWeights[index];
    for (int n = 0; n < count; ++n)
    {
        const int s = rng.weighted (weights);
        if (s < 0)
            break;
        on[static_cast<std::size_t> (s)] = true;
        weights[static_cast<std::size_t> (s)] = 0.0;
    }
    return on;
}

// A': el motivo con un cambio pequeño (una nota menos o una más), para que la repetición no sea mecánica.
BarRhythm answerRhythm (const BarRhythm& motif, MelodyStyle style, SeededRandom& rng)
{
    if (style == MelodyStyle::arpeggio)
        return motif;
    BarRhythm result = motif;
    int count = 0;
    for (const bool b : motif)
        count += b ? 1 : 0;
    if (count >= 3 && rng.chance (0.5))
    {
        std::array<double, 16> removable {};
        bool first = true;
        for (std::size_t s = 0; s < 16; ++s)
            if (motif[s])
            {
                removable[s] = first ? 0.0 : 1.0; // la primera nota se queda: es la que hace reconocible el motivo
                first = false;
            }
        if (const int s = rng.weighted (removable); s >= 0)
            result[static_cast<std::size_t> (s)] = false;
    }
    else
    {
        auto weights = onsetWeights[static_cast<std::size_t> (style)];
        for (std::size_t s = 0; s < 16; ++s)
            if (motif[s])
                weights[s] = 0.0;
        if (const int s = rng.weighted (weights); s >= 0)
            result[static_cast<std::size_t> (s)] = true;
    }
    return result;
}

// El compás que cierra la frase: el principio del motivo y una nota larga en el tiempo 3 (la frase respira).
BarRhythm cadenceRhythm (const BarRhythm& motif)
{
    BarRhythm result {};
    for (std::size_t s = 0; s < 8; ++s)
        result[s] = motif[s];
    result[8] = true;
    return result;
}

struct Onset
{
    int start = 0;
    int cap = 1;
    bool phraseEnd = false; // la nota larga del final de una frase
};

} // namespace

Line generateMelody (const Progression& progression, const Key& key, const MelodyOptions& options)
{
    const int length = lineLength (progression);
    if (length <= 0)
        return {};
    const auto range = sanitize (options.low, options.high);
    const double density = std::clamp (options.density, 0.0, 1.0);
    const auto styleIndex = static_cast<std::size_t> (std::clamp (static_cast<int> (options.style), 0, melodyStyleCount - 1));
    const auto style = static_cast<MelodyStyle> (styleIndex);
    const auto& rules = pitchRules[styleIndex];
    SeededRandom rng (options.seed, 1);

    // 1. Ritmo: el motivo A en un compás, A' (A con un cambio) y el compás de cierre, en frases de 4 compases:
    //    A A' A cierre. El último compás de la línea siempre cierra.
    const auto motif = motifRhythm (style, density, rng);
    const auto answer = answerRhythm (motif, style, rng);
    const auto cadence = cadenceRhythm (motif);
    const int bars = (length + 15) / 16;
    std::vector<Onset> onsets;
    for (int bar = 0; bar < bars; ++bar)
    {
        const int role = bar == bars - 1 ? 3 : bar % 4;
        const auto& rhythm = role == 1 ? answer : (role == 3 ? cadence : motif);
        for (int s = 0; s < 16; ++s)
            if (rhythm[static_cast<std::size_t> (s)] && bar * 16 + s < length)
            {
                const bool longNote = role == 3 && s == 8;
                onsets.push_back ({ bar * 16 + s, longNote ? cadenceCap[styleIndex] : noteCap[styleIndex], longNote });
            }
    }
    if (onsets.empty())
        return { {}, length };

    // 2. Duraciones: hasta la nota siguiente (la última, hasta la primera de la vuelta siguiente), con el tope del estilo
    //    y sin pasar del final de la vuelta. El R&B a veces deja un respiro antes de la siguiente.
    std::vector<int> lengths (onsets.size());
    for (std::size_t i = 0; i < onsets.size(); ++i)
    {
        const int next = i + 1 < onsets.size() ? onsets[i + 1].start : length + onsets[0].start;
        const int gap = next - onsets[i].start;
        int len = std::min ({ gap, onsets[i].cap, length - onsets[i].start });
        if (style == MelodyStyle::rnb && gap > 2 && ! onsets[i].phraseEnd && rng.chance (0.35))
            len = std::min (len, gap - 1);
        lengths[i] = std::max (1, len);
    }

    // 3. Alturas: para cada nota, la opción más barata del rango (ver PitchRules y los costos de abajo).
    const int centre = (range.low + range.high) / 2;
    const int tonicPc = key.tonic.pitchClass();
    std::array<int, 16> motifPitch {}; // las alturas del primer compás: el motivo que los demás imitan
    motifPitch.fill (-1);
    std::array<int, 16> motifStep {};
    std::vector<LineNote> notes;
    int previous = -1, previousInterval = 0;
    int arpeggioShape = rng.below (4), arpeggioAnchor = range.low + (range.high - range.low) / 3, arpeggioIndex = 0, arpeggioStep = -2;

    for (std::size_t i = 0; i < onsets.size(); ++i)
    {
        const auto& onset = onsets[i];
        const int t = onset.start, slot = t % 16, bar = t / 16;
        const int len = lengths[i];
        const auto h = harmonyAt (progression, key, t);
        const bool change = chordChangesAt (progression, t);
        const bool strong = slot % 8 == 0 || change || len >= 4 || (style == MelodyStyle::sung && slot % 4 == 0);
        const bool lineEnd = i + 1 == onsets.size();
        int chosen = -1;

        if (style == MelodyStyle::arpeggio)
        {
            // Arpegio: las notas del acorde en una ventana de algo más de una octava, recorridas con una figura fija
            // (arriba, abajo, arriba-abajo o 1-5-3-5); la ventana se mueve lo menos posible al cambiar de acorde.
            if (h.step != arpeggioStep)
            {
                arpeggioStep = h.step;
                arpeggioIndex = 0;
                int best = -1;
                for (int n = range.low; n <= range.high - 7; ++n)
                    if (inMask (h.chordBits, n) && (best < 0 || std::abs (n - arpeggioAnchor) < std::abs (best - arpeggioAnchor)))
                        best = n;
                arpeggioAnchor = best >= 0 ? best : range.low;
            }
            std::array<int, 8> tones {};
            int count = 0;
            for (int n = arpeggioAnchor; n <= std::min (range.high, arpeggioAnchor + 14) && count < 8; ++n)
                if (inMask (h.chordBits, n))
                    tones[static_cast<std::size_t> (count++)] = n;
            if (count == 0)
                tones[static_cast<std::size_t> (count++)] = arpeggioAnchor;
            const int k = arpeggioIndex++;
            int index = 0;
            switch (arpeggioShape)
            {
                case 0: index = k % count; break;
                case 1: index = count - 1 - k % count; break;
                case 2: { const int period = std::max (1, 2 * count - 2); const int p = k % period; index = p < count ? p : period - p; break; }
                default: { constexpr std::array<int, 4> figure { 0, 2, 1, 2 }; index = std::min (count - 1, figure[static_cast<std::size_t> (k % 4)]); break; }
            }
            chosen = tones[static_cast<std::size_t> (index)];
            if (onset.phraseEnd)
            {
                // El final de la frase descansa en la tónica (si es del acorde) o en la fundamental.
                const int target = inMask (h.chordBits, tonicPc) ? tonicPc : h.rootPc;
                for (int c = 0; c < count; ++c)
                    if (wrap12 (tones[static_cast<std::size_t> (c)]) == target)
                    {
                        chosen = tones[static_cast<std::size_t> (c)];
                        break;
                    }
            }
        }
        else
        {
            // La referencia: la nota del primer compás en la misma semicorchea (el motivo). Imitar su intervalo (en
            // grados de la escala) hace que el motivo se repita adaptado al acorde nuevo: una secuencia.
            const bool hasReference = bar > 0 && motifPitch[static_cast<std::size_t> (slot)] >= 0;
            const int referenceSteps = hasReference ? motifStep[static_cast<std::size_t> (slot)] : 0;
            const bool sameChordAsMotif = hasReference && harmonyAt (progression, key, slot).step == h.step;

            double bestCost = 1.0e30;
            for (int c = range.low; c <= range.high; ++c)
            {
                if (! inMask (h.scale, c))
                    continue;
                const bool chordTone = inMask (h.chordBits, c);
                double cost = 0.0;
                // Apoyos en notas del acorde; tensiones y notas a evitar, mejor de paso.
                if (! chordTone)
                    cost += strong ? (isAvoid (c, h.chordBits) ? rules.avoidStrong : rules.colorStrong) : (isAvoid (c, h.chordBits) ? 0.8 : 0.3);
                if (previous >= 0)
                {
                    const int leap = std::abs (c - previous);
                    if (leap > 12)
                        continue;
                    cost += leapCost (leap, rules);
                    // Tras un salto grande, mejor volver por grados en la dirección contraria (se "rellena" el hueco).
                    const int dir = (c > previous) - (c < previous);
                    const int lastDir = (previousInterval > 0) - (previousInterval < 0);
                    if (std::abs (previousInterval) >= 5 && dir != 0 && dir == lastDir)
                        cost += 2.0;
                    if (std::abs (previousInterval) >= 5 && dir == -lastDir && leap <= 2)
                        cost -= 0.5;
                    if (hasReference)
                    {
                        cost += 1.0 * std::abs (scaleSteps (h.scale, previous, c) - referenceSteps);
                        if (sameChordAsMotif && c == motifPitch[static_cast<std::size_t> (slot)])
                            cost -= 1.5;
                    }
                }
                else
                {
                    // La primera nota: del acorde y cerca del centro del rango.
                    cost += chordTone ? 0.0 : 10.0;
                    cost += 0.25 * std::abs (c - centre);
                }
                cost += 0.12 * std::abs (c - centre); // gravedad hacia el centro: no se escapa a los extremos
                if (onset.phraseEnd || lineEnd)
                {
                    // El final de la frase: en casa (la tónica) si el acorde la tiene, o en su fundamental.
                    if (chordTone && wrap12 (c) == tonicPc)
                        cost -= lineEnd ? 4.0 : 2.5;
                    else if (chordTone && wrap12 (c) == h.rootPc)
                        cost -= 1.5;
                    else if (! chordTone)
                        cost += 4.0;
                }
                cost += rules.temperature * rng.unit();
                if (cost < bestCost)
                {
                    bestCost = cost;
                    chosen = c;
                }
            }
            if (chosen < 0)
                chosen = nearestInMask (h.chordBits != 0 ? h.chordBits : h.scale, previous >= 0 ? previous : centre, range);
        }

        if (bar == 0)
        {
            motifPitch[static_cast<std::size_t> (slot)] = chosen;
            motifStep[static_cast<std::size_t> (slot)] = previous >= 0 ? scaleSteps (h.scale, previous, chosen) : 0;
        }

        // Velocidad: los tiempos fuertes más fuertes; en el gancho, los golpes del dembow acentuados.
        int velocity = slot == 0 ? 100 : (slot == 8 ? 94 : (slot % 4 == 0 ? 88 : (slot % 2 == 0 ? 80 : 74)));
        if (style == MelodyStyle::hook && (slot == 3 || slot == 6 || slot == 11 || slot == 14))
            velocity += 8;
        if (style == MelodyStyle::rnb && slot % 2 == 1)
            velocity += 4;
        if (onset.phraseEnd)
            velocity = std::max (velocity, 90);
        velocity += rng.below (7) - 3;

        notes.push_back (clampedNote (t, len, chosen, velocity, length));
        previousInterval = previous >= 0 ? chosen - previous : 0;
        previous = chosen;
    }
    return toLine (std::move (notes), length);
}

// --- Bajo --------------------------------------------------------------------------------------------------------------

namespace
{
struct Hit
{
    int start = 0;
    int length = 1;
};

// Los golpes de un compás según el estilo y la densidad (inicio, duración en semicorcheas).
std::vector<Hit> bassBar (BassStyle style, double density)
{
    const int level = density < 1.0 / 3.0 ? 0 : (density < 2.0 / 3.0 ? 1 : 2);
    switch (style)
    {
        case BassStyle::root:
            if (level == 0) return { { 0, 16 } };
            if (level == 1) return { { 0, 8 }, { 8, 8 } };
            return { { 0, 4 }, { 4, 4 }, { 8, 4 }, { 12, 4 } };
        case BassStyle::octaves:
        {
            const int grid = level == 0 ? 4 : (level == 1 ? 2 : 1);
            std::vector<Hit> hits;
            for (int s = 0; s < 16; s += grid)
                hits.push_back ({ s, grid });
            return hits;
        }
        case BassStyle::walking:
            if (level == 0) return { { 0, 8 }, { 8, 8 } };
            return { { 0, 4 }, { 4, 4 }, { 8, 4 }, { 12, 4 } };
        case BassStyle::dembow:
        case BassStyle::pedal:
        default:
            if (level == 0) return { { 0, 6 }, { 8, 6 } };
            if (level == 1) return { { 0, 3 }, { 6, 2 }, { 8, 3 }, { 14, 2 } };
            return { { 0, 3 }, { 3, 3 }, { 6, 2 }, { 8, 3 }, { 11, 3 }, { 14, 2 } };
    }
}

// La nota de esta clase de altura más cerca de 'previous' dentro del rango (sin anterior: la más grave del rango; en un
// empate, la de abajo). Es lo que hace que el bajo se mueva poco: de La a Fa baja una 3.ª en vez de subir una 6.ª.
int placeNear (int pitchClass, int previous, Range range) noexcept
{
    int best = -1;
    for (int n = range.low; n <= range.high; ++n)
    {
        if (wrap12 (n) != wrap12 (pitchClass))
            continue;
        if (previous < 0)
            return n;
        if (best < 0 || std::abs (n - previous) < std::abs (best - previous))
            best = n;
    }
    return best >= 0 ? best : std::clamp (range.low + wrap12 (pitchClass - range.low), range.low, range.high);
}
} // namespace

Line generateBass (const Progression& progression, const Key& key, const BassOptions& options)
{
    const int length = lineLength (progression);
    if (length <= 0)
        return {};
    const auto range = sanitize (options.low, options.high);
    const double density = std::clamp (options.density, 0.0, 1.0);
    const auto style = static_cast<BassStyle> (std::clamp (static_cast<int> (options.style), 0, bassStyleCount - 1));
    SeededRandom rng (options.seed, 2);

    // 1. Golpes: el patrón del compás repetido; cada cambio de acorde empieza un golpe (si caía dentro de uno, lo corta).
    std::vector<Hit> hits;
    const auto bar = bassBar (style, density);
    for (int start = 0; start < length; start += 16)
        for (const auto& h : bar)
            if (start + h.start < length)
                hits.push_back ({ start + h.start, h.length });
    const int loop = progression.totalBeats() * sixteenthsPerQuarter;
    if (style != BassStyle::pedal)
        for (int base = 0; base < length; base += loop)
            for (int i = 0; i < static_cast<int> (progression.steps.size()); ++i)
            {
                const int change = base + progression.stepStart (i);
                if (change < length && std::none_of (hits.begin(), hits.end(), [change] (const Hit& h) { return h.start == change; }))
                    hits.push_back ({ change, 16 });
            }
    std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.start < b.start; });
    for (std::size_t i = 0; i < hits.size(); ++i)
    {
        const int next = i + 1 < hits.size() ? hits[i + 1].start : length;
        hits[i].length = std::max (1, std::min (hits[i].length, next - hits[i].start));
    }
    // El walking denso: una corchea de aproximación justo antes de cada cambio de acorde.
    if (style == BassStyle::walking && density >= 2.0 / 3.0)
    {
        std::vector<Hit> extra;
        for (auto& h : hits)
        {
            const int end = h.start + h.length;
            if (h.length == 4 && end <= length && chordChangesAt (progression, end % length) && progression.stepAt (end % length) != progression.stepAt (h.start))
            {
                h.length = 2;
                extra.push_back ({ h.start + 2, 2 });
            }
        }
        hits.insert (hits.end(), extra.begin(), extra.end());
        std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.start < b.start; });
    }

    // 2. Notas.
    const int tonicPc = key.tonic.pitchClass();
    std::vector<LineNote> notes;
    int root = -1, firstRoot = -1, previous = -1, sinceChange = 0, step = -2;
    for (std::size_t i = 0; i < hits.size(); ++i)
    {
        const int t = hits[i].start, slot = t % 16;
        const auto h = harmonyAt (progression, key, t);
        const bool change = h.step != step;
        if (change)
        {
            step = h.step;
            sinceChange = 0;
            root = placeNear (style == BassStyle::pedal ? tonicPc : h.rootPc, root, range);
            if (firstRoot < 0)
                firstRoot = root;
        }
        // ¿Es el último golpe antes de que cambie el acorde? (la vuelta del final al principio también cuenta)
        const int nextStart = i + 1 < hits.size() ? hits[i + 1].start : length;
        const auto nextHarmony = harmonyAt (progression, key, nextStart % length);
        const bool lastBefore = nextHarmony.step != h.step;
        // Al final de la vuelta, el siguiente es el primero de la línea (ya colocado): la aproximación llega a él.
        const int nextRoot = i + 1 < hits.size() ? placeNear (nextHarmony.rootPc, root, range) : firstRoot;

        int note = root;
        int velocity = slot == 0 || change ? 100 : (slot == 8 ? 96 : 90);
        const auto approach = [&] (bool chromatic) {
            const int dir = nextRoot >= root ? -1 : 1; // desde el lado por el que se llega
            const int n = chromatic ? nextRoot + dir : stepFrom (h.scale, nextRoot, dir);
            return n >= range.low && n <= range.high && n != nextRoot ? n : -1;
        };
        switch (style)
        {
            case BassStyle::root:
            case BassStyle::pedal:
                break;
            case BassStyle::octaves:
                if (sinceChange % 2 == 1)
                {
                    note = root + 12 <= range.high ? root + 12 : (root - 12 >= range.low ? root - 12 : root);
                    velocity = 82;
                }
                break;
            case BassStyle::dembow:
                if (! change && slot != 0 && slot != 8 && slot != 3 && slot != 11)
                {
                    // El "2" del 3+3+2: una aproximación al acorde siguiente, la 5.ª, la octava o la fundamental.
                    const int a = lastBefore && rng.chance (0.55) ? approach (false) : -1;
                    if (a >= 0)
                    {
                        note = a;
                        velocity = 84;
                    }
                    else
                    {
                        const int fifth = root + 7 <= range.high ? root + 7 : (root - 5 >= range.low ? root - 5 : root);
                        const int octave = root + 12 <= range.high ? root + 12 : root;
                        const std::array<double, 3> weights { 0.5, 0.3, 0.2 };
                        const int pick = rng.weighted (weights);
                        note = pick == 0 ? fifth : (pick == 1 ? octave : root);
                        velocity = 88;
                    }
                }
                break;
            case BassStyle::walking:
                if (! change)
                {
                    const int a = lastBefore ? approach (rng.chance (0.5)) : -1;
                    if (a >= 0)
                    {
                        note = a;
                        velocity = 84;
                    }
                    else
                    {
                        // Una nota del acorde cerca de la anterior (no la misma): el bajo camina.
                        std::array<int, 2> nearest { -1, -1 };
                        for (int d = 1; d <= 9 && nearest[1] < 0; ++d)
                            for (const int n : { previous + d, previous - d })
                                if (n >= range.low && n <= range.high && inMask (h.chordBits, n) && nearest[1] < 0 && n != nearest[0])
                                    (nearest[0] < 0 ? nearest[0] : nearest[1]) = n;
                        const int pick = nearest[1] >= 0 ? nearest[static_cast<std::size_t> (rng.below (2))] : nearest[0];
                        note = pick >= 0 ? pick : root;
                        velocity = 88;
                    }
                }
                break;
        }
        velocity += rng.below (5) - 2;
        notes.push_back (clampedNote (t, hits[i].length, note, velocity, length));
        previous = note;
        ++sinceChange;
    }
    return toLine (std::move (notes), length);
}

// --- Variaciones -------------------------------------------------------------------------------------------------------

Line varyMelody (const Line& source, const Progression& progression, const Key& key, const VariationOptions& options)
{
    if (options.amount <= 0.0 || source.notes.empty() || source.length <= 0)
        return source;
    const int length = source.length;
    const auto range = sanitize (options.low, options.high);
    const double amount = std::min (1.0, options.amount);
    SeededRandom rng (options.seed, 3);

    // Cuánto se varía cada compás de la frase: el primero poco (es el que hace reconocible la melodía), el último más.
    constexpr std::array<double, 4> roleFactor { 0.3, 0.8, 0.6, 1.0 };
    const int bars = (length + 15) / 16;
    const auto strongAt = [&] (int t) { return t % 8 == 0 || chordChangesAt (progression, t); };

    struct Varied
    {
        LineNote note;
        bool changed = false;
    };
    std::vector<Varied> out;
    out.reserve (source.notes.size() * 2);
    const std::vector<LineNote> in (source.notes.begin(), source.notes.end());

    enum Op { neighbor, anticipate, split, merge, flip, passing };
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        LineNote n = in[i];
        const int bar = n.start / 16;
        const int role = bars > 1 && bar == bars - 1 ? 3 : bar % 4;
        if (! rng.chance (amount * roleFactor[static_cast<std::size_t> (role)]))
        {
            out.push_back ({ n, false });
            continue;
        }
        const LineNote* next = i + 1 < in.size() ? &in[i + 1] : nullptr;
        Varied* prev = out.empty() ? nullptr : &out.back();
        const auto h = harmonyAt (progression, key, n.start);
        const bool strong = strongAt (n.start);

        // Las transformaciones posibles para esta nota, con su peso.
        std::array<double, 6> weights {};
        weights[neighbor] = 3.0;
        if (n.start % 4 == 0 && n.start > 0 && (prev == nullptr || prev->note.start + prev->note.length <= n.start - 1 || prev->note.length >= 2))
            weights[anticipate] = 2.0; // adelantarla una semicorchea: el "empuje" del reggaetón
        if (n.length >= 2)
            weights[split] = 2.0;      // partirla en dos: más movimiento
        if (next != nullptr && next->start == n.start + n.length && ! strongAt (next->start))
            weights[merge] = 1.5;      // unirla con la siguiente: más aire
        if (prev != nullptr)
            weights[flip] = 1.0;       // el intervalo desde la anterior, hacia el otro lado
        if (next != nullptr && n.length >= 2 && std::abs (next->note - n.note) >= 3 && std::abs (next->note - n.note) <= 4)
            weights[passing] = 1.5;    // una nota de paso que rellena la 3.ª hasta la siguiente

        const int dir = rng.chance (0.5) ? 1 : -1;
        switch (rng.weighted (weights))
        {
            case neighbor:
                n.note = strong && h.chordBits != 0 ? nearestInMask (h.chordBits, n.note, range, dir, true) : stepFrom (h.scale, n.note, dir);
                break;
            case anticipate:
                if (prev != nullptr && prev->note.start + prev->note.length > n.start - 1)
                {
                    prev->note.length = std::max (1, n.start - 1 - prev->note.start);
                    prev->changed = true;
                }
                n.start -= 1;
                n.length += 1;
                break;
            case split:
            {
                const int half = n.length / 2;
                LineNote first = n;
                first.length = half;
                out.push_back ({ first, true });
                n.start += half;
                n.length -= half;
                n.velocity = std::max (1, n.velocity - 8);
                if (rng.chance (0.5))
                    n.note = stepFrom (h.scale, n.note, dir);
                break;
            }
            case merge:
                n.length += next->length;
                ++i;
                break;
            case flip:
            {
                const int target = prev->note.note - (n.note - prev->note.note);
                n.note = nearestInMask (strong && h.chordBits != 0 ? h.chordBits : h.scale, target, range, dir);
                break;
            }
            case passing:
            {
                const int half = n.length / 2;
                LineNote first = n;
                first.length = half;
                out.push_back ({ first, false });
                n.start += half;
                n.length -= half;
                n.note = stepFrom (h.scale, n.note, next->note > n.note ? 1 : -1);
                n.velocity = std::max (1, n.velocity - 10);
                break;
            }
            default:
                break;
        }
        out.push_back ({ n, true });
    }

    // Lo que cambió vuelve al rango y, si cae en un apoyo, a una nota del acorde. Lo que no se tocó queda igual (en una
    // melodía capturada puede haber una apoyatura a propósito).
    std::vector<LineNote> notes;
    notes.reserve (out.size());
    for (auto& v : out)
    {
        auto n = v.note;
        if (v.changed)
        {
            while (n.note < range.low)
                n.note += 12;
            while (n.note > range.high)
                n.note -= 12;
            const auto h = harmonyAt (progression, key, n.start);
            if (strongAt (n.start) && h.chordBits != 0 && ! inMask (h.chordBits, n.note))
                n.note = nearestInMask (h.chordBits, n.note, range);
        }
        notes.push_back (clampedNote (n.start, n.length, n.note, n.velocity, length));
    }
    return toLine (std::move (notes), length);
}

std::vector<NoteEvent> renderLine (const Line& line, int loops)
{
    std::vector<NoteEvent> events;
    if (line.length <= 0)
        return events;
    for (int loop = 0; loop < std::max (1, loops); ++loop)
        for (const auto& n : line.notes)
            events.push_back ({ n.note, static_cast<double> (loop * line.length + n.start) / sixteenthsPerQuarter,
                                static_cast<double> (n.length) / sixteenthsPerQuarter, n.velocity });
    return events;
}

} // namespace deepwater::theory
