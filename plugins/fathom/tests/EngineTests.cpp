// Tests de Fathom sin JUCE: el motor de audio (salida, bypass y las bandas) y las cuentas de la edición (deshacer, arrastrar nodos). Se lanzan con ctest. Si algo falla, imprime el archivo, la línea y la
// condición.

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "deepwater/dsp/Decibels.h"
#include "dsp/Engine.h"
#include "edit/NodeEditing.h"
#include "edit/UndoStack.h"

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

constexpr double pi = 3.141592653589793;
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

// Las 24 bandas apagadas salvo la primera.
Engine::BandArray only (const BandSettings& band)
{
    Engine::BandArray bands {};
    bands[0] = band;
    return bands;
}

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
// Amplitud de un seno de frecuencia 'hz' dentro de 'signal' (desde 'start'), por correlación con ventana de Hann.
double toneAmplitude (const std::vector<float>& signal, double hz, double sampleRate, size_t start)
{
    const size_t count = signal.size() - start;
    std::complex<double> correlation {};
    double windowSum = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        const double window = 0.5 - 0.5 * std::cos (2.0 * pi * static_cast<double> (i) / static_cast<double> (count - 1));
        correlation += window * static_cast<double> (signal[start + i])
                       * std::polar (1.0, -2.0 * pi * hz * static_cast<double> (start + i) / sampleRate);
        windowSum += window;
    }
    return 2.0 * std::abs (correlation) / windowSum;
}

std::vector<float> sine (double hz, double amplitude, double sampleRate, size_t numSamples)
{
    std::vector<float> signal (numSamples);
    for (size_t i = 0; i < numSamples; ++i)
        signal[i] = static_cast<float> (amplitude * std::sin (2.0 * pi * hz * static_cast<double> (i) / sampleRate));
    return signal;
}

// Lo más "puntiagudo" de una señal: la mayor segunda diferencia |y[n] − 2·y[n−1] + y[n−2]|. Un seno de frecuencia f y
// amplitud A no pasa de A·(2·sin(π·f/fs))²; un clic (un escalón) la dispara al tamaño del salto.
float biggestSecondDifference (const std::vector<float>& y)
{
    float biggest = 0.0f;
    for (size_t i = 2; i < y.size(); ++i)
        biggest = std::max (biggest, std::abs (y[i] - 2.0f * y[i - 1] + y[i - 2]));
    return biggest;
}

void testBandMatchesItsCurve()
{
    std::printf ("Banda: el audio medido = la curva que dibuja la GUI (mismos coeficientes), con la salida sumada\n");
    double worst = 0.0;
    for (const double sampleRate : sampleRates)
    {
        const BandSettings band { true, 1000.0f, 9.0f, 2.0f };
        const auto curve = Band::coefficientsFor (band, sampleRate);
        for (const double hz : { 100.0, 700.0, 1000.0, 1300.0, 6000.0 })
        {
            Engine engine;
            engine.prepare (sampleRate, false, -3.0f, only (band));
            auto left = sine (hz, 0.25, sampleRate, 32768);
            auto right = left;
            float* channels[] { left.data(), right.data() };
            engine.process (channels, 2, static_cast<int> (left.size()));
            const double measured = 20.0 * std::log10 (toneAmplitude (left, hz, sampleRate, 16384) / 0.25);
            worst = std::max (worst, std::abs (measured - (curve.magnitudeDb (hz, sampleRate) - 3.0)));
            CHECK (left == right);
        }
    }
    std::printf ("  peor diferencia: %.5f dB\n", worst);
    CHECK (worst < 0.01);
}

void testBandAutomationHasNoClicks()
{
    std::printf ("Banda: frecuencia, ganancia, Q y encendido saltando en cada bloque (automatización) no hacen clic\n");
    for (const double sampleRate : sampleRates)
    {
        // Un seno de 1 kHz; la banda salta entre extremos al azar cada bloque (de 1 a 256 muestras).
        Engine engine;
        engine.prepare (sampleRate, false, 0.0f, only ({ true, 1000.0f, 0.0f, 1.0f }));
        auto left = sine (1000.0, 0.05, sampleRate, static_cast<size_t> (sampleRate));
        auto right = left;
        BlockSizes sizes;
        std::uint32_t seed = 99;
        auto pick = [&seed] (auto a, auto b) {
            seed = seed * 1664525u + 1013904223u;
            return (seed >> 16) % 2 == 0 ? a : b;
        };
        const int total = static_cast<int> (left.size());
        for (int start = 0; start < total;)
        {
            const int count = std::min (sizes.next (256), total - start);
            engine.setBand (0, { pick (true, false), pick (300.0f, 3000.0f), pick (-24.0f, 24.0f), pick (0.5f, 4.0f) });
            float* channels[] { left.data() + start, right.data() + start };
            engine.process (channels, 2, count);
            start += count;
        }

        // Cota: el seno de 1 kHz amplificado al máximo (+24 dB ≈ ×16), con margen. Sin el suavizado (probado con rampas
        // de 1 µs) la medida sale 10-100 veces más alta que esta cota; con él, casi 10 veces más baja.
        const double steepest = 16.0 * 0.05 * std::pow (2.0 * std::sin (pi * 1000.0 / sampleRate), 2.0);
        const float measured = biggestSecondDifference (left);
        std::printf ("  %6.0f Hz: segunda diferencia %.4f (cota %.4f)\n", sampleRate, static_cast<double> (measured), 1.5 * steepest);
        CHECK (measured < 1.5 * steepest);
        CHECK (left == right);
    }
}

void testBandOffAndZeroDbAreBitExact()
{
    std::printf ("Banda: apagarla (o llevarla a 0 dB) baja en rampa y después la señal pasa bit a bit\n");
    for (const double sampleRate : sampleRates)
        for (const bool switchOff : { true, false })
        {
            Engine engine;
            engine.prepare (sampleRate, false, 0.0f, only ({ true, 2000.0f, 12.0f, 1.0f }));
            StereoSignal signal (static_cast<int> (sampleRate * 0.3), sampleRate);
            const StereoSignal original = signal;
            const int switchAt = static_cast<int> (sampleRate * 0.1);
            processInBlocks (engine, signal, 300, [&] (int start) {
                if (start >= switchAt)
                    engine.setBand (0, { ! switchOff, 2000.0f, switchOff ? 12.0f : 0.0f, 1.0f });
            });
            const auto before = static_cast<size_t> (switchAt / 2);
            CHECK (signal.right[before] != original.right[before]); // antes del cambio, la banda sí suena
            const auto settled = static_cast<size_t> (switchAt + static_cast<int> (sampleRate * Band::rampSeconds) + 301);
            bool exact = true;
            for (size_t i = settled; i < signal.left.size(); ++i)
                exact = exact && signal.left[i] == original.left[i] && signal.right[i] == original.right[i];
            CHECK (exact);
        }
}

void testBandIsStableAtExtremes()
{
    std::printf ("Banda: estable con ajustes extremos al azar (Q 40, +-30 dB, frecuencias sobre Nyquist)\n");
    for (const double sampleRate : sampleRates)
    {
        Engine engine;
        engine.prepare (sampleRate, false, 0.0f, only ({ true, 30000.0f, 30.0f, 40.0f }));
        std::uint32_t seed = 4242;
        auto random = [&seed] {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float> (seed >> 8) / 16777216.0f;
        };
        float peak = 0.0f;
        std::array<float, 64> left {}, right {};
        for (int block = 0; block < 4000; ++block)
        {
            engine.setBand (0, { true, 10.0f * std::pow (3000.0f, random()), -30.0f + 60.0f * random(), 0.025f * std::pow (1600.0f, random()) });
            for (size_t i = 0; i < left.size(); ++i)
                left[i] = right[i] = random() - 0.5f;
            float* channels[] { left.data(), right.data() };
            engine.process (channels, 2, static_cast<int> (left.size()));
            for (const float x : left)
                peak = std::max (peak, std::abs (x));
        }
        CHECK (std::isfinite (peak));
        CHECK (peak < 1000.0f);
    }
}

// --- Fase 3: todas las formas y 24 bandas -----------------------------------------------------------------------------

// La respuesta total que dibuja la GUI: la suma en dB de las bandas (cada una con sus coeficientes) y la salida.
double curveDb (const Engine::BandArray& bands, float outputDb, double hz, double sampleRate)
{
    double total = static_cast<double> (outputDb);
    for (const auto& band : bands)
        total += Band::coefficientsFor (band, sampleRate).magnitudeDb (hz, sampleRate);
    return total;
}

void testShapesMatchTheirCurves()
{
    std::printf ("Formas: cada forma (y cada pendiente) con otras bandas encendidas suena igual que la curva de la GUI\n");
    struct Case
    {
        Shape shape;
        int slopeIndex;
        float gainDb, q;
    };
    const std::array<Case, 14> cases { {
        { Shape::bell, 1, 9.0f, 2.0f },        { Shape::lowShelf, 1, 6.0f, 0.71f },  { Shape::highShelf, 1, -9.0f, 1.5f },
        { Shape::tiltShelf, 1, 8.0f, 0.7f },   { Shape::flatTilt, 1, -15.0f, 1.0f }, { Shape::lowCut, 0, 0.0f, 1.0f },
        { Shape::lowCut, 3, 0.0f, 2.0f },      { Shape::lowCut, 8, 0.0f, 0.71f },    { Shape::highCut, 2, 0.0f, 0.71f },
        { Shape::highCut, 6, 0.0f, 3.0f },     { Shape::notch, 1, 0.0f, 4.0f },      { Shape::bandPass, 1, 0.0f, 0.8f },
        { Shape::allPass, 1, 0.0f, 0.7f },     { Shape::highCut, 8, 0.0f, 0.71f },
    } };

    double worst = 0.0;
    for (const double sampleRate : { 44100.0, 96000.0 })
        for (const auto& c : cases)
        {
            // La forma a probar en la banda 3, con otras dos encendidas (las bandas van en serie: los dB se suman).
            Engine::BandArray bands {};
            bands[2] = { true, 1000.0f, c.gainDb, c.q, c.shape, c.slopeIndex };
            bands[7] = { true, 150.0f, 4.0f, 1.0f, Shape::bell };
            bands[23] = { true, 9000.0f, -3.0f, 0.71f, Shape::highShelf };
            for (const double hz : { 80.0, 500.0, 900.0, 1000.0, 1200.0, 4000.0 })
            {
                const double expected = curveDb (bands, 2.0f, hz, sampleRate);
                if (expected < -70.0) // muy abajo, un seno en float ya no se mide bien (el corte de 96 dB/oct)
                    continue;
                Engine engine;
                engine.prepare (sampleRate, false, 2.0f, bands);
                auto left = sine (hz, 0.1, sampleRate, 32768);
                auto right = left;
                float* channels[] { left.data(), right.data() };
                engine.process (channels, 2, static_cast<int> (left.size()));
                const double measured = 20.0 * std::log10 (toneAmplitude (left, hz, sampleRate, 16384) / 0.1);
                worst = std::max (worst, std::abs (measured - expected));
                CHECK (left == right);
            }
        }
    std::printf ("  peor diferencia: %.5f dB\n", worst);
    CHECK (worst < 0.01);
}

void testAllBandsFlatAreBitExact()
{
    std::printf ("24 bandas: todas campanas en 0 dB = bit a bit; un corte, un notch y un flat tilt apagados = bit a bit\n");
    for (const double sampleRate : sampleRates)
    {
        Engine::BandArray bands {};
        for (int b = 0; b < Engine::numBands; ++b)
            bands[static_cast<size_t> (b)] = { true, 30.0f * std::pow (1.3f, static_cast<float> (b)), 0.0f, 1.0f + 0.1f * static_cast<float> (b) };
        Engine engine;
        engine.prepare (sampleRate, false, 0.0f, bands);
        StereoSignal signal (4096, sampleRate);
        const StereoSignal original = signal;
        processInBlocks (engine, signal, 512, [] (int) {});
        CHECK (signal.left == original.left && signal.right == original.right);

        // Formas sin ganancia: se apagan con el fundido, después nada.
        Engine shapes;
        Engine::BandArray on {};
        on[0] = { true, 300.0f, 0.0f, 0.71f, Shape::lowCut, 8 };
        on[1] = { true, 3150.0f, 0.0f, 8.0f, Shape::notch };
        on[2] = { true, 1000.0f, 20.0f, 1.0f, Shape::flatTilt };
        on[3] = { true, 8000.0f, 0.0f, 1.0f, Shape::highCut, 0 };
        shapes.prepare (sampleRate, false, 0.0f, on);
        StereoSignal music (static_cast<int> (sampleRate * 0.3), sampleRate);
        const StereoSignal before = music;
        const int switchAt = static_cast<int> (sampleRate * 0.1);
        processInBlocks (shapes, music, 300, [&] (int start) {
            if (start >= switchAt)
                for (int b = 0; b < 4; ++b)
                    shapes.setBand (b, { false, on[static_cast<size_t> (b)].frequency, on[static_cast<size_t> (b)].gainDb,
                                         on[static_cast<size_t> (b)].q, on[static_cast<size_t> (b)].shape, on[static_cast<size_t> (b)].slopeIndex });
        });
        CHECK (music.left[1000] != before.left[1000]); // encendidas sí cambian el sonido
        const auto settled = static_cast<size_t> (switchAt + static_cast<int> (sampleRate * Band::rampSeconds) + 301);
        bool exact = true;
        for (size_t i = settled; i < music.left.size(); ++i)
            exact = exact && music.left[i] == before.left[i] && music.right[i] == before.right[i];
        CHECK (exact);
        for (int b = 0; b < 4; ++b)
            CHECK (shapes.getBand (b).isIdle());
    }
}

void testShapeAndSlopeChangesHaveNoClicks()
{
    std::printf ("Formas: cambiar forma, pendiente, encendido y ajustes al azar en cada bloque no hace clic\n");
    for (const double sampleRate : sampleRates)
    {
        Engine engine;
        engine.prepare (sampleRate, false, 0.0f, only ({ true, 1000.0f, 6.0f, 1.0f }));
        auto left = sine (1000.0, 0.05, sampleRate, static_cast<size_t> (sampleRate));
        auto right = left;
        BlockSizes sizes;
        std::uint32_t seed = 2024;
        auto random = [&seed] (std::uint32_t range) {
            seed = seed * 1664525u + 1013904223u;
            return (seed >> 16) % range;
        };
        const int total = static_cast<int> (left.size());
        for (int start = 0; start < total;)
        {
            const int count = std::min (sizes.next (256), total - start);
            for (int b = 0; b < 3; ++b) // tres bandas cambiando a la vez
                engine.setBand (b, { random (4) != 0, random (2) == 0 ? 300.0f : 3000.0f, random (2) == 0 ? -24.0f : 24.0f,
                                     random (2) == 0 ? 0.5f : 2.0f, static_cast<Shape> (random (numShapes)),
                                     static_cast<int> (random (static_cast<std::uint32_t> (slopes.size()))) });
            float* channels[] { left.data() + start, right.data() + start };
            engine.process (channels, 2, count);
            start += count;
        }
        // La misma cota que con una banda: el seno de 1 kHz subido al máximo de una banda (+24 dB ≈ ×16), con margen.
        const double steepest = 16.0 * 0.05 * std::pow (2.0 * std::sin (pi * 1000.0 / sampleRate), 2.0);
        const float measured = biggestSecondDifference (left);
        std::printf ("  %6.0f Hz: segunda diferencia %.4f (cota %.4f)\n", sampleRate, static_cast<double> (measured), 1.5 * steepest);
        CHECK (measured < 1.5 * steepest);
        CHECK (std::isfinite (measured));
        CHECK (left == right);
    }
}

void testAllBandsAreStableAtExtremes()
{
    std::printf ("24 bandas: estables con formas, pendientes y ajustes extremos al azar en cada bloque\n");
    for (const double sampleRate : sampleRates)
    {
        Engine engine;
        engine.prepare (sampleRate, false, 0.0f);
        std::uint32_t seed = 777;
        auto random = [&seed] {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float> (seed >> 8) / 16777216.0f;
        };
        bool finite = true;
        std::array<float, 64> left {}, right {};
        for (int block = 0; block < 1000; ++block)
        {
            for (int b = 0; b < Engine::numBands; ++b)
                engine.setBand (b, { random() < 0.8f, 10.0f * std::pow (3000.0f, random()), -12.0f + 24.0f * random(),
                                     0.025f * std::pow (1600.0f, random()), static_cast<Shape> (static_cast<int> (random() * numShapes)),
                                     static_cast<int> (random() * static_cast<float> (slopes.size())) });
            for (size_t i = 0; i < left.size(); ++i)
                left[i] = right[i] = random() - 0.5f;
            float* channels[] { left.data(), right.data() };
            engine.process (channels, 2, static_cast<int> (left.size()));
            for (const float x : left)
                finite = finite && std::isfinite (x);
        }
        CHECK (finite);
    }
}
void testUndoStack()
{
    std::printf ("Deshacer: guarda solo lo que cambio, deshace y rehace en orden, un cambio nuevo borra lo rehacible\n");
    using fathom::edit::UndoStack;
    UndoStack stack (3);
    CHECK (! stack.canUndo() && ! stack.canRedo() && stack.undo() == nullptr && stack.redo() == nullptr);
    CHECK (! stack.record ({ 0.1f, 0.2f }, { 0.1f, 0.2f })); // nada cambió: no es un paso

    CHECK (stack.record ({ 0.1f, 0.2f, 0.3f }, { 0.1f, 0.5f, 0.3f }));
    CHECK (stack.record ({ 0.1f, 0.5f, 0.3f }, { 0.9f, 0.5f, 0.0f }));
    const auto* last = stack.undo();
    CHECK (last != nullptr && last->indices == std::vector<int> ({ 0, 2 }));
    CHECK (last->before == std::vector<float> ({ 0.1f, 0.3f }) && last->after == std::vector<float> ({ 0.9f, 0.0f }));
    const auto* first = stack.undo();
    CHECK (first != nullptr && first->indices == std::vector<int> ({ 1 }) && first->before[0] == 0.2f);
    CHECK (! stack.canUndo() && stack.canRedo());
    const auto* again = stack.redo();
    CHECK (again != nullptr && again->indices == std::vector<int> ({ 1 }) && again->after[0] == 0.5f);

    // Un cambio nuevo con algo por rehacer: lo rehacible se pierde.
    CHECK (stack.record ({ 0.1f, 0.5f, 0.3f }, { 0.1f, 0.5f, 0.7f }));
    CHECK (! stack.canRedo() && stack.size() == 2);

    // Con el límite lleno, se olvida el más viejo.
    CHECK (stack.record ({ 0.0f }, { 1.0f }));
    CHECK (stack.record ({ 1.0f }, { 0.0f }));
    CHECK (stack.size() == 3);
    int undone = 0;
    while (stack.undo() != nullptr)
        ++undone;
    CHECK (undone == 3);
}

void testNodeDragging()
{
    std::printf ("Arrastrar nodos: octavas y dB, el shelf sigue al mouse, cortes solo en horizontal, limites\n");
    using namespace fathom::edit;
    BandSettings bell { true, 1000.0f, 3.0f, 1.0f, Shape::bell, 1 };
    const auto up = dragged (bell, 1.0, 2.0, 20.0f, 20000.0f);
    CHECK (std::abs (up.frequency - 2000.0f) < 1.0e-3f && std::abs (up.gainDb - 5.0f) < 1.0e-5f && up.q == 1.0f);
    const auto down = dragged (bell, -2.0, -40.0, 20.0f, 20000.0f);
    CHECK (std::abs (down.frequency - 250.0f) < 1.0e-3f && down.gainDb == BandSettings::minGainDb);
    CHECK (dragged (bell, 20.0, 0.0, 20.0f, 20000.0f).frequency == 20000.0f);
    CHECK (dragged (bell, -20.0, 0.0, 20.0f, 20000.0f).frequency == 20.0f);

    // Shelf: el nodo está en el punto medio (la mitad de la ganancia); subir el nodo 1 dB = 2 dB de ganancia.
    BandSettings shelf { true, 8000.0f, 4.0f, 0.71f, Shape::highShelf, 1 };
    CHECK (std::abs (dragged (shelf, 0.0, 1.0, 20.0f, 20000.0f).gainDb - 6.0f) < 1.0e-5f);
    const auto node = [] (const BandSettings& s) { return Band::coefficientsFor (s, 48000.0).magnitudeDb (s.frequency, 48000.0); };
    const auto moved = dragged (shelf, 0.0, 1.5, 20.0f, 20000.0f);
    CHECK (std::abs ((node (moved) - node (shelf)) - 1.5) < 0.01);

    // Formas sin ganancia: la vertical no cambia nada.
    BandSettings cut { true, 100.0f, 0.0f, 0.71f, Shape::lowCut, 3 };
    const auto cutMoved = dragged (cut, 0.5, 6.0, 20.0f, 20000.0f);
    CHECK (cutMoved.gainDb == 0.0f && std::abs (cutMoved.frequency - 141.4214f) < 1.0e-2f && cutMoved.slopeIndex == 3);

    // Rueda: multiplica el Q y se queda en el rango.
    CHECK (std::abs (scaledQ (1.0f, 2.0) - 2.0f) < 1.0e-6f);
    CHECK (scaledQ (30.0f, 4.0) == BandSettings::maxQ && scaledQ (0.05f, 0.1) == BandSettings::minQ);
}

void testBandCreationAndNotes()
{
    std::printf ("Crear bandas y notas: primera banda libre, forma segun la posicion, notas exactas de FL\n");
    using namespace fathom::edit;
    std::array<bool, 4> used { true, false, true, false };
    CHECK (freeBand (used) == 1);
    used = { true, true, true, true };
    CHECK (freeBand (used) == -1);
    CHECK (shapeForNewBand (20.0f) == Shape::lowCut && shapeForNewBand (1000.0f) == Shape::bell
           && shapeForNewBand (18000.0f) == Shape::highCut);

    CHECK (std::abs (nearestNoteHz (445.0f) - 440.0f) < 1.0e-3f);    // A5 de FL
    CHECK (std::abs (nearestNoteHz (56.0f) - 55.0f) < 1.0e-4f);      // A2 (un 808 en La)
    CHECK (std::abs (nearestNoteHz (261.0f) - 261.6256f) < 1.0e-3f); // C5 = Do central
    CHECK (std::abs (hzToNote (noteToHz (36.0)) - 36.0) < 1.0e-9);
    CHECK (! isBlackKey (60) && isBlackKey (61) && ! isBlackKey (64) && ! isBlackKey (65) && isBlackKey (70) && ! isBlackKey (71));
}
} // namespace

int main()
{
    testPassthroughIsBitExact();
    testOutputGain();
    testBypassFadeHasNoClicks();
    testMonoAndExtraChannels();
    testBandMatchesItsCurve();
    testBandAutomationHasNoClicks();
    testBandOffAndZeroDbAreBitExact();
    testBandIsStableAtExtremes();
    testShapesMatchTheirCurves();
    testAllBandsFlatAreBitExact();
    testShapeAndSlopeChangesHaveNoClicks();
    testAllBandsAreStableAtExtremes();
    testUndoStack();
    testNodeDragging();
    testBandCreationAndNotes();

    if (failures > 0)
    {
        std::printf ("\n%d FALLO(S)\n", failures);
        return 1;
    }
    std::printf ("\nTodos los tests pasaron\n");
    return 0;
}
