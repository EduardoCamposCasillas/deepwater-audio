#pragma once

#include <atomic>
#include <cstdint>

#include "midi/ChordTrigger.h"
#include "midi/NoteTracker.h"

namespace tidechart::midi
{

// La etapa MIDI de Tidechart, sin JUCE: lo que entra, lo que sale y la garantía de que nada queda colgado.
//
// Fase 1: todo lo que entra sale tal cual, en su misma muestra (passthrough). Es lo que hace en bypass.
// Fase 3: con los acordes activos (ChordSettings::enabled), cada tecla dispara un acorde de la tonalidad (ChordTrigger)
// y la tecla en sí no sale; lo que no es una nota (CC, pitch bend, sustain) sigue pasando igual. La paleta de la GUI
// también hace sonar acordes (requestPreview).
// TODO lo que sale pasa por emit(), que lo cuenta en el NoteTracker. Así, cuando algo corta el flujo normal, se sabe
// exactamente qué note-off faltan:
//   - requestRelease(): el host reinició el audio (prepareToPlay / releaseResources / reset) o se cargó un proyecto.
//     Al principio del bloque siguiente se apaga lo que suena.
//   - requestPanic(): el botón de pánico. Además de lo anterior, CC 123 (All Notes Off) en los 16 canales, por si el
//     receptor tiene notas colgadas que no salieron de aquí (de antes de cargar el plugin, o de otro programa).
// Las peticiones pueden venir de otro hilo (la GUI, el hilo de mensajes): son atómicos y el audio las recoge al
// empezar el bloque.
//
// Lo que NO se apaga aquí: las notas que solo pasan. Su note-off lo manda el host (FL los manda al parar, al saltar y
// al hacer loop) o el teclado al soltar la tecla; cortarlas al parar el transporte silenciaría una nota que se está
// tocando en vivo. Los acordes de una tecla siguen la misma regla: suenan mientras la tecla esté abajo, y el note-off
// de la tecla (el del host al parar, saltar o hacer loop) los apaga.
// Cambiar entre bypass y acordes apaga lo del otro modo en la muestra 0: al entrar en bypass, los acordes; al salir,
// las notas que pasaron tal cual (su note-off llegaría ya con los acordes activos, que no lo dejan pasar).
//
// Sink: sink (const std::uint8_t* data, int size, int sample), el que escribe en el MidiBuffer de salida.
class MidiStage
{
public:
    void requestRelease() noexcept { releasePending.store (true, std::memory_order_release); }
    void requestPanic() noexcept { panicPending.store (true, std::memory_order_release); }

    // Cualquier hilo: la paleta de la GUI pide que suene el acorde del grado 0..6 (−1 = soltar). Se atiende al principio
    // del bloque siguiente, en la octava del Do central (C5).
    void requestPreview (int degree) noexcept { previewRequest.store (degree, std::memory_order_release); }

    // Hilo de audio, antes de beginBlock: los ajustes de este bloque (los arma el procesador con los parámetros).
    void setChordSettings (const ChordSettings& newSettings) noexcept { pendingSettings = newSettings; }
    [[nodiscard]] const ChordSettings& getChordSettings() const noexcept { return settings; }

    // Al principio de cada bloque, antes de los eventos: atiende las peticiones pendientes en la muestra 0.
    template <typename Sink>
    void beginBlock (Sink&& sink)
    {
        const bool panic = panicPending.exchange (false, std::memory_order_acq_rel);
        const bool release = releasePending.exchange (false, std::memory_order_acq_rel);
        if (panic || release)
        {
            // Copia: emit() actualiza 'sounding' mientras se recorre.
            NoteTracker toRelease = sounding;
            const int sent = toRelease.releaseAll ([&] (ShortMessage off) { emit (off, 0, sink); });
            if (panic)
                for (int channel = 0; channel < numChannels; ++channel)
                    emit (ShortMessage::controller (channel, 123, 0), 0, sink);
            if (panic || sent > 0) // el host reinicia el audio a menudo sin nada sonando: eso no se cuenta
                ++releases;
            // Ya se apagó todo: los acordes que sonaban se olvidan (el note-off tardío de su tecla no hará nada).
            chords.clear();
            previewPlaying = -1;
        }

        // Bypass ↔ acordes: lo del modo que se deja se apaga.
        if (pendingSettings.enabled != settings.enabled)
        {
            if (settings.enabled)
                chords.releaseAll ([&] (ShortMessage off) { emit (off, 0, sink); });
            else
            {
                NoteTracker passed = sounding;
                passed.releaseAll ([&] (ShortMessage off) { emit (off, 0, sink); });
            }
            previewPlaying = -1;
        }
        settings = pendingSettings;

        // La paleta: suelta el acorde anterior y hace sonar el pedido (solo con los acordes activos).
        const int wanted = settings.enabled ? previewRequest.load (std::memory_order_acquire) : -1;
        if (wanted != previewPlaying)
        {
            auto out = [&] (ShortMessage m) { emit (m, 0, sink); };
            chords.release (ChordTrigger::previewSlot, out);
            if (wanted >= 0 && wanted < 7)
                if (const auto chord = chordForDegree (wanted, 60, settings))
                    chords.press (ChordTrigger::previewSlot, 0, previewVelocity, *chord, out);
            previewPlaying = wanted;
        }
    }

    // Un evento de entrada (bytes crudos, en su muestra del bloque). En bypass sale igual; con los acordes activos, una
    // tecla dispara su acorde y se apaga con el note-off de la misma tecla (una tecla que no dispara nada no sale).
    template <typename Sink>
    void process (const std::uint8_t* data, int size, int sample, Sink&& sink)
    {
        const auto message = readShortMessage (data, size);
        if (! settings.enabled || ! (message.isNoteOn() || message.isNoteOff()))
        {
            emitRaw (data, size, sample, sink);
            return;
        }

        auto out = [&] (ShortMessage m) { emit (m, sample, sink); };
        const int slot = ChordTrigger::keySlot (message.channel(), message.data1);
        if (message.isNoteOff())
            chords.release (slot, out);
        else if (const auto chord = chordForKey (message.data1, settings))
            chords.press (slot, message.channel(), message.data2, *chord, out);
    }

    // Lo que suena en el receptor según lo que salió de aquí.
    [[nodiscard]] const NoteTracker& getSounding() const noexcept { return sounding; }
    // Cuántas veces se apagó algo que sonaba, más los pánicos (para la GUI y los tests).
    [[nodiscard]] std::uint32_t releaseCount() const noexcept { return releases; }
    // Los acordes que suenan (qué grados, para encender la paleta).
    [[nodiscard]] const ChordTrigger& getChords() const noexcept { return chords; }

    static constexpr int previewVelocity = 100;

private:
    template <typename Sink>
    void emit (ShortMessage message, int sample, Sink& sink)
    {
        const std::uint8_t bytes[3] { message.status, message.data1, message.data2 };
        emitRaw (bytes, 3, sample, sink);
    }

    template <typename Sink>
    void emitRaw (const std::uint8_t* data, int size, int sample, Sink& sink)
    {
        sounding.observe (readShortMessage (data, size));
        sink (data, size, sample);
    }

    NoteTracker sounding;
    std::atomic<bool> releasePending { false };
    std::atomic<bool> panicPending { false };
    std::uint32_t releases = 0;

    ChordTrigger chords;
    ChordSettings settings;        // los del bloque en curso
    ChordSettings pendingSettings; // los del bloque siguiente
    std::atomic<int> previewRequest { -1 };
    int previewPlaying = -1;
};

} // namespace tidechart::midi
