#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <span>

#include "deepwater/theory/Progression.h"
#include "deepwater/theory/Scale.h"

namespace deepwater::theory
{

// --- Detección de tonalidad (Fase 5 de Tidechart) ---------------------------------------------------------------------
// ¿En qué tonalidad está lo que suena? Dos preguntas en una:
//   1. QUÉ NOTAS: cuánto sonó cada clase de altura (el histograma, pesado por duración). Se compara con el "perfil" de
//      cada tonalidad candidata —cuánto se espera que suene cada nota en ella: mucho la tónica, bastante la 5.ª y la
//      3.ª, algo el resto de la escala, poco lo que está fuera— con la correlación de Pearson (1 = mismo dibujo, 0 =
//      nada que ver). Es la idea de Krumhansl y Schmuckler; los perfiles salen de los de Krumhansl y Kessler (1982),
//      simplificados a la media por función de la nota para poder construir el de cualquier modo.
//   2. CUÁL ES LA CASA: La menor y Do mayor tienen las mismas notas, y una progresión como i–♭VI–♭III–♭VII (Am F C G)
//      hace sonar más el Do que el La. Lo que decide al oído es dónde se apoya la música: el bajo al empezar cada
//      frase. Cada inicio de frase es un "voto" para la clase de altura de su nota más grave; la parte de los votos
//      que se lleva la tónica de una candidata suma hasta tonicCueWeight a su puntuación.
// Todo en valores fijos y sin reservar memoria (rankKeys corre donde sea; en Tidechart, en la GUI).

// Lo escuchado. Las unidades de pitchWeight dan igual (segundos en vivo, negras al analizar notas): la correlación no
// cambia al escalar, y los votos cuentan por su proporción.
struct KeyEvidence
{
    std::array<double, 12> pitchWeight {}; // cuánto sonó cada clase de altura; la nota más grave suma bassWeight más
    std::array<double, 12> tonicCue {};    // votos de "casa": la nota más grave al empezar cada frase

    // Cuánto pesa la nota más grave además de sí misma: el bajo dice la fundamental, así que cuenta el doble.
    static constexpr double bassWeight = 1.0;

    [[nodiscard]] constexpr double totalWeight() const noexcept
    {
        double total = 0.0;
        for (double w : pitchWeight)
            total += w;
        return total;
    }

    [[nodiscard]] constexpr double totalCues() const noexcept
    {
        double total = 0.0;
        for (double c : tonicCue)
            total += c;
        return total;
    }

    // Cuántas clases de altura sonaron algo (más del 1 % del total): con 1 o 2 no hay tonalidad que decidir.
    [[nodiscard]] constexpr int distinctPitchClasses() const noexcept
    {
        const double total = totalWeight();
        int count = 0;
        for (double w : pitchWeight)
            if (total > 0.0 && w > 0.01 * total)
                ++count;
        return count;
    }

    constexpr bool operator== (const KeyEvidence&) const = default;
};

// Cuánto suma a la puntuación que todos los votos de "casa" caigan en la tónica. Lo bastante para decidir entre
// relativas (sus correlaciones se separan ~0.1), no tanto como para imponer una tónica que las notas contradicen.
inline constexpr double tonicCueWeight = 0.25;

// Una frase = 8 compases: con acordes de 1 o de 2 compases, el compás 1 de cada frase cae en el primer acorde del loop.
inline constexpr int phraseBars = 8;

// Ventana alrededor del inicio de una frase en la que una nota que empieza cuenta como "la de la frase" (en negras):
// un poco antes (una nota tocada a mano, adelantada) y un poco después (tarde, o un bajo que entra en la semicorchea).
inline constexpr double cueWindowBefore = 0.125;
inline constexpr double cueWindowAfter = 0.25;

// Si una nota que empieza en 'beat' (negras desde el principio) cae en la ventana del inicio de una frase, cuál
// (0 = la del principio); si no, −1.
[[nodiscard]] inline long long phraseCueIndex (double beat, double beatsPerPhrase) noexcept
{
    if (! (beatsPerPhrase > 0.0) || ! std::isfinite (beat))
        return -1;
    const double phrase = std::round (beat / beatsPerPhrase);
    const double offset = beat - phrase * beatsPerPhrase;
    if (phrase < 0.0 || offset < -cueWindowBefore || offset >= cueWindowAfter)
        return -1;
    return static_cast<long long> (phrase);
}

// El perfil de un modo, respecto de su tónica (índice 0 = la tónica, 7 = una 5.ª justa arriba...). Medias por función
// de los perfiles mayor y menor de Krumhansl y Kessler: tónica 6.34, 5.ª justa 4.97, 3.ª 4.88, el resto de la escala
// 3.56 y lo de fuera 2.55. (En el locrio la 5.ª es disminuida: pesa como una nota más de la escala.)
[[nodiscard]] constexpr std::array<double, 12> keyProfile (Mode mode) noexcept
{
    const auto& semitones = modeInfo (mode).semitones;
    std::array<double, 12> profile {};
    profile.fill (2.55);
    for (int s : semitones)
        profile[static_cast<std::size_t> (s)] = 3.56;
    profile[static_cast<std::size_t> (semitones[2])] = 4.88;
    if (semitones[4] == 7)
        profile[7] = 4.97;
    profile[0] = 6.34;
    return profile;
}

// Correlación de Pearson entre lo escuchado y un perfil rotado a la tónica 'tonic': ¿suben y bajan juntos? Si lo
// escuchado no varía (nada, o las 12 notas igual), 0.
[[nodiscard]] inline double profileCorrelation (const std::array<double, 12>& heard, const std::array<double, 12>& profile, PitchClass tonic) noexcept
{
    double meanHeard = 0.0, meanProfile = 0.0;
    for (std::size_t i = 0; i < 12; ++i)
    {
        meanHeard += heard[i];
        meanProfile += profile[i];
    }
    meanHeard /= 12.0;
    meanProfile /= 12.0;

    double cross = 0.0, varHeard = 0.0, varProfile = 0.0;
    for (int pc = 0; pc < 12; ++pc)
    {
        const double h = heard[static_cast<std::size_t> (pc)] - meanHeard;
        const double p = profile[static_cast<std::size_t> (wrap12 (pc - tonic))] - meanProfile;
        cross += h * p;
        varHeard += h * h;
        varProfile += p * p;
    }
    if (varHeard <= 1.0e-12 * (1.0 + meanHeard * meanHeard) || varProfile <= 0.0)
        return 0.0;
    return cross / std::sqrt (varHeard * varProfile);
}

// Los modos de más común a menos: desempata candidatas con la misma puntuación (las 7 rotaciones de una escala sin
// pistas de "casa" pueden empatar). Primero lo que más se oye en el pop y el reggaetón.
inline constexpr std::array<Mode, modeCount> modesByCommonness { Mode::aeolian, Mode::ionian, Mode::dorian, Mode::phrygian,
                                                                  Mode::mixolydian, Mode::harmonicMinor, Mode::lydian,
                                                                  Mode::phrygianDominant, Mode::melodicMinor, Mode::locrian };

struct KeyCandidate
{
    Key key;
    double score = 0.0; // fit + cue: lo que ordena
    double fit = 0.0;   // la correlación con el perfil (−1..1)
    double cue = 0.0;   // la parte de los votos de "casa" que cae en la tónica (0..1)
};

inline constexpr int keyCandidateCount = 12 * modeCount;
using KeyRanking = std::array<KeyCandidate, keyCandidateCount>;

// Las 120 tonalidades (12 tónicas × 10 modos) de mejor a peor. Empates: el modo más común y luego la tónica más baja.
// Cada tonalidad sale escrita como se escribe habitualmente (standardKey: Si♭ mayor, Sol♯ menor).
[[nodiscard]] inline KeyRanking rankKeys (const KeyEvidence& evidence) noexcept
{
    KeyRanking ranking {};
    const double cues = evidence.totalCues();
    std::size_t index = 0;
    for (const Mode mode : modesByCommonness)
    {
        const auto profile = keyProfile (mode);
        for (int tonic = 0; tonic < 12; ++tonic)
        {
            auto& candidate = ranking[index++];
            candidate.key = standardKey (tonic, mode);
            candidate.fit = profileCorrelation (evidence.pitchWeight, profile, tonic);
            candidate.cue = cues > 0.0 ? evidence.tonicCue[static_cast<std::size_t> (tonic)] / cues : 0.0;
            candidate.score = candidate.fit + tonicCueWeight * candidate.cue;
        }
    }
    // Inserción estable (120 elementos): conserva el orden de desempate en el que se generaron.
    for (std::size_t i = 1; i < ranking.size(); ++i)
    {
        const auto item = ranking[i];
        std::size_t j = i;
        for (; j > 0 && ranking[j - 1].score < item.score - 1.0e-9; --j)
            ranking[j] = ranking[j - 1];
        ranking[j] = item;
    }
    return ranking;
}

// ¿Las dos tonalidades usan las mismas 7 notas? (La menor y Do mayor sí: relativas; La menor y La dórico no.)
[[nodiscard]] constexpr bool sameNotes (const Key& a, const Key& b) noexcept
{
    for (int pc = 0; pc < 12; ++pc)
        if (a.contains (pc) != b.contains (pc))
            return false;
    return true;
}

// Qué tan segura es la mejor candidata, por cuánto le gana a la segunda.
enum class KeyConfidence
{
    none,     // no hay con qué decidir (menos de 3 notas distintas)
    doubtful, // la segunda está casi igual: escuchar más o decidir a oído
    likely,
    clear,
};

// Dos modos de la misma tónica que difieren en una nota (La menor y La frigio: Si o Si♭) se separan ~0.04 cuando esa
// nota suena; las relativas, con los votos de "casa", más de 0.1.
inline constexpr double likelyMargin = 0.015;
inline constexpr double clearMargin = 0.035;

[[nodiscard]] constexpr KeyConfidence keyConfidence (const KeyEvidence& evidence, const KeyRanking& ranking) noexcept
{
    if (evidence.distinctPitchClasses() < 3)
        return KeyConfidence::none;
    const double margin = ranking[0].score - ranking[1].score;
    if (margin >= clearMargin)
        return KeyConfidence::clear;
    return margin >= likelyMargin ? KeyConfidence::likely : KeyConfidence::doubtful;
}

// La evidencia de una lista de notas (lo que hay en un Piano Roll, una progresión renderizada, lo que mande el servidor
// MCP). Lo mismo que escucha Tidechart en vivo con el host reproduciendo desde el principio: duración de cada nota, el
// bajo de cada tramo cuenta bassWeight más, y un voto para la nota más grave que empieza cerca del inicio de cada
// frase (cada 'beatsPerPhrase' negras desde el 0; por defecto 8 compases de 4/4). Reserva memoria: fuera del audio.
[[nodiscard]] KeyEvidence keyEvidence (std::span<const NoteEvent> notes, double beatsPerPhrase = phraseBars * 4.0);

} // namespace deepwater::theory
