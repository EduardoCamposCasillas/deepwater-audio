// Tests del motor de Riptide (sin JUCE): se compilan como un ejecutable normal y se lanzan con ctest. Si algo falla,
// imprime el archivo, la línea y la condición.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

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

namespace dw = deepwater::dsp;
using riptide::dsp::Engine;
using riptide::dsp::EngineSettings;

constexpr double pi = std::numbers::pi;
constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };

// Ruido blanco repetible (xorshift32) en ±amplitude: material de prueba con todas las frecuencias.
std::vector<float> noise (size_t count, float amplitude, std::uint32_t seed = 0x12345678u)
{
    std::vector<float> result (count);
    for (auto& sample : result)
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        sample = amplitude * (static_cast<float> (seed) / 4294967295.0f * 2.0f - 1.0f);
    }
    return result;
}

std::vector<float> sine (double hz, double sampleRate, size_t count, float amplitude)
{
    std::vector<float> result (count);
    for (size_t i = 0; i < count; ++i)
        result[i] = amplitude * static_cast<float> (std::sin (2.0 * pi * hz * static_cast<double> (i) / sampleRate));
    return result;
}

// Amplitud de un seno de frecuencia 'hz' dentro de 'signal' (desde 'start'), por correlación con ventana de Hann.
double toneAmplitude (const std::vector<float>& signal, double hz, double sampleRate, size_t start)
{
    const size_t count = signal.size() - start;
    double re = 0.0, im = 0.0, windowSum = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        const double window = 0.5 - 0.5 * std::cos (2.0 * pi * static_cast<double> (i) / static_cast<double> (count - 1));
        const double phase = 2.0 * pi * hz * static_cast<double> (start + i) / sampleRate;
        re += window * signal[start + i] * std::cos (phase);
        im -= window * signal[start + i] * std::sin (phase);
        windowSum += window;
    }
    return 2.0 * std::sqrt (re * re + im * im) / windowSum;
}

// Procesa 'left'/'right' (en el sitio) en bloques de 'blockSize'. right vacío = mono.
void processInBlocks (Engine& engine, std::vector<float>& left, std::vector<float>& right, int blockSize)
{
    const int total = static_cast<int> (left.size());
    for (int start = 0; start < total; start += blockSize)
    {
        const int count = std::min (blockSize, total - start);
        float* channels[2] { left.data() + start, right.empty() ? nullptr : right.data() + start };
        engine.process (channels, right.empty() ? 1 : 2, count);
    }
}

float maxStep (const std::vector<float>& signal)
{
    float result = 0.0f;
    for (size_t i = 1; i < signal.size(); ++i)
        result = std::max (result, std::abs (signal[i] - signal[i - 1]));
    return result;
}

EngineSettings withOversampling (int index, EngineSettings settings = {})
{
    settings.oversampling = index;
    return settings;
}

void testBypassIsDelayedInput()
{
    std::printf ("Engine: con bypass la salida es la entrada retrasada la latencia, bit a bit (mono/estéreo, cualquier bloque)\n");

    for (const double sr : sampleRates)
        for (int os = 0; os < riptide::dsp::numOversamplingChoices; ++os)
            for (const int blockSize : { 1, 7, 512, 4096 })
                for (const bool stereo : { false, true })
                {
                    Engine engine;
                    engine.prepare (sr, withOversampling (os, { 12.0f, 6.0f, true }));
                    const auto latency = static_cast<size_t> (engine.getLatency());
                    const auto inputL = noise (10000, 0.9f, 1u);
                    const auto inputR = noise (10000, 0.9f, 2u);
                    auto left = inputL;
                    auto right = stereo ? inputR : std::vector<float> {};
                    processInBlocks (engine, left, right, blockSize);

                    bool exact = true;
                    for (size_t i = 0; i < left.size(); ++i)
                    {
                        const float expectedL = i < latency ? 0.0f : inputL[i - latency];
                        const float expectedR = i < latency ? 0.0f : inputR[i - latency];
                        exact = exact && left[i] == expectedL && (! stereo || right[i] == expectedR);
                    }
                    CHECK (exact);
                }
}

void testLatencyAlignsWetAndDry()
{
    std::printf ("Engine: la señal procesada sale con la misma latencia que la original (a nivel bajo, tanh ≈ recta)\n");

    for (const double sr : sampleRates)
        for (int os = 0; os < riptide::dsp::numOversamplingChoices; ++os)
        {
            Engine engine;
            engine.prepare (sr, withOversampling (os));
            const auto latency = static_cast<size_t> (engine.getLatency());
            // −40 dBFS: tanh se desvía de la recta en 10⁻⁵ relativo. Lo que queda es el filtro DC (desfase de 0.3° a
            // 1 kHz: 0.5 % de error). Media muestra de desalineación sería un 7 %.
            const auto input = sine (1000.0, sr, 20000, 0.01f);
            auto left = input;
            std::vector<float> right;
            processInBlocks (engine, left, right, 256);
            float worst = 0.0f;
            for (size_t i = 10000; i < left.size(); ++i)
                worst = std::max (worst, std::abs (left[i] - input[i - latency]));
            CHECK (worst < 0.01f * 0.01f);
        }
}

void testSaturation()
{
    std::printf ("Engine: tanh limita a ±1, crea armónicos impares y no pares; a nivel bajo la ganancia es la de las perillas\n");

    for (const double sr : sampleRates)
    {
        // +24 dB sobre un seno a −6 dBFS: casi una onda cuadrada.
        Engine hot;
        hot.prepare (sr, withOversampling (2, { 24.0f, 0.0f, false }));
        auto left = sine (1000.0, sr, static_cast<size_t> (sr), 0.5f);
        std::vector<float> right;
        processInBlocks (hot, left, right, 512);
        const size_t start = left.size() / 2;
        float peak = 0.0f;
        for (size_t i = start; i < left.size(); ++i)
            peak = std::max (peak, std::abs (left[i]));
        CHECK (peak <= 1.1f); // tanh ≤ 1; el filtro halfband agrega un pequeño rizado (Gibbs) en las esquinas
        const double fundamental = toneAmplitude (left, 1000.0, sr, start);
        CHECK (fundamental > 1.1); // una cuadrada de amplitud 1 tiene fundamental 4/π
        CHECK (toneAmplitude (left, 3000.0, sr, start) > 0.2 * fundamental);
        CHECK (toneAmplitude (left, 2000.0, sr, start) < 1.0e-4 * fundamental); // curva simétrica: sin pares

        // Nivel bajo: Input +6 y Output −18 dejan el seno en −40 + 6 − 18 = −52 dBFS.
        Engine soft;
        soft.prepare (sr, withOversampling (1));
        soft.setSettings (withOversampling (1, { 6.0f, -18.0f, false }));
        auto quiet = sine (1000.0, sr, static_cast<size_t> (sr), 0.01f);
        processInBlocks (soft, quiet, right, 100);
        const auto level = static_cast<float> (toneAmplitude (quiet, 1000.0, sr, quiet.size() / 2));
        CHECK (std::abs (dw::gainToDb (level / 0.01f) - (6.0f - 18.0f)) < 0.01f);
    }
}

void testDcIsRemoved()
{
    std::printf ("Engine: el filtro DC quita un desplazamiento constante de la entrada\n");

    Engine engine;
    engine.prepare (48000.0, withOversampling (1));
    std::vector<float> left (96000, 0.2f), right;
    processInBlocks (engine, left, right, 480);
    CHECK (std::abs (left.back()) < 1.0e-4f);
}

void testBypassToggleIsSmooth()
{
    std::printf ("Engine: conmutar el bypass es un fundido de 20 ms sin saltos (también a mitad de un fundido)\n");

    for (const double sr : sampleRates)
    {
        // Seno grave a nivel alto con +12 dB de drive: procesado y original son muy distintos, pero están alineados.
        const EngineSettings settings = withOversampling (2, { 12.0f, 0.0f, false });
        const int fadeSamples = static_cast<int> (std::lround (Engine::bypassFadeSeconds * sr));
        const auto input = sine (100.0, sr, static_cast<size_t> (fadeSamples) * 20, 0.5f);

        Engine still;
        still.prepare (sr, settings);
        std::vector<float> reference = input, none;
        processInBlocks (still, reference, none, 64); // procesado sin conmutar: su paso máximo es el de la onda

        Engine engine;
        engine.prepare (sr, settings);
        std::vector<float> signal;
        size_t position = 0;
        for (int toggle = 0; position < input.size(); ++toggle)
        {
            auto toggled = settings;
            toggled.bypassed = toggle % 2 == 0;
            engine.setSettings (toggled);
            // Algunos cambios llegan a mitad de un fundido (cada 0.7 fundidos) y otros con el fundido terminado.
            const size_t length = std::min (input.size() - position,
                                            static_cast<size_t> (toggle < 3 ? fadeSamples * 7 / 10 : fadeSamples * 3));
            std::vector<float> block (input.begin() + static_cast<std::ptrdiff_t> (position),
                                      input.begin() + static_cast<std::ptrdiff_t> (position + length));
            processInBlocks (engine, block, none, 32);
            signal.insert (signal.end(), block.begin(), block.end());
            position += length;
        }
        // Paso de la onda + lo que cambia el fundido en una muestra (como mucho la diferencia entre las dos señales).
        CHECK (maxStep (signal) <= std::max (maxStep (reference), maxStep (input)) + 1.5f / static_cast<float> (fadeSamples));
    }
}

void testOversamplingSwitchIsClickless()
{
    std::printf ("Engine: cambiar el oversampling baja a silencio, cambia la latencia y vuelve sin saltos\n");

    for (const double sr : sampleRates)
    {
        const auto settingsFor = [] (int os) { return withOversampling (os, { 6.0f, 0.0f, false }); };
        const int fadeSamples = static_cast<int> (std::lround (Engine::factorFadeSeconds * sr));
        const auto input = sine (200.0, sr, static_cast<size_t> (sr), 0.5f);

        // Referencia: el paso máximo de la onda saturada sin cambios de factor.
        Engine still;
        still.prepare (sr, settingsFor (4));
        std::vector<float> reference = input, none;
        processInBlocks (still, reference, none, 64);

        Engine engine;
        engine.prepare (sr, settingsFor (0));
        std::vector<float> output;
        size_t position = 0;
        int index = 0;
        for (const int os : { 4, 1, 3, 0, 2, 4, 0 })
        {
            engine.setSettings (settingsFor (os));
            // Algunos cambios llegan antes de terminar el fundido anterior.
            const size_t length = static_cast<size_t> (index++ % 2 == 0 ? fadeSamples * 3 : fadeSamples / 2);
            std::vector<float> block (input.begin() + static_cast<std::ptrdiff_t> (position),
                                      input.begin() + static_cast<std::ptrdiff_t> (position + length));
            processInBlocks (engine, block, none, 64);
            output.insert (output.end(), block.begin(), block.end());
            position += length;
        }
        std::vector<float> tail (input.begin() + static_cast<std::ptrdiff_t> (position),
                                 input.begin() + static_cast<std::ptrdiff_t> (position + 4000));
        processInBlocks (engine, tail, none, 64);
        output.insert (output.end(), tail.begin(), tail.end());

        // El fundido suma como mucho 1/fadeSamples por muestra (la salida no pasa de 1).
        CHECK (maxStep (output) <= maxStep (reference) * 1.05f + 1.0f / static_cast<float> (fadeSamples));
        CHECK (engine.getActiveOversampling() == 0);
        CHECK (engine.getLatency() == 0);
    }

    // La latencia publicada sigue al factor activo, y cambia solo cuando el cambio se aplica (en silencio).
    Engine engine;
    engine.prepare (44100.0, withOversampling (0));
    std::vector<float> block (2000, 0.0f), none;
    engine.setSettings (withOversampling (4));
    CHECK (engine.getLatency() == 0);
    processInBlocks (engine, block, none, 512);
    CHECK (engine.getActiveOversampling() == 4);
    CHECK (engine.getLatency() > 70);
}

void testBlockSizeIndependence()
{
    std::printf ("Engine: el resultado no depende del tamaño de bloque (con automatización y cambios de factor)\n");

    constexpr size_t changeEvery = 28672; // múltiplo de todos los bloques probados: los cambios caen en la misma muestra
    const std::array<EngineSettings, 5> changes { { { 8.0f, -3.0f, false, 2 }, { -6.0f, 4.0f, true, 4 },
                                                     { 2.5f, 0.0f, false, 0 }, { 18.0f, -24.0f, false, 3 },
                                                     { 0.0f, 0.0f, false, 1 } } };
    const auto inputL = noise (changeEvery * changes.size(), 0.7f, 3u);
    const auto inputR = noise (changeEvery * changes.size(), 0.7f, 4u);

    std::vector<float> referenceL, referenceR;
    for (const int blockSize : { 1, 7, 64, 4096 })
    {
        Engine engine;
        engine.prepare (44100.0, {});
        std::vector<float> outL, outR;
        for (size_t c = 0; c < changes.size(); ++c)
        {
            engine.setSettings (changes[c]);
            std::vector<float> left (inputL.begin() + static_cast<std::ptrdiff_t> (c * changeEvery),
                                     inputL.begin() + static_cast<std::ptrdiff_t> ((c + 1) * changeEvery));
            std::vector<float> right (inputR.begin() + static_cast<std::ptrdiff_t> (c * changeEvery),
                                      inputR.begin() + static_cast<std::ptrdiff_t> ((c + 1) * changeEvery));
            processInBlocks (engine, left, right, blockSize);
            outL.insert (outL.end(), left.begin(), left.end());
            outR.insert (outR.end(), right.begin(), right.end());
        }
        if (referenceL.empty())
        {
            referenceL = outL;
            referenceR = outR;
        }
        CHECK (outL == referenceL);
        CHECK (outR == referenceR);
    }
}

void testMeters()
{
    std::printf ("Engine: IN mide después del input gain y OUT la salida; cada canal por separado\n");

    for (const double sr : sampleRates)
    {
        Engine engine;
        engine.prepare (sr, { 6.0f, -10.0f, false, 2 });

        // L: seno de 1 kHz a −40 dBFS (tanh casi recta); R: silencio.
        const size_t count = static_cast<size_t> (3.0 * sr); // 3 s = 10 constantes de tiempo del RMS (llega al 99.995 %)
        std::vector<float> left = sine (1000.0, sr, count, dw::dbToGain (-40.0f)), right (count, 0.0f);
        processInBlocks (engine, left, right, 512);

        CHECK (std::abs (dw::gainToDb (engine.inputMeter (0).takePeak()) - (-34.0f)) < 0.01f);
        // El filtro DC desplaza un poco la fase: los puntos muestreados ya no caen igual de cerca de la cresta.
        CHECK (std::abs (dw::gainToDb (engine.outputMeter (0).takePeak()) - (-44.0f)) < 0.05f);
        CHECK (std::abs (dw::gainToDb (engine.inputMeter (0).rms()) - (-34.0f - 3.01f)) < 0.05f);
        CHECK (std::abs (dw::gainToDb (engine.outputMeter (0).rms()) - (-44.0f - 3.01f)) < 0.05f);
        CHECK (engine.inputMeter (1).takePeak() == 0.0f && engine.outputMeter (1).rms() == 0.0f);
    }

    // Mono: solo se mide el canal 0.
    Engine mono;
    mono.prepare (48000.0, {});
    std::vector<float> left = sine (1000.0, 48000.0, 4800, 0.1f), none;
    processInBlocks (mono, left, none, 480);
    CHECK (mono.outputMeter (0).takePeak() > 0.09f);
    CHECK (mono.outputMeter (1).takePeak() == 0.0f);
}

} // namespace

int main()
{
    testBypassIsDelayedInput();
    testLatencyAlignsWetAndDry();
    testSaturation();
    testDcIsRemoved();
    testBypassToggleIsSmooth();
    testOversamplingSwitchIsClickless();
    testBlockSizeIndependence();
    testMeters();

    if (failures == 0)
        std::printf ("\nTodos los tests de Riptide pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);
    return failures == 0 ? 0 : 1;
}
