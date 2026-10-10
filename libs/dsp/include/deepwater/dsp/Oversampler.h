#pragma once

#include <algorithm>
#include <array>
#include <utility>

#include "deepwater/dsp/Halfband.h"

namespace deepwater::dsp
{

// Oversampling ×2, ×4, ×8 o ×16 alrededor de un proceso no lineal (un waveshaper), para UN canal. Sin JUCE.
//
// Una curva no lineal crea armónicos que pueden pasar de Nyquist (sr/2). En digital no desaparecen: se reflejan como
// en un espejo y caen en frecuencias inarmónicas (aliasing). Si el proceso se calcula a F·sr, los armónicos tienen
// sitio hasta F·sr/2; después un low-pass quita lo que no cabe a sr y se vuelve a la frecuencia del host.
//
// Cada factor es una cadena de etapas ×2 (halfband): ×16 = 4 etapas. Subida: 1x → 2x → 4x...; bajada: al revés.
// La etapa k trabaja a sr·2^(k−1) → sr·2^k, y su diseño solo depende de esa frecuencia: cuanto más adentro, más
// holgada es la transición (de 20 kHz a la mitad de una frecuencia cada vez más alta) y más corto el filtro. Por eso
// ×16 cuesta poco más que ×4.
//
// Latencia: cada etapa (subida + bajada) retrasa 2H − 1 muestras de su frecuencia alta (H = halfLength de su filtro).
// Pasado a muestras del host, la suma suele tener decimales; para que el host pueda compensarla (y el camino dry
// alinearse) tiene que ser un número entero. Se completa con un pequeño retardo puro en la frecuencia más alta.
class Oversampler
{
public:
    static constexpr int maxStages = 4;
    static constexpr int maxFactor = 1 << maxStages;

    // Fuera del hilo de audio: diseña TODAS las etapas (para poder cambiar de factor sin recalcular nada) y deja
    // activas 'numStages' (0 = sin oversampling).
    void prepare (double sampleRate, int initialNumStages) noexcept
    {
        double rate = sampleRate;
        for (auto& stage : stages)
        {
            stage.up.prepare (rate);
            stage.down.prepare (rate);
            rate *= 2.0;
        }

        // Latencia de cada factor, en muestras de la frecuencia más alta de ese factor.
        for (int n = 0; n <= maxStages; ++n)
        {
            int highRateDelay = 0;
            for (int k = 0; k < n; ++k)
            {
                const int halfLength = (stages[static_cast<size_t> (k)].down.getNumTaps() - 1) / 2;
                highRateDelay += (2 * halfLength - 1) << (n - 1 - k);
            }
            const int factor = 1 << n;
            latencies[static_cast<size_t> (n)] = (highRateDelay + factor - 1) / factor; // redondeo hacia arriba
            paddings[static_cast<size_t> (n)] = latencies[static_cast<size_t> (n)] * factor - highRateDelay;
        }

        setNumStages (initialNumStages);
    }

    // Cambia el factor y borra la historia de los filtros. Corta la señal en seco: quien lo llama tiene que hacerlo
    // en silencio (con un fundido), porque además cambia la latencia.
    void setNumStages (int newNumStages) noexcept
    {
        numStages = std::clamp (newNumStages, 0, maxStages);
        reset();
    }

    void reset() noexcept
    {
        for (auto& stage : stages)
        {
            stage.up.reset();
            stage.down.reset();
        }
        padding.fill (0.0f);
        paddingIndex = 0;
    }

    [[nodiscard]] int getNumStages() const noexcept { return numStages; }
    [[nodiscard]] int getFactor() const noexcept { return 1 << numStages; }

    // Retardo entero, en muestras del host, que agrega el factor activo (o el pedido).
    [[nodiscard]] int getLatency() const noexcept { return latencies[static_cast<size_t> (numStages)]; }
    [[nodiscard]] int getLatency (int stagesCount) const noexcept
    {
        return latencies[static_cast<size_t> (std::clamp (stagesCount, 0, maxStages))];
    }

    // Una muestra del host entra, se convierte en 'factor' muestras, cada una pasa por 'shape' y se vuelve a una.
    // Muestra a muestra: no hay buffers por bloque, así que funciona igual con cualquier tamaño de bloque.
    template <typename Shape>
    [[nodiscard]] float process (float input, Shape&& shape) noexcept
    {
        if (numStages == 0)
            return shape (input);

        std::array<float, maxFactor> a, b;
        float* source = a.data();
        float* target = b.data();
        source[0] = input;
        int count = 1;

        // Subida: cada etapa recibe las muestras en orden temporal (su filtro tiene memoria) y escribe el doble.
        for (int s = 0; s < numStages; ++s)
        {
            auto& up = stages[static_cast<size_t> (s)].up;
            for (int i = 0; i < count; ++i)
                up.process (source[i], target[2 * i], target[2 * i + 1]);
            count *= 2;
            std::swap (source, target);
        }

        const int pad = paddings[static_cast<size_t> (numStages)];
        for (int i = 0; i < count; ++i)
        {
            padding[paddingIndex] = source[i];
            const float delayed = padding[(paddingIndex - static_cast<size_t> (pad)) & (paddingSize - 1)];
            paddingIndex = (paddingIndex + 1) & (paddingSize - 1);
            source[i] = shape (delayed);
        }

        // Bajada: en el sitio (la muestra i se escribe después de leer 2i y 2i + 1, que nunca están por detrás).
        for (int s = numStages - 1; s >= 0; --s)
        {
            auto& down = stages[static_cast<size_t> (s)].down;
            count /= 2;
            for (int i = 0; i < count; ++i)
                source[i] = down.process (source[2 * i], source[2 * i + 1]);
        }
        return source[0];
    }

private:
    struct Stage
    {
        HalfbandInterpolator up;
        HalfbandDecimator down;
    };

    static constexpr size_t paddingSize = maxFactor; // el relleno siempre es menor que el factor

    std::array<Stage, maxStages> stages;
    std::array<int, maxStages + 1> latencies {};
    std::array<int, maxStages + 1> paddings {};
    std::array<float, paddingSize> padding {};
    size_t paddingIndex = 0;
    int numStages = 0;
};

} // namespace deepwater::dsp
