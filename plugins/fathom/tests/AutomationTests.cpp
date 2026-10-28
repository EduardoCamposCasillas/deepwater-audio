// Tests de la Fase 16 (automatización extrema). Con JUCE, sobre el procesador COMPLETO, como lo usa un host: los
// parámetros cambian con setValueNotifyingHost (lo mismo que la automatización de FL) y el audio pasa por
// processBlock. Los tests del motor (EngineTests.cpp) ya prueban cada etapa con sus ajustes al azar; aquí se prueba
// todo junto y lo que solo pasa en el plugin:
//   1. caos: los 369 parámetros saltando al azar en bloques de 1 a 1024 muestras, a las cuatro frecuencias de muestreo,
//      mono y estéreo, con y sin sidechain: sin NaN, sin reservas de memoria en el audio, la latencia siempre una de las
//      válidas y la salida acotada;
//   2. barridos rápidos: bandas que recorren de 20 Hz a 20 kHz en 50 ms con la ganancia y el Q moviéndose, sobre un
//      seno grave, en los tres modos de fase: sin clics (lo que aparece en los agudos, donde la entrada no tiene nada);
//   3. hilos a la vez: el audio en tiempo real en su hilo mientras el hilo de mensajes mueve parámetros, carga presets,
//      cambia A/B, deshace y carga el proyecto: sin NaN ni reservas en el audio, y nada se cuelga.
//
//   FathomAutomationTests             todos los tests

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/AnalogColor.h"
#include "dsp/PhaseDesign.h"
#include "dsp/SpectralDynamics.h"

#include "AllocationCounter.h" // reemplaza operator new / delete en este programa

namespace
{
namespace id = fathom::params;

int failures = 0;

void check (bool condition, const juce::String& message)
{
    if (! condition)
    {
        ++failures;
        std::printf ("    FALLO: %s\n", message.toRawUTF8());
    }
}

void runTest (const char* name, const std::function<void()>& test)
{
    const int before = failures;
    std::printf ("%s\n", name);
    test();
    std::printf ("  %s\n", failures == before ? "ok" : "FALLA");
}

constexpr double twoPi = 6.283185307179586;
constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };

// Todos los parámetros del plugin (los de AudioProcessor, no el APVTS: así se ven como los ve el host).
juce::Array<juce::RangedAudioParameter*> allParameters (FathomAudioProcessor& processor)
{
    juce::Array<juce::RangedAudioParameter*> list;
    for (auto* parameter : processor.AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            list.add (ranged);
    return list;
}

void setValue (FathomAudioProcessor& processor, const std::string& parameterId, float value)
{
    auto* parameter = processor.getParameters().getParameter (parameterId);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void prepare (FathomAudioProcessor& processor, double sampleRate, int channels, bool sidechain, bool offline, int maxBlock)
{
    auto layout = processor.getBusesLayout();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.getReference (0) = set;
    layout.outputBuses.getReference (0) = set;
    layout.inputBuses.getReference (1) = sidechain ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    check (processor.setBusesLayout (layout), "layout aceptado");
    processor.setNonRealtime (offline);
    processor.setRateAndBufferSizeDetails (sampleRate, maxBlock);
    processor.prepareToPlay (sampleRate, maxBlock);
}

// La mayor latencia posible: fase lineal en la calidad máxima + la espectral + el color (75 muestras a 44.1 kHz; 160 es
// el tope de su línea de retardo).
int maxLatency (double sampleRate)
{
    using namespace fathom::dsp;
    return PhaseSizes::latency (PhaseMode::linear, numLinearQualities - 1, sampleRate) + SpectralSizes::latency (sampleRate) + 160;
}

// Señal de prueba: un 808 en A1 + un acorde + ruido (de todo un poco), un poco distinta en cada lado; el sidechain, un
// kick cada 500 ms.
struct Signal
{
    juce::AudioBuffer<float> main, sidechain;
};

Signal makeSignal (double sampleRate, double seconds)
{
    const int total = static_cast<int> (seconds * sampleRate);
    Signal signal;
    signal.main.setSize (2, total);
    signal.sidechain.setSize (2, total);
    juce::Random random (99);
    for (int i = 0; i < total; ++i)
    {
        const double t = i / sampleRate;
        const double kickTime = std::fmod (t, 0.5);
        const auto kick = static_cast<float> (0.9 * std::exp (-kickTime / 0.07) * std::sin (twoPi * (50.0 * kickTime + 3.3 * (1.0 - std::exp (-30.0 * kickTime)))));
        for (int ch = 0; ch < 2; ++ch)
        {
            const double tone = 0.3 * std::sin (twoPi * 55.0 * t) + 0.05 * std::sin (twoPi * (330.0 + 2.0 * ch) * t);
            signal.main.setSample (ch, i, static_cast<float> (tone + 0.05 * (random.nextDouble() * 2.0 - 1.0)));
            signal.sidechain.setSample (ch, i, kick);
        }
    }
    return signal;
}

struct Run
{
    bool finite = true;
    int allocations = 0;
    bool latencyValid = true;
    float peak = 0.0f;
    juce::AudioBuffer<float> output;
};

// Procesa 'input' en bloques de largo 'blockLength (block)'; antes de cada bloque, 'beforeBlock (block)'.
Run process (FathomAudioProcessor& processor, const Signal& input, int channels, bool sidechain, double sampleRate,
             const std::function<int (int)>& blockLength, const std::function<void (int)>& beforeBlock)
{
    const int total = input.main.getNumSamples();
    const int sidechainChannels = sidechain ? 2 : 0;
    Run run;
    run.output.setSize (channels, total);
    juce::AudioBuffer<float> buffer (channels + sidechainChannels, 4096);
    juce::MidiBuffer midi;
    const int limit = maxLatency (sampleRate);
    for (int start = 0, block = 0; start < total; ++block)
    {
        const int count = std::min (blockLength (block), total - start);
        buffer.setSize (channels + sidechainChannels, count, false, false, true);
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom (ch, 0, input.main, ch, start, count);
        for (int ch = 0; ch < sidechainChannels; ++ch)
            buffer.copyFrom (channels + ch, 0, input.sidechain, ch, start, count);
        if (beforeBlock)
            beforeBlock (block);
        allocation::count = 0;
        {
            const allocation::Scope countAllocations;
            processor.processBlock (buffer, midi);
        }
        run.allocations += allocation::count.load();
        const int latency = processor.getLatencySamples();
        run.latencyValid = run.latencyValid && latency >= 0 && latency <= limit;
        for (int ch = 0; ch < channels; ++ch)
            run.output.copyFrom (ch, start, buffer, ch, 0, count);
        start += count;
    }
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < total; ++i)
        {
            const float y = run.output.getSample (ch, i);
            if (! std::isfinite (y))
                run.finite = false;
            else
                run.peak = std::max (run.peak, std::abs (y));
        }
    return run;
}

// --- 1. Caos -----------------------------------------------------------------------------------------------------------

void testChaos()
{
    // Cada bloque cambia hasta 40 parámetros al azar (a veces todos de golpe); el bypass casi nunca, para que se oiga el
    // EQ. Bloques de 1 a 1024 muestras (también de 1 y 7, como pluginval). La salida puede subir muchísimo de verdad
    // (varias campanas de +30 dB apiladas, con la escala al 200 % y +24 dB de salida), así que su pico no dice nada. Lo
    // que delata un filtro inestable o un estado roto es que no se recupere: después del caos todo vuelve a su valor por
    // defecto y, pasados los fundidos, la salida tiene que ser la entrada bit a bit.
    for (const double sampleRate : sampleRates)
        for (const int channels : { 2, 1 })
            for (const bool sidechain : { true, false })
            {
                const auto owner = std::make_unique<FathomAudioProcessor>(); // grande: en el heap (en la pila la desborda)
                auto& processor = *owner;
                prepare (processor, sampleRate, channels, sidechain, true, 1024);
                const auto parameters = allParameters (processor);
                juce::Random random (static_cast<juce::int64> (sampleRate) + channels * 10 + (sidechain ? 1 : 0));
                const auto signal = makeSignal (sampleRate, 3.0);
                const auto run = process (
                    processor, signal, channels, sidechain, sampleRate,
                    [&random] (int) {
                        const int kind = random.nextInt (10);
                        return kind == 0 ? 1 : kind == 1 ? 7 : 1 + random.nextInt (1024);
                    },
                    [&] (int) {
                        const bool everything = random.nextInt (50) == 0;
                        const int changes = everything ? parameters.size() : random.nextInt (41);
                        for (int c = 0; c < changes; ++c)
                        {
                            auto* parameter = everything ? parameters[c] : parameters[random.nextInt (parameters.size())];
                            if (parameter->getParameterID() == id::bypass)
                                parameter->setValueNotifyingHost (random.nextInt (40) == 0 ? 1.0f : 0.0f);
                            else
                                parameter->setValueNotifyingHost (random.nextFloat());
                        }
                    });
                // Todo a su valor por defecto (EQ plano, cero latencia, sin color) y 3 s más: el último segundo = la entrada.
                for (auto* parameter : parameters)
                    parameter->setValueNotifyingHost (parameter->getDefaultValue());
                const auto calm = process (processor, signal, channels, sidechain, sampleRate, [] (int) { return 256; }, {});
                const int total = signal.main.getNumSamples();
                bool recovered = calm.finite && processor.getLatencySamples() == 0;
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = total - static_cast<int> (sampleRate); i < total; ++i)
                        recovered = recovered && calm.output.getSample (ch, i) == signal.main.getSample (ch, i);

                std::printf ("  %6.0f Hz %s %s: pico %.0f (%.0f dB), reservas %d, %s\n", sampleRate, channels == 2 ? "estereo" : "mono   ",
                             sidechain ? "con sidechain" : "sin sidechain", static_cast<double> (run.peak),
                             20.0 * std::log10 (std::max (1.0e-9, static_cast<double> (run.peak))), run.allocations + calm.allocations,
                             recovered ? "despues plano = entrada bit a bit" : "NO SE RECUPERA");
                check (run.finite, "sin NaN ni infinitos");
                check (run.allocations + calm.allocations == 0, "sin reservas de memoria en processBlock");
                check (run.latencyValid, "latencia siempre entre 0 y la máxima posible");
                check (recovered, "después del caos, con todo por defecto, la salida vuelve a ser la entrada bit a bit");
                processor.releaseResources();
            }
}

// --- 2. Barridos rápidos -----------------------------------------------------------------------------------------------

// Lo que queda de la salida por encima de ~6 kHz (un pasa altos de 8 polos): con un seno de 110 Hz en la entrada, solo
// un clic (un escalón: tiene de todo) pone algo ahí. La banda que barre pasa por los agudos, pero ahí no hay nada que
// subir. En dB respecto al pico de la salida.
double highFrequencyDb (const juce::AudioBuffer<float>& buffer, double sampleRate, int from)
{
    const auto cascade = deepwater::dsp::EqCascade::butterworthHighPass (6000.0, 8, 0.7071, sampleRate);
    float residual = 0.0f, peak = 1.0e-9f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        deepwater::dsp::EqCascadeFilter filter;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float x = buffer.getSample (ch, i);
            const float y = filter.process (x, cascade);
            if (i >= from)
            {
                residual = std::max (residual, std::abs (y));
                peak = std::max (peak, std::abs (x));
            }
        }
    }
    return 20.0 * std::log10 (std::max (1.0e-12, static_cast<double> (residual / peak)));
}

void testFastSweeps()
{
    // Tres campanas (una en mid) que barren 20 Hz-20 kHz-20 Hz en 100 ms, con la ganancia entre −18 y +18 dB y el Q entre
    // 0.3 y 10 a otro ritmo, más un corte de graves de 96 dB/oct que se mueve; la automatización llega cada 32 muestras.
    // Se compara con la misma señal y las mismas bandas QUIETAS (lo que pone el EQ de por sí en los agudos, que es
    // nada): el barrido no debe dejar nada audible arriba.
    for (const auto mode : { fathom::dsp::PhaseMode::zeroLatency, fathom::dsp::PhaseMode::natural, fathom::dsp::PhaseMode::linear })
        for (const double sampleRate : { 44100.0, 96000.0 })
        {
            const auto total = static_cast<int> (2.0 * sampleRate);
            Signal signal;
            signal.main.setSize (2, total);
            signal.sidechain.setSize (2, total);
            signal.sidechain.clear();
            for (int i = 0; i < total; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    signal.main.setSample (ch, i, static_cast<float> (0.25 * std::sin (twoPi * 110.0 * i / sampleRate)));

            const auto setUp = [&] (FathomAudioProcessor& processor) {
                setValue (processor, id::phaseMode, static_cast<float> (static_cast<int> (mode)));
                for (int b = 0; b < 3; ++b)
                {
                    setValue (processor, id::band (b, id::enabled), 1.0f);
                    setValue (processor, id::band (b, id::shape), 0.0f); // campana
                }
                setValue (processor, id::band (2, id::stereo), 3.0f); // mid
                setValue (processor, id::band (3, id::enabled), 1.0f);
                setValue (processor, id::band (3, id::shape), 5.0f); // corte de graves
                setValue (processor, id::band (3, id::slope), 8.0f); // 96 dB/oct
            };
            const auto moveTo = [&] (FathomAudioProcessor& processor, double seconds) {
                for (int b = 0; b < 3; ++b)
                {
                    const double phase = seconds * 10.0 + b / 3.0; // 10 vueltas por segundo
                    const double sweep = 1.0 - std::abs (2.0 * (phase - std::floor (phase)) - 1.0); // triángulo 0-1-0
                    setValue (processor, id::band (b, id::frequency), static_cast<float> (20.0 * std::pow (1000.0, sweep)));
                    setValue (processor, id::band (b, id::gain), static_cast<float> (18.0 * std::sin (twoPi * 3.7 * seconds + b)));
                    setValue (processor, id::band (b, id::q), static_cast<float> (0.3 * std::pow (33.3, 0.5 + 0.5 * std::sin (twoPi * 2.3 * seconds + b))));
                }
                setValue (processor, id::band (3, id::frequency), static_cast<float> (25.0 + 40.0 * (0.5 + 0.5 * std::sin (twoPi * 1.1 * seconds))));
            };

            const auto movingOwner = std::make_unique<FathomAudioProcessor>(), stillOwner = std::make_unique<FathomAudioProcessor>();
            auto& moving = *movingOwner;
            auto& still = *stillOwner;
            for (auto* processor : { &moving, &still })
            {
                setUp (*processor);
                moveTo (*processor, 0.0);
                prepare (*processor, sampleRate, 2, false, true, 32);
            }
            const auto every32 = [] (int) { return 32; };
            const auto runMoving = process (moving, signal, 2, false, sampleRate, every32,
                                            [&] (int block) { moveTo (moving, block * 32 / sampleRate); });
            const auto runStill = process (still, signal, 2, false, sampleRate, every32, {});
            const int from = moving.getLatencySamples() + static_cast<int> (0.1 * sampleRate);
            const double movingDb = highFrequencyDb (runMoving.output, sampleRate, from);
            const double stillDb = highFrequencyDb (runStill.output, sampleRate, from);
            const char* name = mode == fathom::dsp::PhaseMode::zeroLatency ? "cero latencia" : mode == fathom::dsp::PhaseMode::natural ? "natural      " : "lineal       ";
            std::printf ("  %s %6.0f Hz: arriba de 6 kHz barriendo %.1f dB (quieto %.1f dB) del pico\n", name, sampleRate, movingDb, stillDb);
            check (runMoving.finite && runMoving.allocations == 0, "sin NaN ni reservas");
            // Calibración: lo que pone un clic chico (la salida quieta sube 1 % de golpe a la mitad).
            auto clicked = runStill.output;
            for (int ch = 0; ch < clicked.getNumChannels(); ++ch)
                clicked.applyGain (ch, total / 2, total - total / 2, 1.01f);
            const double clickDb = highFrequencyDb (clicked, sampleRate, from);
            // La fase lineal cambia de FIR por bloques (fundido de 512 muestras, 1024 a 96 kHz). Con el fundido recto, los
            // quiebres de pendiente entre fundidos encadenados dejaban −65 dB aquí (12 dB debajo del clic de 1 %); con el
            // fundido en S (Convolver.h), −110 dB: la misma cota que las bandas IIR.
            constexpr double bound = -90.0;
            std::printf ("    un clic de 1 %% pone %.1f dB; cota %.0f dB\n", clickDb, bound);
            check (clickDb > bound + 10.0, "el test ve un clic chico");
            check (movingDb < bound, "barrer no deja nada audible en los agudos (un clic sí)");
        }
}

// --- 3. Hilos a la vez -------------------------------------------------------------------------------------------------

void testConcurrentThreads()
{
    // Como en FL: el audio en su hilo, en tiempo real (los FIR los diseña su hilo y la memoria de los modos de fase la
    // pone el temporizador), y a la vez, desde el hilo de mensajes, todo lo que hace la GUI o el host: parámetros al azar,
    // cargar presets, A/B, copiar al otro lado, deshacer/rehacer, guardar y cargar el proyecto.
    for (const double sampleRate : { 48000.0, 96000.0 })
    {
        const auto owner = std::make_unique<FathomAudioProcessor>(); // grande: en el heap (en la pila la desborda)
        auto& processor = *owner;
        constexpr int blockSize = 64;
        prepare (processor, sampleRate, 2, true, false, blockSize);
        const auto signal = makeSignal (sampleRate, 1.0);
        const int total = signal.main.getNumSamples();

        std::atomic<bool> running { true };
        std::atomic<int> allocations { 0 }, blocks { 0 };
        std::atomic<bool> finite { true };
        std::thread audio ([&] {
            juce::AudioBuffer<float> buffer (4, blockSize);
            juce::MidiBuffer midi;
            int position = 0;
            const auto period = std::chrono::duration<double> (blockSize / sampleRate);
            auto next = std::chrono::steady_clock::now();
            while (running.load())
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    buffer.copyFrom (ch, 0, signal.main, ch, position, blockSize);
                    buffer.copyFrom (2 + ch, 0, signal.sidechain, ch, position, blockSize);
                }
                position = position + 2 * blockSize > total ? 0 : position + blockSize;
                allocation::count = 0;
                {
                    const allocation::Scope countAllocations;
                    processor.processBlock (buffer, midi);
                }
                allocations += allocation::count.load();
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                        if (! std::isfinite (buffer.getSample (ch, i)) || std::abs (buffer.getSample (ch, i)) > 1.0e4f)
                            finite = false;
                ++blocks;
                // A ritmo de tiempo real (como una placa de audio), así el hilo de mensajes y el de diseño corren entre medio.
                next += std::chrono::duration_cast<std::chrono::steady_clock::duration> (period);
                std::this_thread::sleep_until (next);
            }
        });

        const auto parameters = allParameters (processor);
        auto& presets = processor.getPresetManager();
        const auto& factory = presets.getPresets();
        juce::Random random (7);
        juce::MemoryBlock project;
        processor.getStateInformation (project);
        auto& loop = *juce::MessageManager::getInstance();
        const auto end = juce::Time::getMillisecondCounter() + 4000;
        int actions = 0;
        while (juce::Time::getMillisecondCounter() < end)
        {
            switch (random.nextInt (8))
            {
                case 0:
                case 1:
                case 2:
                    for (int c = 0; c < 20; ++c)
                        if (auto* parameter = parameters[random.nextInt (parameters.size())]; parameter->getParameterID() != id::bypass)
                        {
                            parameter->beginChangeGesture(); // como una perilla de la GUI: entra al deshacer
                            parameter->setValueNotifyingHost (random.nextFloat());
                            parameter->endChangeGesture();
                        }
                    break;
                case 3: presets.load (factory[static_cast<size_t> (random.nextInt (static_cast<int> (factory.size())))]); break;
                case 4: processor.switchCompareSide(); break;
                case 5:
                    if (random.nextBool())
                        processor.getEditHistory().undo();
                    else
                        processor.getEditHistory().redo();
                    break;
                case 6: processor.copyToOtherSide(); break;
                default:
                    if (random.nextBool())
                        processor.getStateInformation (project);
                    else
                        processor.setStateInformation (project.getData(), static_cast<int> (project.getSize()));
                    break;
            }
            ++actions;
            loop.runDispatchLoopUntil (5); // los temporizadores del procesador (latencia, memoria de los modos de fase)
        }
        running = false;
        audio.join();
        std::printf ("  %6.0f Hz: %d acciones en el hilo de mensajes, %d bloques de audio, reservas en el audio %d\n", sampleRate,
                     actions, blocks.load(), allocations.load());
        check (finite.load(), "sin NaN ni salidas disparadas");
        check (allocations.load() == 0, "sin reservas de memoria en processBlock");
        check (blocks.load() > static_cast<int> (3.0 * sampleRate / blockSize), "el audio siguió corriendo (nada se colgó)");
        processor.releaseResources();
    }
}

} // namespace

int main()
{
    const juce::ScopedJuceInitialiser_GUI init;
    runTest ("Caos: todos los parametros al azar en cada bloque (1-1024 muestras), 4 frecuencias, mono/estereo, con y sin sidechain",
             testChaos);
    runTest ("Barridos rapidos: bandas que recorren 20 Hz-20 kHz en 50 ms (ganancia y Q moviendose) sin clics, en los tres modos de fase",
             testFastSweeps);
    runTest ("Hilos a la vez: el audio en tiempo real mientras el hilo de mensajes mueve parametros, presets, A/B, deshacer y el proyecto",
             testConcurrentThreads);

    if (failures > 0)
    {
        std::printf ("\n%d comprobaciones fallaron\n", failures);
        return 1;
    }
    std::printf ("\nTodos los tests pasaron\n");
    return 0;
}
