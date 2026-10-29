#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "deepwater/theory/Harmony.h"
#include "deepwater/theory/Rhythm.h"
#include "deepwater/theory/StaticVector.h"
#include "deepwater/theory/Voicing.h"

namespace deepwater::theory
{

// --- Progresiones (Fase 4 de Tidechart) -------------------------------------------------------------------------------
// Una progresión es una lista de pasos: un acorde escrito como grado (número romano) y cuántos tiempos (negras) dura.
// Como grado y no como acorde: "i–♭VI–♭III–♭VII" es la misma progresión en cualquier tonalidad, y cambiar la tónica o el
// modo la transporta entera. El grado también guarda el tipo (♭VImaj7, iv9, ♭IIsus2...) y la alteración: así entran los
// acordes prestados de otros modos (♭VI, ♭VII y iv en mayor; V y IV en menor).
//
// Se repite sin fin desde el principio de la canción (el compás 1): en un compás dado siempre suena el mismo paso, como
// una pista más de la canción. Valores puros, sin reservar memoria: se copia tal cual al hilo de audio.
struct ProgressionStep
{
    RomanNumeral numeral;
    int beats = 4; // 1..maxStepBeats tiempos (negras)

    constexpr bool operator== (const ProgressionStep&) const = default;
};

inline constexpr int maxProgressionSteps = 16; // 8 hasta la Fase 8 de Tidechart
inline constexpr int maxStepBeats = 16;

struct Progression
{
    StaticVector<ProgressionStep, maxProgressionSteps> steps;

    [[nodiscard]] static constexpr int clampBeats (int beats) noexcept { return beats < 1 ? 1 : (beats > maxStepBeats ? maxStepBeats : beats); }

    [[nodiscard]] constexpr int totalBeats() const noexcept
    {
        int total = 0;
        for (const auto& step : steps)
            total += clampBeats (step.beats);
        return total;
    }

    // Dónde empieza el paso 'index' dentro de la progresión, en semicorcheas.
    [[nodiscard]] constexpr int stepStart (int index) const noexcept
    {
        int start = 0;
        for (int i = 0; i < index && i < static_cast<int> (steps.size()); ++i)
            start += clampBeats (steps[static_cast<std::size_t> (i)].beats) * sixteenthsPerQuarter;
        return start;
    }

    // El paso que suena en la semicorchea absoluta 'sixteenth' (desde el principio de la canción, repitiéndose), o −1
    // si la progresión está vacía.
    [[nodiscard]] constexpr int stepAt (long long sixteenth) const noexcept
    {
        const long long length = static_cast<long long> (totalBeats()) * sixteenthsPerQuarter;
        if (length <= 0)
            return -1;
        long long position = sixteenth % length;
        if (position < 0)
            position += length;
        for (int i = 0; i < static_cast<int> (steps.size()); ++i)
        {
            position -= clampBeats (steps[static_cast<std::size_t> (i)].beats) * sixteenthsPerQuarter;
            if (position < 0)
                return i;
        }
        return static_cast<int> (steps.size()) - 1;
    }

    constexpr bool operator== (const Progression&) const = default;
};

// La tónica cerca del Do 'octaveC' (una tecla C): entre F♯ de la octava de abajo y F de la misma (en La menor, C5 → A4),
// así los acordes suben desde ahí sin irse lejos. Es la regla de la paleta y de las teclas blancas (Fase 3).
[[nodiscard]] constexpr int tonicNoteNear (const Key& key, int octaveC) noexcept
{
    const int tonicPc = key.tonic.pitchClass();
    return octaveC + tonicPc - (tonicPc >= 6 ? 12 : 0);
}

// La tecla de la fundamental de un grado en estado fundamental, con la tónica en 'tonicNote': los semitonos del grado en
// la escala MAYOR más la alteración (♭VI con la tónica en A4 = F5).
[[nodiscard]] constexpr int rootNoteFor (const RomanNumeral& numeral, int tonicNote) noexcept
{
    return tonicNote + majorScaleSemitones[static_cast<std::size_t> ((numeral.degree + 6) % 7)] + numeral.accidental;
}

// El centro (grave + agudo) de la tríada de tónica en estado fundamental: el registro alrededor del cual se mueven las
// voces de toda la progresión.
[[nodiscard]] constexpr int tonicCentre2 (int tonicNote) noexcept { return 2 * tonicNote + 7; }

// Un acorde de la progresión listo para sonar: el acorde, dónde está su fundamental, las voces y el bajo.
struct VoicedChord
{
    Chord chord;
    int rootNote = 0;
    Voicing upper;      // las voces, de grave a agudo
    int bassNote = -1;  // la fundamental una octava por debajo de rootNote, o −1 sin bajo

    // Bajo + voces, de grave a agudo, solo lo que cae en 0..127.
    [[nodiscard]] constexpr StaticVector<int, 9> notes() const noexcept
    {
        StaticVector<int, 9> result;
        if (bassNote >= 0 && bassNote <= 127)
            result.push_back (bassNote);
        for (const int note : upper)
            if (note >= 0 && note <= 127)
                result.push_back (note);
        return result;
    }
};

// Un acorde con su voicing. Con bajo, la fundamental va una octava debajo de rootNote y ninguna voz baja hasta ahí.
[[nodiscard]] constexpr VoicedChord voiceStep (const Chord& chord, int rootNote, VoicingStyle style, int centre2, const Voicing* previous,
                                               bool bass) noexcept
{
    VoicedChord result { chord, rootNote, {}, bass ? rootNote - 12 : -1 };
    result.upper = voiceChord (chord, rootNote, style, centre2, previous, result.bassNote);
    return result;
}

// Toda la progresión con su voicing, con la tónica en 'tonicNote'. Con conducción de voces cada acorde se voicea desde
// el anterior, y la progresión se recorre dos veces: la segunda empieza desde el último acorde de la primera, así la
// vuelta del final al principio también se mueve poco (es un loop). Determinista: mismos datos, mismas notas.
[[nodiscard]] constexpr std::array<VoicedChord, maxProgressionSteps> voiceProgression (const Progression& progression, const Key& key,
                                                                                      VoicingStyle style, int tonicNote, bool bass) noexcept
{
    std::array<VoicedChord, maxProgressionSteps> result {};
    const int count = static_cast<int> (progression.steps.size());
    const int centre2 = tonicCentre2 (tonicNote);
    const int passes = style == VoicingStyle::rootPosition || count < 2 ? 1 : 2;
    Voicing previous;
    for (int pass = 0; pass < passes; ++pass)
        for (int i = 0; i < count; ++i)
        {
            const auto& numeral = progression.steps[static_cast<std::size_t> (i)].numeral;
            auto& voiced = result[static_cast<std::size_t> (i)];
            voiced = voiceStep (chordFor (numeral, key), rootNoteFor (numeral, tonicNote), style, centre2,
                                previous.empty() ? nullptr : &previous, bass);
            previous = voiced.upper;
        }
    return result;
}

// --- La progresión como notas en el tiempo (para escribirla en el Piano Roll y para el servidor MCP) ------------------
// Lo mismo que toca Tidechart en vivo, una vez por vuelta: en cada semicorchea suena el paso que cae ahí, si el patrón
// tiene un golpe en ella. Un acorde se ataca al empezar un golpe, al cambiar de paso dentro de un golpe y al empezar la
// progresión; se suelta al terminar el golpe. Sostenido: un acorde por paso.
struct NoteEvent
{
    int note = 60;
    double start = 0.0;  // en negras desde el principio
    double length = 1.0; // en negras
    int velocity = 100;

    bool operator== (const NoteEvent&) const = default;
};

struct RenderOptions
{
    VoicingStyle style = VoicingStyle::rootPosition;
    int tonicNote = 57;  // A4: la tónica de La menor cerca del Do central (tonicNoteNear (key, 60))
    bool bass = false;
    int rhythm = 0;      // índice de rhythmPatterns
    int velocity = 100;
    int loops = 1;
};

// La velocidad de un golpe: la de la tecla por el acento, en 1..127.
[[nodiscard]] constexpr int accentedVelocity (int velocity, int accent) noexcept
{
    const int v = (velocity * accent + 50) / 100;
    return v < 1 ? 1 : (v > 127 ? 127 : v);
}

// Reserva memoria (std::vector): fuera del hilo de audio.
[[nodiscard]] std::vector<NoteEvent> renderProgression (const Progression& progression, const Key& key, const RenderOptions& options);

} // namespace deepwater::theory
