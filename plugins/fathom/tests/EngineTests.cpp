// Tests de Fathom sin JUCE: el motor de audio. Se lanzan con ctest. Si algo falla, imprime el archivo, la línea y la
// condición.

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "deepwater/dsp/Decibels.h"
#include "dsp/Engine.h"

namespace
{
int failures = 0;

#define CHECK(condition)                                                                   \
    do                                                                                     \
    {                                                                                      \
        if (! (condition))                                                                 \
        {                                                                                  \
            std::printf ("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #condition);           \
            ++failures;                                                                    \
        }                                                                                  \
    } while (false)

using namespace fathom::dsp;

constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };

// Generador de tamaños de bloque "al azar" pero repetible (FL cambia el tamaño del bloque de un callback a otro).
struct BlockSizes
{
    std::uint32_t state = 12345;
    int next (int maxSize) noexcept
    {
        state = state * 1664525u + 1013904223u;
        return 1 + static_cast<int> ((state >> 8) % static_cast<std::uint32_t> (maxSize));
    }
};

// Señal estéreo de prueba: dos senos distintos (así se nota si un canal se cruza con el otro).
struct StereoSignal
{
    std::vector<float> left, right;

    StereoSignal (int numSamples, double sampleRate) : left (static_cast<size_t> (numSamples)), right (static_cast<size_t> (numSamples))
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const double t = i / sampleRate;
            left[static_cast<size_t> (i)] = static_cast<float> (0.5 * std::sin (2.0 * 3.141592653589793 * 220.0 * t));
            right[static_cast<size_t> (i)] = static_cast<float> (0.3 * std::sin (2.0 * 3.141592653589793 * 3150.0 * t));
        }
    }
};

// Procesa toda la señal en bloques de tamaño variable; 'eachBlock' se llama antes de cada bloque con su inicio.
template <typename EachBlock>
void processInBlocks (Engine& engine, StereoSignal& signal, int maxBlock, EachBlock&& eachBlock)
{
    BlockSizes sizes;
    const int total = static_cast<int> (signal.left.size());
    for (int start = 0; start < total;)
    {
        const int count = std::min (sizes.next (maxBlock), total - start);
        eachBlock (start);
        float* channels[] { signal.left.data() + start, signal.right.data() + start };
        engine.process (channels, 2, count);
        start += count;
    }
}

void testPassthroughIsBitExact()
{
    std::printf ("Passthrough: en 0 dB, con o sin bypass, la salida es la entrada bit a bit\n");
    for (const double sampleRate : sampleRates)
        for (const bool bypassed : { false, true })
        {
            Engine engine;
            engine.prepare (sampleRate, bypassed, 0.0f);
            StereoSignal signal (4096, sampleRate);
            const StereoSignal original = signal;
            processInBlocks (engine, signal, 512, [] (int) {});
            CHECK (signal.left == original.left);
            CHECK (signal.right == original.right);
        }
}

void testOutputGain()
{
    std::printf ("Salida: llega exacto a la ganancia pedida, en rampa (sin saltos) y limitada a +-24 dB\n");
    for (const double sampleRate : sampleRates)
    {
        // Constante en 1 para leer la ganancia directamente en la salida.
        Engine engine;
        engine.prepare (sampleRate, false, 0.0f);
        const int total = static_cast<int> (sampleRate * 0.1);
        std::vector<float> left (static_cast<size_t> (total), 1.0f), right (static_cast<size_t> (total), 1.0f);
        engine.setOutputDb (-6.0f);
        float* channels[] { left.data(), right.data() };
        engine.process (channels, 2, total);

        const float target = deepwater::dsp::dbToGain (-6.0f);
        const auto rampSamples = static_cast<size_t> (std::lround (Engine::gainRampSeconds * sampleRate));
        CHECK (left.back() == target && right.back() == target);
        CHECK (left[rampSamples - 1] == target);   // la rampa termina justo a los 20 ms
        CHECK (left[rampSamples / 2] > target + 0.1f); // a la mitad todavía va bajando
        float biggestStep = 0.0f;
        for (size_t i = 1; i < left.size(); ++i)
            biggestStep = std::max (biggestStep, std::abs (left[i] - left[i - 1]));
        CHECK (biggestStep < 0.001f); // 0.5 de bajada repartida en ~900-1900 muestras

        // Fuera de rango: se limita.
        Engine clamped;
        clamped.prepare (sampleRate, false, 60.0f);
        float one[] { 1.0f };
        float* mono[] { one };
        clamped.process (mono, 1, 1);
        CHECK (std::abs (one[0] - deepwater::dsp::dbToGain (Engine::maxOutputDb)) < 1.0e-4f);
    }
}

void testBypassFadeHasNoClicks()
{
    std::printf ("Bypass: con +12 dB, conmutar sonando no salta (fundido de 20 ms) y termina en la entrada exacta\n");
    for (const double sampleRate : sampleRates)
    {
        Engine engine;
        engine.prepare (sampleRate, false, 12.0f);
        StereoSignal signal (static_cast<int> (sampleRate * 0.5), sampleRate);
        const StereoSignal original = signal;
        const int switchAt = static_cast<int> (sampleRate * 0.1);
        processInBlocks (engine, signal, 333, [&] (int start) { engine.setBypassed (start >= switchAt); });

        // El cambio de la salida entre muestras vecinas nunca supera el de un seno de 220 Hz amplificado (+12 dB ≈ ×4):
        // un salto de golpe de ×4 a ×1 sí lo superaría.
        const double maxSlope = 4.0 * 0.5 * 2.0 * 3.141592653589793 * 220.0 / sampleRate;
        float biggestStep = 0.0f;
        for (size_t i = 1; i < signal.left.size(); ++i)
            biggestStep = std::max (biggestStep, std::abs (signal.left[i] - signal.left[i - 1]));
        CHECK (biggestStep < maxSlope * 1.05);

        // 20 ms después del cambio (más un bloque), bit a bit la entrada.
        const auto settled = static_cast<size_t> (switchAt + static_cast<int> (sampleRate * Engine::bypassFadeSeconds) + 334);
        bool exact = true;
        for (size_t i = settled; i < signal.left.size(); ++i)
            exact = exact && signal.left[i] == original.left[i] && signal.right[i] == original.right[i];
        CHECK (exact);
    }
}

void testMonoAndExtraChannels()
{
    std::printf ("Canales: mono funciona; un tercer canal se deja intacto\n");
    Engine engine;
    engine.prepare (48000.0, false, -6.0f);
    std::array<float, 64> a {}, b {}, c {};
    a.fill (1.0f);
    b.fill (1.0f);
    c.fill (1.0f);
    float* mono[] { a.data() };
    engine.process (mono, 1, 64);
    CHECK (std::abs (a.back() - deepwater::dsp::dbToGain (-6.0f)) < 1.0e-6f);

    float* three[] { a.data(), b.data(), c.data() };
    engine.process (three, 3, 64);
    CHECK (c.back() == 1.0f);
}
} // namespace

int main()
{
    testPassthroughIsBitExact();
    testOutputGain();
    testBypassFadeHasNoClicks();
    testMonoAndExtraChannels();

    if (failures > 0)
    {
        std::printf ("\n%d FALLO(S)\n", failures);
        return 1;
    }
    std::printf ("\nTodos los tests pasaron\n");
    return 0;
}
