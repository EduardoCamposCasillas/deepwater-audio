#pragma once

#include <array>
#include <cstdint>

namespace tidechart::midi
{

inline constexpr int numChannels = 16;
inline constexpr int numNotes = 128;

// Un mensaje MIDI corto (note-on, note-off, CC…): el byte de estado y hasta dos de datos. El canal va en los 4 bits
// bajos del estado (0–15; FL y casi todos los programas lo muestran como 1–16).
struct ShortMessage
{
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;

    [[nodiscard]] int channel() const noexcept { return status & 0x0f; }
    [[nodiscard]] int kind() const noexcept { return status & 0xf0; }
    // Un note-on con velocidad 0 es un note-off (así lo manda mucho hardware, por "running status").
    [[nodiscard]] bool isNoteOn() const noexcept { return kind() == 0x90 && data2 > 0; }
    [[nodiscard]] bool isNoteOff() const noexcept { return kind() == 0x80 || (kind() == 0x90 && data2 == 0); }
    // CC 120 (All Sound Off) y CC 123 (All Notes Off): el receptor apaga todo lo de ese canal.
    [[nodiscard]] bool isAllNotesOff() const noexcept { return kind() == 0xb0 && (data1 == 120 || data1 == 123); }

    [[nodiscard]] static ShortMessage noteOff (int channel, int note) noexcept
    {
        return { static_cast<std::uint8_t> (0x80 | (channel & 0x0f)), static_cast<std::uint8_t> (note & 0x7f), 0 };
    }
    [[nodiscard]] static ShortMessage controller (int channel, int number, int value) noexcept
    {
        return { static_cast<std::uint8_t> (0xb0 | (channel & 0x0f)), static_cast<std::uint8_t> (number & 0x7f),
                 static_cast<std::uint8_t> (value & 0x7f) };
    }
};

// Lee un mensaje de bytes crudos. Los que no son cortos (SysEx, de más de 3 bytes) devuelven estado 0.
[[nodiscard]] inline ShortMessage readShortMessage (const std::uint8_t* data, int size) noexcept
{
    if (data == nullptr || size < 1 || size > 3 || (data[0] & 0x80) == 0 || data[0] == 0xf0)
        return {};
    return { data[0], size > 1 ? static_cast<std::uint8_t> (data[1] & 0x7f) : std::uint8_t {},
             size > 2 ? static_cast<std::uint8_t> (data[2] & 0x7f) : std::uint8_t {} };
}

// Lleva la cuenta de las notas que SUENAN en el receptor: lo que el plugin mandó con note-on y todavía no apagó.
// Es la base de la regla más importante de Tidechart: ninguna nota colgada. Se le muestra todo lo que sale del plugin
// (observe) y, cuando algo corta el flujo normal (pánico, cargar un proyecto, el host reinicia el audio), releaseAll
// manda exactamente los note-off que faltan.
//
// Cuenta por canal y nota, no un sí/no: si llegan dos note-on de la misma tecla sin note-off en medio (dos notas del
// Piano Roll que se solapan, o en fases futuras dos acordes que comparten una nota), hacen falta dos note-off para
// que la cuenta vuelva a 0. Tamaño fijo (16 × 128 bytes): no reserva memoria y se puede usar en el hilo de audio.
class NoteTracker
{
public:
    // Actualiza la cuenta con un mensaje que sale del plugin. Lo que no es una nota (salvo All Notes Off) no cambia nada.
    void observe (ShortMessage message) noexcept
    {
        if (message.isNoteOn())
            noteOn (message.channel(), message.data1);
        else if (message.isNoteOff())
            noteOff (message.channel(), message.data1);
        else if (message.isAllNotesOff())
            clearChannel (message.channel());
    }

    void noteOn (int channel, int note) noexcept
    {
        auto& count = counts[index (channel, note)];
        if (count == 0)
            ++sounding;
        if (count < 255) // 255 note-on seguidos de la misma tecla sin note-off: el receptor tampoco los distingue
            ++count;
    }

    // Devuelve true si la nota estaba sonando. Un note-off de algo que no suena (llegó antes de cargar el plugin, o se
    // perdió el note-on) se deja pasar igual, pero no cuenta.
    bool noteOff (int channel, int note) noexcept
    {
        auto& count = counts[index (channel, note)];
        if (count == 0)
            return false;
        if (--count == 0)
            --sounding;
        return true;
    }

    void clearChannel (int channel) noexcept
    {
        for (int note = 0; note < numNotes; ++note)
        {
            auto& count = counts[index (channel, note)];
            if (count > 0)
                --sounding;
            count = 0;
        }
    }

    void clear() noexcept
    {
        counts.fill (0);
        sounding = 0;
    }

    [[nodiscard]] int count (int channel, int note) const noexcept { return counts[index (channel, note)]; }
    [[nodiscard]] bool isSounding (int channel, int note) const noexcept { return count (channel, note) > 0; }
    // Cuántas teclas distintas (canal y nota) suenan.
    [[nodiscard]] int soundingCount() const noexcept { return sounding; }

    // Recorre lo que suena: fn (canal, nota, cuántos note-on sin pareja).
    template <typename Fn>
    void forEachSounding (Fn&& fn) const
    {
        if (sounding == 0)
            return;
        for (int channel = 0; channel < numChannels; ++channel)
            for (int note = 0; note < numNotes; ++note)
                if (const int c = counts[index (channel, note)]; c > 0)
                    fn (channel, note, c);
    }

    // Manda un note-off por cada note-on sin pareja (sink (ShortMessage)) y deja la cuenta en 0. Devuelve cuántos mandó.
    template <typename Sink>
    int releaseAll (Sink&& sink)
    {
        int sent = 0;
        forEachSounding ([&] (int channel, int note, int c) {
            for (int i = 0; i < c; ++i, ++sent)
                sink (ShortMessage::noteOff (channel, note));
        });
        clear();
        return sent;
    }

private:
    [[nodiscard]] static int index (int channel, int note) noexcept { return (channel & 0x0f) * numNotes + (note & 0x7f); }

    std::array<std::uint8_t, numChannels * numNotes> counts {};
    int sounding = 0;
};

} // namespace tidechart::midi
