#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace deepwater::dsp
{

// Toma de señal para un analizador: el hilo de audio escribe las muestras (mezcladas a mono) en un buffer circular y
// otro hilo (la GUI) copia las últimas N cuando quiere dibujar. Sin locks y sin que el audio espere nunca a la GUI.
//
// Es un buffer que SE PISA: el audio no sabe si la GUI leyó o no, siempre escribe sobre lo más viejo. La GUI solo
// quiere "lo más reciente", así que no importa que se pierdan muestras entre dos cuadros. El único peligro es copiar
// una zona mientras el audio la está sobrescribiendo (la copia mezclaría muestras de dos momentos). Se detecta como en
// un seqlock: el audio anuncia hasta dónde VA a escribir (claimed) antes de escribir y publica hasta dónde escribió
// (written) después; la GUI copia y después mira si el audio llegó a reclamar la zona copiada. Si la pisó, la copia
// se descarta y se reintenta en el cuadro siguiente.
//
// Las muestras son std::atomic<float> con orden relajado: en x64 son lecturas y escrituras normales (no cuestan más),
// pero leer y escribir la misma muestra desde dos hilos deja de ser una carrera de datos (comportamiento indefinido).
template <size_t Capacity>
class AnalyzerTap
{
    static_assert (Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "la capacidad debe ser potencia de 2");

public:
    static constexpr size_t capacity = Capacity;

    // Hilo de audio. Promedia los canales (L + R) / 2: un seno igual en los dos canales mide lo mismo que en mono.
    void push (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (numChannels <= 0 || numSamples <= 0)
            return;
        const std::uint64_t start = written.load (std::memory_order_relaxed);
        const auto count = static_cast<std::uint64_t> (numSamples);
        claimed.store (start + count, std::memory_order_relaxed);
        // Ordena el anuncio ANTES de las muestras: si la GUI ve una muestra nueva, también verá el anuncio.
        std::atomic_thread_fence (std::memory_order_release);

        const float scale = 1.0f / static_cast<float> (numChannels);
        for (int i = 0; i < numSamples; ++i)
        {
            float sum = channels[0][i];
            for (int ch = 1; ch < numChannels; ++ch)
                sum += channels[ch][i];
            samples[static_cast<size_t> (start + static_cast<std::uint64_t> (i)) & mask].store (sum * scale, std::memory_order_relaxed);
        }
        written.store (start + count, std::memory_order_release);
    }

    // Hilo de audio. El side de un par estéreo, (L − R) / 2: lo que es distinto entre los lados (un sonido en el centro
    // da 0). Con push() en otra toma (el mid, (L + R) / 2) se analizan las dos partes por separado.
    void pushSide (const float* left, const float* right, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;
        const std::uint64_t start = written.load (std::memory_order_relaxed);
        const auto count = static_cast<std::uint64_t> (numSamples);
        claimed.store (start + count, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        for (int i = 0; i < numSamples; ++i)
            samples[static_cast<size_t> (start + static_cast<std::uint64_t> (i)) & mask].store (0.5f * (left[i] - right[i]),
                                                                                                 std::memory_order_relaxed);
        written.store (start + count, std::memory_order_release);
    }

    // Cualquier otro hilo. Copia las últimas 'count' muestras en orden (la más vieja primero). Devuelve false si aún
    // no se escribieron tantas o si el audio pisó la zona mientras se copiaba (destination queda a medias: no usarla).
    [[nodiscard]] bool copyLatest (float* destination, size_t count) const noexcept
    {
        if (count == 0 || count > Capacity)
            return false;
        const std::uint64_t end = written.load (std::memory_order_acquire);
        if (end < count)
            return false;
        const std::uint64_t start = end - count;
        for (size_t i = 0; i < count; ++i)
            destination[i] = samples[static_cast<size_t> (start + i) & mask].load (std::memory_order_relaxed);

        // Si alguna muestra copiada ya era nueva, este fence se sincroniza con el del audio y 'claimed' lo delata.
        std::atomic_thread_fence (std::memory_order_acquire);
        return claimed.load (std::memory_order_relaxed) - start <= Capacity;
    }

    // Cuántas muestras se escribieron desde el principio (para saber si llegó audio nuevo desde el último cuadro).
    [[nodiscard]] std::uint64_t getWritten() const noexcept { return written.load (std::memory_order_acquire); }

private:
    static constexpr size_t mask = Capacity - 1;
    std::array<std::atomic<float>, Capacity> samples {};
    std::atomic<std::uint64_t> claimed { 0 };
    std::atomic<std::uint64_t> written { 0 };
};

} // namespace deepwater::dsp
