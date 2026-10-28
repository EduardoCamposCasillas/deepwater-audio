#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "deepwater/theory/Harmony.h"
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

// Lo que la etapa MIDI necesita saber para convertir teclas en acordes. Lo arma el procesador en cada bloque con los
// parámetros; enabled = false es el passthrough de la Fase 1 (el bypass).
struct ChordSettings
{
    bool enabled = false;
    deepwater::theory::Key key { deepwater::theory::notes::A, deepwater::theory::Mode::aeolian };
    deepwater::theory::ChordSize size = deepwater::theory::ChordSize::triad;
    KeyMapping mapping = KeyMapping::whiteKeys;
    bool bass = false; // además, la fundamental una octava abajo

    bool operator== (const ChordSettings&) const = default;
};

// Un acorde listo para sonar: su grado (0..6), el acorde y las teclas MIDI.
struct TriggeredChord
{
    int degree = 0;
    deepwater::theory::Chord chord;
    deepwater::theory::StaticVector<int, 9> notes;
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

    TriggeredChord result { degree, *chord, {} };
    if (settings.bass && rootNote - 12 >= 0)
        result.notes.push_back (rootNote - 12);
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
    const int tonicPc = settings.key.tonic.pitchClass();
    const int tonicNote = octaveC + tonicPc - (tonicPc >= 6 ? 12 : 0);
    return chordAtRoot (degree, tonicNote + deepwater::theory::modeInfo (settings.key.mode).semitones[static_cast<std::size_t> (degree)], settings);
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
    // Un "slot" por tecla de entrada (canal × nota) y uno más para la paleta de la GUI.
    static constexpr int previewSlot = numChannels * numNotes;
    static constexpr int slotCount = previewSlot + 1;

    [[nodiscard]] static constexpr int keySlot (int channel, int note) noexcept { return (channel & 0x0f) * numNotes + (note & 0x7f); }

    // Hace sonar un acorde desde 'slot' (si ya tenía uno, primero lo suelta). emit (ShortMessage).
    template <typename Emit>
    void press (int slot, int channel, int velocity, const TriggeredChord& chord, Emit&& emit)
    {
        release (slot, emit);
        auto& held = slots[static_cast<std::size_t> (slot)];
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
        ++degreeCounts[static_cast<std::size_t> (chord.degree)];
        ++active;
    }

    // Suelta el acorde de 'slot' (si tiene uno): note-off de las notas que ya no usa ningún otro acorde.
    template <typename Emit>
    void release (int slot, Emit&& emit)
    {
        auto& held = slots[static_cast<std::size_t> (slot)];
        if (held.degree < 0)
            return;
        for (int i = 0; i < held.count; ++i)
        {
            auto& ref = refs[static_cast<std::size_t> (keySlot (held.channel, held.notes[static_cast<std::size_t> (i)]))];
            if (ref > 0 && --ref == 0)
                emit (ShortMessage::noteOff (held.channel, held.notes[static_cast<std::size_t> (i)]));
        }
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

    [[nodiscard]] bool isHeld (int slot) const noexcept { return slots[static_cast<std::size_t> (slot)].degree >= 0; }
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
        std::int8_t degree = -1; // −1 = el slot no tiene acorde
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
