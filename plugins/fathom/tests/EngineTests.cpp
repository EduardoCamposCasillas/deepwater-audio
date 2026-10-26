// Tests de Fathom sin JUCE: el motor de audio (salida, bypass, las bandas, auto gain, solo, estéreo por banda, EQ dinámico y modos de fase), las cuentas de la edición (deshacer, arrastrar
// nodos) y las del analizador (inclinación, balística, retención y picos). Se lanzan con ctest. Si algo falla, imprime el archivo, la línea y la
// condición.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <tuple>
#include <thread>
#include <vector>

#include "analysis/SpectrumView.h"
#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/MatchedEq.h"
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
        // Un shelf de graves sobre Nyquist con la BILINEAL sube TODO 6 dB (aprieta la subida contra Nyquist): compensa −6.
        // (Con la respuesta analógica no: el shelf vale la mitad en su frecuencia, y los últimos kHz suben menos.)
        BandSettings allUp { true, 30000.0f, 6.0f, 0.71f, Shape::lowShelf };
        allUp.analog = false;
        const float everything = compensation (only (allUp));
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
            engine.prepare (sampleRate, false, 0.0f, only (allUp));
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
// --- Fase 7: respuesta analógica --------------------------------------------------------------------------------------

void testAnalogResponse()
{
    std::printf ("Respuesta analogica: una campana y un shelf agudos suenan como el prototipo analogico (la bilineal no) y como su curva\n");
    for (const double sampleRate : sampleRates)
    {
        // Campana de +12 dB en 10 kHz con Q 1 y shelf de agudos de −9 dB en 6 kHz: medidos en el audio, arriba.
        const BandSettings bell { true, 10000.0f, 12.0f, 1.0f, Shape::bell };
        const BandSettings shelf { true, 6000.0f, -9.0f, 0.71f, Shape::highShelf };
        const auto analogBell = deepwater::dsp::AnalogBiquad::bell (10000.0, 1.0, 12.0);
        const auto analogShelf = deepwater::dsp::AnalogBiquad::highShelf (6000.0, 0.71, -9.0);
        double worstAnalog = 0.0, worstBilinear = 0.0, worstCurve = 0.0;
        for (const bool analog : { true, false })
        {
            auto bands = only (bell);
            bands[0].analog = analog;
            bands[1] = shelf;
            bands[1].analog = analog;
            for (const double hz : { 1000.0, 8000.0, 12000.0, 16000.0, 19000.0 })
            {
                Engine engine;
                engine.prepare (sampleRate, false, 0.0f, bands);
                auto left = sine (hz, 0.1, sampleRate, 32768);
                auto right = left;
                float* channels[] { left.data(), right.data() };
                engine.process (channels, 2, static_cast<int> (left.size()));
                const double measured = 20.0 * std::log10 (toneAmplitude (left, hz, sampleRate, 16384) / 0.1);
                const double target = 10.0 * std::log10 (analogBell.power (hz) * analogShelf.power (hz));
                (analog ? worstAnalog : worstBilinear) = std::max (analog ? worstAnalog : worstBilinear, std::abs (measured - target));
                worstCurve = std::max (worstCurve, std::abs (measured - curveDb (bands, 0.0f, hz, sampleRate)));
            }
        }
        std::printf ("  %6.0f Hz: contra el analogico %.3f dB (bilineal %.2f dB); audio contra la curva %.5f dB\n", sampleRate, worstAnalog,
                     worstBilinear, worstCurve);
        CHECK (worstAnalog < 1.0); // lo peor, en 19 kHz a 44.1 kHz (0.63 dB)
        CHECK (worstBilinear > 2.0 * worstAnalog);
        CHECK (worstCurve < 0.01);
    }

    // En graves no cambia nada que se oiga: una campana de 200 Hz suena igual con las dos respuestas.
    BandSettings low { true, 200.0f, 9.0f, 2.0f };
    const auto analogCurve = Band::coefficientsFor (low, 48000.0);
    low.analog = false;
    const auto bilinearCurve = Band::coefficientsFor (low, 48000.0);
    double worstLow = 0.0;
    for (double hz = 20.0; hz < 2000.0; hz *= 1.05)
        worstLow = std::max (worstLow, std::abs (analogCurve.magnitudeDb (hz, 48000.0) - bilinearCurve.magnitudeDb (hz, 48000.0)));
    CHECK (worstLow < 0.01);

    // Lo que no tenga el ajuste (un proyecto de las Fases 1-6) suena con la respuesta analógica; en 0 dB, bit a bit igual.
    CHECK (BandSettings {}.analog);
}

void testAnalogSwitchHasNoClicks()
{
    std::printf ("Respuesta analogica: alternar analogica/bilineal en cada bloque (con bandas agudas fuertes) no hace clic\n");
    for (const double sampleRate : sampleRates)
    {
        Engine::BandArray bands {};
        bands[0] = { true, 8000.0f, 24.0f, 2.0f, Shape::bell };
        bands[1] = { true, 12000.0f, -18.0f, 0.71f, Shape::highShelf };
        bands[2] = { true, 9000.0f, 0.0f, 2.0f, Shape::highCut, 6 };
        bands[3] = { true, 3000.0f, 18.0f, 0.5f, Shape::lowShelf };
        // Dos senos (1 y 5 kHz) por las mismas bandas: una vez alternando cada bloque, otra con la respuesta analógica fija.
        const auto run = [&] (bool toggle) {
            Engine engine;
            engine.prepare (sampleRate, false, 0.0f, bands);
            auto left = sine (1000.0, 0.05, sampleRate, static_cast<size_t> (sampleRate));
            const auto high = sine (5000.0, 0.02, sampleRate, left.size());
            for (size_t i = 0; i < left.size(); ++i)
                left[i] += high[i];
            auto right = left;
            BlockSizes sizes;
            std::uint32_t seed = 7;
            auto settings = bands;
            const int total = static_cast<int> (left.size());
            for (int start = 0; start < total;)
            {
                const int count = std::min (sizes.next (256), total - start);
                seed = seed * 1664525u + 1013904223u;
                for (int b = 0; b < 4; ++b)
                {
                    settings[static_cast<size_t> (b)].analog = ! toggle || (seed >> 16) % 2 == 0;
                    engine.setBand (b, settings[static_cast<size_t> (b)]);
                }
                float* channels[] { left.data() + start, right.data() + start };
                engine.process (channels, 2, count);
                start += count;
            }
            CHECK (left == right);
            return left;
        };
        // Un clic es un salto: energía en TODAS las frecuencias. La entrada no tiene nada encima de 5 kHz, así que se
        // mira lo que sale encima de 14 kHz (pasa altos de 96 dB/oct). Cambiar los coeficientes despacio solo agrega
        // bandas laterales angostas junto a los senos; de golpe, ruido de banda ancha. Medido: con la rampa de 20 ms,
    // 0.000005; cambiando de golpe (rampa de 1 µs), 0.0054 (mil veces más: el test falla).
        const auto above = [sampleRate] (const std::vector<float>& signal) {
            const auto highPass = deepwater::dsp::EqCascade::butterworthHighPass (14000.0, 16, 0.71, sampleRate);
            deepwater::dsp::EqCascadeFilter filter;
            float peak = 0.0f;
            for (size_t i = 0; i < signal.size(); ++i)
            {
                const float y = filter.process (signal[i], highPass);
                if (i > signal.size() / 10) // después de que el pasa altos se asienta
                    peak = std::max (peak, std::abs (y));
            }
            return peak;
        };
        const float clicks = above (run (true)), still = above (run (false));
        std::printf ("  %6.0f Hz: encima de 14 kHz, alternando %.6f, fija %.6f\n", sampleRate, static_cast<double> (clicks),
                     static_cast<double> (still));
        CHECK (clicks < 2.0e-4f);
    }
}
// Fase 8: el mid y el side de un par estéreo.
std::vector<float> midOf (const std::vector<float>& left, const std::vector<float>& right)
{
    std::vector<float> mid (left.size());
    for (size_t i = 0; i < mid.size(); ++i)
        mid[i] = 0.5f * (left[i] + right[i]);
    return mid;
}

std::vector<float> sideOf (const std::vector<float>& left, const std::vector<float>& right)
{
    std::vector<float> side (left.size());
    for (size_t i = 0; i < side.size(); ++i)
        side[i] = 0.5f * (left[i] - right[i]);
    return side;
}

float biggestDifference (const std::vector<float>& a, const std::vector<float>& b)
{
    float biggest = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        biggest = std::max (biggest, std::abs (a[i] - b[i]));
    return biggest;
}

BandSettings placed (BandSettings band, Placement placement)
{
    band.placement = placement;
    return band;
}

void testStereoPlacements()
{
    std::printf ("Estereo por banda: L y R tocan solo su canal (el otro bit a bit), mid y side solo su parte, en mono el side no hace nada\n");
    for (const double sampleRate : sampleRates)
    {
        // El mismo tono con distinto nivel a cada lado: mid = 0.3, side = 0.1 (los dos con contenido).
        constexpr double hz = 1000.0;
        const auto numSamples = static_cast<size_t> (32768);
        const auto inLeft = sine (hz, 0.4, sampleRate, numSamples), inRight = sine (hz, 0.2, sampleRate, numSamples);
        const BandSettings bell { true, 1000.0f, 12.0f, 1.0f };
        const double gain = std::abs (Band::coefficientsFor (bell, sampleRate).response (hz, sampleRate));
        const auto run = [&] (Placement placement, std::vector<float>& left, std::vector<float>& right) {
            Engine engine;
            engine.prepare (sampleRate, false, 0.0f, only (placed (bell, placement)));
            left = inLeft;
            right = inRight;
            float* channels[] { left.data(), right.data() };
            engine.process (channels, 2, static_cast<int> (numSamples));
        };
        const auto level = [&] (const std::vector<float>& signal, double amplitude) {
            return toneAmplitude (signal, hz, sampleRate, numSamples / 2) / amplitude;
        };
        std::vector<float> left, right;

        run (Placement::left, left, right);
        CHECK (right == inRight && std::abs (level (left, 0.4) / gain - 1.0) < 1.0e-3);
        run (Placement::right, left, right);
        CHECK (left == inLeft && std::abs (level (right, 0.2) / gain - 1.0) < 1.0e-3);

        // Mid: el centro sube y el side queda igual (salvo redondeo); side: al revés.
        run (Placement::mid, left, right);
        CHECK (std::abs (level (midOf (left, right), 0.3) / gain - 1.0) < 1.0e-3);
        CHECK (biggestDifference (sideOf (left, right), sideOf (inLeft, inRight)) < 1.0e-6f);
        run (Placement::side, left, right);
        CHECK (std::abs (level (sideOf (left, right), 0.1) / gain - 1.0) < 1.0e-3);
        CHECK (biggestDifference (midOf (left, right), midOf (inLeft, inRight)) < 1.0e-6f);

        run (Placement::stereo, left, right);
        CHECK (std::abs (level (left, 0.4) / gain - 1.0) < 1.0e-3 && std::abs (level (right, 0.2) / gain - 1.0) < 1.0e-3);

        // Bit a bit: una banda mid o side en 0 dB no toca nada; una side, por fuerte que sea, no toca un sonido al
        // centro (L = R: el side es 0 exacto); una mid no toca un sonido "fuera de fase" (L = −R).
        for (const auto placement : { Placement::mid, Placement::side })
        {
            Engine flat;
            flat.prepare (sampleRate, false, 0.0f, only (placed ({ true, 1000.0f, 0.0f, 1.0f }, placement)));
            StereoSignal signal (4096, sampleRate);
            const StereoSignal original = signal;
            processInBlocks (flat, signal, 512, [] (int) {});
            CHECK (signal.left == original.left && signal.right == original.right);
        }
        {
            Engine side;
            side.prepare (sampleRate, false, 0.0f, only (placed (bell, Placement::side)));
            auto centreLeft = inLeft, centreRight = inLeft;
            float* channels[] { centreLeft.data(), centreRight.data() };
            side.process (channels, 2, static_cast<int> (numSamples));
            CHECK (centreLeft == inLeft && centreRight == inLeft);

            Engine mid;
            mid.prepare (sampleRate, false, 0.0f, only (placed (bell, Placement::mid)));
            auto wideLeft = inLeft;
            std::vector<float> wideRight (numSamples);
            for (size_t i = 0; i < numSamples; ++i)
                wideRight[i] = -inLeft[i];
            const auto originalRight = wideRight;
            float* wide[] { wideLeft.data(), wideRight.data() };
            mid.process (wide, 2, static_cast<int> (numSamples));
            CHECK (wideLeft == inLeft && wideRight == originalRight);
        }

        // Mono: el side no hace nada (bit a bit); izquierda, derecha y mid procesan el canal como una estéreo.
        for (const auto placement : { Placement::stereo, Placement::left, Placement::right, Placement::mid, Placement::side })
        {
            Engine mono;
            mono.prepare (sampleRate, false, 0.0f, only (placed (bell, placement)));
            auto signal = inLeft;
            float* channel[] { signal.data() };
            mono.process (channel, 1, static_cast<int> (numSamples));
            if (placement == Placement::side)
                CHECK (signal == inLeft);
            else
                CHECK (std::abs (level (signal, 0.4) / gain - 1.0) < 1.0e-3);
        }
    }
}

void testPlacementChangesHaveNoClicks()
{
    std::printf ("Estereo por banda: cambiar el canal de tres bandas al azar en cada bloque no hace clic\n");
    for (const double sampleRate : sampleRates)
    {
        Engine engine;
        engine.prepare (sampleRate, false, 0.0f, only ({ true, 1000.0f, 12.0f, 1.0f }));
        auto left = sine (1000.0, 0.05, sampleRate, static_cast<size_t> (sampleRate));
        auto right = sine (1000.0, 0.02, sampleRate, static_cast<size_t> (sampleRate));
        BlockSizes sizes;
        std::uint32_t seed = 77;
        auto random = [&seed] (std::uint32_t range) {
            seed = seed * 1664525u + 1013904223u;
            return (seed >> 16) % range;
        };
        const int total = static_cast<int> (left.size());
        for (int start = 0; start < total;)
        {
            const int count = std::min (sizes.next (256), total - start);
            for (int b = 0; b < 3; ++b)
                engine.setBand (b, placed ({ true, b == 1 ? 3000.0f : 1000.0f, random (2) == 0 ? -24.0f : 24.0f, 1.0f },
                                           static_cast<Placement> (random (numPlacements))));
            float* channels[] { left.data() + start, right.data() + start };
            engine.process (channels, 2, count);
            start += count;
        }
        // La cota de una banda de +24 dB sobre el canal más fuerte (con margen): ×16 de 0.05.
        const double steepest = 16.0 * 0.05 * std::pow (2.0 * std::sin (pi * 1000.0 / sampleRate), 2.0);
        const float measured = std::max (biggestSecondDifference (left), biggestSecondDifference (right));
        std::printf ("  %6.0f Hz: segunda diferencia %.4f (cota %.4f)\n", sampleRate, static_cast<double> (measured), 1.5 * steepest);
        CHECK (measured < 1.5 * steepest && std::isfinite (measured));
    }
}

void testStereoAutoGainAndSolo()
{
    std::printf ("Estereo por banda: auto gain (side < estereo, mid + side = estereo, mono sin side) y solo en el canal de la banda\n");
    for (const double sampleRate : sampleRates)
    {
        const auto compensation = [sampleRate] (const Engine::BandArray& bands, bool mono = false) {
            AutoGain autoGain;
            autoGain.prepare (sampleRate);
            autoGain.setMono (mono);
            for (int b = 0; b < Engine::numBands; ++b)
                autoGain.setBand (b, bands[static_cast<size_t> (b)]);
            return autoGain.compensationDb();
        };
        const BandSettings wide { true, 2000.0f, 6.0f, 0.5f };
        const float stereo = compensation (only (wide));
        const float mid = compensation (only (placed (wide, Placement::mid)));
        const float side = compensation (only (placed (wide, Placement::side)));
        const float left = compensation (only (placed (wide, Placement::left)));
        std::printf ("  %6.0f Hz: estereo %.2f, mid %.2f, izquierda %.2f, side %.2f dB\n", sampleRate, static_cast<double> (stereo),
                     static_cast<double> (mid), static_cast<double> (left), static_cast<double> (side));
        // El side de una mezcla típica tiene menos energía que el mid: subirlo cambia menos la sonoridad.
        CHECK (stereo < mid && mid < left && left < side && side < 0.0f);
        // Mid + side con la misma banda = estéreo; izquierda + derecha, también.
        Engine::BandArray split {};
        split[0] = placed (wide, Placement::mid);
        split[1] = placed (wide, Placement::side);
        CHECK (std::abs (compensation (split) - stereo) < 1.0e-4f);
        split[0] = placed (wide, Placement::left);
        split[1] = placed (wide, Placement::right);
        CHECK (std::abs (compensation (split) - stereo) < 1.0e-4f);
        // En mono el side no existe y el mid es todo.
        CHECK (std::abs (compensation (only (placed (wide, Placement::side)), true)) < 1.0e-6f);
        CHECK (std::abs (compensation (only (placed (wide, Placement::mid)), true) - stereo) < 1.0e-4f);

        // Solo de una banda side: se oye la zona del side (L = z, R = −z: nada en el centro); de una izquierda, nada a
        // la derecha. Se mira la segunda mitad (pasado el fundido del solo).
        const auto numSamples = static_cast<size_t> (32768);
        const auto inLeft = sine (1000.0, 0.4, sampleRate, numSamples), inRight = sine (1000.0, 0.2, sampleRate, numSamples);
        const BandSettings bell { true, 1000.0f, 6.0f, 4.0f };
        const double zone = std::abs (Band::coefficientsFor (*soloZoneFor (bell), sampleRate).response (1000.0, sampleRate));
        for (const auto placement : { Placement::side, Placement::left })
        {
            Engine engine;
            engine.prepare (sampleRate, false, 0.0f, only (placed (bell, placement)));
            engine.setSolo (0);
            auto outLeft = inLeft, outRight = inRight;
            float* channels[] { outLeft.data(), outRight.data() };
            engine.process (channels, 2, static_cast<int> (numSamples));
            if (placement == Placement::side)
            {
                const auto centre = midOf (outLeft, outRight);
                CHECK (biggestDifference (std::vector<float> (centre.begin() + numSamples / 2, centre.end()), std::vector<float> (numSamples / 2, 0.0f)) == 0.0f);
                CHECK (std::abs (toneAmplitude (outLeft, 1000.0, sampleRate, numSamples / 2) / (0.1 * zone) - 1.0) < 1.0e-3);
            }
            else
            {
                CHECK (std::vector<float> (outRight.begin() + numSamples / 2, outRight.end()) == std::vector<float> (numSamples / 2, 0.0f));
                CHECK (std::abs (toneAmplitude (outLeft, 1000.0, sampleRate, numSamples / 2) / (0.4 * zone) - 1.0) < 1.0e-3);
            }
        }
    }
}

// Fase 9: una campana dinámica (Q 2: la zona es un pasa banda de ~0.7 octavas).
BandSettings dynamicBell (float hz, float gainDb, float rangeDb, float thresholdDb)
{
    BandSettings band { true, hz, gainDb, 2.0f };
    band.dynamics.enabled = true;
    band.dynamics.rangeDb = rangeDb;
    band.dynamics.thresholdDb = thresholdDb;
    return band;
}

// Procesa un par estéreo en bloques de tamaño variable, con un sidechain opcional (el mismo en los dos canales).
void runStereo (Engine& engine, std::vector<float>& left, std::vector<float>& right, const std::vector<float>* sidechain = nullptr)
{
    BlockSizes sizes;
    const int total = static_cast<int> (left.size());
    for (int start = 0; start < total;)
    {
        const int count = std::min (sizes.next (512), total - start);
        float* channels[] { left.data() + start, right.data() + start };
        if (sidechain != nullptr)
        {
            const float* key[] { sidechain->data() + start, sidechain->data() + start };
            engine.process (channels, 2, count, key, 2);
        }
        else
            engine.process (channels, 2, count);
        start += count;
    }
}

double levelDb (double amplitude) { return 20.0 * std::log10 (amplitude); }

void testDynamicGainCurve()
{
    std::printf ("Dinamica: la curva de ganancia (rodilla de 6 dB, 1 dB por dB hasta el rango)\n");
    const auto change = [] (float level, float threshold, float range) { return BandDynamics::gainChangeDb (level, threshold, range); };
    CHECK (change (-30.0f, -20.0f, -6.0f) == 0.0f);
    CHECK (change (-23.0f, -20.0f, -6.0f) == 0.0f);                    // el borde de abajo de la rodilla
    CHECK (std::abs (change (-20.0f, -20.0f, -6.0f) + 0.75f) < 1.0e-6f); // en el umbral: rodilla / 8
    CHECK (std::abs (change (-17.0f, -20.0f, -6.0f) + 3.0f) < 1.0e-6f);  // el borde de arriba: ya 1 dB por dB
    CHECK (std::abs (change (-16.0f, -20.0f, -12.0f) + 4.0f) < 1.0e-6f);
    CHECK (change (-10.0f, -20.0f, -6.0f) == -6.0f); // hasta el rango
    CHECK (change (-10.0f, -20.0f, 6.0f) == 6.0f);   // un rango positivo sube
    CHECK (change (0.0f, -20.0f, 0.0f) == 0.0f);
    // Sin saltos: de a 0.01 dB por la rodilla, la ganancia nunca cambia más de 0.01 dB.
    float previous = change (-30.0f, -20.0f, -30.0f), worst = 0.0f;
    for (float level = -30.0f; level < 20.0f; level += 0.01f)
    {
        const float now = change (level, -20.0f, -30.0f);
        worst = std::max (worst, std::abs (now - previous));
        previous = now;
    }
    CHECK (worst <= 0.0101f);
}

void testDynamicActsOnlyWhenTheZoneIsLoud()
{
    std::printf ("Dinamica: corta su zona cuando suena fuerte (= la curva con la ganancia dinamica) y no toca lo demas\n");
    for (const double sampleRate : sampleRates)
    {
        const auto numSamples = static_cast<size_t> (sampleRate);
        const auto band = dynamicBell (3000.0f, 0.0f, -6.0f, -24.0f);

        // Fuerte en su zona: −6 dBFS, 18 dB sobre el umbral → todo el rango.
        {
            Engine engine;
            engine.prepare (sampleRate, false, 0.0f, only (band));
            auto left = sine (3000.0, 0.5, sampleRate, numSamples), right = left;
            runStereo (engine, left, right);
            CHECK (engine.getDynamics (0).getGainDb() == -6.0f);
            const double expected = Band::coefficientsFor (band, sampleRate, -6.0f).magnitudeDb (3000.0, sampleRate);
            const double measured = levelDb (toneAmplitude (left, 3000.0, sampleRate, numSamples / 2) / 0.5);
            CHECK (std::abs (expected + 6.0) < 0.05);
            CHECK (std::abs (measured - expected) < 0.01);
            CHECK (left == right);
        }
        // Suave en su zona (−40 dBFS): no hace nada, bit a bit.
        {
            Engine engine;
            engine.prepare (sampleRate, false, 0.0f, only (band));
            auto left = sine (3000.0, 0.01, sampleRate, numSamples), right = left;
            const auto original = left;
            runStereo (engine, left, right);
            CHECK (left == original);
            CHECK (engine.getDynamics (0).getGainDb() == 0.0f);
        }
        // Fuerte pero lejos de su zona (300 Hz): el pasa banda del detector lo baja ~26 dB, debajo del umbral.
        {
            Engine engine;
            engine.prepare (sampleRate, false, 0.0f, only (band));
            auto left = sine (300.0, 0.5, sampleRate, numSamples), right = left;
            const auto original = left;
            runStereo (engine, left, right);
            CHECK (engine.getDynamics (0).getGainDb() == 0.0f);
            CHECK (biggestDifference (std::vector<float> (left.begin() + static_cast<long> (numSamples / 2), left.end()),
                                      std::vector<float> (original.begin() + static_cast<long> (numSamples / 2), original.end()))
                   == 0.0f);
        }
    }
}

void testDynamicThresholdAndBoost()
{
    std::printf ("Dinamica: el nivel (RMS + 3 dB) de un seno es su pico; 6 dB sobre el umbral = 6 dB de cambio (cortando o subiendo)\n");
    for (const double sampleRate : sampleRates)
        for (const float range : { -12.0f, 12.0f })
        {
            const double amplitude = 0.25;
            const auto threshold = static_cast<float> (levelDb (amplitude) - 6.0);
            Engine engine;
            engine.prepare (sampleRate, false, 0.0f, only (dynamicBell (3000.0f, 0.0f, range, threshold)));
            auto left = sine (3000.0, amplitude, sampleRate, static_cast<size_t> (sampleRate / 2)), right = left;
            runStereo (engine, left, right);
            const auto& dynamics = engine.getDynamics (0);
            CHECK (std::abs (static_cast<double> (dynamics.getLevelDb()) - levelDb (amplitude)) < 0.2); // la ventana de 1 ms ondula ±0.1 dB
            CHECK (std::abs (std::abs (dynamics.getGainDb()) - 6.0f) < 0.1f);
            CHECK ((dynamics.getGainDb() > 0.0f) == (range > 0.0f));
        }
}

void testDynamicAttackAndRelease()
{
    std::printf ("Dinamica: ataque y liberacion son el tiempo de la ganancia (la mitad del cambio en ln 2 = 0.69 veces), sin importar el umbral\n");
    const double sampleRate = 48000.0;
    // Cuánto tarda la ganancia en cruzar −6 dB (bajando con el tono, o subiendo después de él).
    const auto crossing = [sampleRate] (float attackMs, float releaseMs, bool measureRelease) {
        auto band = dynamicBell (1000.0f, 0.0f, -12.0f, -40.0f);
        band.dynamics.attackMs = attackMs;
        band.dynamics.releaseMs = releaseMs;
        Engine engine;
        engine.prepare (sampleRate, false, 0.0f, only (band));
        const auto tone = sine (1000.0, 0.5, sampleRate, static_cast<size_t> (sampleRate * 0.5));
        std::array<float, 16> left {}, right {};
        for (size_t at = 0; at < static_cast<size_t> (sampleRate * 4.0); at += left.size())
        {
            for (size_t i = 0; i < left.size(); ++i)
                left[i] = right[i] = at + i < tone.size() ? tone[at + i] : 0.0f;
            float* channels[] { left.data(), right.data() };
            engine.process (channels, 2, static_cast<int> (left.size()));
            const float gain = engine.getDynamics (0).getGainDb();
            if (! measureRelease && gain <= -6.0f)
                return static_cast<double> (at) / sampleRate;
            if (measureRelease && at >= tone.size() && gain > -6.0f)
                return static_cast<double> (at - tone.size()) / sampleRate;
        }
        return 100.0;
    };
    const double fastAttack = crossing (1.0f, 150.0f, false), slowAttack = crossing (50.0f, 150.0f, false);
    const double fastRelease = crossing (10.0f, 50.0f, true), slowRelease = crossing (10.0f, 500.0f, true);
    std::printf ("  ataque 1 ms: %.1f ms, 50 ms: %.1f ms; liberacion 50 ms: %.0f ms, 500 ms: %.0f ms\n", 1000.0 * fastAttack,
                 1000.0 * slowAttack, 1000.0 * fastRelease, 1000.0 * slowRelease);
    // La mitad del cambio de un filtro de 1 polo tarda ln 2 · τ. El ataque, eso aunque el umbral esté 34 dB debajo de la
    // señal (con el nivel suavizado en lineal llegaba en 2.7 ms en vez de 35). La liberación empieza unos 10 ms tarde: lo
    // que tarda el nivel en bajar los 22 dB que estaba por encima del umbral + el rango.
    CHECK (fastAttack < 0.004);
    CHECK (std::abs (slowAttack - 0.05 * std::log (2.0)) < 0.003);
    CHECK (fastRelease > 0.05 * std::log (2.0) && fastRelease < 0.05 * std::log (2.0) + 0.015);
    CHECK (slowRelease > 0.5 * std::log (2.0) && slowRelease < 0.5 * std::log (2.0) + 0.015);
}

void testAutoThreshold()
{
    std::printf ("Dinamica: con el umbral automatico, lo de siempre no se toca y lo que sube de golpe si\n");
    const double sampleRate = 44100.0;
    auto band = dynamicBell (3000.0f, 0.0f, -6.0f, -60.0f); // el umbral manual (−60) no cuenta
    band.dynamics.autoThreshold = true;
    Engine engine;
    engine.prepare (sampleRate, false, 0.0f, only (band));
    const auto& dynamics = engine.getDynamics (0);
    const auto play = [&] (double amplitude, double seconds) {
        auto left = sine (3000.0, amplitude, sampleRate, static_cast<size_t> (sampleRate * seconds)), right = left;
        runStereo (engine, left, right);
    };
    play (0.1, 6.0); // −20 dBFS sostenido: se vuelve el nivel típico
    std::printf ("  sostenido: umbral %.2f dB, ganancia %.2f dB\n", static_cast<double> (dynamics.getThresholdDb()),
                 static_cast<double> (dynamics.getGainDb()));
    CHECK (std::abs (dynamics.getThresholdDb() - (-20.0f + BandDynamics::autoMarginDb)) < 0.75f); // 3 dB sobre lo típico
    CHECK (dynamics.getGainDb() > -0.5f);
    play (0.4, 0.05); // +12 dB de golpe: corta todo el rango
    std::printf ("  de golpe +12 dB: ganancia %.2f dB\n", static_cast<double> (dynamics.getGainDb()));
    CHECK (dynamics.getGainDb() < -5.5f);
    play (0.4, 8.0); // si se queda así, pasa a ser lo normal
    std::printf ("  sostenido otra vez: umbral %.2f dB, ganancia %.2f dB\n", static_cast<double> (dynamics.getThresholdDb()),
                 static_cast<double> (dynamics.getGainDb()));
    CHECK (dynamics.getGainDb() > -1.0f);
}

void testDynamicSidechain()
{
    std::printf ("Dinamica: con el detector externo escucha el sidechain (sin sidechain, nada)\n");
    for (const double sampleRate : sampleRates)
    {
        const auto numSamples = static_cast<size_t> (sampleRate);
        auto band = dynamicBell (3000.0f, 0.0f, -6.0f, -24.0f);
        const auto quiet = sine (3000.0, 0.02, sampleRate, numSamples); // la pista: debajo del umbral
        const auto key = sine (3000.0, 0.5, sampleRate, numSamples);    // el sidechain: fuerte
        for (const bool external : { true, false })
            for (const bool connected : { true, false })
            {
                band.dynamics.external = external;
                Engine engine;
                engine.prepare (sampleRate, false, 0.0f, only (band));
                auto left = quiet, right = quiet;
                runStereo (engine, left, right, connected ? &key : nullptr);
                if (external && connected)
                    CHECK (std::abs (levelDb (toneAmplitude (left, 3000.0, sampleRate, numSamples / 2) / 0.02) + 6.0) < 0.05);
                else
                    CHECK (left == quiet);
            }
    }
}

void testDynamicStereoScaleAndAutoGain()
{
    std::printf ("Dinamica: una banda side no ve el centro; la escala escala el rango; el auto gain mira la ganancia propia\n");
    const double sampleRate = 48000.0;
    const auto numSamples = static_cast<size_t> (sampleRate);
    const auto tone = sine (3000.0, 0.5, sampleRate, numSamples);
    {
        Engine engine;
        engine.prepare (sampleRate, false, 0.0f, only (placed (dynamicBell (3000.0f, 0.0f, -6.0f, -40.0f), Placement::side)));
        auto left = tone, right = tone; // todo al centro: el side es silencio
        runStereo (engine, left, right);
        CHECK (left == tone && right == tone);
    }
    {
        Engine engine;
        engine.setGainScale (0.5f);
        engine.prepare (sampleRate, false, 0.0f, only (dynamicBell (3000.0f, 0.0f, -6.0f, -40.0f)));
        auto left = tone, right = tone;
        runStereo (engine, left, right);
        CHECK (engine.getDynamics (0).getGainDb() == -3.0f);
    }
    {
        Engine engine;
        engine.setAutoGain (true);
        engine.prepare (sampleRate, false, 0.0f, only (dynamicBell (3000.0f, 0.0f, -12.0f, -40.0f)));
        auto left = tone, right = tone;
        runStereo (engine, left, right);
        CHECK (engine.getDynamics (0).getGainDb() == -12.0f);
        CHECK (std::abs (engine.getAutoGainDb()) < 1.0e-6f); // la banda está en 0 dB: nada que compensar aunque se mueva
    }
}

void testDynamicOffIsBitExact()
{
    std::printf ("Dinamica: apagarla suelta la ganancia sin clic y despues la senal pasa bit a bit\n");
    for (const double sampleRate : sampleRates)
    {
        Engine engine;
        engine.prepare (sampleRate, false, 0.0f, only (dynamicBell (1000.0f, 0.0f, -12.0f, -40.0f)));
        auto left = sine (1000.0, 0.5, sampleRate, static_cast<size_t> (sampleRate * 0.6)), right = left;
        const auto original = left;
        const auto switchAt = static_cast<size_t> (sampleRate * 0.3);
        BlockSizes sizes;
        for (size_t start = 0; start < left.size();)
        {
            const auto count = std::min (static_cast<size_t> (sizes.next (256)), left.size() - start);
            auto band = dynamicBell (1000.0f, 0.0f, -12.0f, -40.0f);
            band.dynamics.enabled = start < switchAt;
            engine.setBand (0, band);
            float* channels[] { left.data() + start, right.data() + start };
            engine.process (channels, 2, static_cast<int> (count));
            start += count;
        }
        CHECK (left[switchAt / 2] != original[switchAt / 2]); // antes, cortaba
        const auto settled = switchAt + static_cast<size_t> (sampleRate * Band::rampSeconds) + 256 + 2 * Band::dynamicInterval;
        CHECK (std::vector<float> (left.begin() + static_cast<long> (settled), left.end())
               == std::vector<float> (original.begin() + static_cast<long> (settled), original.end()));
        const double steepest = 0.5 * std::pow (2.0 * std::sin (pi * 1000.0 / sampleRate), 2.0);
        CHECK (biggestSecondDifference (left) < 1.5 * steepest);
    }
}

void testDynamicChangesHaveNoClicks()
{
    std::printf ("Dinamica: todo al azar en cada bloque (con un sidechain a golpes) no hace clic\n");
    for (const double sampleRate : sampleRates)
    {
        Engine engine;
        engine.prepare (sampleRate, false, 0.0f, only (dynamicBell (1000.0f, 0.0f, -12.0f, -30.0f)));
        const auto numSamples = static_cast<size_t> (sampleRate);
        auto left = sine (1000.0, 0.05, sampleRate, numSamples), right = left;
        // El sidechain: el mismo tono, fuerte, entrando y saliendo cada 10 ms (de golpe).
        auto key = sine (1000.0, 0.5, sampleRate, numSamples);
        for (size_t i = 0; i < numSamples; ++i)
            if ((i / static_cast<size_t> (sampleRate * 0.01)) % 2 == 1)
                key[i] = 0.0f;
        BlockSizes sizes;
        std::uint32_t seed = 909;
        auto random = [&seed] (std::uint32_t range) {
            seed = seed * 1664525u + 1013904223u;
            return (seed >> 16) % range;
        };
        static constexpr std::array<Shape, 5> shapes { Shape::bell, Shape::lowShelf, Shape::highShelf, Shape::tiltShelf, Shape::lowCut };
        const int total = static_cast<int> (numSamples);
        for (int start = 0; start < total;)
        {
            const int count = std::min (sizes.next (256), total - start);
            for (int b = 0; b < 3; ++b)
            {
                BandSettings band { random (8) != 0, random (2) == 0 ? 700.0f : 1400.0f, random (2) == 0 ? -6.0f : 6.0f,
                                    random (2) == 0 ? 0.7f : 2.0f, shapes[random (static_cast<std::uint32_t> (shapes.size()))] };
                band.placement = random (4) == 0 ? Placement::mid : Placement::stereo;
                auto& d = band.dynamics;
                d.enabled = random (6) != 0;
                d.rangeDb = random (2) == 0 ? -24.0f : 24.0f;
                d.thresholdDb = random (2) == 0 ? -50.0f : -20.0f;
                d.attackMs = random (2) == 0 ? 0.5f : 30.0f;
                d.releaseMs = random (2) == 0 ? 5.0f : 300.0f;
                d.autoThreshold = random (5) == 0;
                d.external = random (2) == 0;
                engine.setBand (b, band);
            }
            float* channels[] { left.data() + start, right.data() + start };
            const float* sidechain[] { key.data() + start, key.data() + start };
            engine.process (channels, 2, count, sidechain, 2);
            start += count;
        }
        // Cota: un cuarto de lo que daría el tono subido al máximo que pueden sumar la banda y su rango (+30 dB ≈ ×32).
        // Con la ganancia dinámica aplicada de golpe cada 16 muestras (sin interpolar los coeficientes) sale entre 0.3
        // y 0.9 de esa cota; con la interpolación, menos de 0.08.
        const double steepest = 32.0 * 0.05 * std::pow (2.0 * std::sin (pi * 1000.0 / sampleRate), 2.0);
        const float measured = biggestSecondDifference (left);
        std::printf ("  %6.0f Hz: segunda diferencia %.4f (cota %.4f)\n", sampleRate, static_cast<double> (measured), 0.25 * steepest);
        CHECK (std::isfinite (measured));
        CHECK (measured < 0.25 * steepest);
        CHECK (left == right);
    }
}
// --- Fase 10: modos de fase -------------------------------------------------------------------------------------------

// La respuesta al impulso del motor (estéreo: el mismo impulso en los dos canales) en 'length' muestras.
std::array<std::vector<float>, 2> impulseResponse (Engine& engine, int length, float leftAmount = 1.0f, float rightAmount = 1.0f)
{
    std::array<std::vector<float>, 2> h { std::vector<float> (static_cast<size_t> (length), 0.0f),
                                          std::vector<float> (static_cast<size_t> (length), 0.0f) };
    h[0][0] = leftAmount;
    h[1][0] = rightAmount;
    for (int start = 0; start < length; start += 256)
    {
        const int count = std::min (256, length - start);
        float* channels[] { h[0].data() + start, h[1].data() + start };
        engine.process (channels, 2, count);
    }
    return h;
}

// La DFT de una respuesta al impulso en 'hz', sin el retardo 'delay' (H·e^(jωD)).
std::complex<double> responseAt (const std::vector<float>& h, double hz, double sampleRate, int delay)
{
    std::complex<double> sum {};
    for (size_t n = 0; n < h.size(); ++n)
        sum += static_cast<double> (h[n]) * std::polar (1.0, -2.0 * pi * hz * (static_cast<double> (n) - delay) / sampleRate);
    return sum;
}

std::vector<double> logFrequencies (double from, double to, int count)
{
    std::vector<double> hz;
    for (int i = 0; i < count; ++i)
        hz.push_back (from * std::pow (to / from, i / (count - 1.0)));
    return hz;
}

// Una banda encendida, con lo esencial.
BandSettings bandOf (Shape shape, float hz, float gainDb, float q, int slopeIndex = 1)
{
    BandSettings band;
    band.enabled = true;
    band.shape = shape;
    band.frequency = hz;
    band.gainDb = gainDb;
    band.q = q;
    band.slopeIndex = slopeIndex;
    return band;
}

// Un motor en un modo de fase, listo (offline: los FIR se diseñan en el mismo hilo).
void prepareIn (Engine& engine, PhaseMode mode, int quality, double sampleRate, const Engine::BandArray& bands)
{
    engine.setPhaseMode (mode, quality);
    engine.prepare (sampleRate, false, 0.0f, bands);
}

void testPhaseLatencies()
{
    std::printf ("Fase: latencias informadas (la mitad del FIR + un bloque de la convolucion), el doble de muestras a 88.2/96\n");
    for (const double sampleRate : sampleRates)
    {
        const int factor = sampleRate > 50000.0 ? 2 : 1;
        Engine engine;
        CHECK (engine.getLatencySamples() == 0);
        prepareIn (engine, PhaseMode::natural, 0, sampleRate, {});
        CHECK (engine.getLatencySamples() == (128 + 32) * factor);
        for (int q = 0; q < numLinearQualities; ++q)
        {
            engine.setPhaseMode (PhaseMode::linear, q);
            CHECK (engine.getLatencySamples() == ((2048 << q) / 2 + 512) * factor);
        }
        engine.setPhaseMode (PhaseMode::zeroLatency, 0);
        CHECK (engine.getLatencySamples() == 0);
    }
    for (int q = 0; q < numLinearQualities; ++q)
        std::printf ("   lineal %d: %d muestras a 44.1 kHz (%.0f ms)\n", q, PhaseSizes::latency (PhaseMode::linear, q, 44100.0),
                     1000.0 * PhaseSizes::latency (PhaseMode::linear, q, 44100.0) / 44100.0);
    std::printf ("   natural: %d muestras (%.1f ms)\n", PhaseSizes::latency (PhaseMode::natural, 0, 44100.0),
                 1000.0 * PhaseSizes::latency (PhaseMode::natural, 0, 44100.0) / 44100.0);
}

void testPhaseModesFlatAreTransparent()
{
    std::printf ("Fase: con todo plano (y en bypass) cada modo es la entrada retrasada su latencia, bit a bit\n");
    for (const double sampleRate : { 44100.0, 96000.0 })
        for (const auto mode : { PhaseMode::natural, PhaseMode::linear })
            for (const bool bypassed : { false, true })
            {
                auto bands = only (bandOf (Shape::bell, 1000.0f, 0.0f, 1.0f)); // encendida pero en 0 dB
                if (bypassed)
                    bands[1] = bandOf (Shape::bell, 3000.0f, 9.0f, 2.0f); // con bypass suena lo de antes, aunque haya bandas
                Engine engine;
                engine.setPhaseMode (mode, 0);
                engine.prepare (sampleRate, bypassed, 0.0f, bands);
                const int latency = engine.getLatencySamples();
                StereoSignal signal (latency + 4096, sampleRate);
                const StereoSignal original = signal;
                processInBlocks (engine, signal, 700, [] (int) {});
                bool exact = true;
                for (size_t i = 0; i < signal.left.size(); ++i)
                {
                    const bool before = i < static_cast<size_t> (latency);
                    const float left = before ? 0.0f : original.left[i - static_cast<size_t> (latency)];
                    const float right = before ? 0.0f : original.right[i - static_cast<size_t> (latency)];
                    exact = exact && signal.left[i] == left && signal.right[i] == right;
                }
                CHECK (exact);
            }
}

void testLinearPhaseIsSymmetricWithTheCurve()
{
    std::printf ("Fase lineal: la respuesta al impulso es simetrica (todas las frecuencias se retrasan igual) y su magnitud es la curva\n");
    for (const double sampleRate : sampleRates)
    {
        Engine::BandArray bands {};
        bands[0] = bandOf (Shape::bell, 1000.0f, 6.0f, 1.0f);
        bands[1] = bandOf (Shape::lowCut, 80.0f, 0.0f, 0.71f, 3); // 24 dB/oct
        bands[2] = bandOf (Shape::highShelf, 6000.0f, -4.0f, 0.71f);
        bands[3] = bandOf (Shape::notch, 3500.0f, 0.0f, 6.0f);
        Engine engine;
        prepareIn (engine, PhaseMode::linear, 2, sampleRate, bands); // alta
        const int latency = engine.getLatencySamples();
        const int length = PhaseSizes::linearLength (2, sampleRate);
        const auto h = impulseResponse (engine, latency + length);
        // El FIR va de latency − N/2 a latency + N/2: simétrico alrededor de 'latency'.
        float asymmetry = 0.0f, peak = 0.0f;
        for (int k = 0; k < length / 2; ++k)
        {
            asymmetry = std::max (asymmetry, std::abs (h[0][static_cast<size_t> (latency + k)] - h[0][static_cast<size_t> (latency - k)]));
            peak = std::max (peak, std::abs (h[0][static_cast<size_t> (latency + k)]));
        }
        CHECK (asymmetry < 1.0e-6f * peak);
        CHECK (h[0] == h[1]);

        // Magnitud contra la curva de las bandas (los mismos coeficientes), de 60 Hz a 18 kHz y donde no está cortado.
        double worstDb = 0.0, worstPhase = 0.0;
        for (const double hz : logFrequencies (60.0, std::min (18000.0, 0.45 * sampleRate), 120))
        {
            double curveDb = 0.0;
            for (const auto& band : bands)
                curveDb += Band::coefficientsFor (band, sampleRate).magnitudeDb (hz, sampleRate);
            if (curveDb < -30.0)
                continue;
            const auto measured = responseAt (h[0], hz, sampleRate, latency);
            worstDb = std::max (worstDb, std::abs (20.0 * std::log10 (std::abs (measured)) - curveDb));
            worstPhase = std::max (worstPhase, std::abs (std::arg (measured)) * 180.0 / pi);
        }
        CHECK (worstDb < 0.1);
        CHECK (worstPhase < 0.5);
        std::printf ("   %6.0f Hz: |error| %.3f dB, fase %.3f grados\n", sampleRate, worstDb, worstPhase);
    }
}

void testNaturalPhaseMatchesTheAnalog()
{
    std::printf ("Fase natural: magnitud Y fase del prototipo analogico tambien cerca de Nyquist (cero latencia: solo la magnitud)\n");
    // Tres casos: bandas agudas suaves; una campana angosta cerca de Nyquist con un corte de 48 dB/oct, un shelf grave y
    // un all-pass; y lo extremo (campana Q 20 en 18 kHz, notch Q 10, shelf en 19 kHz).
    for (const int hard : { 0, 1, 2 })
        for (const double sampleRate : sampleRates)
    {
        Engine::BandArray bands {};
        bands[0] = bandOf (Shape::bell, 12000.0f, 9.0f, 1.0f);
        bands[1] = bandOf (Shape::highShelf, 5000.0f, -6.0f, 0.71f);
        bands[2] = bandOf (Shape::bell, 300.0f, -4.0f, 2.0f);
        bands[3] = bandOf (Shape::highCut, 18000.0f, 0.0f, 0.71f, 1);
        if (hard)
        {
            bands[0] = bandOf (Shape::bell, 15000.0f, -12.0f, 8.0f);
            bands[3] = bandOf (Shape::highCut, 16000.0f, 0.0f, 0.71f, 8);
            bands[4] = bandOf (Shape::lowShelf, 40.0f, 6.0f, 1.0f);
            bands[5] = bandOf (Shape::allPass, 9000.0f, 0.0f, 2.0f);
        }
        if (hard == 2)
        {
            bands = {};
            bands[0] = bandOf (Shape::bell, 18000.0f, 12.0f, 20.0f);
            bands[1] = bandOf (Shape::notch, 16500.0f, 0.0f, 10.0f);
            bands[2] = bandOf (Shape::highShelf, 19000.0f, 9.0f, 2.0f);
        }
        const auto analog = [&] (double hz) {
            std::complex<double> total { 1.0 };
            for (const auto& band : bands)
                if (const auto prototype = Band::analogPrototype (band, sampleRate))
                    total *= prototype->response (hz);
            return total;
        };
        std::array<double, 2> worstDb {}, worstPhase {};
        for (const auto mode : { PhaseMode::zeroLatency, PhaseMode::natural })
        {
            Engine engine;
            prepareIn (engine, mode, 0, sampleRate, bands);
            const int latency = engine.getLatencySamples();
            const auto h = impulseResponse (engine, latency + static_cast<int> (0.2 * sampleRate));
            const auto m = static_cast<size_t> (mode == PhaseMode::natural);
            for (const double hz : logFrequencies (40.0, std::min (20000.0, 0.45 * sampleRate), 150))
            {
                const auto expected = analog (hz);
                if (std::abs (expected) < 0.03) // −30 dB: lo que el corte ya se llevó
                    continue;
                const auto measured = responseAt (h[0], hz, sampleRate, latency);
                worstDb[m] = std::max (worstDb[m], std::abs (20.0 * std::log10 (std::abs (measured) / std::abs (expected))));
                worstPhase[m] = std::max (worstPhase[m], std::abs (std::arg (measured / expected)) * 180.0 / pi);
            }
        }
        std::printf ("   %6.0f Hz: cero latencia %.2f dB %.1f grados; natural %.3f dB %.2f grados\n", sampleRate, worstDb[0], worstPhase[0],
                     worstDb[1], worstPhase[1]);
        CHECK (worstDb[1] < 0.25);
        CHECK (worstPhase[1] < 2.0);
        CHECK (worstPhase[1] < 0.25 * worstPhase[0]);
    }
}

void testPhaseModeSwitchHasNoClicks()
{
    std::printf ("Fase: cambiar de modo y de calidad sonando no hace clic (fundido de 20 ms entre las dos salidas)\n");
    struct Step
    {
        PhaseMode mode;
        int quality;
    };
    const std::array<Step, 8> steps { { { PhaseMode::natural, 0 },
                                        { PhaseMode::linear, 1 },
                                        { PhaseMode::linear, 3 },
                                        { PhaseMode::natural, 0 },
                                        { PhaseMode::zeroLatency, 0 },
                                        { PhaseMode::linear, 0 },
                                        { PhaseMode::zeroLatency, 0 },
                                        { PhaseMode::linear, 2 } } };
    for (const double sampleRate : sampleRates)
    {
        Engine::BandArray bands {};
        bands[0] = bandOf (Shape::bell, 1000.0f, 6.0f, 1.0f);
        bands[1] = bandOf (Shape::lowCut, 60.0f, 0.0f, 0.71f, 3);
        bands[2] = bandOf (Shape::highShelf, 8000.0f, 4.0f, 0.71f);
        Engine engine;
        prepareIn (engine, PhaseMode::zeroLatency, 0, sampleRate, bands);
        const int interval = static_cast<int> (0.6 * sampleRate);
        StereoSignal signal (interval * static_cast<int> (steps.size() + 1), sampleRate);
        bool latencyFollows = true;
        processInBlocks (engine, signal, 512, [&] (int start) {
            const auto step = static_cast<size_t> (start / interval);
            if (step >= 1 && step <= steps.size())
            {
                const auto& wanted = steps[step - 1];
                engine.setPhaseMode (wanted.mode, wanted.quality);
                latencyFollows = latencyFollows && engine.getLatencySamples() == PhaseSizes::latency (wanted.mode, wanted.quality, sampleRate);
            }
        });
        CHECK (latencyFollows);
        // Cota: lo más puntiagudo de cada seno (220 Hz × 0.5 a la izquierda, 3150 Hz × 0.3 a la derecha) con la
        // ganancia más alta de la curva por esa zona (+1 dB a lo sumo) y un margen.
        const auto bound = [&] (double hz, double amplitude) {
            const double s = 2.0 * std::sin (pi * hz / sampleRate);
            return static_cast<float> (1.5 * 1.15 * amplitude * s * s);
        };
        const std::vector<float> left (signal.left.begin() + interval / 2, signal.left.end());
        const std::vector<float> right (signal.right.begin() + interval / 2, signal.right.end());
        const float leftPeak = biggestSecondDifference (left), rightPeak = biggestSecondDifference (right);
        CHECK (leftPeak < bound (220.0, 0.5));
        CHECK (rightPeak < bound (3150.0, 0.3));
        std::printf ("   %6.0f Hz: segunda diferencia L %.5f (cota %.5f), R %.4f (cota %.4f)\n", sampleRate, leftPeak, bound (220.0, 0.5),
                     rightPeak, bound (3150.0, 0.3));
    }
}

void testLinearPhasePlacements()
{
    std::printf ("Fase lineal por canal: mid no toca el side, izquierda no toca la derecha; mismas ganancias que cero latencia\n");
    const double sampleRate = 48000.0;
    for (const auto placement : { Placement::mid, Placement::side, Placement::left })
    {
        auto band = bandOf (Shape::bell, 1000.0f, 9.0f, 1.0f);
        band.placement = placement;
        // Lo que la banda no debería ver: el side (contrafase) para mid, el centro para side, lo de la derecha para izquierda.
        const auto untouched = [&] (std::vector<float>& l, std::vector<float>& r) {
            const auto tone = sine (1000.0, 0.4, sampleRate, 16384);
            l = tone;
            r = tone;
            if (placement == Placement::mid)
                for (auto& x : r)
                    x = -x;
            if (placement == Placement::left)
                std::fill (l.begin(), l.end(), 0.0f);
        };
        Engine engine;
        prepareIn (engine, PhaseMode::linear, 1, sampleRate, only (band));
        const int latency = engine.getLatencySamples();
        std::vector<float> left, right;
        untouched (left, right);
        const auto originalLeft = left, originalRight = right;
        runStereo (engine, left, right);
        float worst = 0.0f;
        for (size_t i = static_cast<size_t> (latency); i < left.size(); ++i)
            worst = std::max ({ worst, std::abs (left[i] - originalLeft[i - static_cast<size_t> (latency)]),
                                std::abs (right[i] - originalRight[i - static_cast<size_t> (latency)]) });
        CHECK (worst < 1.0e-4f);

        // Lo que sí ve (solo su parte: un tono al centro para mid, en contrafase para side, a la izquierda para
        // izquierda): el mismo nivel que con cero latencia (la magnitud es la misma; cambia la fase). Con una mezcla de
        // las dos partes los niveles de L y R no tienen por qué coincidir: en cero latencia el mid filtrado vuelve con
        // la fase de la banda y se suma distinto con el side intacto.
        std::array<double, 2> levels {};
        for (const auto mode : { PhaseMode::zeroLatency, PhaseMode::linear })
        {
            Engine e;
            prepareIn (e, mode, 1, sampleRate, only (band));
            auto l = sine (1000.0, 0.4, sampleRate, 32768), r = l;
            if (placement == Placement::side)
                for (auto& x : r)
                    x = -x;
            if (placement == Placement::left)
                r = sine (2000.0, 0.2, sampleRate, 32768);
            runStereo (e, l, r);
            levels[mode == PhaseMode::linear] = levelDb (toneAmplitude (l, 1000.0, sampleRate, 16384) / 0.4);
        }
        CHECK (std::abs (levels[0] - levels[1]) < 0.02);
        CHECK (std::abs (levels[1] - 9.0) < 0.05);
    }
}

void testLinearPhaseDynamics()
{
    std::printf ("Fase lineal con EQ dinamico: corta lo mismo que con cero latencia y no toca lo suave\n");
    const double sampleRate = 44100.0;
    auto band = bandOf (Shape::bell, 1000.0f, 0.0f, 1.0f);
    band.dynamics.enabled = true;
    band.dynamics.rangeDb = -12.0f;
    band.dynamics.thresholdDb = -30.0f;
    for (const double amplitude : { 0.5, 0.002 })
    {
        std::array<double, 2> levels {};
        for (const auto mode : { PhaseMode::zeroLatency, PhaseMode::linear })
        {
            Engine engine;
            prepareIn (engine, mode, 1, sampleRate, only (band));
            auto l = sine (1000.0, amplitude, sampleRate, static_cast<size_t> (sampleRate)), r = l;
            runStereo (engine, l, r);
            levels[mode == PhaseMode::linear] = levelDb (toneAmplitude (l, 1000.0, sampleRate, l.size() / 2) / amplitude);
        }
        std::printf ("   seno de %.3f: cero latencia %.2f dB, lineal %.2f dB\n", amplitude, levels[0], levels[1]);
        CHECK (std::abs (levels[0] - levels[1]) < 0.3);
        CHECK (amplitude > 0.1 ? levels[1] < -11.0 : std::abs (levels[1]) < 0.01);
    }
}

void testPhaseDesignThread()
{
    std::printf ("Fase: en tiempo real los FIR se disenan en otro hilo y llegan en unos bloques\n");
    const double sampleRate = 48000.0;
    Engine engine;
    engine.setRealtime (true);
    prepareIn (engine, PhaseMode::linear, 1, sampleRate, {});
    // Una banda nueva con el motor ya sonando: el audio la pide y sigue; el hilo la diseña.
    engine.setBand (0, bandOf (Shape::bell, 2000.0f, -9.0f, 1.0f));
    auto left = sine (2000.0, 0.5, sampleRate, static_cast<size_t> (sampleRate)), right = left;
    for (size_t start = 0; start < left.size(); start += 256)
    {
        float* channels[] { left.data() + start, right.data() + start };
        engine.process (channels, 2, static_cast<int> (std::min<size_t> (256, left.size() - start)));
        std::this_thread::sleep_for (std::chrono::microseconds (500));
    }
    const double level = levelDb (toneAmplitude (left, 2000.0, sampleRate, left.size() / 2) / 0.5);
    std::printf ("   nivel con la campana de -9 dB: %.2f dB\n", level);
    CHECK (std::abs (level + 9.0) < 0.05);
}

void testPreRinging()
{
    std::printf ("Fase: el pre-eco (energia ANTES del golpe) de una campana angosta en cada modo\n");
    const double sampleRate = 44100.0;
    const auto bands = only (bandOf (Shape::bell, 100.0f, -12.0f, 8.0f));
    std::array<double, 3> before {};
    for (const auto mode : { PhaseMode::zeroLatency, PhaseMode::natural, PhaseMode::linear })
    {
        Engine engine;
        prepareIn (engine, mode, 3, sampleRate, bands);
        const int latency = engine.getLatencySamples();
        const auto h = impulseResponse (engine, latency + static_cast<int> (sampleRate));
        double early = 0.0, total = 0.0;
        for (size_t n = 0; n < h[0].size(); ++n)
        {
            const double e = static_cast<double> (h[0][n]) * h[0][n];
            total += e;
            if (n + 1 < static_cast<size_t> (latency))
                early += e;
        }
        before[static_cast<size_t> (mode)] = 10.0 * std::log10 (std::max (early, 1.0e-30) / total);
    }
    std::printf ("   energia antes del golpe: cero latencia %.0f dB, natural %.0f dB, lineal %.0f dB\n", std::max (before[0], -300.0), before[1],
                 before[2]);
    CHECK (before[0] < -200.0);           // cero latencia: nada antes del golpe (es causal)
    CHECK (before[1] < -60.0);            // natural: casi nada (el FIR de corrección es corto y casi un impulso)
    CHECK (before[2] > before[1] + 20.0); // lineal: la mitad del "ringing" de la campana suena antes
}

void testLinearQualityResolution()
{
    std::printf ("Fase lineal: la calidad es la resolucion en graves (una campana de +9 dB, Q 4 en 40 Hz)\n");
    const double sampleRate = 44100.0;
    const auto bands = only (bandOf (Shape::bell, 40.0f, 9.0f, 4.0f));
    double previous = 0.0;
    for (int q = 0; q < numLinearQualities; ++q)
    {
        Engine engine;
        prepareIn (engine, PhaseMode::linear, q, sampleRate, bands);
        auto l = sine (40.0, 0.1, sampleRate, static_cast<size_t> (2.0 * sampleRate)), r = l;
        runStereo (engine, l, r);
        const double gain = levelDb (toneAmplitude (l, 40.0, sampleRate, l.size() / 2) / 0.1);
        std::printf ("   calidad %d (%5d muestras): %.2f dB en 40 Hz\n", q, PhaseSizes::linearLength (q, sampleRate), gain);
        CHECK (gain > previous - 0.01);
        previous = gain;
    }
    CHECK (std::abs (previous - 9.0) < 0.5);
}

void testFirModesAutomationHasNoClicks()
{
    std::printf ("Fase natural y lineal: la banda saltando en cada bloque (automatizacion) no hace clic\n");
    for (const auto mode : { PhaseMode::natural, PhaseMode::linear })
        for (const double sampleRate : sampleRates)
        {
            // Lo mismo que en cero latencia: el FIR cambia (a lo sumo) una vez por bloque de la convolución, fundido.
            Engine engine;
            prepareIn (engine, mode, 0, sampleRate, only ({ true, 1000.0f, 0.0f, 1.0f }));
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
            const double steepest = 16.0 * 0.05 * std::pow (2.0 * std::sin (pi * 1000.0 / sampleRate), 2.0);
            const float measured = biggestSecondDifference (left);
            std::printf ("  %s %6.0f Hz: segunda diferencia %.4f (cota %.4f)\n", mode == PhaseMode::natural ? "natural" : "lineal ", sampleRate,
                         static_cast<double> (measured), 1.5 * steepest);
            CHECK (measured < 1.5 * steepest);
            CHECK (left == right);
        }
}

void testPhaseModesInMono()
{
    std::printf ("Fase natural y lineal en mono: la curva en el unico canal; una banda side no hace nada (retardo exacto)\n");
    const double sampleRate = 44100.0;
    for (const auto mode : { PhaseMode::natural, PhaseMode::linear })
    {
        Engine engine;
        prepareIn (engine, mode, 1, sampleRate, only (bandOf (Shape::bell, 1000.0f, 6.0f, 1.0f)));
        auto x = sine (1000.0, 0.25, sampleRate, 32768);
        float* mono[] { x.data() };
        engine.process (mono, 1, static_cast<int> (x.size()));
        CHECK (std::abs (toneDb (x, 1000.0, 0.25, sampleRate) - 6.0) < 0.02);

        auto side = bandOf (Shape::bell, 1000.0f, 6.0f, 1.0f);
        side.placement = Placement::side;
        Engine sideOnly;
        prepareIn (sideOnly, mode, 1, sampleRate, only (side));
        const int latency = sideOnly.getLatencySamples();
        auto y = sine (1000.0, 0.25, sampleRate, 16384);
        const auto original = y;
        for (size_t start = 0; start < y.size(); start += 333)
        {
            float* channel[] { y.data() + start };
            sideOnly.process (channel, 1, static_cast<int> (std::min<size_t> (333, y.size() - start)));
        }
        bool exact = true;
        for (size_t i = static_cast<size_t> (latency); i < y.size(); ++i)
            exact = exact && y[i] == original[i - static_cast<size_t> (latency)];
        CHECK (exact);
    }
}

void testLinearPhaseSolo()
{
    std::printf ("Fase lineal: el solo es la zona de la banda sobre la original retrasada (alineado con la salida)\n");
    const double sampleRate = 48000.0;
    const BandSettings band { true, 1000.0f, -6.0f, 4.0f };
    const auto zone = Band::coefficientsFor (*soloZoneFor (band), sampleRate);
    for (const double hz : { 1000.0, 100.0 })
    {
        Engine engine;
        prepareIn (engine, PhaseMode::linear, 1, sampleRate, only (band));
        engine.setSolo (0);
        const double level = toneDb (processedSine (engine, hz, 0.25, sampleRate, 32768), hz, 0.25, sampleRate);
        CHECK (std::abs (level - zone.magnitudeDb (hz, sampleRate)) < 0.01);
    }
}

void testPhaseMemoryOnDemand()
{
    std::printf ("Fase: en tiempo real la memoria de los modos de fase se reserva recien al elegir uno (desde el hilo de mensajes)\n");
    const double sampleRate = 48000.0;
    Engine engine;
    engine.setRealtime (true);
    engine.prepare (sampleRate, false, 0.0f, only (bandOf (Shape::bell, 2000.0f, -9.0f, 1.0f)));
    CHECK (! engine.hasPhaseMemory()); // cero latencia: no hace falta
    engine.setPhaseMode (PhaseMode::linear, 0);
    auto left = sine (2000.0, 0.5, sampleRate, static_cast<size_t> (sampleRate)), right = left;
    bool swapped = false;
    for (size_t start = 0; start < left.size(); start += 256)
    {
        float* channels[] { left.data() + start, right.data() + start };
        engine.process (channels, 2, static_cast<int> (std::min<size_t> (256, left.size() - start)));
        engine.providePhaseMemory(); // lo que hace el temporizador del procesador
        swapped = swapped || engine.hasPhaseMemory();
        std::this_thread::sleep_for (std::chrono::microseconds (500));
    }
    CHECK (swapped);
    CHECK (engine.getLatencySamples() == PhaseSizes::latency (PhaseMode::linear, 0, sampleRate));
    // Ya en fase lineal: la salida es el seno con la campana (con la latencia; el nivel no depende de ella).
    const double level = levelDb (toneAmplitude (left, 2000.0, sampleRate, left.size() / 2) / 0.5);
    CHECK (std::abs (level + 9.0) < 0.05);
    engine.providePhaseMemory(); // destruye la etapa vieja que devolvió el audio

    // Offline (render) se reserva siempre en prepare: no hay a quién esperar.
    Engine offline;
    offline.prepare (sampleRate, false, 0.0f, {});
    CHECK (offline.hasPhaseMemory());
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
    testAnalogResponse();
    testAnalogSwitchHasNoClicks();
    testStereoPlacements();
    testPlacementChangesHaveNoClicks();
    testStereoAutoGainAndSolo();
    testDynamicGainCurve();
    testDynamicActsOnlyWhenTheZoneIsLoud();
    testDynamicThresholdAndBoost();
    testDynamicAttackAndRelease();
    testAutoThreshold();
    testDynamicSidechain();
    testDynamicStereoScaleAndAutoGain();
    testDynamicOffIsBitExact();
    testDynamicChangesHaveNoClicks();
    testPhaseLatencies();
    testPhaseModesFlatAreTransparent();
    testLinearPhaseIsSymmetricWithTheCurve();
    testNaturalPhaseMatchesTheAnalog();
    testPhaseModeSwitchHasNoClicks();
    testLinearPhasePlacements();
    testLinearPhaseDynamics();
    testPhaseDesignThread();
    testPreRinging();
    testLinearQualityResolution();
    testFirModesAutomationHasNoClicks();
    testPhaseModesInMono();
    testLinearPhaseSolo();
    testPhaseMemoryOnDemand();

    if (failures > 0)
    {
        std::printf ("\n%d FALLO(S)\n", failures);
        return 1;
    }
    std::printf ("\nTodos los tests pasaron\n");
    return 0;
}
