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
using riptide::dsp::Mode;
using riptide::dsp::ToneSettings;
namespace curves = riptide::dsp::curves;

constexpr double pi = std::numbers::pi;
constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };
constexpr std::array<Mode, riptide::dsp::numModes> allModes { Mode::tape,     Mode::tube,    Mode::transistor,
                                                              Mode::hardClip, Mode::rectify, Mode::fold };

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

EngineSettings withMode (Mode mode, EngineSettings settings = {})
{
    settings.mode = mode;
    return settings;
}

// Procesa una señal entera con un motor nuevo (mono, bloques de 256).
std::vector<float> render (double sampleRate, const EngineSettings& settings, std::vector<float> signal)
{
    Engine engine;
    engine.prepare (sampleRate, settings);
    std::vector<float> none;
    processInBlocks (engine, signal, none, 256);
    return signal;
}

EngineSettings withTone (const ToneSettings& tone, EngineSettings settings = {})
{
    settings.tone = tone;
    return settings;
}

// Ganancia en dB de la fundamental de un seno a −40 dBFS (la curva es recta: solo se mide el tono).
double lowLevelGainDb (double sampleRate, const EngineSettings& settings, double hz)
{
    const auto out = render (sampleRate, settings, sine (hz, sampleRate, static_cast<size_t> (sampleRate), 0.01f));
    return dw::gainToDb (static_cast<float> (toneAmplitude (out, hz, sampleRate, out.size() / 2) / 0.01));
}

void testCurves()
{
    std::printf ("Curvas: f(0) = 0, pendiente 1 en el origen, simetría (pares/impares), techos y dureza de la rodilla\n");

    using Curve = float (*) (float);
    const std::array<Curve, riptide::dsp::numModes> shapes { curves::tape,     curves::tube,    curves::transistor,
                                                             curves::hardClip, curves::rectify, curves::fold };
    for (size_t m = 0; m < shapes.size(); ++m)
    {
        const auto f = shapes[m];
        CHECK (std::abs (f (0.0f)) < 1.0e-7f);
        if (allModes[m] == Mode::rectify)
            continue; // |x|: da vuelta la onda, no tiene sentido hablar de pendiente 1
        const float h = 1.0e-3f;
        const float slope = (f (h) - f (-h)) / (2.0f * h);
        CHECK (std::abs (slope - 1.0f) < 1.0e-3f); // a nivel bajo todos los modos tienen el mismo volumen
    }

    // Impares (solo armónicos impares): f(−x) = −f(x). El bulbo no lo es.
    for (const auto f : { curves::tape, curves::transistor, curves::hardClip, curves::fold })
        for (const float x : { 0.1f, 0.5f, 1.0f, 3.0f, 10.0f })
            CHECK (f (-x) == -f (x));
    CHECK (curves::tube (1.0f) < 0.65f && curves::tube (-1.0f) < -0.95f); // el positivo se aplasta antes
    CHECK (std::abs (curves::tube (100.0f) - 0.774f) < 0.01f && std::abs (curves::tube (-100.0f) + 1.411f) < 0.01f);
    CHECK (curves::rectify (-0.5f) == curves::rectify (0.5f)); // par: el semiciclo negativo se da vuelta

    // Techos: cinta, transistor y clip nunca pasan de ±1.
    for (const float x : { 1.0f, 2.0f, 10.0f, 1000.0f })
        CHECK (curves::tape (x) <= 1.0f && curves::transistor (x) <= 1.0f && curves::hardClip (x) == 1.0f);

    // Rodilla: con x = 0.5 la cinta ya redondea más que el transistor, y el transistor más que el clip (recto).
    // En x = 1 el orden es el mismo: la cinta llega a 0.71, el transistor a 0.84, el clip a 1.
    CHECK (curves::tape (0.5f) < curves::transistor (0.5f) && curves::transistor (0.5f) < curves::hardClip (0.5f));
    CHECK (std::abs (curves::tape (1.0f) - 0.7071f) < 1.0e-3f && std::abs (curves::transistor (1.0f) - 0.8409f) < 1.0e-3f);

    // Wavefold: pasado π/2 la salida BAJA al subir la entrada (se dobla).
    CHECK (curves::fold (2.5f) < curves::fold (1.5f));
}

void testTapeEmphasisCancels()
{
    std::printf ("Cinta: el de-énfasis es la inversa exacta del énfasis (con señal baja la cinta es plana en agudos)\n");

    for (const double sr : sampleRates)
    {
        riptide::dsp::TapeEmphasis emphasis;
        emphasis.prepare (sr);
        const auto input = noise (20000, 0.5f, 7u);
        float worst = 0.0f, boosted = 0.0f;
        for (const float x : input)
        {
            const float pre = emphasis.emphasize (x);
            boosted = std::max (boosted, std::abs (pre));
            worst = std::max (worst, std::abs (emphasis.deEmphasize (pre) - x));
        }
        CHECK (worst < 1.0e-5f);
        CHECK (boosted > 0.7f); // el ruido tiene muchos agudos: el énfasis los sube (hasta ×2)
    }
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
    std::printf ("Engine: la señal procesada sale con la misma latencia que la original (a nivel bajo la curva ≈ recta)\n");

    for (const double sr : sampleRates)
        for (int os = 0; os < riptide::dsp::numOversamplingChoices; ++os)
            for (const Mode mode : { Mode::transistor, Mode::hardClip, Mode::fold })
            {
                Engine engine;
                engine.prepare (sr, withOversampling (os, withMode (mode)));
                const auto latency = static_cast<size_t> (engine.getLatency());
                // −40 dBFS: las curvas se desvían de la recta en menos de 10⁻⁴ relativo. Lo que queda es el filtro DC
                // (desfase de 0.3° a 1 kHz: 0.5 % de error). Media muestra de desalineación sería un 7 %.
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

void testModeCharacter()
{
    std::printf ("Engine: con drive alto cada modo crea sus armónicos (impares, pares, octava sin fundamental)\n");

    for (const double sr : { 44100.0, 96000.0 })
    {
        // Seno de 200 Hz a −6 dBFS con +18 dB de drive: amplitud 4 en la curva (el wavefold ya se dobla 2 veces).
        const auto input = sine (200.0, sr, static_cast<size_t> (sr), 0.5f);
        const size_t start = input.size() / 2;
        for (const Mode mode : allModes)
        {
            const auto out = render (sr, withOversampling (2, withMode (mode, { 18.0f, 0.0f, false })), input);
            const double h1 = toneAmplitude (out, 200.0, sr, start);
            const double h2 = toneAmplitude (out, 400.0, sr, start);
            const double h3 = toneAmplitude (out, 600.0, sr, start);
            switch (mode)
            {
                case Mode::tape:
                case Mode::transistor:
                case Mode::hardClip:
                case Mode::fold:
                    CHECK (h2 < 1.0e-4 * h1); // simétricas: sin pares
                    CHECK (h3 > 0.1 * h1);
                    break;
                case Mode::tube:
                    CHECK (h2 > 0.05 * h1); // asimétrica: aparecen los pares
                    CHECK (h3 > 0.05 * h1);
                    break;
                case Mode::rectify:
                    CHECK (h1 < 1.0e-3 * h2); // la fundamental desaparece: sube una octava
                    CHECK (h2 > 0.8);         // casi una cuadrada de amplitud 1 a 400 Hz (4/π ≈ 1.27)
                    break;
            }
            if (mode == Mode::hardClip || mode == Mode::transistor)
            {
                // Casi cuadrada de amplitud 1: fundamental cerca de 4/π.
                CHECK (h1 > 1.15 && h1 < 1.3);
            }
        }
    }
}

void testLowLevelIsClean()
{
    std::printf ("Engine: a −40 dBFS los modos pasan casi limpios y con el volumen de las perillas; la cinta suma su head bump\n");

    for (const double sr : sampleRates)
        for (const Mode mode : allModes)
        {
            if (mode == Mode::rectify)
                continue;
            // Input +6 y Output −18 dejan el seno en −40 + 6 − 18 = −52 dBFS.
            const auto settings = withOversampling (1, withMode (mode, { 6.0f, -18.0f, false }));
            for (const double hz : { 70.0, 1000.0, 10000.0 })
            {
                const auto out = render (sr, settings, sine (hz, sr, static_cast<size_t> (sr), 0.01f));
                const size_t start = out.size() / 2;
                const double gainDb = dw::gainToDb (static_cast<float> (toneAmplitude (out, hz, sr, start) / 0.01)) + 12.0;
                double expected = 0.0;
                if (mode == Mode::tape && hz == 70.0)
                    expected = riptide::dsp::Saturator::headBumpDb;
                // Cinta a 1 kHz: la falda de la campana todavía suma un poco; a 10 kHz el énfasis se cancela.
                const double tolerance = mode == Mode::tape && hz == 1000.0 ? 0.25 : 0.05;
                CHECK (std::abs (gainDb - expected) < tolerance);
                // Los armónicos, muy por debajo (−60 dB). Salvo el 2º del bulbo: su curva no tiene una parte recta
                // y a −34 dBFS ya agrega un poco (entre −60 y −40 dB): es su "calidez" incluso con señal baja.
                if (hz < 5000.0)
                {
                    const double fundamental = toneAmplitude (out, hz, sr, start);
                    const double second = toneAmplitude (out, 2.0 * hz, sr, start);
                    if (mode == Mode::tube)
                        CHECK (second > 1.0e-3 * fundamental && second < 1.0e-2 * fundamental);
                    else
                        CHECK (second < 1.0e-3 * fundamental);
                    CHECK (toneAmplitude (out, 3.0 * hz, sr, start) < 1.0e-3 * fundamental);
                }
            }
        }
}

void testTapeCompressesHighsMore()
{
    std::printf ("Engine: con drive, la cinta comprime los agudos más que los graves (pre-énfasis)\n");

    // Compresión = cuánto baja la ganancia de la fundamental al pasar de nivel bajo a nivel alto.
    const double sr = 48000.0;
    const auto compressionDb = [sr] (double hz)
    {
        const auto settings = withOversampling (2, withMode (Mode::tape, { 12.0f, 0.0f, false }));
        const auto low = render (sr, settings, sine (hz, sr, 48000, 0.001f));
        const auto high = render (sr, settings, sine (hz, sr, 48000, 0.25f));
        return dw::gainToDb (static_cast<float> (toneAmplitude (high, hz, sr, 24000) / 0.25))
               - dw::gainToDb (static_cast<float> (toneAmplitude (low, hz, sr, 24000) / 0.001));
    };
    const double lows = compressionDb (300.0);
    const double highs = compressionDb (8000.0);
    CHECK (lows < -1.0);         // la cinta comprime
    CHECK (highs < lows - 2.0);  // y los agudos más
}

void testRectifyDcIsRemoved()
{
    std::printf ("Engine: el rectificador genera mucha DC y el motor la quita\n");

    const auto out = render (48000.0, withMode (Mode::rectify, { 6.0f, 0.0f, false }), sine (110.0, 48000.0, 96000, 0.5f));
    double mean = 0.0;
    for (size_t i = 48000; i < out.size(); ++i)
        mean += out[i];
    mean /= 48000.0;
    CHECK (std::abs (mean) < 1.0e-3);
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

void testModeSwitchIsClickless()
{
    std::printf ("Engine: cambiar de modo cruza las dos curvas sin saltos (también a mitad de un cruce y junto al factor)\n");

    for (const double sr : sampleRates)
    {
        const EngineSettings base = withOversampling (2, { 12.0f, 0.0f, false });
        const int fadeSamples = static_cast<int> (std::lround (Engine::modeFadeSeconds * sr));
        const auto input = sine (100.0, sr, static_cast<size_t> (sr) * 2, 0.5f);

        // Paso máximo de cada modo sin cambios.
        float reference = maxStep (input);
        for (const Mode mode : allModes)
            reference = std::max (reference, maxStep (render (sr, withMode (mode, base), input)));

        Engine engine;
        engine.prepare (sr, base);
        const int latency = engine.getLatency();
        std::vector<float> output, none;
        size_t position = 0;
        int index = 0;
        for (const Mode mode : { Mode::tube, Mode::fold, Mode::rectify, Mode::hardClip, Mode::tape, Mode::transistor,
                                 Mode::rectify, Mode::tape, Mode::fold })
        {
            engine.setSettings (withMode (mode, base));
            // Algunos cambios llegan a mitad de un cruce.
            const size_t length = static_cast<size_t> (index++ % 3 == 1 ? fadeSamples / 2 : fadeSamples * 4);
            std::vector<float> block (input.begin() + static_cast<std::ptrdiff_t> (position),
                                      input.begin() + static_cast<std::ptrdiff_t> (position + length));
            processInBlocks (engine, block, none, 64);
            output.insert (output.end(), block.begin(), block.end());
            position += length;
            CHECK (engine.getLatency() == latency); // el modo no cambia la latencia
        }
        std::vector<float> tail (input.begin() + static_cast<std::ptrdiff_t> (position),
                                 input.begin() + static_cast<std::ptrdiff_t> (position + static_cast<size_t> (sr / 4)));
        processInBlocks (engine, tail, none, 64);
        output.insert (output.end(), tail.begin(), tail.end());

        // El cruce cambia en una muestra como mucho 1/fadeSamples de la diferencia entre los dos modos (≤ 2.5: el
        // bulbo llega a −1.41).
        CHECK (maxStep (output) <= reference + 2.5f / static_cast<float> (fadeSamples));
        CHECK (engine.getActiveMode() == Mode::fold);
        CHECK (! engine.isSwitchingMode());
    }

    // Después del cambio el sonido es exactamente el del modo nuevo (no queda nada del anterior).
    const double sr = 44100.0;
    const auto input = noise (static_cast<size_t> (sr) * 2, 0.5f, 9u);
    for (const Mode mode : allModes)
    {
        const auto settings = withOversampling (3, withMode (mode, { 9.0f, 0.0f, false }));
        const auto direct = render (sr, settings, input);

        // Primera mitad en otro modo, segunda mitad en este.
        Engine engine;
        engine.prepare (sr, withMode (mode == Mode::tube ? Mode::fold : Mode::tube, settings));
        const auto middle = static_cast<std::ptrdiff_t> (input.size() / 2);
        std::vector<float> first (input.begin(), input.begin() + middle), second (input.begin() + middle, input.end()), none;
        processInBlocks (engine, first, none, 4096);
        engine.setSettings (settings);
        processInBlocks (engine, second, none, 4096);
        float worst = 0.0f;
        for (size_t i = second.size() / 2; i < second.size(); ++i)
            worst = std::max (worst, std::abs (second[i] - direct[i + static_cast<size_t> (middle)]));
        CHECK (worst < 1.0e-4f);
    }

    // Cambiar el factor a mitad de un cruce lo termina en silencio: queda el modo nuevo.
    Engine engine;
    engine.prepare (sr, withOversampling (1, withMode (Mode::tape)));
    std::vector<float> block (2000, 0.1f), none;
    engine.setSettings (withOversampling (3, withMode (Mode::fold)));
    processInBlocks (engine, block, none, 512);
    CHECK (engine.getActiveMode() == Mode::fold && ! engine.isSwitchingMode() && engine.getActiveOversampling() == 3);
}

void testToneNeutralIsTransparent()
{
    std::printf ("Tono: con los cortes apagados y emphasis/tilt en 0 la señal no cambia (en cualquier posición PRE/POST)\n");

    const double sr = 48000.0;
    const auto input = noise (24000, 0.7f, 11u);
    for (const Mode mode : allModes)
    {
        const auto settings = withOversampling (2, withMode (mode, { 9.0f, -2.0f, false }));
        const auto reference = render (sr, settings, input);
        ToneSettings flipped;
        flipped.lowCutPost = true;
        flipped.highCutPost = false;
        CHECK (render (sr, withTone (flipped, settings), input) == reference);
    }
}

void testCutFilters()
{
    std::printf ("Tono: Low Cut y High Cut son de 12 dB/oct (−3 dB en el corte), en PRE y en POST\n");

    for (const double sr : sampleRates)
        for (const bool post : { false, true })
        {
            ToneSettings low;
            low.lowCutHz = 200.0f;
            low.lowCutPost = post;
            const auto lowSettings = withTone (low, withMode (Mode::hardClip));
            CHECK (std::abs (lowLevelGainDb (sr, lowSettings, 200.0) + 3.01) < 0.1);
            CHECK (std::abs (lowLevelGainDb (sr, lowSettings, 50.0) + 24.1) < 0.3); // 2 octavas debajo: −24 dB
            CHECK (std::abs (lowLevelGainDb (sr, lowSettings, 3000.0)) < 0.05);

            ToneSettings high;
            high.highCutHz = 4000.0f;
            high.highCutPost = post;
            const auto highSettings = withTone (high, withMode (Mode::hardClip));
            CHECK (std::abs (lowLevelGainDb (sr, highSettings, 4000.0) + 3.01) < 0.1);
            CHECK (lowLevelGainDb (sr, highSettings, 16000.0) < -20.0); // 2 octavas arriba (la bilineal cae aún más)
            CHECK (std::abs (lowLevelGainDb (sr, highSettings, 200.0)) < 0.05);
        }
}

void testPreAndPostSoundDifferent()
{
    std::printf ("Tono: el mismo corte suena distinto antes y después de la curva\n");

    const double sr = 48000.0;
    // +24 dB: el seno llega con amplitud 8 a la curva y sale casi cuadrado (armónicos impares fuertes).
    const auto base = withOversampling (2, withMode (Mode::hardClip, { 24.0f, 0.0f, false }));

    // Bajo de 100 Hz y Low Cut de 1 kHz. PRE: al bajo le quedan −40 dB (amplitud 0.08) al llegar a la curva: no satura y
    // no crea armónicos. POST: satura al máximo y el corte quita la fundamental, pero quedan sus armónicos (11º = 1.1 kHz).
    const auto bass = sine (100.0, sr, 48000, 0.5f);
    ToneSettings lowCut;
    lowCut.lowCutHz = 1000.0f;
    lowCut.lowCutPost = false;
    const double preHarmonic = toneAmplitude (render (sr, withTone (lowCut, base), bass), 1100.0, sr, 24000);
    lowCut.lowCutPost = true;
    const double postHarmonic = toneAmplitude (render (sr, withTone (lowCut, base), bass), 1100.0, sr, 24000);
    CHECK (preHarmonic < 1.0e-4);
    CHECK (postHarmonic > 0.05);

    // Seno de 200 Hz y High Cut de 1 kHz. PRE: el seno pasa entero y la curva crea los armónicos DESPUÉS del filtro (el
    // 15º, en 3 kHz, sigue ahí). POST: el corte se lleva los armónicos agudos (−20 dB a 3 kHz).
    // (No se mide más arriba: este seno recortado tiene un "hueco" en su espectro cerca del armónico 25, en 5 kHz.)
    const auto tone = sine (200.0, sr, 48000, 0.5f);
    ToneSettings highCut;
    highCut.highCutHz = 1000.0f;
    highCut.highCutPost = false;
    const double preHigh = toneAmplitude (render (sr, withTone (highCut, base), tone), 3000.0, sr, 24000);
    highCut.highCutPost = true;
    const double postHigh = toneAmplitude (render (sr, withTone (highCut, base), tone), 3000.0, sr, 24000);
    CHECK (preHigh > 0.03);
    CHECK (preHigh > 5.0 * postHigh);
}

void testEmphasisCancelsAtLowLevel()
{
    std::printf ("Tono: con señal baja el de-emphasis deshace exactamente el emphasis (respuesta plana)\n");

    for (const double sr : sampleRates)
    {
        // Ruido a −40 dBFS: con +12 dB de emphasis no pasa de 0.04, el clip duro es una recta (no satura).
        const auto input = noise (20000, 0.01f, 5u);
        const auto settings = withOversampling (1, withMode (Mode::hardClip));
        const auto flat = render (sr, settings, input);
        for (const float db : { -12.0f, 6.0f, 12.0f })
            for (const float hz : { 60.0f, 1000.0f, 9000.0f })
            {
                ToneSettings tone;
                tone.emphasisDb = db;
                tone.emphasisHz = hz;
                const auto out = render (sr, withTone (tone, settings), input);
                float worst = 0.0f;
                for (size_t i = 0; i < out.size(); ++i)
                    worst = std::max (worst, std::abs (out[i] - flat[i]));
                CHECK (worst < 1.0e-6f);
            }
    }
}

void testEmphasisChoosesWhatSaturates()
{
    std::printf ("Tono: con drive, la zona de emphasis es la que más satura (y con emphasis negativo, la más limpia)\n");

    const double sr = 48000.0;
    const auto base = withOversampling (2, withMode (Mode::transistor, { 6.0f, 0.0f, false }));
    // Distorsión = 3er armónico relativo a la fundamental.
    const auto distortion = [&] (double hz, float emphasisDb)
    {
        ToneSettings tone;
        tone.emphasisDb = emphasisDb;
        tone.emphasisHz = 2000.0f;
        const auto out = render (sr, withTone (tone, base), sine (hz, sr, 48000, 0.4f));
        return toneAmplitude (out, 3.0 * hz, sr, 24000) / toneAmplitude (out, hz, sr, 24000);
    };
    const double neutral = distortion (2000.0, 0.0f);
    CHECK (distortion (2000.0, 12.0f) > 3.0 * neutral);  // en la zona: mucha más saturación
    CHECK (distortion (2000.0, -12.0f) < 0.5 * neutral); // emphasis negativo: esa zona queda más limpia
    const double bassNeutral = distortion (100.0, 0.0f);
    CHECK (std::abs (distortion (100.0, 12.0f) / bassNeutral - 1.0) < 0.3); // lejos de la zona: casi igual
}

void testTiltAtLowLevel()
{
    std::printf ("Tono: el tilt inclina la salida alrededor de 1 kHz (+dB en agudos, −dB en graves)\n");

    for (const double sr : sampleRates)
        for (const float db : { -6.0f, 4.0f })
        {
            ToneSettings tone;
            tone.tiltDb = db;
            const auto settings = withTone (tone, withMode (Mode::hardClip));
            CHECK (std::abs (lowLevelGainDb (sr, settings, 30.0) + db) < 0.15);
            CHECK (std::abs (lowLevelGainDb (sr, settings, 1000.0)) < 0.1);
            CHECK (std::abs (lowLevelGainDb (sr, settings, 15000.0) - db) < 0.5);
        }
}

void testToneChangesAreClickless()
{
    std::printf ("Tono: mover las perillas de golpe y conmutar PRE/POST no produce saltos\n");

    const std::array<ToneSettings, 6> steps { { { 400.0f, false, 3000.0f, true, 5.0f, 12.0f, 200.0f },
                                                { 400.0f, true, 3000.0f, false, -6.0f, -12.0f, 8000.0f },
                                                { 20.0f, true, 20000.0f, false, 0.0f, 0.0f, 1000.0f },
                                                { 2000.0f, false, 500.0f, true, 6.0f, 12.0f, 50.0f },
                                                { 60.0f, true, 12000.0f, true, -3.0f, 6.0f, 3000.0f },
                                                { 20.0f, false, 20000.0f, true, 0.0f, 0.0f, 1000.0f } } };
    for (const double sr : sampleRates)
    {
        const auto base = withOversampling (2, withMode (Mode::tube, { 9.0f, 0.0f, false }));
        const int fadeSamples = static_cast<int> (std::lround (riptide::dsp::ToneControls::rampSeconds * sr));
        const auto input = sine (110.0, sr, static_cast<size_t> (sr) * 2, 0.5f);

        // Paso máximo con cada ajuste quieto.
        float reference = maxStep (render (sr, base, input));
        for (const auto& tone : steps)
            reference = std::max (reference, maxStep (render (sr, withTone (tone, base), input)));

        Engine engine;
        engine.prepare (sr, base);
        std::vector<float> output, none;
        size_t position = 0;
        for (size_t c = 0; position + static_cast<size_t> (fadeSamples) * 4 < input.size(); ++c)
        {
            engine.setSettings (withTone (steps[c % steps.size()], base));
            // Algunos cambios llegan a mitad de una rampa.
            const size_t length = static_cast<size_t> (c % 3 == 1 ? fadeSamples / 2 : fadeSamples * 3);
            std::vector<float> block (input.begin() + static_cast<std::ptrdiff_t> (position),
                                      input.begin() + static_cast<std::ptrdiff_t> (position + length));
            processInBlocks (engine, block, none, 64);
            output.insert (output.end(), block.begin(), block.end());
            position += length;
        }
        std::printf ("  %.0f Hz: paso máximo %.4f (quieto: %.4f)\n", sr, static_cast<double> (maxStep (output)),
                     static_cast<double> (reference));
        CHECK (maxStep (output) <= reference * 1.1f + 2.0f / static_cast<float> (fadeSamples));
    }
}

void testInputMeterSeesEmphasis()
{
    std::printf ("Tono: IN mide lo que llega a la curva (con el emphasis); OUT, la salida ya de-enfatizada\n");

    Engine engine;
    ToneSettings tone;
    tone.emphasisDb = 12.0f;
    tone.emphasisHz = 1000.0f;
    engine.prepare (48000.0, withTone (tone, withMode (Mode::hardClip)));
    std::vector<float> left = sine (1000.0, 48000.0, 96000, dw::dbToGain (-40.0f)), none;
    processInBlocks (engine, left, none, 512);
    CHECK (std::abs (dw::gainToDb (engine.inputMeter (0).takePeak()) - (-28.0f)) < 0.1f);
    CHECK (std::abs (dw::gainToDb (engine.outputMeter (0).takePeak()) - (-40.0f)) < 0.1f);
}

void testBlockSizeIndependence()
{
    std::printf ("Engine: el resultado no depende del tamaño de bloque (con automatización y cambios de factor, modo, tono y mezcla)\n");

    constexpr size_t changeEvery = 28672; // múltiplo de todos los bloques probados: los cambios caen en la misma muestra
    const std::array<EngineSettings, 6> changes { { { 8.0f, -3.0f, false, 2, Mode::tube, { 300.0f, false, 6000.0f, true, 3.0f, 6.0f, 2000.0f }, 0.4f, true, false },
                                                     { -6.0f, 4.0f, true, 4, Mode::rectify, { 300.0f, true, 6000.0f, false, -2.0f, -9.0f, 150.0f } },
                                                     { 2.5f, 0.0f, false, 0, Mode::fold, {}, 0.7f, false, true },
                                                     { 18.0f, -24.0f, false, 3, Mode::hardClip, { 40.0f, false, 900.0f, false, 6.0f, 12.0f, 7000.0f }, 1.0f, true, true },
                                                     { 0.0f, 0.0f, false, 1, Mode::transistor },
                                                     { 6.0f, 0.0f, false, 1, Mode::tape, { 20.0f, true, 20000.0f, true, -6.0f, 0.0f, 1000.0f }, 0.0f, true, false } } };
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
        engine.prepare (sr, { 6.0f, -10.0f, false, 2, Mode::hardClip });

        // L: seno de 1 kHz a −40 dBFS (el clip duro es recto); R: silencio.
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

// --- Fase 5: mix, auto gain y delta ----------------------------------------------------------------------------------

EngineSettings withMix (float mix, bool autoGain, bool delta, EngineSettings settings = {})
{
    settings.mix = mix;
    settings.autoGain = autoGain;
    settings.delta = delta;
    return settings;
}

// La dry del mix: la entrada retrasada la latencia y pasada por un filtro DC igual al de la wet.
std::vector<float> alignedDry (const std::vector<float>& input, double sampleRate, size_t latency)
{
    dw::DcBlocker dc;
    dc.prepare (Engine::dcCutoffHz, sampleRate);
    std::vector<float> result (input.size());
    for (size_t i = 0; i < input.size(); ++i)
        result[i] = dc.process (i < latency ? 0.0f : input[i - latency]);
    return result;
}

// Ruido rosa (−3 dB por octava, como el espectro medio de la música) con un RMS dado. Filtro de Paul Kellet sobre ruido
// blanco.
std::vector<float> pinkNoise (size_t count, float rms, std::uint32_t seed)
{
    auto white = noise (count, 1.0f, seed);
    double b0 = 0.0, b1 = 0.0, b2 = 0.0, sum = 0.0;
    for (auto& x : white)
    {
        b0 = 0.99765 * b0 + x * 0.0990460;
        b1 = 0.96300 * b1 + x * 0.2965164;
        b2 = 0.57000 * b2 + x * 1.0526913;
        x = static_cast<float> (b0 + b1 + b2 + x * 0.1848);
        sum += static_cast<double> (x) * x;
    }
    const auto scale = static_cast<float> (rms / std::sqrt (sum / static_cast<double> (count)));
    for (auto& x : white)
        x *= scale;
    return white;
}

// RMS en ventanas de 10 ms: el volumen "de corto plazo" (lo que se oiría como un salto).
std::vector<double> shortTermDb (const std::vector<float>& signal, double sampleRate, size_t start = 0)
{
    const auto window = static_cast<size_t> (sampleRate / 100.0);
    std::vector<double> result;
    for (; start + window <= signal.size(); start += window)
    {
        double sum = 0.0;
        for (size_t i = start; i < start + window; ++i)
            sum += static_cast<double> (signal[i]) * signal[i];
        result.push_back (10.0 * std::log10 (sum / static_cast<double> (window) + 1.0e-30));
    }
    return result;
}

double rmsDb (const std::vector<float>& signal, size_t start)
{
    double sum = 0.0;
    for (size_t i = start; i < signal.size(); ++i)
        sum += static_cast<double> (signal[i]) * signal[i];
    return 10.0 * std::log10 (sum / static_cast<double> (signal.size() - start));
}

void testMixEndpoints()
{
    std::printf ("Mix: 0 %% = la original alineada (retardo + filtro DC) bit a bit; 100 %% = la saturada; en medio, la mezcla lineal\n");

    for (const double sr : { 44100.0, 96000.0 })
        for (int os = 0; os < riptide::dsp::numOversamplingChoices; os += 2)
            for (const Mode mode : allModes)
            {
                EngineSettings settings = withOversampling (os, withMode (mode, { 12.0f, 0.0f, false }));
                const auto input = noise (20000, 0.5f, 5u);
                Engine probe;
                probe.prepare (sr, settings);
                const auto dry = alignedDry (input, sr, static_cast<size_t> (probe.getLatency()));

                const auto zero = render (sr, withMix (0.0f, false, false, settings), input);
                CHECK (zero == dry);

                const auto wet = render (sr, settings, input);
                const auto third = render (sr, withMix (0.3f, false, false, settings), input);
                float worst = 0.0f;
                for (size_t i = 0; i < input.size(); ++i)
                    worst = std::max (worst, std::abs (third[i] - (dry[i] + (wet[i] - dry[i]) * 0.3f)));
                CHECK (worst < 1.0e-6f);
            }
}

void testDeltaIsSilentWithoutSaturation()
{
    std::printf ("Delta: sin saturación (−40 dBFS, curvas rectas) la diferencia es silencio, también en graves (fase compensada)\n");

    for (const double sr : sampleRates)
        for (int os = 0; os < riptide::dsp::numOversamplingChoices; ++os)
            for (const Mode mode : { Mode::transistor, Mode::hardClip, Mode::fold })
                for (const double hz : { 30.0, 1000.0, 12000.0 })
                {
                    const auto input = sine (hz, sr, static_cast<size_t> (sr / 2), 0.01f);
                    const auto delta = render (sr, withMix (1.0f, false, true, withOversampling (os, withMode (mode))), input);
                    // Sin el filtro DC en la dry, a 30 Hz quedaría un residuo de −16 dB (el desfase del filtro DC de la wet).
                    CHECK (rmsDb (delta, delta.size() / 2) - rmsDb (input, input.size() / 2) < -80.0);
                }
}

void testDeltaIsWhatTheSaturationAdds()
{
    std::printf ("Delta: es mix·(wet − dry); con drive alto contiene los armónicos de la saturación y no la fundamental\n");

    const double sr = 48000.0;
    const auto input = sine (1000.0, sr, 24000, 0.25f);
    const auto settings = withMode (Mode::hardClip, { 18.0f, 0.0f, false });
    Engine probe;
    probe.prepare (sr, settings);
    const auto dry = alignedDry (input, sr, static_cast<size_t> (probe.getLatency()));
    const auto wet = render (sr, settings, input);
    for (const float mix : { 1.0f, 0.5f })
    {
        const auto delta = render (sr, withMix (mix, false, true, settings), input);
        float worst = 0.0f;
        for (size_t i = 0; i < input.size(); ++i)
            worst = std::max (worst, std::abs (delta[i] - mix * (wet[i] - dry[i])));
        CHECK (worst < 1.0e-5f);
        // La dry no tiene 3er armónico: el del delta es exactamente el que creó la saturación (escalado por el mix).
        CHECK (std::abs (toneAmplitude (delta, 3000.0, sr, 12000) - mix * toneAmplitude (wet, 3000.0, sr, 12000)) < 1.0e-4);
    }
}

void testAutoGainMatchesLevel()
{
    std::printf ("Auto gain: la salida queda al volumen de la entrada (seno y ruido rosa: nunca más fuerte, como mucho 3 dB menos)\n");

    const double sr = 48000.0;
    const size_t count = static_cast<size_t> (sr * 6.0); // 6 constantes de tiempo del nivel
    const size_t start = count - static_cast<size_t> (sr);
    Engine probe;
    probe.prepare (sr, {});
    const auto latency = static_cast<size_t> (probe.getLatency());
    // Se compara con la dry alineada: el filtro DC le quita al ruido rosa algo de sus graves más profundos (unos 0.6 dB),
    // y eso no es cosa del auto gain.
    const auto tone = sine (440.0, sr, count, 0.25f);      // −15 dBFS RMS
    const auto hiss = pinkNoise (count, 0.18f, 6u);        // −15 dBFS RMS
    const double toneDb = rmsDb (alignedDry (tone, sr, latency), start);
    const double hissDb = rmsDb (alignedDry (hiss, sr, latency), start);
    for (const Mode mode : allModes)
    {
        std::printf ("   %-10s", riptide::dsp::modeNames[static_cast<size_t> (mode)]);
        for (const float drive : { 0.0f, 6.0f, 12.0f, 24.0f })
        {
            const auto settings = withMix (1.0f, true, false, withMode (mode, { drive, 0.0f, false }));
            const double sineError = rmsDb (render (sr, settings, tone), start) - toneDb;
            const double noiseError = rmsDb (render (sr, settings, hiss), start) - hissDb;
            std::printf ("  %+5.1f dB: seno %+5.2f ruido %+5.2f", drive, sineError, noiseError);
            // Nunca más fuerte (medio dB de margen), como mucho 3 dB más bajo. Las curvas que solo comprimen, con un seno,
            // exactas. (Rectify con un seno queda ~2.7 dB bajo: es el precio de no pasarse con ruido.)
            CHECK (sineError < 0.5 && sineError > -3.0);
            CHECK (noiseError < 0.5 && noiseError > -3.0);
            if (mode == Mode::tube || mode == Mode::transistor || mode == Mode::hardClip)
                CHECK (std::abs (sineError) < 0.2);
        }
        std::printf ("\n");
    }
}

void testAutoGainFollowsDriveInstantly()
{
    std::printf ("Auto gain: mover Input no cambia el volumen (nivel bajo: exacto; saturando: bajar el drive nunca da un salto)\n");

    const double sr = 48000.0;
    // Nivel bajo, curva recta: la compensación es −drive y la salida es la dry alineada, incluso durante la rampa.
    {
        Engine engine;
        auto settings = withMix (1.0f, true, false, withMode (Mode::transistor));
        engine.prepare (sr, settings);
        const auto input = sine (1000.0, sr, 48000, 0.003f);
        const auto dry = alignedDry (input, sr, static_cast<size_t> (engine.getLatency()));
        std::vector<float> out = input, none;
        for (size_t start = 0; start < out.size(); start += 4800)
        {
            settings.inputGainDb = start % 9600 == 0 ? 24.0f : -12.0f; // saltos de 36 dB cada 100 ms
            engine.setSettings (settings);
            float* channels[1] { out.data() + start };
            engine.process (channels, 1, 4800);
        }
        // Muestra a muestra no puede ser exacto durante la rampa (los filtros mezclan muestras saturadas con drives
        // distintos), pero el volumen de corto plazo no se mueve.
        const auto outLevel = shortTermDb (out, sr);
        const auto dryLevel = shortTermDb (dry, sr);
        double worst = 0.0;
        for (size_t w = 1; w < outLevel.size(); ++w)
            worst = std::max (worst, std::abs (outLevel[w] - dryLevel[w]));
        std::printf ("   saltos de 36 dB a nivel bajo: volumen de 10 ms a %.3f dB de la original\n", worst);
        CHECK (worst < 0.1);
    }
    // Saturando mucho (clip duro, +24 dB): al bajar el drive a 0 de golpe la salida no sube más de 1 dB sobre su nivel.
    {
        Engine engine;
        auto settings = withMix (1.0f, true, false, withMode (Mode::hardClip, { 24.0f, 0.0f, false }));
        engine.prepare (sr, settings);
        const auto input = sine (220.0, sr, static_cast<size_t> (sr * 8.0), 0.5f);
        std::vector<float> out = input, none;
        const size_t change = static_cast<size_t> (sr * 5.0);
        std::vector<float> before (out.begin(), out.begin() + static_cast<std::ptrdiff_t> (change));
        processInBlocks (engine, before, none, 512);
        settings.inputGainDb = 0.0f;
        engine.setSettings (settings);
        std::vector<float> after (out.begin() + static_cast<std::ptrdiff_t> (change), out.end());
        processInBlocks (engine, after, none, 512);

        // Volumen de corto plazo (no picos: una cuadrada y un seno del mismo volumen tienen picos distintos).
        const double settled = shortTermDb (before, sr).back();
        double loudest = -100.0;
        for (const double level : shortTermDb (after, sr))
            loudest = std::max (loudest, level);
        std::printf ("   clip duro, drive de +24 a 0 dB: el volumen de 10 ms sube como mucho %.2f dB\n", loudest - settled);
        CHECK (loudest - settled < 1.0);
        CHECK (std::isfinite (after.back()));
    }
}

void testAutoGainHoldsThroughSilence()
{
    std::printf ("Auto gain: en silencio la compensación se queda donde estaba (puerta de −60 dBFS)\n");

    const double sr = 44100.0;
    Engine engine;
    engine.prepare (sr, withMix (1.0f, true, false, withMode (Mode::tube, { 18.0f, 0.0f, false })));
    auto music = noise (static_cast<size_t> (sr * 5.0), 0.5f, 7u);
    std::vector<float> none;
    processInBlocks (engine, music, none, 256);
    const float before = engine.getAutoGainDb();
    std::vector<float> silence (static_cast<size_t> (sr * 3.0), 0.0f);
    processInBlocks (engine, silence, none, 256);
    std::printf ("   tras 3 s de silencio la compensación se movió %.2f dB\n", engine.getAutoGainDb() - before);
    CHECK (std::abs (engine.getAutoGainDb() - before) < 0.6f);
    CHECK (before > -18.0f + 3.0f); // compensa más que el drive: la curva comprimió
}

void testMixControlsAreClickless()
{
    std::printf ("Mix, auto gain y delta: mover el mix y conmutar auto gain y delta no da saltos\n");

    for (const double sr : sampleRates)
    {
        const auto base = withMode (Mode::tube, { 12.0f, 0.0f, false });
        const auto input = sine (100.0, sr, static_cast<size_t> (sr), 0.5f);
        const float fadeStep = 1.0f / static_cast<float> (Engine::switchFadeSeconds * sr);

        // Paso máximo de cada estado quieto: el más empinado es la wet sin compensar.
        float steady = 0.0f;
        for (const bool automatic : { false, true })
            for (const float mix : { 0.0f, 1.0f })
                for (const bool delta : { false, true })
                    steady = std::max (steady, maxStep (render (sr, withMix (mix, automatic, delta, base), input)));

        Engine engine;
        engine.prepare (sr, base);
        std::vector<float> signal = input, none;
        const size_t length = static_cast<size_t> (sr / 20);
        for (size_t start = 0, step = 0; start < signal.size(); start += length, ++step)
        {
            // Saltos bruscos de los tres controles, algunos a mitad de un fundido (cada 50 ms < 20 ms × 3).
            engine.setSettings (withMix (step % 3 == 0 ? 0.0f : 1.0f, step % 2 == 0, step % 4 == 1, base));
            const size_t count = std::min (length, signal.size() - start);
            float* channels[1] { signal.data() + start };
            engine.process (channels, 1, static_cast<int> (count));
        }
        // Wet (Tube +12 dB: hasta 1.4) y dry (0.5): cada fundido agrega como mucho su paso por muestra por la diferencia
        // entre los dos estados. Un escalón (clic) sería del orden de la señal.
        std::printf ("   %6.0f Hz: paso máximo %.4f (quieto: %.4f)\n", sr, maxStep (signal), steady);
        CHECK (maxStep (signal) < steady + 4.0f * fadeStep);
    }
}

void testBypassIgnoresMix()
{
    std::printf ("Bypass: con mix, auto gain y delta puestos, la salida sigue siendo la entrada retrasada, bit a bit\n");

    for (const double sr : sampleRates)
    {
        Engine engine;
        engine.prepare (sr, withMix (0.4f, true, true, withMode (Mode::fold, { 12.0f, 3.0f, true })));
        const auto latency = static_cast<size_t> (engine.getLatency());
        const auto input = noise (10000, 0.9f, 8u);
        auto left = input;
        std::vector<float> none;
        processInBlocks (engine, left, none, 128);
        bool exact = true;
        for (size_t i = 0; i < left.size(); ++i)
            exact = exact && left[i] == (i < latency ? 0.0f : input[i - latency]);
        CHECK (exact);
    }
}

} // namespace

int main()
{
    testBypassIsDelayedInput();
    testCurves();
    testTapeEmphasisCancels();
    testLatencyAlignsWetAndDry();
    testModeCharacter();
    testLowLevelIsClean();
    testTapeCompressesHighsMore();
    testDcIsRemoved();
    testRectifyDcIsRemoved();
    testBypassToggleIsSmooth();
    testOversamplingSwitchIsClickless();
    testModeSwitchIsClickless();
    testToneNeutralIsTransparent();
    testCutFilters();
    testPreAndPostSoundDifferent();
    testEmphasisCancelsAtLowLevel();
    testEmphasisChoosesWhatSaturates();
    testTiltAtLowLevel();
    testToneChangesAreClickless();
    testInputMeterSeesEmphasis();
    testBlockSizeIndependence();
    testMeters();
    testMixEndpoints();
    testDeltaIsSilentWithoutSaturation();
    testDeltaIsWhatTheSaturationAdds();
    testAutoGainMatchesLevel();
    testAutoGainFollowsDriveInstantly();
    testAutoGainHoldsThroughSilence();
    testMixControlsAreClickless();
    testBypassIgnoresMix();

    if (failures == 0)
        std::printf ("\nTodos los tests de Riptide pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);
    return failures == 0 ? 0 : 1;
}
