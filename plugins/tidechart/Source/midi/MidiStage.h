#pragma once

#include <atomic>
#include <cstdint>

#include "midi/NoteTracker.h"

namespace tidechart::midi
{

// La etapa MIDI de Tidechart, sin JUCE: lo que entra, lo que sale y la garantía de que nada queda colgado.
//
// Fase 1: todo lo que entra sale tal cual, en su misma muestra (passthrough). Lo importante es el esqueleto que usarán
// las fases siguientes (acordes, patrones): TODO lo que sale pasa por emit(), que lo cuenta en el NoteTracker. Así,
// cuando algo corta el flujo normal, se sabe exactamente qué note-off faltan:
//   - requestRelease(): el host reinició el audio (prepareToPlay / releaseResources / reset) o se cargó un proyecto.
//     Al principio del bloque siguiente se apaga lo que suena.
//   - requestPanic(): el botón de pánico. Además de lo anterior, CC 123 (All Notes Off) en los 16 canales, por si el
//     receptor tiene notas colgadas que no salieron de aquí (de antes de cargar el plugin, o de otro programa).
// Las peticiones pueden venir de otro hilo (la GUI, el hilo de mensajes): son atómicos y el audio las recoge al
// empezar el bloque.
//
// Lo que NO se apaga aquí: las notas que solo pasan. Su note-off lo manda el host (FL los manda al parar, al saltar y
// al hacer loop) o el teclado al soltar la tecla; cortarlas al parar el transporte silenciaría una nota que se está
// tocando en vivo. Las notas que Tidechart GENERE (desde la Fase 3) sí serán su responsabilidad.
//
// Sink: sink (const std::uint8_t* data, int size, int sample), el que escribe en el MidiBuffer de salida.
class MidiStage
{
public:
    void requestRelease() noexcept { releasePending.store (true, std::memory_order_release); }
    void requestPanic() noexcept { panicPending.store (true, std::memory_order_release); }

    // Al principio de cada bloque, antes de los eventos: atiende las peticiones pendientes en la muestra 0.
    template <typename Sink>
    void beginBlock (Sink&& sink)
    {
        const bool panic = panicPending.exchange (false, std::memory_order_acq_rel);
        const bool release = releasePending.exchange (false, std::memory_order_acq_rel);
        if (! panic && ! release)
            return;

        // Copia: emit() actualiza 'sounding' mientras se recorre.
        NoteTracker toRelease = sounding;
        const int sent = toRelease.releaseAll ([&] (ShortMessage off) { emit (off, 0, sink); });
        if (panic)
            for (int channel = 0; channel < numChannels; ++channel)
                emit (ShortMessage::controller (channel, 123, 0), 0, sink);
        if (panic || sent > 0) // el host reinicia el audio a menudo sin nada sonando: eso no se cuenta
            ++releases;
    }

    // Un evento de entrada (bytes crudos, en su muestra del bloque). Fase 1: sale igual.
    template <typename Sink>
    void process (const std::uint8_t* data, int size, int sample, Sink&& sink)
    {
        emitRaw (data, size, sample, sink);
    }

    // Lo que suena en el receptor según lo que salió de aquí.
    [[nodiscard]] const NoteTracker& getSounding() const noexcept { return sounding; }
    // Cuántas veces se apagó algo que sonaba, más los pánicos (para la GUI y los tests).
    [[nodiscard]] std::uint32_t releaseCount() const noexcept { return releases; }

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
};

} // namespace tidechart::midi
