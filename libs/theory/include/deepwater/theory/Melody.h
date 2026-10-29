#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "deepwater/theory/Harmony.h"
#include "deepwater/theory/Progression.h"
#include "deepwater/theory/StaticVector.h"

namespace deepwater::theory
{

// --- Líneas: melodías y bajos que siguen la progresión (Fase 6 de Tidechart) -----------------------------------------
// Una línea es una lista de notas sobre la cuadrícula de semicorcheas, contadas desde el principio de la vuelta, que se
// repite cada 'length' semicorcheas desde el compás 1 (como la progresión: en un compás dado siempre suena lo mismo).
// Valores puros y de tamaño fijo: se copia tal cual al hilo de audio.
struct LineNote
{
    int start = 0;      // semicorchea donde empieza, desde el principio de la vuelta
    int length = 1;     // en semicorcheas (≥ 1)
    int note = 60;      // MIDI
    int velocity = 100; // 1..127 (con una tecla de velocidad 100, sale tal cual)

    constexpr bool operator== (const LineNote&) const = default;
};

inline constexpr int maxLineNotes = 1024; // 16 por compás en la vuelta más larga (64 compases)
inline constexpr int minLineSixteenths = 64; // una frase: 4 compases
inline constexpr int maxLineSixteenths = maxProgressionSteps * maxStepBeats * sixteenthsPerQuarter;

struct Line
{
    StaticVector<LineNote, maxLineNotes> notes; // ordenadas por inicio (y por nota)
    int length = 0;                             // semicorcheas de la vuelta; 0 = vacía

    constexpr bool operator== (const Line&) const = default;
};

// Cuánto dura la vuelta de una línea para esta progresión: la progresión repetida hasta tener al menos una frase de
// 4 compases (con i–♭II de un compás cada uno, la melodía dura 2 vueltas de la progresión: pregunta y respuesta). Así
// una melodía tiene sitio para repetir su motivo y cerrar, aunque la progresión sea corta.
[[nodiscard]] constexpr int lineLength (const Progression& progression) noexcept
{
    const int loop = progression.totalBeats() * sixteenthsPerQuarter;
    if (loop <= 0)
        return 0;
    int length = loop;
    while (length < minLineSixteenths)
        length += loop;
    return length;
}

// La semicorchea dentro de la vuelta (0..length−1) de una semicorchea absoluta, también negativa (pre-roll).
[[nodiscard]] constexpr long long linePosition (const Line& line, long long sixteenth) noexcept
{
    if (line.length <= 0)
        return -1;
    const long long p = sixteenth % line.length;
    return p < 0 ? p + line.length : p;
}

// El acorde que suena en una semicorchea (desde el compás 1); nullopt con la progresión vacía.
[[nodiscard]] constexpr std::optional<Chord> chordAt (const Progression& progression, const Key& key, long long sixteenth) noexcept
{
    const int step = progression.stepAt (sixteenth);
    if (step < 0)
        return std::nullopt;
    return chordFor (progression.steps[static_cast<std::size_t> (step)].numeral, key);
}

// ¿Empieza un paso de la progresión (un acorde nuevo) en esta semicorchea?
[[nodiscard]] constexpr bool chordChangesAt (const Progression& progression, long long sixteenth) noexcept
{
    const long long length = static_cast<long long> (progression.totalBeats()) * sixteenthsPerQuarter;
    if (length <= 0)
        return false;
    long long position = sixteenth % length;
    if (position < 0)
        position += length;
    for (int i = 0; i < static_cast<int> (progression.steps.size()); ++i)
        if (progression.stepStart (i) == position)
            return true;
    return false;
}

// --- La escala de cada acorde ------------------------------------------------------------------------------------------
// Sobre un acorde de la tonalidad, la melodía usa la escala de la tonalidad. Sobre un PRESTADO, la del modo del que se
// tomó: sobre ♭VI en Do mayor (A♭, de Do menor) la melodía canta en Do menor (E♭, A♭, B♭), si no chocaría con el acorde
// (un La natural contra el La♭ del acorde). Si ningún modo paralelo lo contiene (un acorde cromático), la escala de la
// tonalidad con cada tono del acorde en lugar de la nota de su misma letra.
[[nodiscard]] constexpr std::uint16_t scaleMask (const Key& key) noexcept
{
    std::uint16_t mask = 0;
    for (int i = 0; i < 7; ++i)
        mask = static_cast<std::uint16_t> (mask | (1u << key.degree (i).pitchClass()));
    return mask;
}

// El modo paralelo cuya escala se usa sobre el acorde (nullopt = la de la tonalidad, o cromático).
[[nodiscard]] constexpr std::optional<Mode> chordScaleSource (const Chord& chord, const Key& key) noexcept
{
    if (isDiatonic (chord, key))
        return std::nullopt;
    for (const Mode mode : parallelModeOrder (key))
        if (mode != key.mode && isDiatonic (chord, Key { key.tonic, mode }))
            return mode;
    return std::nullopt;
}

[[nodiscard]] constexpr std::uint16_t chordScaleMask (const Chord& chord, const Key& key) noexcept
{
    if (isDiatonic (chord, key))
        return scaleMask (key);
    if (const auto source = chordScaleSource (chord, key))
        return scaleMask (Key { key.tonic, *source });

    // Cromático: cada tono del acorde reemplaza la nota de la escala con su misma letra.
    std::uint16_t mask = scaleMask (key);
    for (const auto& tone : chord.tones())
        for (int i = 0; i < 7; ++i)
            if (key.degree (i).letter == tone.letter)
            {
                mask = static_cast<std::uint16_t> (mask & ~(1u << key.degree (i).pitchClass()));
                mask = static_cast<std::uint16_t> (mask | (1u << tone.pitchClass()));
            }
    return mask;
}

// El papel de una nota sobre un acorde: nota del ACORDE (el apoyo), de la ESCALA (de paso, de adorno, tensión) o
// CROMÁTICA (fuera de la escala de ese acorde: color o aproximación). Es lo que colorea las notas en la GUI.
enum class NoteRole
{
    chordTone,
    scaleTone,
    chromatic,
};

[[nodiscard]] constexpr NoteRole noteRole (int note, const Chord& chord, const Key& key) noexcept
{
    const auto bit = 1u << wrap12 (note);
    if ((chordMask (chord.root.pitchClass(), chord.type) & bit) != 0)
        return NoteRole::chordTone;
    return (chordScaleMask (chord, key) & bit) != 0 ? NoteRole::scaleTone : NoteRole::chromatic;
}

// --- Generadores ---------------------------------------------------------------------------------------------------------
// El orden de los estilos es parte del formato (se guarda en el proyecto por nombre, pero el índice también se lee):
// lo nuevo, al final.
enum class MelodyStyle : int
{
    hook,     // gancho de reggaetón: notas cortas y repetidas sobre el 3+3+2 del dembow, pocas notas distintas
    sung,     // cantada: notas largas en los tiempos, por grados conjuntos, legato
    arpeggio, // arpegio: las notas del acorde una tras otra en una cuadrícula fija (un pluck, un arpegiador)
    rnb,      // R&B: síncopas y anticipaciones, tensiones (9.ª, 11.ª, 7.ª) en los apoyos
};
inline constexpr int melodyStyleCount = 4;
inline constexpr std::array<std::string_view, melodyStyleCount> melodyStyleNames { "hook", "sung", "arpeggio", "rnb" };

enum class BassStyle : int
{
    root,    // la fundamental de cada acorde, larga (re-atacada en cada compás; más densidad = blancas o negras)
    dembow,  // el 3+3+2 del dembow: fundamental, fundamental y una 5.ª, una octava o una aproximación al siguiente
    octaves, // fundamental y su octava alternadas (disco, house)
    walking, // un paso por tiempo: notas del acorde y una aproximación cromática o diatónica antes de cada cambio
    pedal,   // pedal de tónica: el bajo no se mueve y los acordes cambian encima (tensión, oscuridad)
};
inline constexpr int bassStyleCount = 5;
inline constexpr std::array<std::string_view, bassStyleCount> bassStyleNames { "root", "dembow", "octaves", "walking", "pedal" };

// Rangos por defecto, con los nombres de FL (C5 = 60): la melodía de E5 a A6, por encima de los acordes (que viven
// alrededor de la tónica cerca de C5); el bajo de E2 (41 Hz) a D4 (147 Hz), el registro de un 808 o un sub.
struct MelodyOptions
{
    MelodyStyle style = MelodyStyle::hook;
    double density = 0.5; // 0..1: cuántas notas por compás
    int low = 64;         // nota más grave
    int high = 81;        // nota más aguda (si el rango es menor que una octava, se amplía a una)
    std::uint32_t seed = 1;

    bool operator== (const MelodyOptions&) const = default;
};

struct BassOptions
{
    BassStyle style = BassStyle::dembow;
    double density = 0.5;
    int low = 28;
    int high = 50;
    std::uint32_t seed = 1;

    bool operator== (const BassOptions&) const = default;
};

struct VariationOptions
{
    double amount = 0.3; // 0 = la misma línea; 1 = casi cada nota cambia (pero el primer compás de cada frase poco)
    std::uint32_t seed = 1;
    int low = 0, high = 127;

    bool operator== (const VariationOptions&) const = default;
};

// Generan una vuelta (lineLength de la progresión). Deterministas: misma progresión, tonalidad y opciones (semilla
// incluida) = mismas notas, en cualquier máquina (generador propio, sin std::uniform_*_distribution, que cambia entre
// compiladores). Reservan memoria: fuera del hilo de audio.
[[nodiscard]] Line generateMelody (const Progression& progression, const Key& key, const MelodyOptions& options);
[[nodiscard]] Line generateBass (const Progression& progression, const Key& key, const BassOptions& options);

// Una variación de una melodía existente (generada, capturada de lo que se tocó o fijada): cambia algunas notas y algo
// de ritmo, pero conserva lo que la hace reconocible (el primer compás de cada frase casi igual, las notas largas, el
// contorno) y vuelve a apoyar en notas del acorde los tiempos fuertes. Mantiene el largo de la vuelta de 'source'.
[[nodiscard]] Line varyMelody (const Line& source, const Progression& progression, const Key& key, const VariationOptions& options);

// La línea como notas en el tiempo (negras desde el principio), 'loops' vueltas seguidas: para el Piano Roll (Fase 7),
// el servidor MCP y los tests.
[[nodiscard]] std::vector<NoteEvent> renderLine (const Line& line, int loops = 1);

} // namespace deepwater::theory
