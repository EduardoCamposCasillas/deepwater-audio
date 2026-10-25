// Tests de Fathom sin JUCE: el motor de audio (salida, bypass, las bandas, auto gain y solo), las cuentas de la edición (deshacer, arrastrar
// nodos) y las del analizador (inclinación, balística, retención y picos). Se lanzan con ctest. Si algo falla, imprime el archivo, la línea y la
// condición.

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <tuple>
#include <vector>

#include "analysis/SpectrumView.h"
#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/SpectrumAnalyzer.h"
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
void testAnalyzerView()
{
    std::printf ("Vista del analizador: inclinacion con pivote en 1 kHz, tamanos de FFT, balistica independiente de los "
                 "cuadros por segundo, retencion de picos\n");
    using namespace fathom::analysis;
    CHECK (tiltDb (1000.0, 4.5f) == 0.0f);
    CHECK (std::abs (tiltDb (2000.0, 4.5f) - 4.5f) < 1.0e-5f && std::abs (tiltDb (250.0, 4.5f) + 9.0f) < 1.0e-5f);
    CHECK (tiltDb (20.0, 0.0f) == 0.0f);

    // La resolución: 2048 a 16384 puntos a 44.1/48 kHz; el doble a 88.2/96 (la misma duración).
    CHECK (fftOrder (0, 44100.0) == 11 && fftOrder (1, 48000.0) == 12 && fftOrder (3, 44100.0) == 14);
    CHECK (fftOrder (1, 96000.0) == 13 && fftOrder (3, 88200.0) == 15 && fftOrder (9, 44100.0) == 14);

    // Por defecto, lo mismo que en las Fases 1-4: 96 dB, FFT de 4096 (medio), velocidad media (cae 40 dB/s).
    const ViewSettings defaults;
    CHECK (defaults.range() == 96.0f && fftOrder (defaults.resolutionIndex, 44100.0) == 12 && defaults.speed().fallDbPerSecond == 40.0f);
    CHECK (defaults.tilt() == 4.5f && defaults.holdMode() == Hold::off);

    // Balística: sube con su constante de tiempo y baja a dB/s, igual a 30 que a 60 cuadros por segundo.
    for (const auto& speed : speeds)
    {
        float at30 = -100.0f, at60 = -100.0f;
        for (int frame = 0; frame < 90; ++frame)
            at30 = follow (at30, -20.0f, 1.0 / 30.0, speed);
        for (int frame = 0; frame < 180; ++frame)
            at60 = follow (at60, -20.0f, 1.0 / 60.0, speed);
        CHECK (std::abs (at30 - at60) < 0.01f && std::abs (at30 + 20.0f) < 0.01f); // 3 segundos después, arriba
        float falling = -20.0f;
        for (int frame = 0; frame < 30; ++frame)
            falling = follow (falling, -120.0f, 1.0 / 30.0, speed);
        CHECK (std::abs (falling - std::max (-120.0f, -20.0f - speed.fallDbPerSecond)) < 0.01f);
    }
    CHECK (follow (-100.0f, -10.0f, 1.0 / 30.0, speeds.back()) == -10.0f); // muy rápida = al instante
    // La constante de tiempo: tras riseSeconds recorrió el 63 %.
    {
        float shown = -100.0f;
        for (int frame = 0; frame < 100; ++frame)
            shown = follow (shown, 0.0f, speeds[1].riseSeconds / 100.0, speeds[1]);
        CHECK (std::abs (shown + 100.0f * std::exp (-1.0f)) < 0.01f);
    }

    // Retención: sin ella sigue a la línea; con 2 s, se queda y después cae a 20 dB/s; "siempre" no cae nunca.
    HeldPoint off, timed, forever;
    for (auto* point : { &off, &timed, &forever })
        hold (*point, -10.0f, 0.0, Hold::forever);
    double seconds = 0.0;
    for (int frame = 0; frame < 90; ++frame) // 3 segundos con la línea en −60
    {
        seconds += 1.0 / 30.0;
        hold (off, -60.0f, 1.0 / 30.0, Hold::off);
        hold (timed, -60.0f, 1.0 / 30.0, Hold::seconds);
        hold (forever, -60.0f, 1.0 / 30.0, Hold::forever);
        if (seconds < holdSeconds - 0.01)
            CHECK (timed.db == -10.0f);
    }
    CHECK (off.db == -60.0f && forever.db == -10.0f);
    CHECK (timed.db < -10.0f - 0.9f * holdFallDbPerSecond * 0.9f && timed.db > -10.0f - holdFallDbPerSecond * 1.1f);
    hold (forever, -5.0f, 1.0 / 30.0, Hold::forever); // un máximo nuevo la empuja arriba
    CHECK (forever.db == -5.0f && forever.age == 0.0);
}

void testSpectrumPeaks()
{
    std::printf ("Picos del espectro: prominencia, mesetas, piso, el mas cercano; un seno se agarra a < 0.5 Hz\n");
    using namespace fathom::analysis;
    // Dos picos claros (índices 10 y 30), un bulto de 2 dB en una ladera (no cuenta), una meseta (cuenta una vez) y un
    // pico bajo el piso.
    std::vector<float> levels (60, -60.0f);
    for (int i = 4; i <= 16; ++i)
        levels[static_cast<size_t> (i)] = -20.0f - 3.0f * static_cast<float> (std::abs (i - 10));
    for (int i = 24; i <= 36; ++i)
        levels[static_cast<size_t> (i)] = -30.0f - 2.0f * static_cast<float> (std::abs (i - 30));
    levels[34] = levels[33] + 2.0f; // un bulto sobre la ladera derecha del segundo pico
    levels[44] = levels[45] = levels[46] = -40.0f; // meseta
    levels[54] = -90.0f; levels[53] = levels[55] = -100.0f; // bajo el piso
    const auto peaks = findPeaks (levels, 4, 6.0f, -80.0f);
    CHECK (peaks.size() == 3 && peaks[0] == 10 && peaks[1] == 30 && peaks[2] == 44);
    CHECK (nearestPeak (peaks, 27.0, 5.0) == 1 && nearestPeak (peaks, 20.0, 5.0) == -1 && nearestPeak (peaks, 41.0, 5.0) == 2);
    CHECK (findPeaks (std::vector<float> (50, -30.0f), 4, 6.0f, -80.0f).empty()); // todo plano: ninguno

    // De punta a punta, como en el visor: un seno de 87.3 Hz (F2 de FL) + uno de 2.5 kHz más bajo, a 44.1 y 96 kHz.
    for (const double sampleRate : { 44100.0, 96000.0 })
    {
        deepwater::dsp::SpectrumAnalyzer analyzer;
        analyzer.prepare (fftOrder (2, sampleRate));
        std::vector<float> signal (static_cast<size_t> (analyzer.getSize()));
        for (size_t n = 0; n < signal.size(); ++n)
        {
            const double t = static_cast<double> (n) / sampleRate;
            signal[n] = static_cast<float> (0.5 * std::sin (2.0 * pi * 87.31 * t) + 0.05 * std::sin (2.0 * pi * 2500.0 * t));
        }
        analyzer.analyze (signal.data());
        // 380 puntos de 20 Hz a 20 kHz (un visor de ~760 px), cada uno con su tramo, con la inclinación de 4.5 dB/oct.
        constexpr size_t points = 380;
        const double ratio = std::pow (1000.0, 1.0 / static_cast<double> (points - 1));
        std::vector<float> shown (points);
        for (size_t i = 0; i < points; ++i)
        {
            const double centre = 20.0 * std::pow (ratio, static_cast<double> (i));
            shown[i] = analyzer.levelDb (centre / std::sqrt (ratio), centre * std::sqrt (ratio), sampleRate) + tiltDb (centre, 4.5f);
        }
        const auto found = findPeaks (shown, 6, 6.0f, -90.0f);
        CHECK (found.size() == 2);
        if (found.size() == 2)
        {
            const double centre = 20.0 * std::pow (ratio, static_cast<double> (found[0]));
            const auto peak = analyzer.peak (centre / ratio / ratio, centre * ratio * ratio, sampleRate);
            CHECK (std::abs (peak.hz - 87.31) < 0.5);
            const double high = 20.0 * std::pow (ratio, static_cast<double> (found[1]));
            CHECK (std::abs (analyzer.peak (high / ratio / ratio, high * ratio * ratio, sampleRate).hz - 2500.0) < 0.5);
        }
    }
}
// --- Fase 6: escuchar y comparar ------------------------------------------------------------------------------------

// Procesa un seno (el mismo en los dos canales) de una vez y devuelve el canal izquierdo.
std::vector<float> processedSine (Engine& engine, double hz, double amplitude, double sampleRate, size_t numSamples)
{
    auto left = sine (hz, amplitude, sampleRate, numSamples);
    auto right = left;
    float* channels[] { left.data(), right.data() };
    engine.process (channels, 2, static_cast<int> (left.size()));
    return left;
}

// El nivel del tono en la segunda mitad de la señal (ya pasados los fundidos), en dB respecto de 'amplitude'.
double toneDb (const std::vector<float>& signal, double hz, double amplitude, double sampleRate)
{
    return 20.0 * std::log10 (toneAmplitude (signal, hz, sampleRate, signal.size() / 2) / amplitude);
}

void testGainScale()
{
    std::printf ("Escala: 50 %% = la mitad de los dB (= la curva de la banda a la mitad); 0 %% = plano bit a bit\n");
    for (const double sampleRate : sampleRates)
    {
        const BandSettings band { true, 1000.0f, 12.0f, 1.0f };
        BandSettings half = band;
        half.gainDb = 6.0f;
        Engine engine;
        engine.setGainScale (0.5f);
        engine.prepare (sampleRate, false, 0.0f, only (band));
        const auto out = processedSine (engine, 1000.0, 0.25, sampleRate, 32768);
        CHECK (std::abs (toneDb (out, 1000.0, 0.25, sampleRate) - Band::coefficientsFor (half, sampleRate).magnitudeDb (1000.0, sampleRate)) < 0.01);

        Engine flat;
        flat.setGainScale (0.0f);
        flat.prepare (sampleRate, false, 0.0f, only (band));
        StereoSignal signal (4096, sampleRate);
        const StereoSignal original = signal;
        processInBlocks (flat, signal, 512, [] (int) {});
        CHECK (signal.left == original.left && signal.right == original.right);
    }
}

void testPanAndPolarity()
{
    std::printf ("Salida: balance (el centro no cambia nada, al girar baja el lado contrario), polaridad y sin saltos\n");
    for (const double sampleRate : sampleRates)
    {
        const auto settledAfter = static_cast<size_t> (std::lround (Engine::gainRampSeconds * sampleRate));
        const auto total = static_cast<size_t> (sampleRate * 0.1);
        for (const auto& [pan, invert, wantLeft, wantRight] : { std::tuple { 0.5f, false, 0.5f, 1.0f }, std::tuple { -1.0f, false, 1.0f, 0.0f },
                                                               std::tuple { 0.0f, true, -1.0f, -1.0f }, std::tuple { -0.25f, true, -1.0f, -0.75f } })
        {
            Engine engine;
            engine.prepare (sampleRate, false, 0.0f);
            std::vector<float> left (total, 1.0f), right (total, 1.0f);
            engine.setPan (pan);
            engine.setPhaseInverted (invert);
            float* channels[] { left.data(), right.data() };
            engine.process (channels, 2, static_cast<int> (total));
            CHECK (std::abs (left[settledAfter] - wantLeft) < 1.0e-6f && std::abs (right[settledAfter] - wantRight) < 1.0e-6f);
            CHECK (std::abs (left.back() - wantLeft) < 1.0e-6f && std::abs (right.back() - wantRight) < 1.0e-6f);
            float biggestStep = 0.0f;
            for (size_t i = 1; i < total; ++i)
                biggestStep = std::max ({ biggestStep, std::abs (left[i] - left[i - 1]), std::abs (right[i] - right[i - 1]) });
            CHECK (biggestStep < 2.5f / static_cast<float> (settledAfter)); // de +1 a −1 repartido en 20 ms
        }

        // Ya en su sitio desde prepare (sin rampa); en mono el balance no hace nada.
        Engine mono;
        mono.setPan (1.0f);
        mono.setPhaseInverted (true);
        mono.prepare (sampleRate, false, 0.0f);
        float one[] { 1.0f, 1.0f };
        float* channel[] { one };
        mono.process (channel, 1, 2);
        CHECK (one[0] == -1.0f && one[1] == -1.0f);

        // Invertida y vuelta a la normal = la señal original bit a bit.
        Engine twice;
        twice.prepare (sampleRate, false, 0.0f);
        StereoSignal signal (static_cast<int> (sampleRate * 0.2), sampleRate);
        const StereoSignal original = signal;
        processInBlocks (twice, signal, 256, [&] (int start) { twice.setPhaseInverted (start < static_cast<int> (sampleRate * 0.05)); });
        CHECK (signal.left[static_cast<size_t> (sampleRate * 0.03)] == -original.left[static_cast<size_t> (sampleRate * 0.03)]);
        bool exact = true;
        for (size_t i = static_cast<size_t> (sampleRate * 0.1); i < signal.left.size(); ++i)
            exact = exact && signal.left[i] == original.left[i] && signal.right[i] == original.right[i];
        CHECK (exact);
    }
}

void testAutoGain()
{
    std::printf ("Auto gain: plano = 0 dB bit a bit, compensa lo ancho y casi nada lo angosto, y deja el tono al mismo nivel\n");
    for (const double sampleRate : sampleRates)
    {
        const auto compensation = [sampleRate] (const Engine::BandArray& bands) {
            AutoGain autoGain;
            autoGain.prepare (sampleRate);
            for (int b = 0; b < Engine::numBands; ++b)
                autoGain.setBand (b, bands[static_cast<size_t> (b)]);
            return autoGain.compensationDb();
        };
        CHECK (compensation ({}) == 0.0f);
        // Un shelf de graves sobre Nyquist sube TODO 6 dB: compensa −6.
        const float everything = compensation (only ({ true, 30000.0f, 6.0f, 0.71f, Shape::lowShelf }));
        CHECK (std::abs (everything + 6.0f) < 0.05f);
        // Subir y bajar lo mismo compensa en sentidos opuestos (no exacto: en potencia, +6 pesa más que −6).
        const float up = compensation (only ({ true, 2000.0f, 6.0f, 0.7f }));
        const float down = compensation (only ({ true, 2000.0f, -6.0f, 0.7f }));
        CHECK (up < -1.0f && down > 1.0f && up > -6.0f && down < 6.0f);
        // Angosto o en los extremos: casi nada.
        CHECK (std::abs (compensation (only ({ true, 1000.0f, 6.0f, 40.0f }))) < 0.2f);
        CHECK (std::abs (compensation (only ({ true, 20.0f, 0.0f, 0.71f, Shape::lowCut }))) < 0.3f);
        // El all-pass no cambia el volumen de nada: nada que compensar.
        CHECK (std::abs (compensation (only ({ true, 1000.0f, 0.0f, 1.0f, Shape::allPass }))) < 1.0e-4f);
        // Todo cortado: se limita.
        Engine::BandArray cut {};
        for (auto& band : cut)
            band = { true, 15000.0f, 0.0f, 0.71f, Shape::highCut, 8 };
        cut[0] = { true, 20.0f, 0.0f, 0.71f, Shape::highCut, 8 };
        CHECK (compensation (cut) == AutoGain::maxCompensationDb);

        // Recalcular solo la banda que cambió da lo mismo que calcular todo de cero.
        AutoGain incremental;
        incremental.prepare (sampleRate);
        Engine::BandArray bands {};
        bands[3] = { true, 120.0f, 4.0f, 0.71f, Shape::lowShelf };
        bands[9] = { true, 5000.0f, -3.0f, 2.0f };
        for (int b = 0; b < Engine::numBands; ++b)
            incremental.setBand (b, bands[static_cast<size_t> (b)]);
        (void) incremental.compensationDb();
        bands[9].gainDb = 5.0f;
        incremental.setBand (9, bands[9]);
        CHECK (std::abs (incremental.compensationDb() - compensation (bands)) < 1.0e-6f);

        // Con el EQ plano y auto gain, la señal pasa bit a bit.
        Engine flat;
        flat.setAutoGain (true);
        flat.prepare (sampleRate, false, 0.0f);
        StereoSignal signal (4096, sampleRate);
        const StereoSignal original = signal;
        processInBlocks (flat, signal, 512, [] (int) {});
        CHECK (signal.left == original.left && signal.right == original.right);
        CHECK (flat.getAutoGainDb() == 0.0f);

        // De punta a punta: con todo +6 dB, un tono sale al mismo nivel con auto gain (y 6 dB arriba sin él).
        for (const bool on : { false, true })
        {
            Engine engine;
            engine.setAutoGain (on);
            engine.prepare (sampleRate, false, 0.0f, only ({ true, 30000.0f, 6.0f, 0.71f, Shape::lowShelf }));
            const double level = toneDb (processedSine (engine, 440.0, 0.25, sampleRate, 32768), 440.0, 0.25, sampleRate);
            CHECK (std::abs (level - (on ? 0.0 : 6.0)) < 0.1);
        }

        // Encenderlo sonando (y mover la banda en cada bloque) no salta.
        Engine moving;
        moving.prepare (sampleRate, false, 0.0f, only ({ true, 1000.0f, 12.0f, 0.5f }));
        auto left = sine (1000.0, 0.05, sampleRate, static_cast<size_t> (sampleRate * 0.5));
        auto right = left;
        BlockSizes sizes;
        const int total = static_cast<int> (left.size());
        for (int start = 0, block = 0; start < total; ++block)
        {
            const int count = std::min (sizes.next (256), total - start);
            moving.setAutoGain (start > total / 4);
            moving.setBand (0, { true, 1000.0f, block % 2 == 0 ? 12.0f : -12.0f, 0.5f });
            float* channels[] { left.data() + start, right.data() + start };
            moving.process (channels, 2, count);
            start += count;
        }
        // Cota: el seno con +12 dB (×4) y la compensación de −12 dB al revés (×4 más), con margen.
        const double steepest = 4.0 * 4.0 * 0.05 * std::pow (2.0 * std::sin (pi * 1000.0 / sampleRate), 2.0);
        CHECK (biggestSecondDifference (left) < 1.5 * steepest);
    }
}

void testSoloZones()
{
    std::printf ("Solo: zona de cada forma (0 dB en el centro, lejos muy abajo), tilts sin solo, bypass manda\n");
    CHECK (! soloZoneFor ({ true, 1000.0f, 3.0f, 1.0f, Shape::tiltShelf }).has_value());
    CHECK (! soloZoneFor ({ true, 1000.0f, 3.0f, 1.0f, Shape::flatTilt }).has_value());
    CHECK (soloZoneFor ({ true, 300.0f, 0.0f, 0.71f, Shape::lowCut, 7 })->slopeIndex == 7);
    for (const double sampleRate : sampleRates)
    {
        struct Case
        {
            BandSettings band;
            double inside, outside;
        };
        for (const auto& c : { Case { { true, 1000.0f, -6.0f, 4.0f }, 1000.0, 100.0 },                     // campana
                               Case { { true, 1000.0f, 0.0f, 8.0f, Shape::notch }, 1000.0, 8000.0 },        // notch
                               Case { { false, 200.0f, 0.0f, 0.71f, Shape::lowCut, 3 }, 40.0, 2000.0 },     // corte apagado
                               Case { { true, 6000.0f, 4.0f, 0.71f, Shape::highShelf }, 15000.0, 500.0 } }) // shelf agudos
        {
            const auto zone = Band::coefficientsFor (*soloZoneFor (c.band), sampleRate);
            for (const double hz : { c.inside, c.outside })
            {
                Engine engine;
                engine.prepare (sampleRate, false, 0.0f, only (c.band));
                engine.setSolo (0);
                const double level = toneDb (processedSine (engine, hz, 0.25, sampleRate, 32768), hz, 0.25, sampleRate);
                CHECK (std::abs (level - zone.magnitudeDb (hz, sampleRate)) < 0.01);
                if (hz == c.inside)
                    CHECK (level > -1.0);
                else
                    CHECK (level < -24.0);
            }
        }

        // Un tilt no tiene zona: se oye la mezcla tal cual.
        Engine tilt;
        tilt.prepare (sampleRate, false, 0.0f, only ({ true, 1000.0f, 0.0f, 1.0f, Shape::tiltShelf }));
        tilt.setSolo (0);
        StereoSignal signal (4096, sampleRate);
        const StereoSignal original = signal;
        processInBlocks (tilt, signal, 512, [] (int) {});
        CHECK (signal.left == original.left && ! tilt.isSoloing());

        // En bypass se oye la original aunque haya solo.
        Engine bypassed;
        bypassed.prepare (sampleRate, true, 0.0f, only ({ true, 1000.0f, 6.0f, 4.0f }));
        bypassed.setSolo (0);
        StereoSignal again (4096, sampleRate);
        processInBlocks (bypassed, again, 512, [] (int) {});
        CHECK (again.left == original.left && again.right == original.right);
    }
}

void testSoloSwitchingHasNoClicks()
{
    std::printf ("Solo: encender, pasar de banda (y de clase de filtro), mover la banda y soltar no salta; al soltar, bit a bit\n");
    for (const double sampleRate : sampleRates)
    {
        Engine::BandArray bands {};
        bands[0] = { true, 1000.0f, 0.0f, 2.0f };                      // campana en 0 dB: sin solo, la señal intacta
        bands[1] = { true, 150.0f, 0.0f, 0.71f, Shape::lowShelf };     // pasa bajos
        bands[2] = { false, 5000.0f, 0.0f, 0.71f, Shape::highCut, 4 }; // pasa altos
        Engine engine;
        engine.prepare (sampleRate, false, 0.0f, bands);
        StereoSignal signal (static_cast<int> (sampleRate * 1.2), sampleRate);
        const StereoSignal original = signal;
        const int tenth = static_cast<int> (sampleRate * 0.1);
        processInBlocks (engine, signal, 300, [&] (int start) {
            const int step = start / tenth;
            engine.setSolo (step < 1 ? -1 : step < 3 ? 0 : step < 5 ? 1 : step < 7 ? 2 : step < 8 ? 0 : -1);
            // Mientras la banda 1 está en solo, su frecuencia salta entre bloques (sus rampas la suavizan).
            bands[0].frequency = step == 2 && (start / 300) % 2 == 0 ? 3000.0f : 1000.0f;
            engine.setBand (0, bands[0]);
        });
        // Cota: la pendiente máxima de la señal de prueba a 0 dB (los dos senos juntos); un salto de golpe entre la zona
        // y la mezcla la supera.
        const double steepest = 0.3 * 2.0 * pi * 3150.0 / sampleRate + 0.5 * 2.0 * pi * 220.0 / sampleRate;
        float biggestStep = 0.0f;
        for (size_t i = 1; i < signal.left.size(); ++i)
            biggestStep = std::max ({ biggestStep, std::abs (signal.left[i] - signal.left[i - 1]), std::abs (signal.right[i] - signal.right[i - 1]) });
        std::printf ("  %6.0f Hz: salto mayor %.4f (cota %.4f)\n", sampleRate, static_cast<double> (biggestStep), steepest);
        CHECK (biggestStep < steepest);
        CHECK (! engine.isSoloing());
        bool exact = true;
        for (size_t i = static_cast<size_t> (9 * tenth); i < signal.left.size(); ++i)
            exact = exact && signal.left[i] == original.left[i] && signal.right[i] == original.right[i];
        CHECK (exact);
    }
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
    testAnalyzerView();
    testSpectrumPeaks();
    testGainScale();
    testPanAndPolarity();
    testAutoGain();
    testSoloZones();
    testSoloSwitchingHasNoClicks();

    if (failures > 0)
    {
        std::printf ("\n%d FALLO(S)\n", failures);
        return 1;
    }
    std::printf ("\nTodos los tests pasaron\n");
    return 0;
}
