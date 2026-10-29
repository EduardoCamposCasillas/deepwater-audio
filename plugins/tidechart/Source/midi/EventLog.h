#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "midi/NoteTracker.h"

namespace tidechart::midi
{

// Un evento MIDI tal como lo muestra el monitor de la GUI: el mensaje, si entró o salió, y dónde cayó.
struct LoggedEvent
{
    ShortMessage message;    // estado 0 = mensaje largo (SysEx)
    std::uint16_t size = 0;  // bytes del mensaje original
    bool output = false;     // false = entró al plugin; true = salió
    double ppq = 0.0;        // posición en negras de la muestra del evento (host parado: donde está el cursor)
    bool playing = false;    // el host estaba reproduciendo (para capturar una melodía hace falta la posición)
};

// Cola de un productor (el audio) y un consumidor (la GUI), sin locks ni reservas: un anillo de tamaño fijo con dos
// índices atómicos. El audio escribe y avanza 'written'; la GUI lee hasta ahí y avanza 'read'. Cada casilla la toca un
// solo hilo a la vez: el productor solo escribe casillas que el consumidor ya leyó.
// Si la GUI no lee (cerrada) y el anillo se llena, los eventos nuevos se descartan: el monitor es informativo y el
// audio nunca espera.
template <std::size_t Capacity>
class EventLog
{
    static_assert (Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "Capacity tiene que ser potencia de 2");

public:
    // Hilo de audio. Devuelve false si estaba lleno.
    bool push (const LoggedEvent& event) noexcept
    {
        const auto w = written.load (std::memory_order_relaxed);
        if (w - read.load (std::memory_order_acquire) >= Capacity)
        {
            dropped.fetch_add (1, std::memory_order_relaxed);
            return false;
        }
        slots[w & (Capacity - 1)] = event;
        written.store (w + 1, std::memory_order_release); // publica la casilla escrita
        return true;
    }

    // Hilo de la GUI: fn (const LoggedEvent&) para cada evento pendiente, en orden. Devuelve cuántos leyó.
    template <typename Fn>
    int drain (Fn&& fn)
    {
        auto r = read.load (std::memory_order_relaxed);
        const auto w = written.load (std::memory_order_acquire);
        int count = 0;
        for (; r != w; ++r, ++count)
            fn (slots[r & (Capacity - 1)]);
        read.store (r, std::memory_order_release); // libera las casillas leídas para el productor
        return count;
    }

    [[nodiscard]] std::uint32_t droppedCount() const noexcept { return dropped.load (std::memory_order_relaxed); }

private:
    std::array<LoggedEvent, Capacity> slots {};
    std::atomic<std::uint32_t> written { 0 };
    std::atomic<std::uint32_t> read { 0 };
    std::atomic<std::uint32_t> dropped { 0 };
};

} // namespace tidechart::midi
