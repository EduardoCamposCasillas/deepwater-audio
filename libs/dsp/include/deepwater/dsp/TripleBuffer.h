#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace deepwater::dsp
{

// Buzón del "último valor" entre un hilo que escribe (el audio) y otro que lee (la GUI), sin locks y sin esperas.
// Nació en Ebb (Fase 1) para mandar a la GUI la posición del host: varios campos (tempo, compás, posición) que tienen
// que llegar JUNTOS. Con un atómico por campo la GUI podría leer el compás de un bloque y la posición del siguiente.
//
// Idea: tres copias del valor. El escritor tiene la suya ("back"), el lector la suya ("front") y la tercera está en
// medio. Escribir = rellenar back y cambiarla por la de en medio (marcándola como nueva). Leer = si la de en medio es
// nueva, cambiarla por front. Cada copia la toca un solo hilo a la vez, así que nunca se lee un valor a medio escribir;
// el único punto compartido es un byte atómico (índice de la copia de en medio + bit de "nueva").
//
// El escritor nunca espera (puede escribir miles de veces entre dos lecturas: solo se queda la última) y el lector
// siempre obtiene el valor completo más reciente. T tiene que poder copiarse con memcpy (sin memoria propia).
template <typename T>
class TripleBuffer
{
    static_assert (std::is_trivially_copyable_v<T>, "T se copia entre hilos: tiene que ser trivialmente copiable");

public:
    // Hilo del escritor (audio). Sin reservas de memoria ni bloqueos.
    void write (const T& value) noexcept
    {
        buffers[back] = value;
        // acq_rel: release publica la copia recién escrita; acquire porque recibimos la que soltó el lector.
        back = static_cast<std::uint8_t> (middle.exchange (static_cast<std::uint8_t> (back | freshBit), std::memory_order_acq_rel)
                                          & indexMask);
    }

    // Hilo del lector (GUI). Devuelve true si hubo un valor nuevo desde la lectura anterior. Sin nada nuevo, 'out'
    // recibe otra vez el último valor leído (o T{} si todavía no se escribió nada).
    bool read (T& out) noexcept
    {
        const bool fresh = (middle.load (std::memory_order_relaxed) & freshBit) != 0;
        if (fresh)
            front = static_cast<std::uint8_t> (middle.exchange (front, std::memory_order_acq_rel) & indexMask);
        out = buffers[front];
        return fresh;
    }

private:
    static constexpr std::uint8_t indexMask = 0x3;
    static constexpr std::uint8_t freshBit = 0x4;

    std::array<T, 3> buffers {};
    std::atomic<std::uint8_t> middle { 1 };
    std::uint8_t back = 0;  // solo lo toca el escritor
    std::uint8_t front = 2; // solo lo toca el lector
};

} // namespace deepwater::dsp
