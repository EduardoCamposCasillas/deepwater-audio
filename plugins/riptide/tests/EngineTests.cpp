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

void testUnityIsBitExact()
{
    std::printf ("Engine: con 0 dB / 0 dB la salida es la entrada bit a bit (mono y estéreo, cualquier bloque)\n");

    for (const double sr : sampleRates)
        for (const int blockSize : { 1, 7, 64, 512, 4096 })
            for (const bool stereo : { false, true })
            {
                Engine engine;
                engine.prepare (sr, {});
                const auto inputL = noise (20000, 0.9f, 1u);
                const auto inputR = noise (20000, 0.9f, 2u);
                auto left = inputL;
                auto right = stereo ? inputR : std::vector<float> {};
                processInBlocks (engine, left, right, blockSize);
                CHECK (left == inputL);
                if (stereo)
                    CHECK (right == inputR);
            }

    // Después de mover las perillas y volver a 0 dB también: la rampa llega exacta, no "casi".
    Engine engine;
    engine.prepare (48000.0, {});
    auto scratchL = noise (4800, 0.5f);
    std::vector<float> scratchR;
    engine.setSettings ({ 9.0f, -4.5f, false });
    processInBlocks (engine, scratchL, scratchR, 256);
    engine.setSettings ({});
    scratchL = noise (4800, 0.5f);
    processInBlocks (engine, scratchL, scratchR, 256); // 100 ms: la rampa de 20 ms ya terminó
    const auto input = noise (4800, 0.5f, 7u);
    auto output = input;
    processInBlocks (engine, output, scratchR, 256);
    CHECK (output == input);
}

void testGainsAndRampTime()
{
    std::printf ("Engine: input y output gain se suman en dB y llegan al valor en 20 ms exactos\n");

    for (const double sr : sampleRates)
    {
        Engine engine;
        engine.prepare (sr, {});
        engine.setSettings ({ 6.0f, -18.0f, false });

        const int rampSamples = static_cast<int> (std::lround (Engine::gainRampSeconds * sr));
        std::vector<float> left (static_cast<size_t> (rampSamples) * 2, 0.5f);
        std::vector<float> right;
        processInBlocks (engine, left, right, 100);

        const float expected = 0.5f * dw::dbToGain (6.0f) * dw::dbToGain (-18.0f);
        // La muestra rampSamples − 1 es la última de la rampa: ya vale exactamente el objetivo.
        CHECK (left[static_cast<size_t> (rampSamples) - 1] == expected);
        CHECK (left[static_cast<size_t> (rampSamples) - 2] != expected);
        CHECK (left.back() == expected);
        CHECK (std::abs (dw::gainToDb (left.back() / 0.5f) - (-12.0f)) < 1.0e-4f);
        // Sin saltos: las dos rampas se multiplican (a·b), así que el paso por muestra está acotado por
        // 0.5 · (|Δa| · máx b + máx a · |Δb|) / rampSamples.
        const float deltaIn = dw::dbToGain (6.0f) - 1.0f;
        const float deltaOut = 1.0f - dw::dbToGain (-18.0f);
        CHECK (maxStep (left) <= 0.5f * (deltaIn * 1.0f + dw::dbToGain (6.0f) * deltaOut) / static_cast<float> (rampSamples) * 1.01f);
    }
}

void testBypass()
{
    std::printf ("Engine: bypass quieto = entrada bit a bit; conmutarlo es un fundido de 20 ms sin saltos\n");

    for (const double sr : sampleRates)
    {
        // Bypass desde el principio: las ganancias no se oyen.
        Engine engine;
        engine.prepare (sr, { 12.0f, 6.0f, true });
        const auto input = noise (10000, 0.8f);
        auto left = input;
        std::vector<float> right;
        processInBlocks (engine, left, right, 64);
        CHECK (left == input);

        // Con una señal continua (DC) y +12 dB de diferencia, cualquier conmutación brusca sería un escalón enorme.
        Engine dc;
        dc.prepare (sr, { 0.0f, 12.0f, false });
        const int fadeSamples = static_cast<int> (std::lround (Engine::bypassFadeSeconds * sr));
        std::vector<float> signal;
        const float processed = 0.25f * dw::dbToGain (12.0f);
        for (int toggle = 0; toggle < 6; ++toggle)
        {
            dc.setSettings ({ 0.0f, 12.0f, toggle % 2 == 0 });
            // Algunos cambios llegan a mitad de un fundido (cada 0.7 fundidos) y otros con el fundido terminado.
            std::vector<float> block (static_cast<size_t> (toggle < 3 ? fadeSamples * 7 / 10 : fadeSamples * 3), 0.25f);
            std::vector<float> none;
            processInBlocks (dc, block, none, 32);
            signal.insert (signal.end(), block.begin(), block.end());
        }
        CHECK (maxStep (signal) <= (processed - 0.25f) / static_cast<float> (fadeSamples) * 1.01f);
        CHECK (signal.back() == processed); // el último cambio (toggle 5) quitó el bypass y el fundido terminó
    }
}

void testBlockSizeIndependence()
{
    std::printf ("Engine: el resultado no depende del tamaño de bloque (con automatización)\n");

    constexpr size_t changeEvery = 28672; // múltiplo de todos los bloques probados: los cambios caen en la misma muestra
    const std::array<EngineSettings, 4> changes { { { 8.0f, -3.0f, false }, { -6.0f, 4.0f, true },
                                                     { 2.5f, 0.0f, false }, { 0.0f, -24.0f, false } } };
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
        engine.prepare (sr, { 6.0f, -10.0f, false });

        // L: seno de 1 kHz a −12 dBFS; R: silencio.
        const size_t count = static_cast<size_t> (3.0 * sr); // 3 s = 10 constantes de tiempo del RMS (llega al 99.995 %)
        std::vector<float> left (count), right (count, 0.0f);
        for (size_t i = 0; i < count; ++i)
            left[i] = dw::dbToGain (-12.0f) * static_cast<float> (std::sin (2.0 * pi * 1000.0 * static_cast<double> (i) / sr));
        processInBlocks (engine, left, right, 512);

        CHECK (std::abs (dw::gainToDb (engine.inputMeter (0).takePeak()) - (-6.0f)) < 0.01f);
        CHECK (std::abs (dw::gainToDb (engine.outputMeter (0).takePeak()) - (-16.0f)) < 0.01f);
        CHECK (std::abs (dw::gainToDb (engine.inputMeter (0).rms()) - (-6.0f - 3.01f)) < 0.05f);
        CHECK (std::abs (dw::gainToDb (engine.outputMeter (0).rms()) - (-16.0f - 3.01f)) < 0.05f);
        CHECK (engine.inputMeter (1).takePeak() == 0.0f && engine.outputMeter (1).rms() == 0.0f);
    }

    // Mono: solo se mide el canal 0.
    Engine mono;
    mono.prepare (48000.0, {});
    std::vector<float> left (4800, 0.5f), none;
    processInBlocks (mono, left, none, 480);
    CHECK (mono.outputMeter (0).takePeak() == 0.5f);
    CHECK (mono.outputMeter (1).takePeak() == 0.0f);
}

} // namespace

int main()
{
    testUnityIsBitExact();
    testGainsAndRampTime();
    testBypass();
    testBlockSizeIndependence();
    testMeters();

    if (failures == 0)
        std::printf ("\nTodos los tests de Riptide pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);
    return failures == 0 ? 0 : 1;
}
