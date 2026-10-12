#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#include "dsp/HostClock.h"

namespace ebb::dsp
{

// Fase 7: lo que dibuja el visor en vivo. Es un osciloscopio "de barrido" sincronizado con el compás, como el de LFOTool
// o ShaperBox: el eje x es una ventana de 1, 2 o 4 compases dividida en columnas, y un cursor la recorre de izquierda a
// derecha al ritmo de la canción, reescribiendo cada columna con lo que pasó en ese trozo de tiempo. Como la ventana
// está anclada al compás, lo que se repite (el patrón del kick, la curva del modo tempo) se queda QUIETO en pantalla y
// se ve dónde cae cada golpe dentro del compás; con el host parado el cursor sigue barriendo al tempo conocido (para ver
// los golpes MIDI o del sidechain tocados en vivo).
//
// Cada columna guarda: el pico de la entrada (antes del ducking) y de la salida, el pico del sidechain, la ganancia más
// baja (lo más hondo del ducking en ese trozo) y si ahí empezó una curva. El audio escribe directo en atómicos (orden
// relajado: en x64 son escrituras normales) y la GUI los lee cuando dibuja; si lee una columna a medio escribir, la ve
// mezclada durante un cuadro, sin consecuencias. No hay locks, ni reservas, ni espera.
//
// Dos pasadas por bloque, independientes: el motor escribe entrada, salida, ganancia y golpes (write) y el procesador,
// después, el sidechain (addSidechain). Cada una reinicia su parte de la columna la primera vez que entra en ella.
class LiveScope
{
public:
    static constexpr int numColumns = 512;
    static constexpr std::array<int, 3> barOptions { 1, 2, 4 };

    struct Column
    {
        float input = 0.0f;     // pico de |x| antes del ducking (mayor de los canales)
        float output = 0.0f;    // pico de |x| a la salida
        float sidechain = 0.0f; // pico de |x| del sidechain
        float gain = 1.0f;      // ganancia más baja (con mix y bypass: la que sonó)
        bool hit = false;       // aquí empezó una curva (golpe MIDI o del sidechain, o inicio de ciclo en modo tempo)
    };

    // Fuera del audio (prepareToPlay): todo vacío y el cursor al principio.
    void reset() noexcept
    {
        for (auto& column : columns)
        {
            column.input.store (0.0f, std::memory_order_relaxed);
            column.output.store (0.0f, std::memory_order_relaxed);
            column.sidechain.store (0.0f, std::memory_order_relaxed);
            column.gain.store (1.0f, std::memory_order_relaxed);
            column.hit.store (false, std::memory_order_relaxed);
        }
        cursor.store (0.0f, std::memory_order_relaxed);
        freeBeats = 0.0;
        startBeats = 0.0;
        mainColumn = sidechainColumn = -1;
        mainOpen = sidechainOpen = {};
    }

    // Cualquier hilo: cuántos compases muestra la ventana (1, 2 o 4).
    void setBars (int newBars) noexcept { bars.store (validBars (newBars), std::memory_order_relaxed); }
    [[nodiscard]] int getBars() const noexcept { return bars.load (std::memory_order_relaxed); }

    [[nodiscard]] static int validBars (int value) noexcept
    {
        for (const int option : barOptions)
            if (value <= option)
                return option;
        return barOptions.back();
    }

    // Hilo de audio, una vez por bloque y después de HostClock::beginBlock. Con el host reproduciendo (o el reloj libre
    // del Standalone) la ventana arranca en un inicio de compás: el compás 1, 3, 5... con 2 compases. Parado, el
    // barrido sigue desde donde estaba al tempo conocido.
    void beginBlock (const HostClock& clock, int numSamples) noexcept
    {
        const double quartersPerBar = clock.timeSignature().isValid() ? clock.timeSignature().quartersPerBar() : 4.0;
        const int windowBars = getBars();
        windowBeats = quartersPerBar * windowBars;
        beatsPerSample = clock.getBeatsPerSample();

        if (clock.isRunning())
        {
            const double origin = clock.barOrigin();
            const double barIndex = std::floor (origin / quartersPerBar + 1.0e-6);
            const double barsIntoWindow = barIndex - std::floor (barIndex / windowBars) * windowBars;
            startBeats = wrap (clock.blockStartPpq() - (origin - barsIntoWindow * quartersPerBar));
        }
        else
        {
            startBeats = wrap (freeBeats);
        }
        freeBeats = wrap (startBeats + numSamples * beatsPerSample);
        cursor.store (static_cast<float> (freeBeats / windowBeats), std::memory_order_relaxed);
    }

    // Hilo de audio: la muestra 'offset' del bloque (la llama el motor).
    void write (int offset, float input, float output, float gain, bool hit) noexcept
    {
        const int index = columnAt (offset);
        if (index != mainColumn)
        {
            mainColumn = index;
            mainOpen = {};
        }
        mainOpen.input = std::max (mainOpen.input, input);
        mainOpen.output = std::max (mainOpen.output, output);
        mainOpen.gain = std::min (mainOpen.gain, gain);
        mainOpen.hit = mainOpen.hit || hit;

        auto& column = columns[static_cast<size_t> (index)];
        column.input.store (mainOpen.input, std::memory_order_relaxed);
        column.output.store (mainOpen.output, std::memory_order_relaxed);
        column.gain.store (mainOpen.gain, std::memory_order_relaxed);
        column.hit.store (mainOpen.hit, std::memory_order_relaxed);
    }

    // Hilo de audio: el nivel del sidechain en la muestra 'offset' del bloque (lo llama el procesador).
    void addSidechain (int offset, float level) noexcept
    {
        const int index = columnAt (offset);
        if (index != sidechainColumn)
        {
            sidechainColumn = index;
            sidechainOpen = {};
        }
        sidechainOpen.sidechain = std::max (sidechainOpen.sidechain, level);
        columns[static_cast<size_t> (index)].sidechain.store (sidechainOpen.sidechain, std::memory_order_relaxed);
    }

    // Hilo de la GUI.
    [[nodiscard]] Column column (int index) const noexcept
    {
        const auto& c = columns[static_cast<size_t> (std::clamp (index, 0, numColumns - 1))];
        return { c.input.load (std::memory_order_relaxed), c.output.load (std::memory_order_relaxed),
                 c.sidechain.load (std::memory_order_relaxed), c.gain.load (std::memory_order_relaxed),
                 c.hit.load (std::memory_order_relaxed) };
    }

    // Hilo de la GUI: dónde está el cursor (0–1 de la ventana) al final del último bloque.
    [[nodiscard]] float getCursor() const noexcept { return cursor.load (std::memory_order_relaxed); }

    // Columna de la muestra 'offset' del bloque actual (pública para los tests).
    [[nodiscard]] int columnAt (int offset) const noexcept
    {
        const double beats = wrap (startBeats + offset * beatsPerSample);
        return std::clamp (static_cast<int> (beats / windowBeats * numColumns), 0, numColumns - 1);
    }

private:
    struct AtomicColumn
    {
        std::atomic<float> input { 0.0f }, output { 0.0f }, sidechain { 0.0f }, gain { 1.0f };
        std::atomic<bool> hit { false };
    };

    // Posición dentro de la ventana, en [0, windowBeats). También para posiciones negativas (pre-roll).
    [[nodiscard]] double wrap (double beats) const noexcept
    {
        if (beats >= 0.0 && beats < windowBeats)
            return beats;
        const double wrapped = beats - std::floor (beats / windowBeats) * windowBeats;
        return std::clamp (wrapped, 0.0, std::nextafter (windowBeats, 0.0));
    }

    std::array<AtomicColumn, numColumns> columns {};
    std::atomic<float> cursor { 0.0f };
    std::atomic<int> bars { 1 };

    // Estado del hilo de audio.
    double windowBeats = 4.0;
    double beatsPerSample = 0.0;
    double startBeats = 0.0; // posición de la 1.ª muestra del bloque en la ventana, en negras
    double freeBeats = 0.0;  // dónde sigue el barrido en el bloque siguiente
    int mainColumn = -1, sidechainColumn = -1;
    Column mainOpen, sidechainOpen; // lo acumulado en la columna abierta de cada pasada
};

} // namespace ebb::dsp
