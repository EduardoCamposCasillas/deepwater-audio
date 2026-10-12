// Tests de la Fase 8 (presets). A diferencia de EngineTests.cpp, estos usan JUCE: compilan el procesador completo para
// probar exactamente lo que hace el plugin (parámetros, la curva, el estado del proyecto, XML).
//
//   EbbPresetTests             todos los tests
//   EbbPresetTests --verbose   además, la tabla de cada preset de fábrica (cuánto baja en el kick y lejos de él)
//   EbbPresetTests --notes     imprime las notas de los presets de fábrica como las muestra el navegador
//
// Un ducker no se puede probar sin un kick: aquí se genera un dembow desplazado (kick en los pasos 1, 7, 11 y 15) a
// 120 BPM, el tempo del reloj libre (sin host). El mismo kick llega de las tres formas: por MIDI (notas), por el
// sidechain (audio) y, en modo Tempo, el paso 1 cae en el "1" de la cuadrícula. La entrada principal es un bajo con
// mucho sub más un acorde, lo que Ebb tiene que apartar.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
#include <set>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/CurveShapes.h"
#include "presets/EbbPresets.h"

// --- Reservas de memoria en el hilo de audio -------------------------------------------------------------------------
//
// Regla del proyecto: processBlock no puede reservar ni liberar memoria. Este programa REEMPLAZA operator new / delete
// (C++ lo permite una vez por ejecutable) por versiones que, mientras 'counting' está activo, cuentan cada llamada.
// (La misma técnica que UndertowPresetTests y RiptidePresetTests.)
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
using deepwater::dsp::BreakpointCurve;
using deepwater::presets::Preset;
using deepwater::presets::PresetManager;
namespace id = ebb::params;

int failures = 0;
bool verbose = false;

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

juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

float valueOf (EbbAudioProcessor& processor, const char* parameterId)
{
    return processor.getParameters().getRawParameterValue (parameterId)->load();
}

void setValue (EbbAudioProcessor& processor, const char* parameterId, float value)
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

const BreakpointCurve& shape (const char* name)
{
    for (const auto& s : ebb::dsp::curveShapes)
        if (utf8 (s.name) == utf8 (name))
            return s.curve;
    jassertfalse;
    return ebb::dsp::defaultCurve();
}

// Cada test usa su propia carpeta de usuario temporal: nunca se toca la carpeta real de Documentos.
struct TemporaryFolder
{
    TemporaryFolder()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("EbbPresetTests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())))
    {
        folder.createDirectory();
    }
    ~TemporaryFolder() { folder.deleteRecursively(); }
    juce::File folder;
};

std::unique_ptr<EbbAudioProcessor> makeProcessor (const juce::File& userFolder)
{
    auto processor = std::make_unique<EbbAudioProcessor>();
    processor->getPresetManager().setUserFolder (userFolder);
    return processor;
}

float toDb (double gain)
{
    return static_cast<float> (20.0 * std::log10 (std::max (gain, 1.0e-9)));
}

// --- Señal de prueba: un dembow desplazado a 120 BPM -----------------------------------------------------------------

constexpr double twoPi = 6.283185307179586;
constexpr double bpm = 120.0; // el reloj libre de Ebb sin host
constexpr double stepSeconds = 60.0 / bpm / 4.0; // una semicorchea
constexpr double barSeconds = 16.0 * stepSeconds;
constexpr std::array<int, 4> kickSteps { 0, 6, 10, 14 }; // pasos 1, 7, 11 y 15 del Channel Rack
constexpr int kickNote = 36; // C3 en FL

struct Dembow
{
    juce::AudioBuffer<float> main;      // bajo + acorde (lo que Ebb aparta)
    juce::AudioBuffer<float> sidechain; // el kick solo
    std::vector<int> kicks;             // muestra de cada kick
    std::vector<int> barStarts;         // muestra de cada inicio de compás (también un kick)
};

Dembow makeDembow (double sampleRate, double seconds)
{
    const int total = static_cast<int> (seconds * sampleRate);
    Dembow d;
    d.main.setSize (2, total);
    d.sidechain.setSize (2, total);
    d.main.clear();
    d.sidechain.clear();

    for (int bar = 0; bar * barSeconds < seconds; ++bar)
    {
        d.barStarts.push_back (static_cast<int> (std::lround (bar * barSeconds * sampleRate)));
        for (const int step : kickSteps)
            if (const double t = bar * barSeconds + step * stepSeconds; t < seconds)
                d.kicks.push_back (static_cast<int> (std::lround (t * sampleRate)));
    }

    // Kick: seno que cae de 150 a 50 Hz, seco como el de un dembow (caída de 70 ms, 250 ms de largo). Con una cola larga
    // (un 808 de 180 ms) el kick del paso 15 no deja rearmar el detector antes del "1" (250 ms después) con el umbral de
    // los presets: es la limitación documentada de la Fase 5 (se arregla subiendo el UMBRAL).
    for (const int start : d.kicks)
        for (int i = start; i < std::min (total, start + static_cast<int> (0.25 * sampleRate)); ++i)
        {
            const double t = (i - start) / sampleRate;
            const double sweep = 50.0 * t + (100.0 / 30.0) * (1.0 - std::exp (-30.0 * t));
            const auto sample = static_cast<float> (0.9 * std::exp (-t / 0.07) * std::sin (twoPi * sweep));
            for (int ch = 0; ch < 2; ++ch)
                d.sidechain.addSample (ch, i, sample);
        }

    // Bajo sostenido (F2 = 43.7 Hz de FL, sub + un poco de segundo y tercer armónico) y un acorde suave de A menor.
    for (int i = 0; i < total; ++i)
    {
        const double t = i / sampleRate;
        const double envelope = std::min (1.0, t / 0.01);
        const double bass = 0.35 * std::sin (twoPi * 43.65 * t) + 0.08 * std::sin (twoPi * 87.3 * t) + 0.04 * std::sin (twoPi * 131.0 * t);
        double chord = 0.0;
        for (const double hz : { 220.0, 261.63, 329.63 })
            chord += 0.04 * std::sin (twoPi * hz * t);
        for (int ch = 0; ch < 2; ++ch)
            d.main.setSample (ch, i, static_cast<float> (envelope * (bass + chord)));
    }
    return d;
}

// --- Render ----------------------------------------------------------------------------------------------------------

struct RenderStats
{
    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    bool finite = true;
    int allocations = 0;
    int latency = 0;
    juce::AudioBuffer<float> output;
};

struct RenderOptions
{
    int blockSize = 256;
    int channels = 2;
    bool sidechain = true; // bus sidechain conectado (estéreo) o desactivado
    std::function<void (int block)> beforeBlock;
};

// Pasa el dembow por el procesador en bloques, con las notas del kick en el MIDI y el kick en el sidechain.
RenderStats render (EbbAudioProcessor& processor, const Dembow& input, double sampleRate, const RenderOptions& options = {})
{
    const int channels = options.channels;
    auto layout = processor.getBusesLayout();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.getReference (0) = set;
    layout.outputBuses.getReference (0) = set;
    layout.inputBuses.getReference (1) = options.sidechain ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    check (processor.setBusesLayout (layout), "layout aceptado");
    processor.setRateAndBufferSizeDetails (sampleRate, options.blockSize);
    processor.prepareToPlay (sampleRate, options.blockSize);

    const int total = input.main.getNumSamples();
    const int sidechainChannels = options.sidechain ? 2 : 0;
    RenderStats stats;
    stats.output.setSize (channels, total);
    juce::AudioBuffer<float> buffer (channels + sidechainChannels, options.blockSize);
    juce::MidiBuffer midi;
    midi.ensureSize (1024); // el MidiBuffer reserva al añadir: fuera del bloque medido
    size_t nextKick = 0;

    for (int start = 0, block = 0; start < total; start += options.blockSize, ++block)
    {
        const int count = std::min (options.blockSize, total - start);
        buffer.setSize (channels + sidechainChannels, count, false, false, true);
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom (ch, 0, input.main, ch, start, count);
        for (int ch = 0; ch < sidechainChannels; ++ch)
            buffer.copyFrom (channels + ch, 0, input.sidechain, ch, start, count);
        midi.clear();
        while (nextKick < input.kicks.size() && input.kicks[nextKick] < start + count)
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, kickNote, static_cast<juce::uint8> (110)), input.kicks[nextKick] - start);
            ++nextKick;
        }
        if (options.beforeBlock)
            options.beforeBlock (block);

        allocation::count = 0;
        {
            const allocation::Scope countAllocations;
            processor.processBlock (buffer, midi);
        }
        stats.allocations += allocation::count.load();
        for (int ch = 0; ch < channels; ++ch)
            stats.output.copyFrom (ch, start, buffer, ch, 0, count);
    }

    stats.latency = processor.getLatencySamples();
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < total; ++i)
        {
            const float y = stats.output.getSample (ch, i);
            stats.inputPeak = std::max (stats.inputPeak, std::abs (input.main.getSample (ch, i)));
            if (! std::isfinite (y))
                stats.finite = false;
            else
                stats.outputPeak = std::max (stats.outputPeak, std::abs (y));
        }
    processor.releaseResources();
    return stats;
}

// Volumen de la salida respecto de la entrada (RMS) en una ventana [from, to) en segundos después de cada 'event',
// promediado sobre los eventos (la salida llega 'latencia' muestras tarde). 1 = sin ducking.
double gainAround (const RenderStats& stats, const Dembow& input, const std::vector<int>& events, double sampleRate, double from, double to)
{
    double inSum = 0.0, outSum = 0.0;
    const int total = input.main.getNumSamples();
    for (size_t e = 1; e < events.size(); ++e) // el primero se salta: el suavizado y el reloj aún arrancan
        for (int i = events[e] + static_cast<int> (from * sampleRate); i < events[e] + static_cast<int> (to * sampleRate); ++i)
            if (i + stats.latency < total)
                for (int ch = 0; ch < stats.output.getNumChannels(); ++ch)
                {
                    const double x = input.main.getSample (ch, i);
                    const double y = stats.output.getSample (ch, i + stats.latency);
                    inSum += x * x;
                    outSum += y * y;
                }
    return inSum > 0.0 ? std::sqrt (outSum / inSum) : 1.0;
}

} // namespace

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI init;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String argument (argv[i]);
        verbose = verbose || argument == "--verbose";
        if (argument == "--notes")
        {
            EbbAudioProcessor processor;
            for (const auto& preset : processor.getPresetManager().getPresets())
                std::printf ("===== %s (%s) =====\n%s\n\n", preset.name.toRawUTF8(), preset.category.toRawUTF8(), preset.notes.toRawUTF8());
            return 0;
        }
    }

    runTest ("Presets de fabrica: se leen, nombres unicos, categorias validas, notas, valores y curvas correctos", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        const auto& presets = manager.getPresets();
        const auto categories = ebb::presets::categoryNames();
        check (presets.size() >= 16, "al menos 16 presets de fabrica (hay " + juce::String (static_cast<int> (presets.size())) + ")");

        std::set<juce::String> names;
        std::vector<int> perCategory (static_cast<size_t> (categories.size()), 0);
        for (const auto& preset : presets)
        {
            check (preset.factory, preset.name + ": deberia ser de fabrica");
            check (names.insert (preset.name.toLowerCase()).second, "nombre repetido: " + preset.name);
            check (preset.categoryIndex() < categories.size() - 1, preset.name + ": categoria desconocida " + preset.category);
            check (preset.notes.length() > 300, preset.name + ": las notas tienen que explicar el preset");
            check (preset.notes.contains ("Viene de:") || preset.categoryIndex() == 0, preset.name + ": de que fase viene");
            check (preset.notes.contains ("NO") || preset.categoryIndex() == 0, preset.name + ": cuando NO usarlo");
            check (preset.author == "Ebb", preset.name + ": autor");
            ++perCategory[static_cast<size_t> (preset.categoryIndex())];

            juce::StringArray problems;
            (void) manager.resolve (preset, &problems);
            (void) ebb::presets::curveFromTree (preset.extraState, &problems);
            check (problems.isEmpty(), preset.name + ": " + problems.joinIntoString ("; "));
            for (const auto& [parameterId, value] : preset.values)
                check (parameterId != id::bypass, preset.name + ": un preset no debe tocar el bypass");

            // Cargarlo de verdad tampoco da avisos.
            const auto loadProblems = manager.load (preset);
            check (loadProblems.isEmpty(), preset.name + " al cargar: " + loadProblems.joinIntoString ("; "));
            check (! manager.isModified(), preset.name + ": recien cargado no esta modificado");
        }
        check (perCategory[0] == 1, "un solo Init");
        for (int c = 0; c < categories.size() - 1; ++c)
            check (perCategory[static_cast<size_t> (c)] > 0, "categoria vacia: " + categories[c]);
        std::printf ("    %d presets de fabrica en %d categorias\n", static_cast<int> (presets.size()), categories.size() - 1);
    });

    runTest ("Curva: la forma por nombre, los puntos escritos a mano, y un preset sin <Curve> vuelve a Clasica", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();

        const auto* tail = findPreset (manager, "Dembow 808 Cola Larga");
        check (tail != nullptr, "existe Dembow 808 Cola Larga");
        if (tail != nullptr)
        {
            manager.load (*tail);
            check (processor->getCurve() == shape ("Retenci\xc3\xb3n"), "shape=\"Retencion\" carga la forma de fabrica (el menu la reconoce)");
        }

        const auto* bounce = findPreset (manager, "Rebote");
        check (bounce != nullptr, "existe Rebote");
        if (bounce != nullptr)
        {
            manager.load (*bounce);
            const auto& curve = processor->getCurve();
            check (curve.size() == 5 && ebb::dsp::findCurveShape (curve) < 0, "curva propia de 5 puntos (Personalizada)");
            check (std::abs (curve.valueAt (0.38f) - 0.4f) < 1.0e-4f && std::abs (curve.valueAt (0.3f) - 0.85f) < 1.0e-4f,
                   "los puntos del archivo");
        }

        const auto* clean = findPreset (manager, "Dembow Bajo Limpio"); // no trae <Curve>
        if (clean != nullptr)
        {
            manager.load (*clean);
            check (processor->getCurve() == ebb::dsp::defaultCurve(), "sin <Curve> = la forma por defecto (Clasica)");
        }

        // Archivos raros: forma desconocida y curva con un solo punto -> Clásica, con aviso.
        Preset weird;
        weird.name = "Raro";
        weird.extraState = juce::ValueTree ("Curve", { { "shape", "Laser" } });
        manager.load (*bounce);
        auto problems = manager.load (weird);
        check (problems.size() == 1 && processor->getCurve() == ebb::dsp::defaultCurve(), "forma desconocida: aviso y Clasica");
        weird.extraState = juce::ValueTree ("Curve");
        weird.extraState.appendChild (juce::ValueTree ("Point", { { "x", 0.5f }, { "y", 0.2f } }), nullptr);
        problems = manager.load (weird);
        check (problems.size() == 1 && processor->getCurve() == ebb::dsp::defaultCurve(), "un punto: aviso y Clasica");
    });

    runTest ("Cargar: unidades reales y nombres de opciones, lo que falta vuelve al defecto; el bypass no se toca", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();

        Preset preset;
        preset.name = "Prueba";
        preset.values = { { id::mode, "Audio" }, { id::division, "1/8 T" }, { id::band, "Graves" }, { id::crossover, "95" },
                          { id::lookahead, "3.5" }, { id::velocity, "On" }, { id::threshold, "-30" } };
        setValue (*processor, id::mix, 40.0f);   // no está en el preset: tiene que volver a 100
        setValue (*processor, id::bypass, 1.0f); // excluido: tiene que quedarse como está
        const auto problems = manager.load (preset);
        check (problems.isEmpty(), "sin avisos: " + problems.joinIntoString ("; "));
        check (std::lround (valueOf (*processor, id::mode)) == 2, "modo por nombre");
        check (std::lround (valueOf (*processor, id::division)) == 7, "division por nombre (1/8 T)");
        check (std::lround (valueOf (*processor, id::band)) == 1, "banda por nombre");
        check (std::abs (valueOf (*processor, id::crossover) - 95.0f) < 0.1f, "cruce en Hz");
        check (std::abs (valueOf (*processor, id::lookahead) - 3.5f) < 1.0e-3f, "lookahead en ms (no automatizable, pero el preset lo pone)");
        check (std::abs (valueOf (*processor, id::threshold) + 30.0f) < 1.0e-3f, "umbral en dB");
        check (valueOf (*processor, id::velocity) > 0.5f, "On");
        check (std::abs (valueOf (*processor, id::mix) - 100.0f) < 1.0e-3f, "lo que falta vuelve al defecto");
        check (valueOf (*processor, id::bypass) > 0.5f, "el bypass no cambia al cargar");
        check (manager.getCurrentName() == "Prueba" && ! manager.isModified(), "preset actual sin modificar");

        processor->setCurve (shape ("Puerta"));
        manager.loadInit();
        check (manager.getCurrentName() == "Init" && std::lround (valueOf (*processor, id::mode)) == 0, "Init");
        check (processor->getCurve() == ebb::dsp::defaultCurve(), "Init tambien devuelve la curva a Clasica");
        check (valueOf (*processor, id::bypass) > 0.5f, "Init tampoco toca el bypass");
    });

    runTest ("Modificado: un parametro o la curva marcan el preset; volver los desmarca; el bypass no cuenta", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        check (manager.getCurrentName() == "Init" && ! manager.isModified(), "una instancia nueva es Init sin modificar");
        processor->setCurve (shape ("Lineal"));
        check (manager.isModified(), "instancia nueva + curva cambiada = modificado");
        processor->setCurve (ebb::dsp::defaultCurve());
        check (! manager.isModified(), "curva de vuelta a Clasica");

        manager.load (*findPreset (manager, utf8 ("Eco R\xc3\xadtmico")));
        check (! manager.isModified(), "recien cargado");
        setValue (*processor, id::bypass, 1.0f);
        check (! manager.isModified(), "el bypass no modifica el preset");
        setValue (*processor, id::depth, 12.0f);
        check (manager.isModified(), "parametro: modificado");
        setValue (*processor, id::depth, 80.0f);
        check (! manager.isModified(), "parametro: vuelve a su valor");

        auto edited = processor->getCurve();
        edited.move (2, edited[2].x, 0.1f); // como arrastrar un punto en el editor
        processor->setCurve (edited);
        check (manager.isModified(), "curva editada: modificado");
        processor->setCurve (shape ("Eco"));
        check (! manager.isModified(), "curva de vuelta a la del preset");
    });

    runTest ("Guardar preset de usuario: archivo legible con la curva y sin el bypass, ida y vuelta exacta, reemplazar, borrar", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        manager.load (*findPreset (manager, "Rebote"));
        setValue (*processor, id::crossover, 87.0f);
        setValue (*processor, id::mode, 1.0f);
        auto curve = processor->getCurve();
        curve.setTension (1, 0.37f);
        curve.insert (0.9f, 0.73f);
        processor->setCurve (curve);
        const auto expectedCurve = processor->getCurve();
        std::vector<float> before;
        for (const auto* parameter : manager.getParameters())
            before.push_back (parameter->getValue());

        const auto name = utf8 ("Mi Rebote Ra\xc3\xadz");
        check (manager.save (name, "Creativo", "Yo", "Notas\ncon dos lineas").wasOk(), "guardar");
        const auto file = temp.folder.getChildFile (name + ".ebpreset");
        check (file.existsAsFile(), "archivo .ebpreset en la carpeta del usuario");
        const auto text = file.loadFileAsString();
        check (text.contains ("<EbbPreset") && text.contains ("crossover") && text.contains ("<Curve") && text.contains ("<Point"),
               "XML legible con la curva");
        check (! text.contains ("\"bypass\""), "el bypass no se guarda");
        check (! manager.isModified() && manager.getCurrentName() == name, "guardado = preset actual sin modificar");

        manager.loadInit();
        const auto* saved = findPreset (manager, name);
        check (saved != nullptr && ! saved->factory && saved->category == "Creativo" && saved->notes == "Notas\ncon dos lineas",
               "se relee con categoria y notas");
        if (saved != nullptr)
        {
            manager.load (*saved);
            for (size_t i = 0; i < before.size(); ++i)
                check (std::abs (manager.getParameters()[i]->getValue() - before[i]) < 1.0e-4f,
                       "ida y vuelta: " + manager.getParameters()[i]->paramID);
            check (processor->getCurve() == expectedCurve, "ida y vuelta de la curva, exacta (6 puntos y sus tensiones)");
            check (! manager.isModified(), "recien cargado");
        }
        check (manager.save ("Init", "Creativo", {}, {}).failed(), "no se puede usar el nombre de uno de fabrica");
        check (manager.save (name, "Creativo", {}, "otra").wasOk(), "reemplazar");
        if (const auto* replaced = findPreset (manager, name))
        {
            check (replaced->notes == "otra", "reemplazado");
            check (manager.remove (*replaced).wasOk(), "borrar (a la papelera)");
        }
        check (findPreset (manager, name) == nullptr, "borrado");
    });

    runTest ("Proyecto de FL: el nombre, el 'modificado' y la curva viajan con el proyecto", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        manager.load (*findPreset (manager, "Trance Gate 1/16"));

        juce::MemoryBlock state;
        processor->getStateInformation (state);
        auto reopened = makeProcessor (temp.folder);
        reopened->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        check (reopened->getPresetManager().getCurrentName() == "Trance Gate 1/16", "nombre restaurado");
        check (! reopened->getPresetManager().isModified(), "sin modificar");
        check (reopened->getCurve() == shape ("Puerta"), "curva restaurada");

        auto curve = processor->getCurve();
        curve.move (1, 0.4f, 0.0f);
        processor->setCurve (curve);
        processor->getStateInformation (state);
        reopened = makeProcessor (temp.folder);
        reopened->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        check (reopened->getPresetManager().isModified(), "curva modificada: el '*' se restaura");
        check (reopened->getCurve() == processor->getCurve(), "y la curva tambien");

        // Un proyecto de la Fase 7 (sin las propiedades del preset): "Sin nombre", sin asterisco, con su curva.
        auto tree = processor->getParameters().copyState();
        tree.removeProperty ("presetName", nullptr);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);
        reopened = makeProcessor (temp.folder);
        reopened->setStateInformation (old.getData(), static_cast<int> (old.getSize()));
        check (reopened->getPresetManager().getCurrentName() == "Sin nombre", "proyecto antiguo: Sin nombre");
        check (! reopened->getPresetManager().isModified(), "proyecto antiguo: sin asterisco");
        check (reopened->getCurve() == processor->getCurve(), "proyecto antiguo: su curva");
    });

    runTest ("Cada preset de fabrica sobre el dembow: sin NaN ni reservas, no sube el volumen, baja en el kick y suelta lejos de el", [] {
        constexpr double sampleRate = 48000.0;
        TemporaryFolder temp;
        const auto dembow = makeDembow (sampleRate, 8.0);
        std::vector<Preset> presets;
        {
            auto processor = makeProcessor (temp.folder);
            presets = processor->getPresetManager().getPresets();
        }
        if (verbose)
            std::printf ("    %-30s %-9s %-7s %12s %12s %10s\n", "preset", "categoria", "modo", "en el kick", "lejos", "pico");
        for (const auto& preset : presets)
        {
            auto processor = makeProcessor (temp.folder);
            processor->getPresetManager().load (preset);
            const auto stats = render (*processor, dembow, sampleRate);
            const auto mode = ebb::dsp::triggerModeFromIndex (static_cast<int> (valueOf (*processor, id::mode)));
            check (stats.finite, preset.name + ": NaN o infinito");
            check (stats.allocations == 0, preset.name + ": processBlock reservo memoria " + juce::String (stats.allocations) + " veces");
            // Un ducker solo baja. Con bandas, el cruce LR4 es un allpass: los picos se mueven un poco (±1 dB).
            check (stats.outputPeak <= stats.inputPeak * 1.12f, preset.name + ": el pico sube a " + juce::String (toDb (stats.outputPeak), 1) + " dBFS");

            // En el "1" de cada compás hay kick (y golpe de la cuadrícula en modo Tempo): 5–30 ms después tiene que haber
            // ducking. Lejos del kick (600–700 ms después; el siguiente es a los 750 ms) los modos MIDI y Audio sueltan.
            const double atKick = gainAround (stats, dembow, dembow.barStarts, sampleRate, 0.005, 0.030);
            const double away = gainAround (stats, dembow, dembow.barStarts, sampleRate, 0.600, 0.700);
            check (atKick < 0.95, preset.name + ": no baja en el kick (" + juce::String (toDb (atKick), 2) + " dB)");
            if (ebb::dsp::isHitMode (mode))
                check (away > 0.97, preset.name + ": sigue abajo lejos del kick (" + juce::String (toDb (away), 2) + " dB)");
            if (verbose)
                std::printf ("    %-30s %-9s %-7s %9.1f dB %9.1f dB %7.1f dBFS\n", preset.name.toRawUTF8(), preset.category.toRawUTF8(),
                             ebb::dsp::triggerModeNames[static_cast<size_t> (valueOf (*processor, id::mode))], toDb (atKick),
                             toDb (away), toDb (stats.outputPeak));
        }
    });

    runTest ("Tiempo real: bloques de 1 a 4096, mono y estereo, con y sin sidechain, 4 sample rates: sin NaN ni reservas", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        const auto presets = manager.getPresets();
        int index = 0;
        for (const double sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            const auto dembow = makeDembow (sampleRate, 2.0);
            for (const int blockSize : { 1, 7, 64, 256, 4096 })
                for (const int channels : { 1, 2 })
                    for (const bool sidechain : { true, false })
                    {
                        // Un preset distinto en cada combinación (recorre la biblioteca).
                        manager.load (presets[static_cast<size_t> (index++ % static_cast<int> (presets.size()))]);
                        RenderOptions options;
                        options.blockSize = blockSize;
                        options.channels = channels;
                        options.sidechain = sidechain;
                        const auto stats = render (*processor, dembow, sampleRate, options);
                        const auto tag = juce::String (sampleRate / 1000.0, 1) + " kHz, bloques de " + juce::String (blockSize) + ", "
                                         + juce::String (channels) + " canal(es)" + (sidechain ? ", con sidechain" : ", sin sidechain");
                        check (stats.finite, tag + ": NaN");
                        check (stats.allocations == 0, tag + ": reservas de memoria: " + juce::String (stats.allocations));
                    }
        }
    });

    runTest ("Cambiar de preset con audio sonando (cada 150 ms, toda la biblioteca): sin NaN, sin saltos ni reservas", [] {
        constexpr double sampleRate = 48000.0;
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        const auto presets = manager.getPresets();
        RenderOptions options;
        const int blocksPerPreset = static_cast<int> (0.15 * sampleRate / options.blockSize);
        const double seconds = 0.15 * static_cast<double> (presets.size() * 2) + 0.5;

        // Un seno de 110 Hz a −12 dBFS en la entrada (sin transitorios propios: cualquier salto es un clic) y el dembow en
        // el sidechain y el MIDI, para que los presets de MIDI y Audio también bajen y suban.
        auto dembow = makeDembow (sampleRate, seconds);
        for (int i = 0; i < dembow.main.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                dembow.main.setSample (ch, i, 0.25f * static_cast<float> (std::sin (twoPi * 110.0 * i / sampleRate)));

        // Dos vueltas a la biblioteca en orden (cada preset después de otro distinto).
        options.beforeBlock = [&] (int block) {
            if (block % blocksPerPreset == 0)
                manager.load (presets[static_cast<size_t> ((block / blocksPerPreset) % static_cast<int> (presets.size()))]);
        };
        const auto stats = render (*processor, dembow, sampleRate, options);
        check (stats.finite, "NaN");
        check (stats.allocations == 0, "reservas de memoria al cambiar de preset: " + juce::String (stats.allocations));

        // Salto: la mayor diferencia entre muestras consecutivas. Un seno de 110 Hz a −12 dBFS cambia como mucho 0.0036
        // por muestra; un ducking con 1.5 ms de suavizado suma otro tanto. Un clic (un cambio sin fundido) es de 0.05 a 0.5.
        float largestStep = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 1; i < stats.output.getNumSamples(); ++i)
                largestStep = std::max (largestStep, std::abs (stats.output.getSample (ch, i) - stats.output.getSample (ch, i - 1)));
        std::printf ("    pico %.1f dBFS, mayor salto entre muestras %.4f\n", toDb (stats.outputPeak), largestStep);
        check (largestStep < 0.02f, "salto entre muestras de " + juce::String (largestStep, 4));
    });

    std::printf ("\n%s (%d fallos)\n", failures == 0 ? "TODOS LOS TESTS PASAN" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}
