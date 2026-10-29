#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "deepwater/theory/Harmony.h"
#include "deepwater/theory/Progression.h"
#include "deepwater/theory/Voicing.h"
#include "midi/NoteTracker.h"

namespace tidechart::midi
{

// Qué tecla dispara qué acorde (Fase 3).
//   whiteKeys:  las teclas blancas son los grados, en cualquier tonalidad: C = I, D = ii, E = iii... (en La menor, C = i
//               = Am). Se toca en cualquier tonalidad sin saber dónde caen sus notas; las negras no hacen nada.
//   scaleNotes: cada nota de la escala dispara el acorde de su grado con la fundamental en esa misma tecla (en La menor,
//               A = Am, C = C). Lo de fuera de la escala no hace nada.
enum class KeyMapping
{
    whiteKeys,
    scaleNotes,
};

// Qué hacen las teclas (Fase 4).
//   keys:        cada tecla dispara su acorde (Fase 3), con el ritmo y el voicing elegidos.
//   progression: cualquier tecla abajo hace sonar la PROGRESIÓN, sincronizada con el host: en cada compás suena el paso
//                que cae ahí. La octava de la tecla elige el registro (C5 = el de la paleta) y su velocidad, la fuerza.
enum class PlayMode
{
    keys,
    progression,
};

// Lo que la etapa MIDI necesita saber para convertir teclas en acordes. Lo arma el procesador en cada bloque con los
// parámetros; enabled = false es el passthrough de la Fase 1 (el bypass).
struct ChordSettings
{
    bool enabled = false;
    deepwater::theory::Key key { deepwater::theory::notes::A, deepwater::theory::Mode::aeolian };
    deepwater::theory::ChordSize size = deepwater::theory::ChordSize::triad;
    KeyMapping mapping = KeyMapping::whiteKeys;
    bool bass = false; // además, la fundamental una octava abajo
    // Fase 4.
    PlayMode playMode = PlayMode::keys;
    deepwater::theory::VoicingStyle voicing = deepwater::theory::VoicingStyle::rootPosition;
    int rhythm = 0; // índice de deepwater::theory::rhythmPatterns (0 = sostenido)
    // Fase 6: qué capas suenan en el modo progresión (en el modo teclas, siempre los acordes y nada más).
    bool playChords = true;
    bool playBass = false;
    bool playMelody = false;

    bool operator== (const ChordSettings&) const = default;
};

// Un acorde listo para sonar: su grado (0..6, o −1 si no es un acorde de la escala: un prestado), el acorde, las teclas
// MIDI y de dónde salieron (para poder darle otro voicing).
struct TriggeredChord
{
    int degree = 0;
    deepwater::theory::Chord chord;
    deepwater::theory::StaticVector<int, 9> notes;
    int rootNote = 0;     // la fundamental en estado fundamental
    int tonicNote = 0;    // la tónica del registro (el centro de la conducción de voces)
    bool hasBass = false; // notes[0] es el bajo (la fundamental una octava abajo)
};

// El grado de una tecla blanca (C = 0 ... B = 6), o −1 si es negra.
[[nodiscard]] constexpr int whiteKeyDegree (int note) noexcept
{
    constexpr std::array<int, 12> degrees { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
    return degrees[static_cast<std::size_t> (((note % 12) + 12) % 12)];
}

// El acorde del grado 'degree' (0..6) con la fundamental en la tecla 'rootNote'; con bajo, además la fundamental una
// octava abajo. Las notas fuera de 0..127 se descartan.
[[nodiscard]] constexpr std::optional<TriggeredChord> chordAtRoot (int degree, int rootNote, const ChordSettings& settings) noexcept
{
    const auto chord = deepwater::theory::diatonicChord (settings.key, degree, settings.size);
    if (! chord)
        return std::nullopt;

    TriggeredChord result { degree, *chord, {}, rootNote,
                            rootNote - deepwater::theory::modeInfo (settings.key.mode).semitones[static_cast<std::size_t> (degree)], false };
    if (settings.bass && rootNote - 12 >= 0)
    {
        result.notes.push_back (rootNote - 12);
        result.hasBass = true;
    }
    for (const int note : chord->midiNotes (rootNote))
        result.notes.push_back (note);
    if (result.notes.empty())
        return std::nullopt;
    return result;
}

// El acorde del grado 'degree' con la tónica cerca del Do de 'octaveC' (una tecla C): la tónica queda entre F♯ de la
// octava de abajo y F de la misma (en La menor, C5 → A4), así los 7 grados suben desde ahí sin irse lejos de la tecla
// pulsada. Es la octava que usan las teclas blancas y la previsualización de la paleta.
[[nodiscard]] constexpr std::optional<TriggeredChord> chordForDegree (int degree, int octaveC, const ChordSettings& settings) noexcept
{
    const int tonicNote = deepwater::theory::tonicNoteNear (settings.key, octaveC);
    return chordAtRoot (degree, tonicNote + deepwater::theory::modeInfo (settings.key.mode).semitones[static_cast<std::size_t> (degree)], settings);
}

// El grado de la escala (0..6) de un acorde si es un acorde de la tonalidad, o −1 (un prestado). Sirve para encender
// los pads de la paleta.
[[nodiscard]] constexpr int paletteDegree (const deepwater::theory::Chord& chord, const deepwater::theory::Key& key) noexcept
{
    return deepwater::theory::isDiatonic (chord, key) ? key.degreeOf (chord.root.pitchClass()) : -1;
}

// Cualquier acorde escrito como grado (también los prestados y las variantes: ♭VI, iv9, ♭IIsus2), en posición
// fundamental con la tónica cerca del Do de 'octaveC', como chordForDegree.
[[nodiscard]] constexpr std::optional<TriggeredChord> chordForNumeral (const deepwater::theory::RomanNumeral& numeral, int octaveC,
                                                                     const ChordSettings& settings) noexcept
{
    namespace th = deepwater::theory;
    const int tonicNote = th::tonicNoteNear (settings.key, octaveC);
    const int rootNote = th::rootNoteFor (numeral, tonicNote);
    const auto chord = th::chordFor (numeral, settings.key);
    const auto voiced = th::voiceStep (chord, rootNote, th::VoicingStyle::rootPosition, 0, nullptr, settings.bass && rootNote - 12 >= 0);
    TriggeredChord result { paletteDegree (chord, settings.key), chord, voiced.notes(), rootNote, tonicNote, voiced.bassNote >= 0 };
    if (result.notes.empty())
        return std::nullopt;
    return result;
}

// Las voces del acorde, sin el bajo.
[[nodiscard]] constexpr deepwater::theory::Voicing upperVoices (const TriggeredChord& chord) noexcept
{
    deepwater::theory::Voicing voices;
    for (std::size_t i = chord.hasBass ? 1 : 0; i < chord.notes.size(); ++i)
        voices.push_back (chord.notes[i]);
    return voices;
}

// El mismo acorde con el voicing de los ajustes, conduciendo las voces desde 'previous' (las voces del acorde
// anterior; vacío = el primero). Estado fundamental lo deja como está (la Fase 3). El bajo no cambia.
[[nodiscard]] constexpr TriggeredChord withVoicing (TriggeredChord chord, const ChordSettings& settings,
                                                    const deepwater::theory::Voicing& previous) noexcept
{
    namespace th = deepwater::theory;
    if (settings.voicing == th::VoicingStyle::rootPosition)
        return chord;
    const int bassNote = chord.hasBass ? chord.notes[0] : -1;
    const auto voices = th::voiceChord (chord.chord, chord.rootNote, settings.voicing, th::tonicCentre2 (chord.tonicNote),
                                        previous.empty() ? nullptr : &previous, bassNote);
    chord.notes.clear();
    if (bassNote >= 0)
        chord.notes.push_back (bassNote);
    for (const int note : voices)
        if (note >= 0 && note <= 127)
            chord.notes.push_back (note);
    return chord;
}

// La paleta pide qué hacer sonar con un solo número (un atómico entre la GUI y el audio): 0 = nada; 1..7 = el grado
// 0..6 de la escala con el tamaño de acorde de los ajustes; desde numeralCodeBase, cualquier grado escrito (un prestado,
// un paso de la progresión).
inline constexpr int numeralCodeBase = 1000;

[[nodiscard]] constexpr int previewCode (const deepwater::theory::RomanNumeral& numeral) noexcept
{
    const int degree = ((numeral.degree - 1) % 7 + 7) % 7;
    const int accidental = numeral.accidental < -2 ? 0 : (numeral.accidental > 2 ? 4 : numeral.accidental + 2);
    return numeralCodeBase + degree + 7 * accidental + 35 * static_cast<int> (numeral.type);
}

[[nodiscard]] constexpr deepwater::theory::RomanNumeral numeralFromCode (int code) noexcept
{
    const int value = code - numeralCodeBase;
    return { value % 7 + 1, (value / 7) % 5 - 2, static_cast<deepwater::theory::ChordTypeId> (value / 35) };
}

// El acorde que dispara una tecla con estos ajustes, o nada (negras con teclas blancas; notas fuera de la escala).
[[nodiscard]] constexpr std::optional<TriggeredChord> chordForKey (int note, const ChordSettings& settings) noexcept
{
    if (settings.mapping == KeyMapping::whiteKeys)
    {
        const int degree = whiteKeyDegree (note);
        return degree < 0 ? std::nullopt : chordForDegree (degree, note - note % 12, settings);
    }
    const int degree = settings.key.degreeOf (deepwater::theory::wrap12 (note));
    return degree < 0 ? std::nullopt : chordAtRoot (degree, note, settings);
}

// Lleva los acordes que suenan, cada uno colgado de lo que lo disparó (una tecla en un canal, o la paleta), y emite sus
// notas. Dos ideas:
//   - Cada acorde guarda SUS teclas MIDI. Al soltar se apagan esas, aunque mientras tanto cambien la tonalidad, el modo
//     o el tipo de acorde: soltar nunca depende de recalcular, así que cambiar un parámetro no cuelga nada.
//   - Dos acordes que comparten una nota (Am y C comparten C y E) la cuentan dos veces, pero el instrumento solo debe
//     ver UNA nota encendida: si recibiera dos note-on y luego un note-off, la apagaría con el otro acorde aún sonando.
//     Por eso se cuenta cuántos acordes usan cada nota (refs): el note-off sale cuando la suelta el último. Si la nota
//     ya sonaba, el acorde nuevo la vuelve a atacar (note-off + note-on en la misma muestra): cada acorde tiene su golpe.
// Tamaño fijo y sin reservar memoria: se usa en el hilo de audio.
class ChordTrigger
{
public:
    // Un "slot" por tecla de entrada (canal × nota), uno para la paleta de la GUI, uno para la progresión (Fase 4) y uno
    // por voz de cada línea (Fase 6: el bajo y la melodía, cada nota en su voz).
    static constexpr int previewSlot = numChannels * numNotes;
    static constexpr int progressionSlot = previewSlot + 1;
    static constexpr int lineCount = 2;
    static constexpr int lineVoices = 8;
    static constexpr int firstLineSlot = progressionSlot + 1;
    static constexpr int slotCount = firstLineSlot + lineCount * lineVoices;

    [[nodiscard]] static constexpr int lineSlot (int line, int voice) noexcept { return firstLineSlot + line * lineVoices + voice; }

    [[nodiscard]] static constexpr int keySlot (int channel, int note) noexcept { return (channel & 0x0f) * numNotes + (note & 0x7f); }

    // Hace sonar un acorde desde 'slot' (si ya tenía uno, primero lo suelta). emit (ShortMessage).
    template <typename Emit>
    void press (int slot, int channel, int velocity, const TriggeredChord& chord, Emit&& emit)
    {
        release (slot, emit);
        auto& held = slots[static_cast<std::size_t> (slot)];
        held.used = true;
        held.channel = static_cast<std::uint8_t> (channel & 0x0f);
        held.degree = static_cast<std::int8_t> (chord.degree);
        held.count = 0;
        const auto vel = static_cast<std::uint8_t> (velocity < 1 ? 1 : (velocity > 127 ? 127 : velocity));
        for (const int note : chord.notes)
        {
            auto& ref = refs[static_cast<std::size_t> (keySlot (channel, note))];
            if (ref > 0)
                emit (ShortMessage::noteOff (channel, note));
            emit (noteOn (channel, note, vel));
            if (ref < 255)
                ++ref;
            held.notes[held.count++] = static_cast<std::uint8_t> (note);
        }
        if (chord.degree >= 0 && chord.degree < 7)
            ++degreeCounts[static_cast<std::size_t> (chord.degree)];
        ++active;
    }

    // Suelta el acorde de 'slot' (si tiene uno): note-off de las notas que ya no usa ningún otro acorde.
    template <typename Emit>
    void release (int slot, Emit&& emit)
    {
        auto& held = slots[static_cast<std::size_t> (slot)];
        if (! held.used)
            return;
        for (int i = 0; i < held.count; ++i)
        {
            auto& ref = refs[static_cast<std::size_t> (keySlot (held.channel, held.notes[static_cast<std::size_t> (i)]))];
            if (ref > 0 && --ref == 0)
                emit (ShortMessage::noteOff (held.channel, held.notes[static_cast<std::size_t> (i)]));
        }
        if (held.degree >= 0 && held.degree < 7)
            --degreeCounts[static_cast<std::size_t> (held.degree)];
        --active;
        held = {};
    }

    template <typename Emit>
    void releaseAll (Emit&& emit)
    {
        for (int slot = 0; slot < slotCount && active > 0; ++slot)
            release (slot, emit);
    }

    // Olvida todo sin mandar nada: cuando la etapa ya apagó lo que suena por su cuenta (pánico, reinicio del audio).
    void clear() noexcept
    {
        slots.fill ({});
        refs.fill (0);
        degreeCounts.fill (0);
        active = 0;
    }

    [[nodiscard]] bool isHeld (int slot) const noexcept { return slots[static_cast<std::size_t> (slot)].used; }
    [[nodiscard]] int activeCount() const noexcept { return active; }
    // Qué grados suenan ahora (bit d = grado d), para encender la paleta.
    [[nodiscard]] std::uint8_t heldDegrees() const noexcept
    {
        std::uint8_t mask = 0;
        for (int d = 0; d < 7; ++d)
            if (degreeCounts[static_cast<std::size_t> (d)] > 0)
                mask = static_cast<std::uint8_t> (mask | (1u << d));
        return mask;
    }

private:
    [[nodiscard]] static ShortMessage noteOn (int channel, int note, std::uint8_t velocity) noexcept
    {
        return { static_cast<std::uint8_t> (0x90 | (channel & 0x0f)), static_cast<std::uint8_t> (note & 0x7f), velocity };
    }

    struct Held
    {
        bool used = false;       // el slot tiene un acorde sonando
        std::int8_t degree = -1; // su grado en la escala (−1 = un prestado: no enciende la paleta)
        std::uint8_t channel = 0;
        std::uint8_t count = 0;
        std::array<std::uint8_t, 9> notes {};
    };

    std::array<Held, slotCount> slots {};
    std::array<std::uint8_t, numChannels * numNotes> refs {};
    std::array<int, 7> degreeCounts {};
    int active = 0;
};

} // namespace tidechart::midi
