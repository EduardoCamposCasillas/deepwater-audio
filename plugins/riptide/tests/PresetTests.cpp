// Tests de la Fase 9 (presets). A diferencia de EngineTests.cpp, estos usan JUCE: compilan el procesador completo para
// probar exactamente lo que hace el plugin (parámetros, estado del proyecto, XML).
//
//   RiptidePresetTests                    todos los tests
//   RiptidePresetTests --verbose          además, la tabla de niveles de cada preset de fábrica
//   RiptidePresetTests --user "C:\ruta"   mide también los presets de esa carpeta (p. ej. los tuyos)
//   RiptidePresetTests --render <carpeta> guarda las señales de prueba y cada preset aplicado a la suya, en WAV (no corre
//                                         los tests; sirve para escucharlos y para null tests de la Fase 10)
//   RiptidePresetTests --notes            imprime las notas de los presets de fábrica como las muestra el navegador
//
// Un efecto no se puede probar sin una señal: aquí se generan señales parecidas a material real (una batería, un bajo,
// una voz, un acorde y una mezcla de las tres) con una semilla fija, así cada ejecución mide exactamente lo mismo.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <new>
#include <set>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "presets/RiptidePresets.h"

// --- Reservas de memoria en el hilo de audio -------------------------------------------------------------------------
//
// Regla del proyecto: processBlock no puede reservar ni liberar memoria. No hay forma de verlo escuchando, así que este
// programa REEMPLAZA operator new / delete (C++ lo permite una vez por ejecutable) por versiones que, mientras
// 'counting' está activo, cuentan cada llamada. (La misma técnica que UndertowPresetTests.)
namespace allocation
{
std::atomic<bool> counting { false };
std::atomic<int> count { 0 };

void note (void* p) noexcept
{
    if (p != nullptr && counting.load (std::memory_order_relaxed))
        count.fetch_add (1, std::memory_order_relaxed);
}

void* allocate (std::size_t size)
{
    note (reinterpret_cast<void*> (1));
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* allocateAligned (std::size_t size, std::align_val_t alignment)
{
    note (reinterpret_cast<void*> (1));
    if (void* p = _aligned_malloc (size == 0 ? 1 : size, static_cast<std::size_t> (alignment)))
        return p;
    throw std::bad_alloc();
}

struct Scope
{
    Scope() { counting = true; }
    ~Scope() { counting = false; }
};
} // namespace allocation

void* operator new (std::size_t size) { return allocation::allocate (size); }
void* operator new[] (std::size_t size) { return allocation::allocate (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    allocation::note (reinterpret_cast<void*> (1));
    return std::malloc (size == 0 ? 1 : size);
}
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    allocation::note (reinterpret_cast<void*> (1));
    return std::malloc (size == 0 ? 1 : size);
}
void* operator new (std::size_t size, std::align_val_t alignment) { return allocation::allocateAligned (size, alignment); }
void* operator new[] (std::size_t size, std::align_val_t alignment) { return allocation::allocateAligned (size, alignment); }
void operator delete (void* p) noexcept { allocation::note (p); std::free (p); }
void operator delete[] (void* p) noexcept { allocation::note (p); std::free (p); }
void operator delete (void* p, std::size_t) noexcept { allocation::note (p); std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { allocation::note (p); std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept { allocation::note (p); _aligned_free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { allocation::note (p); _aligned_free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { allocation::note (p); _aligned_free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { allocation::note (p); _aligned_free (p); }

namespace
{
using deepwater::presets::Preset;
using deepwater::presets::PresetManager;
namespace id = riptide::params;

int failures = 0;
bool verbose = false;
juce::File measuredUserFolder; // --user

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

float valueOf (RiptideAudioProcessor& processor, const char* parameterId)
{
    return processor.getParameters().getRawParameterValue (parameterId)->load();
}

void setValue (RiptideAudioProcessor& processor, const char* parameterId, float value)
{
    auto* parameter = processor.getParameters().getParameter (parameterId);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

const Preset* findPreset (PresetManager& manager, const juce::String& name)
{
    for (const auto& preset : manager.getPresets())
        if (preset.name == name)
            return &preset;
    return nullptr;
}

// Cada test usa su propia carpeta de usuario temporal: nunca se toca la carpeta real de Documentos.
struct TemporaryFolder
{
    TemporaryFolder()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("RiptidePresetTests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())))
    {
        folder.createDirectory();
    }
    ~TemporaryFolder() { folder.deleteRecursively(); }
    juce::File folder;
};

std::unique_ptr<RiptideAudioProcessor> makeProcessor (const juce::File& userFolder)
{
    auto processor = std::make_unique<RiptideAudioProcessor>();
    processor->getPresetManager().setUserFolder (userFolder);
    return processor;
}

float toDb (double gain)
{
    return static_cast<float> (20.0 * std::log10 (std::max (gain, 1.0e-9)));
}

// --- Señales de prueba -----------------------------------------------------------------------------------------------
//
// Estéreo, ~8 s, con una semilla fija. Niveles de una pista bien preparada (gain staging de la Fase 1): RMS cerca de
// −18 dBFS, picos de −10 a −3 dBFS. Las notas usan los nombres de FL (C5 = MIDI 60).
enum class Material { drums, bass, voice, chord, mix };

const char* materialName (Material material)
{
    switch (material)
    {
        case Material::drums: return "bateria";
        case Material::bass:  return "bajo";
        case Material::voice: return "voz";
        case Material::chord: return "acorde";
        case Material::mix:   return "mezcla";
    }
    return "?";
}

// Qué señal recibe cada categoría (la última, "Otros", y los presets del usuario: la mezcla).
Material materialFor (const juce::String& category)
{
    if (category == juce::String::fromUTF8 ("Bater\xc3\xad" "a") || category == "Bus")
        return Material::drums;
    if (category == "Bajo")
        return Material::bass;
    if (category == "Voz")
        return Material::voice;
    if (category == "Instrumentos" || category == "Creativo")
        return Material::chord;
    return Material::mix;
}

constexpr double twoPi = 6.283185307179586;

// Onda de dientes de sierra con armónicos que caen 1/n y un paso bajo suave (como un sintetizador o una cuerda): suma
// de senos, sin aliasing.
double softSaw (double phase, double hz, double sampleRate, double brightnessHz)
{
    double sum = 0.0;
    for (int n = 1; n * hz < std::min (sampleRate * 0.45, 16000.0); ++n)
    {
        const double h = n * hz;
        sum += std::sin (twoPi * phase * n) / n / std::sqrt (1.0 + (h / brightnessHz) * (h / brightnessHz));
    }
    return sum;
}

juce::AudioBuffer<float> makeSignal (Material material, double sampleRate, double seconds = 8.0)
{
    const int total = static_cast<int> (seconds * sampleRate);
    juce::AudioBuffer<float> buffer (2, total);
    buffer.clear();
    juce::Random random (1234);

    const auto addDrums = [&] (float gain) {
        // 120 BPM: bombo en 1 y 3, caja en 2 y 4, hi-hat cerrado en corcheas. El bombo es un seno que cae de 150 a 50 Hz;
        // la caja, un tono de 190 Hz + ruido; el hi-hat, ruido con un paso alto de 1.er orden (diferencia).
        double hatState = 0.0;
        for (int i = 0; i < total; ++i)
        {
            const double t = i / sampleRate;
            const double beat = std::fmod (t, 0.5);
            const double bar = std::fmod (t, 2.0);
            double sample = 0.0;
            const double kickT = std::fmod (t, 1.0);
            if (kickT < 0.4)
            {
                const double sweep = 50.0 * kickT + (100.0 / 30.0) * (1.0 - std::exp (-30.0 * kickT));
                sample += 0.9 * std::exp (-kickT / 0.18) * std::sin (twoPi * sweep);
            }
            const double snareT = std::fmod (bar - 0.5 + 2.0, 1.0);
            if (snareT < 0.25)
                sample += std::exp (-snareT / 0.06) * (0.35 * std::sin (twoPi * 190.0 * snareT) + 0.3 * (random.nextDouble() * 2.0 - 1.0));
            const double hatT = std::fmod (beat, 0.25);
            const double noise = random.nextDouble() * 2.0 - 1.0;
            const double hat = noise - hatState;
            hatState = noise;
            if (hatT < 0.08)
                sample += 0.12 * std::exp (-hatT / 0.02) * hat;
            for (int ch = 0; ch < 2; ++ch)
                buffer.addSample (ch, i, static_cast<float> (gain * sample));
        }
    };

    const auto addBass = [&] (float gain) {
        // Un bajo de synth: notas de 1 compás (F2 = 43.7 Hz, G#2, C3, A#2 de FL) con ataque corto, sierra suave y
        // abundante sub.
        constexpr std::array<double, 4> notes { 43.65, 51.91, 65.41, 58.27 };
        double phase = 0.0;
        for (int i = 0; i < total; ++i)
        {
            const double t = i / sampleRate;
            const double hz = notes[static_cast<size_t> (static_cast<int> (t / 2.0) % 4)];
            const double noteT = std::fmod (t, 2.0);
            phase += hz / sampleRate;
            const double envelope = std::min (1.0, noteT / 0.005) * (0.55 + 0.45 * std::exp (-noteT / 0.25))
                                    * std::min (1.0, (2.0 - noteT) / 0.01);
            const double sample = envelope * (0.6 * std::sin (twoPi * phase) + 0.35 * softSaw (phase, hz, sampleRate, 600.0));
            for (int ch = 0; ch < 2; ++ch)
                buffer.addSample (ch, i, static_cast<float> (gain * sample));
        }
    };

    const auto addVoice = [&] (float gain) {
        // Una "voz": serie armónica de 196–262 Hz (G4–C5 de FL) con vibrato de 5 Hz y tres formantes (vocal "a":
        // 800, 1200 y 2600 Hz), frases de 1.5 s con pausas, y una "s" de ruido agudo al empezar cada frase.
        constexpr std::array<double, 3> formants { 800.0, 1200.0, 2600.0 };
        constexpr std::array<double, 4> notes { 196.0, 220.0, 261.63, 233.08 };
        double phase = 0.0, sState = 0.0;
        for (int i = 0; i < total; ++i)
        {
            const double t = i / sampleRate;
            const double phraseT = std::fmod (t, 2.0);
            const double hz = notes[static_cast<size_t> (static_cast<int> (t / 2.0) % 4)] * (1.0 + 0.006 * std::sin (twoPi * 5.0 * t));
            phase += hz / sampleRate;
            double sample = 0.0;
            if (phraseT > 0.1 && phraseT < 1.6)
            {
                const double envelope = std::min (1.0, (phraseT - 0.1) / 0.04) * std::min (1.0, (1.6 - phraseT) / 0.08)
                                        * (0.8 + 0.2 * std::sin (twoPi * 0.7 * t));
                double voiced = 0.0;
                for (int n = 1; n * hz < 8000.0; ++n)
                {
                    double weight = 0.0;
                    for (const double f : formants)
                        weight += 1.0 / (1.0 + std::pow ((n * hz - f) / (0.12 * f), 2.0));
                    voiced += std::sin (twoPi * phase * n) * weight / std::sqrt (static_cast<double> (n));
                }
                sample += 0.32 * envelope * voiced;
            }
            if (phraseT < 0.1) // la "s": ruido con paso alto
            {
                const double noise = random.nextDouble() * 2.0 - 1.0;
                sample += 0.25 * std::sin (juce::MathConstants<double>::pi * phraseT / 0.1) * (noise - sState);
                sState = noise;
            }
            for (int ch = 0; ch < 2; ++ch)
                buffer.addSample (ch, i, static_cast<float> (gain * sample));
        }
    };

    const auto addChord = [&] (float gain) {
        // Acorde de A menor (A4, C5, E5 de FL) sostenido con sierras suaves, un poco abierto en estéreo.
        constexpr std::array<double, 3> notes { 220.0, 261.63, 329.63 };
        std::array<double, 3> phase {};
        for (int i = 0; i < total; ++i)
        {
            const double t = i / sampleRate;
            const double envelope = std::min (1.0, t / 0.05) * std::min (1.0, (seconds - t) / 0.05);
            std::array<double, 2> sample {};
            for (size_t n = 0; n < notes.size(); ++n)
            {
                phase[n] += notes[n] / sampleRate;
                const double voice = softSaw (phase[n], notes[n], sampleRate, 3000.0);
                sample[0] += voice * (n == 2 ? 0.7 : 1.0);
                sample[1] += voice * (n == 0 ? 0.7 : 1.0);
            }
            for (int ch = 0; ch < 2; ++ch)
                buffer.addSample (ch, i, static_cast<float> (gain * envelope * sample[static_cast<size_t> (ch)]));
        }
    };

    switch (material)
    {
        case Material::drums: addDrums (0.55f); break;
        case Material::bass:  addBass (0.30f); break;
        case Material::voice: addVoice (0.95f); break;
        case Material::chord: addChord (0.07f); break;
        case Material::mix:
            addDrums (0.40f);
            addBass (0.22f);
            addChord (0.035f);
            break;
    }
    return buffer;
}

// --- Render ----------------------------------------------------------------------------------------------------------

struct RenderStats
{
    float peak = 0.0f;
    double inputRms = 0.0;  // de la señal de entrada, en la ventana medida
    double outputRms = 0.0; // de la salida, en la misma ventana (compensando la latencia)
    bool finite = true;
    int allocations = 0; // reservas o liberaciones de memoria dentro de processBlock (debe ser 0)
    juce::AudioBuffer<float> output;

    // Cuánto cambia el volumen el preset (RMS de la salida respecto de la entrada).
    [[nodiscard]] float gainDb() const { return toDb (outputRms) - toDb (inputRms); }
};

// Pasa 'input' por el procesador en bloques. 'measureFrom' = desde qué segundo se mide el RMS (el auto gain tarda ~1 s
// en asentarse). beforeBlock se llama antes de cada bloque (p. ej. para cambiar de preset).
RenderStats render (RiptideAudioProcessor& processor, const juce::AudioBuffer<float>& input, double sampleRate, int blockSize = 256,
                    double measureFrom = 3.0, const std::function<void (int block)>& beforeBlock = {}, int channels = 2)
{
    // El layout se elige antes de preparar, como hace un host.
    auto layout = processor.getBusesLayout();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.getReference (0) = set;
    layout.outputBuses.getReference (0) = set;
    processor.setBusesLayout (layout);
    processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    const int total = input.getNumSamples();
    RenderStats stats;
    stats.output.setSize (channels, total);
    juce::AudioBuffer<float> buffer (channels, blockSize);
    juce::MidiBuffer midi;

    for (int start = 0, block = 0; start < total; start += blockSize, ++block)
    {
        const int count = std::min (blockSize, total - start);
        buffer.setSize (channels, count, false, false, true);
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom (ch, 0, input, ch, start, count);
        if (beforeBlock)
            beforeBlock (block);

        allocation::count = 0;
        {
            const allocation::Scope countAllocations;
            processor.processBlock (buffer, midi);
        }
        stats.allocations += allocation::count.load();
        for (int ch = 0; ch < channels; ++ch)
            stats.output.copyFrom (ch, start, buffer, ch, 0, count);
    }

    // RMS de la entrada y de la salida en la misma ventana (la salida llega 'latencia' muestras tarde).
    const int latency = processor.getLatencySamples();
    const int from = static_cast<int> (measureFrom * sampleRate);
    double inSum = 0.0, outSum = 0.0;
    int measured = 0;
    for (int i = from; i + latency < total; ++i, ++measured)
        for (int ch = 0; ch < channels; ++ch)
        {
            const double x = input.getSample (ch, i);
            const double y = stats.output.getSample (ch, i + latency);
            inSum += x * x;
            outSum += y * y;
        }
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < total; ++i)
        {
            const float y = stats.output.getSample (ch, i);
            if (! std::isfinite (y))
                stats.finite = false;
            else
                stats.peak = std::max (stats.peak, std::abs (y));
        }
    stats.inputRms = std::sqrt (inSum / std::max (1, measured * channels));
    stats.outputRms = std::sqrt (outSum / std::max (1, measured * channels));
    processor.releaseResources();
    return stats;
}

// --- Render para escuchar (--render) ---------------------------------------------------------------------------------

bool writeWav (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::File& file)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
    auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                                          .withNumChannels (audio.getNumChannels())
                                                                          .withBitsPerSample (32)
                                                                          .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

int renderPresets (const juce::File& outputFolder)
{
    constexpr double sampleRate = 48000.0;
    outputFolder.createDirectory();
    for (const auto material : { Material::drums, Material::bass, Material::voice, Material::chord, Material::mix })
        if (! writeWav (makeSignal (material, sampleRate), sampleRate, outputFolder.getChildFile (juce::String ("_senal-") + materialName (material) + ".wav")))
            return 1;

    TemporaryFolder temp;
    const auto names = [&temp] {
        juce::StringArray result;
        auto processor = makeProcessor (temp.folder);
        for (const auto& preset : processor->getPresetManager().getPresets())
            result.add (preset.name);
        return result;
    }();
    for (const auto& name : names)
    {
        // Un procesador nuevo por preset: el auto gain y el LFO empiezan siempre igual (el mismo código da las mismas
        // muestras: sirve para null tests).
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        const auto* preset = findPreset (manager, name);
        manager.load (*preset);
        const auto stats = render (*processor, makeSignal (materialFor (preset->category), sampleRate), sampleRate, 512);
        const auto file = outputFolder.getChildFile (juce::File::createLegalFileName (name).removeCharacters (" ") + ".wav");
        if (! writeWav (stats.output, sampleRate, file))
            return 1;
        std::printf ("%-28s -> %s\n", name.toRawUTF8(), file.getFileName().toRawUTF8());
    }
    return 0;
}

} // namespace

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI init;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String argument (argv[i]);
        verbose = verbose || argument == "--verbose";
        if (argument == "--user" && i + 1 < argc)
            measuredUserFolder = juce::File (juce::String (argv[++i]));
        if (argument == "--render" && i + 1 < argc)
            return renderPresets (juce::File (juce::String (argv[i + 1])));
        if (argument == "--notes")
        {
            RiptideAudioProcessor processor;
            for (const auto& preset : processor.getPresetManager().getPresets())
                std::printf ("===== %s (%s) =====\n%s\n\n", preset.name.toRawUTF8(), preset.category.toRawUTF8(),
                             preset.notes.toRawUTF8());
            return 0;
        }
    }

    runTest ("Presets de fabrica: se leen, nombres unicos, categorias validas, notas y valores correctos", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        const auto& presets = manager.getPresets();
        const auto categories = riptide::presets::categoryNames();
        check (presets.size() >= 20, "al menos 20 presets de fabrica (hay " + juce::String (static_cast<int> (presets.size())) + ")");

        std::set<juce::String> names;
        std::vector<int> perCategory (static_cast<size_t> (categories.size()), 0);
        for (const auto& preset : presets)
        {
            check (preset.factory, preset.name + ": deberia ser de fabrica");
            check (names.insert (preset.name.toLowerCase()).second, "nombre repetido: " + preset.name);
            check (preset.categoryIndex() < categories.size() - 1, preset.name + ": categoria desconocida " + preset.category);
            check (preset.notes.length() > 200, preset.name + ": las notas tienen que explicar el preset");
            check (preset.author == "Riptide", preset.name + ": autor");
            ++perCategory[static_cast<size_t> (preset.categoryIndex())];

            juce::StringArray problems;
            (void) manager.resolve (preset, &problems);
            check (problems.isEmpty(), preset.name + ": " + problems.joinIntoString ("; "));
            for (const auto& [parameterId, value] : preset.values)
                check (parameterId != id::bypass, preset.name + ": un preset no debe tocar el bypass");
        }
        check (perCategory[0] == 1, "un solo Init");
        for (int c = 0; c < categories.size() - 1; ++c)
            check (perCategory[static_cast<size_t> (c)] > 0, "categoria vacia: " + categories[c]);
        std::printf ("    %d presets de fabrica en %d categorias\n", static_cast<int> (presets.size()), categories.size() - 1);
    });

    runTest ("Cargar: aplica valores (unidades reales y nombres de opciones), lo que falta vuelve al defecto; el bypass no se toca", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();

        Preset preset;
        preset.name = "Prueba";
        preset.values = { { id::inputGain, "6.5" }, { id::mode, "Transistor" }, { id::lowCutPosition, "Post" },
                          { id::autoGain, "On" }, { id::bands, "2" }, { id::lfoDivision, "1/8 T" }, { id::highCut, "8000" } };
        setValue (*processor, id::tilt, 3.0f);   // no está en el preset: tiene que volver a 0
        setValue (*processor, id::bypass, 1.0f); // excluido: tiene que quedarse como está
        const auto problems = manager.load (preset);
        check (problems.isEmpty(), "sin avisos: " + problems.joinIntoString ("; "));
        check (std::abs (valueOf (*processor, id::inputGain) - 6.5f) < 1.0e-3f, "Input en dB");
        check (std::lround (valueOf (*processor, id::mode)) == 2, "modo por nombre");
        check (valueOf (*processor, id::lowCutPosition) > 0.5f, "Post por nombre");
        check (valueOf (*processor, id::autoGain) > 0.5f, "On");
        check (std::lround (valueOf (*processor, id::bands)) == 2, "un numero en una lista es el indice: \"2\" = 3 bandas");
        check (std::abs (valueOf (*processor, id::highCut) - 8000.0f) < 1.0f, "High Cut en Hz");
        check (std::abs (valueOf (*processor, id::tilt)) < 1.0e-3f, "lo que falta vuelve al defecto");
        check (valueOf (*processor, id::bypass) > 0.5f, "el bypass no cambia al cargar");
        check (manager.getCurrentName() == "Prueba" && ! manager.isModified(), "preset actual sin modificar");

        manager.loadInit();
        check (manager.getCurrentName() == "Init" && std::abs (valueOf (*processor, id::inputGain)) < 1.0e-3f, "Init");
        check (valueOf (*processor, id::bypass) > 0.5f, "Init tampoco toca el bypass");
    });

    runTest ("Modificado: tocar un parametro marca el preset; volver al valor lo desmarca; el bypass no cuenta", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        manager.load (manager.getPresets().back());
        check (! manager.isModified(), "recien cargado");
        setValue (*processor, id::bypass, 1.0f);
        check (! manager.isModified(), "el bypass no modifica el preset");
        const float before = valueOf (*processor, id::outputGain);
        setValue (*processor, id::outputGain, before + 3.0f);
        check (manager.isModified(), "modificado");
        setValue (*processor, id::outputGain, before);
        check (! manager.isModified(), "vuelve a su valor");
    });

    runTest ("Guardar preset de usuario: archivo legible sin el bypass, ida y vuelta exacta, reemplazar, borrar", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        manager.load (*findPreset (manager, manager.getPresets()[3].name));
        setValue (*processor, id::emphasisFreq, 3150.0f);
        setValue (*processor, id::lfoShape, 5.0f);
        const auto expected = manager.resolve (Preset {}, nullptr); // tamaño
        std::vector<float> before;
        for (const auto* parameter : manager.getParameters())
            before.push_back (parameter->getValue());

        check (manager.save (juce::String::fromUTF8 ("Mi Bater\xc3\xad" "a"), "Bus", "Yo", "Notas\ncon dos lineas").wasOk(), "guardar");
        const auto file = temp.folder.getChildFile (juce::String::fromUTF8 ("Mi Bater\xc3\xad" "a.rtpreset"));
        check (file.existsAsFile(), "archivo .rtpreset en la carpeta del usuario");
        const auto text = file.loadFileAsString();
        check (text.contains ("<RiptidePreset") && text.contains ("emphasisFreq"), "XML legible");
        check (! text.contains ("\"bypass\""), "el bypass no se guarda");

        manager.loadInit();
        const auto* saved = findPreset (manager, juce::String::fromUTF8 ("Mi Bater\xc3\xad" "a"));
        check (saved != nullptr && ! saved->factory && saved->category == "Bus" && saved->notes == "Notas\ncon dos lineas",
               "se relee con categoria y notas");
        if (saved != nullptr)
        {
            manager.load (*saved);
            for (size_t i = 0; i < before.size(); ++i)
                check (std::abs (manager.getParameters()[i]->getValue() - before[i]) < 1.0e-4f,
                       "ida y vuelta: " + manager.getParameters()[i]->paramID);
        }
        check (expected.size() == before.size(), "resolve cubre todos los parametros");
        check (manager.save ("Init", "Bus", {}, {}).failed(), "no se puede usar el nombre de uno de fabrica");
        check (manager.save (juce::String::fromUTF8 ("Mi Bater\xc3\xad" "a"), "Bus", {}, "otra").wasOk(), "reemplazar");
        if (const auto* replaced = findPreset (manager, juce::String::fromUTF8 ("Mi Bater\xc3\xad" "a")))
        {
            check (replaced->notes == "otra", "reemplazado");
            check (manager.remove (*replaced).wasOk(), "borrar (a la papelera)");
        }
        check (findPreset (manager, juce::String::fromUTF8 ("Mi Bater\xc3\xad" "a")) == nullptr, "borrado");
    });

    runTest ("Archivos raros: ids desconocidos, opciones inexistentes y XML ajeno no rompen nada", [] {
        TemporaryFolder temp;
        temp.folder.getChildFile ("Raro.rtpreset").replaceWithText (
            "<RiptidePreset formatVersion=\"1\" name=\"Raro\" category=\"Inventada\"><Parameters>"
            "<PARAM id=\"noExiste\" value=\"1\"/><PARAM id=\"mode\" value=\"Laser\"/><PARAM id=\"inputGain\" value=\"6\"/>"
            "</Parameters></RiptidePreset>");
        temp.folder.getChildFile ("Futuro.rtpreset").replaceWithText ("<RiptidePreset formatVersion=\"99\" name=\"Futuro\"/>");
        temp.folder.getChildFile ("Ajeno.rtpreset").replaceWithText ("<UndertowPreset formatVersion=\"1\" name=\"Ajeno\"/>");
        temp.folder.getChildFile ("Roto.rtpreset").replaceWithText ("<RiptidePreset name=");
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        const auto* weird = findPreset (manager, "Raro");
        check (weird != nullptr && weird->categoryIndex() == riptide::presets::categoryNames().size() - 1, "categoria desconocida -> Otros");
        check (findPreset (manager, "Futuro") == nullptr && findPreset (manager, "Ajeno") == nullptr, "se ignoran");
        if (weird != nullptr)
        {
            const auto problems = manager.load (*weird);
            check (problems.size() == 2, "dos avisos (id y opcion)");
            check (std::abs (valueOf (*processor, id::inputGain) - 6.0f) < 1.0e-3f, "lo valido se aplica");
        }
    });

    runTest ("Proyecto de FL: el nombre y el estado 'modificado' se guardan con el proyecto", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        const auto name = manager.getPresets()[2].name;
        manager.load (*findPreset (manager, name));

        juce::MemoryBlock state;
        processor->getStateInformation (state);
        auto reopened = makeProcessor (temp.folder);
        reopened->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        check (reopened->getPresetManager().getCurrentName() == name, "nombre restaurado");
        check (! reopened->getPresetManager().isModified(), "sin modificar");

        setValue (*processor, id::mix, 37.0f);
        processor->getStateInformation (state);
        reopened = makeProcessor (temp.folder);
        reopened->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        check (reopened->getPresetManager().isModified(), "modificado restaurado");
        check (std::abs (valueOf (*reopened, id::mix) - 37.0f) < 0.05f, "el valor tambien");

        // Un proyecto de la Fase 8 (sin las propiedades del preset): "Sin nombre".
        auto tree = processor->getParameters().copyState();
        tree.removeProperty ("presetName", nullptr);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);
        reopened = makeProcessor (temp.folder);
        reopened->setStateInformation (old.getData(), static_cast<int> (old.getSize()));
        check (reopened->getPresetManager().getCurrentName() == "Sin nombre", "proyecto antiguo: Sin nombre");
        check (! reopened->getPresetManager().isModified(), "proyecto antiguo: sin asterisco");
    });

    runTest ("Cada preset de fabrica con su senal: sin NaN, sin pasar de 0 dBFS, sin reservar memoria y con el volumen igualado", [] {
        constexpr double sampleRate = 48000.0;
        TemporaryFolder temp;
        std::map<int, juce::AudioBuffer<float>> signals;
        const auto signalFor = [&signals] (Material material) -> const juce::AudioBuffer<float>& {
            auto& buffer = signals[static_cast<int> (material)];
            if (buffer.getNumSamples() == 0)
                buffer = makeSignal (material, sampleRate);
            return buffer;
        };

        std::vector<Preset> presets;
        {
            auto processor = makeProcessor (measuredUserFolder.isDirectory() ? measuredUserFolder : temp.folder);
            presets = processor->getPresetManager().getPresets();
        }
        float lowest = 100.0f, highest = -100.0f;
        if (verbose)
            std::printf ("    %-28s %-12s %-8s %9s %9s %9s\n", "preset", "categoria", "senal", "entrada", "salida", "cambio");
        for (const auto& preset : presets)
        {
            auto processor = makeProcessor (temp.folder);
            processor->getPresetManager().load (preset);
            const auto material = materialFor (preset.category);
            const auto& input = signalFor (material);
            const auto stats = render (*processor, input, sampleRate);
            const auto tag = preset.name + " (" + materialName (material) + ")";
            check (stats.finite, tag + ": NaN o infinito");
            check (stats.allocations == 0, tag + ": processBlock reservo memoria " + juce::String (stats.allocations) + " veces");
            check (stats.peak < 1.0f, tag + ": pico de " + juce::String (toDb (stats.peak), 1) + " dBFS");
            // Volumen igualado (presets de fábrica): ±1.5 dB de la señal original, salvo Init (no compensa nada).
            if (preset.factory && preset.categoryIndex() != 0)
            {
                check (std::abs (stats.gainDb()) <= 1.5f, tag + ": cambia el volumen " + juce::String (stats.gainDb(), 1) + " dB");
                lowest = std::min (lowest, stats.gainDb());
                highest = std::max (highest, stats.gainDb());
            }
            if (verbose)
                std::printf ("    %-28s %-12s %-8s %6.1f dB %6.1f dB %+6.1f dB   pico %5.1f dBFS\n", preset.name.toRawUTF8(),
                             preset.category.toRawUTF8(), materialName (material), toDb (stats.inputRms), toDb (stats.outputRms),
                             stats.gainDb(), toDb (stats.peak));
        }
        std::printf ("    cambio de volumen de los presets de fabrica: de %+.1f a %+.1f dB\n", lowest, highest);
    });

    runTest ("Tiempo real: bloques de 1 a 4096 muestras, mono y estereo, 4 sample rates: sin NaN ni reservas de memoria", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        const auto presets = manager.getPresets();
        int index = 0;
        for (const double sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
            for (const int blockSize : { 1, 7, 64, 256, 4096 })
                for (const int channels : { 1, 2 })
                {
                    // Un preset distinto en cada combinación (recorre la biblioteca), con la mezcla de 2 s.
                    manager.load (presets[static_cast<size_t> (index++ % static_cast<int> (presets.size()))]);
                    const auto stats = render (*processor, makeSignal (Material::mix, sampleRate, 2.0), sampleRate, blockSize, 1.0, {}, channels);
                    const auto tag = juce::String (sampleRate / 1000.0, 1) + " kHz, bloques de " + juce::String (blockSize) + ", "
                                     + juce::String (channels) + " canal(es)";
                    check (stats.finite, tag + ": NaN");
                    check (stats.allocations == 0, tag + ": reservas de memoria: " + juce::String (stats.allocations));
                }
    });

    runTest ("Cambiar de preset con audio sonando (cada 150 ms, toda la biblioteca): sin NaN, sin saltos ni reservas", [] {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 256;
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        const auto presets = manager.getPresets();
        const int blocksPerPreset = static_cast<int> (0.15 * sampleRate / blockSize);
        const double seconds = 0.15 * static_cast<double> (presets.size() * 2) + 0.5;

        // Un seno de 110 Hz a −12 dBFS: sin transitorios propios, cualquier salto en la salida es un clic del cambio.
        juce::AudioBuffer<float> sine (2, static_cast<int> (seconds * sampleRate));
        for (int i = 0; i < sine.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                sine.setSample (ch, i, 0.25f * static_cast<float> (std::sin (twoPi * 110.0 * i / sampleRate)));

        // Dos vueltas a la biblioteca en orden (cada preset después de otro distinto).
        const auto stats = render (*processor, sine, sampleRate, blockSize, 0.5, [&] (int block) {
            if (block % blocksPerPreset == 0)
                manager.load (presets[static_cast<size_t> ((block / blocksPerPreset) % static_cast<int> (presets.size()))]);
        });
        check (stats.finite, "NaN");
        check (stats.allocations == 0, "reservas de memoria al cambiar de preset: " + juce::String (stats.allocations));
        check (stats.peak < 1.0f, "pico de " + juce::String (toDb (stats.peak), 1) + " dBFS");

        // Salto: la mayor diferencia entre muestras consecutivas, comparada con la de un seno de 110 Hz a 0 dBFS (0.0144).
        // Los modos duros (Hard Clip, Fold) tienen pendientes más grandes por sí solos; un clic de un cambio sin fundido
        // es de otro orden (saltos de 0.1 a 1).
        float largestStep = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 1; i < stats.output.getNumSamples(); ++i)
                largestStep = std::max (largestStep, std::abs (stats.output.getSample (ch, i) - stats.output.getSample (ch, i - 1)));
        std::printf ("    pico %.1f dBFS, mayor salto entre muestras %.3f\n", toDb (stats.peak), largestStep);
        check (largestStep < 0.08f, "salto entre muestras de " + juce::String (largestStep, 3));
    });

    runTest ("Tortura (Fase 10): todos los parametros al azar cada pocos bloques, bloques de tamano al azar, 4 sample rates", [] {
        // Lo que hace pluginval con --randomise, pero más tiempo y con señal de música: cualquier combinación de modo,
        // factor, bandas, tono, mezcla y modulación, cambiando a mitad de sonido. Nada debe dar NaN, explotar (un filtro
        // inestable crece sin límite) ni reservar memoria.
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& parameters = processor->getParameters();
        juce::Random random (0x52697074); // repetible
        constexpr int maxBlock = 4096;
        for (const double sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            processor->setRateAndBufferSizeDetails (sampleRate, maxBlock);
            processor->prepareToPlay (sampleRate, maxBlock);
            const auto input = makeSignal (Material::mix, sampleRate, 6.0);
            juce::AudioBuffer<float> buffer (2, maxBlock);
            juce::MidiBuffer midi;
            float peak = 0.0f;
            bool finite = true;
            int allocations = 0, changes = 0;
            for (int start = 0, block = 0; start < input.getNumSamples(); ++block)
            {
                // Sobre todo bloques chicos e irregulares; de vez en cuando uno grande.
                const int wanted = random.nextInt (10) == 0 ? 1 + random.nextInt (maxBlock) : 1 + random.nextInt (300);
                const int count = std::min (wanted, input.getNumSamples() - start);
                if (block % 6 == 0)
                {
                    // Un tercio de los parámetros cambia a un valor cualquiera (el bypass también).
                    for (auto* parameter : processor->juce::AudioProcessor::getParameters())
                        if (random.nextInt (3) == 0)
                        {
                            parameter->setValueNotifyingHost (random.nextFloat());
                            ++changes;
                        }
                    // Que no se quede casi todo el tiempo en bypass: ahí no se prueba nada.
                    if (auto* bypass = parameters.getParameter (id::bypass); bypass->getValue() >= 0.5f && random.nextInt (2) == 0)
                        bypass->setValueNotifyingHost (0.0f);
                }
                buffer.setSize (2, count, false, false, true);
                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom (ch, 0, input, ch, start, count);
                allocation::count = 0;
                {
                    const allocation::Scope countAllocations;
                    processor->processBlock (buffer, midi);
                }
                allocations += allocation::count.load();
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < count; ++i)
                    {
                        const float y = buffer.getSample (ch, i);
                        finite = finite && std::isfinite (y);
                        if (std::isfinite (y))
                            peak = std::max (peak, std::abs (y));
                    }
                start += count;
            }
            processor->releaseResources();
            const auto tag = juce::String (sampleRate / 1000.0, 1) + " kHz";
            std::printf ("    %s: %d cambios de parametros, pico %+.1f dBFS\n", tag.toRawUTF8(), changes, toDb (peak));
            check (finite, tag + ": NaN o infinito");
            check (allocations == 0, tag + ": reservas de memoria: " + juce::String (allocations));
            // Output llega a +24 dB y el drive a +48 dB: picos altos son posibles, pero +40 dBFS solo sale de algo que
            // explota.
            check (peak < 100.0f, tag + ": pico de " + juce::String (toDb (peak), 1) + " dBFS");
        }
    });

    std::printf ("\n%s (%d fallos)\n", failures == 0 ? "TODOS LOS TESTS PASAN" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}
