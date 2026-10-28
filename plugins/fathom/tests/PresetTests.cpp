// Tests de la Fase 15 (presets). Como InstanceTests.cpp, usan JUCE: compilan el procesador completo para probar
// exactamente lo que hace el plugin (parámetros, el estado del proyecto, el deshacer, el A/B, XML).
//
//   FathomPresetTests             todos los tests
//   FathomPresetTests --verbose   además, la tabla de cada preset de fábrica (volumen, latencia, pico)
//   FathomPresetTests --notes     imprime las notas de los presets de fábrica como las muestra el navegador
//
// La señal de prueba es una mezcla chica de reggaetón: un 808 en A2 (55 Hz) con armónicos, un acorde, ruido rosa (lo
// que tiene "de todo": presencia, sibilancia, aire) y un kick en el sidechain (para los presets con detector externo).

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
#include "dsp/AutoGain.h"
#include "presets/FathomPresets.h"

#include "AllocationCounter.h" // reemplaza operator new / delete en este programa

namespace
{
using deepwater::presets::Preset;
using deepwater::presets::PresetManager;
namespace id = fathom::params;

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

float valueOf (FathomAudioProcessor& processor, const std::string& parameterId)
{
    return processor.getParameters().getRawParameterValue (parameterId)->load();
}

void setValue (FathomAudioProcessor& processor, const std::string& parameterId, float value)
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
                      .getChildFile ("FathomPresetTests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())))
    {
        folder.createDirectory();
    }
    ~TemporaryFolder() { folder.deleteRecursively(); }
    juce::File folder;
};

std::unique_ptr<FathomAudioProcessor> makeProcessor (const juce::File& userFolder)
{
    auto processor = std::make_unique<FathomAudioProcessor>();
    processor->getPresetManager().setUserFolder (userFolder);
    return processor;
}

float toDb (double gain)
{
    return static_cast<float> (20.0 * std::log10 (std::max (gain, 1.0e-9)));
}

// Lo que el AUTO GAIN compensaría con las bandas de ahora (ruido rosa con peso K): la sonoridad que cambia el EQ.
float loudnessCompensationDb (FathomAudioProcessor& processor, double sampleRate)
{
    fathom::dsp::AutoGain autoGain;
    autoGain.prepare (sampleRate);
    const auto bands = processor.getAllBandSettings();
    for (int b = 0; b < id::numBands; ++b)
        autoGain.setBand (b, bands[static_cast<size_t> (b)]);
    return autoGain.compensationDb();
}

// --- Señal de prueba ---------------------------------------------------------------------------------------------------

constexpr double twoPi = 6.283185307179586;

struct Mix
{
    juce::AudioBuffer<float> main;      // 808 + acorde + ruido rosa (estéreo, un poco distinto en cada lado)
    juce::AudioBuffer<float> sidechain; // un kick cada 500 ms
};

Mix makeMix (double sampleRate, double seconds)
{
    const int total = static_cast<int> (seconds * sampleRate);
    Mix mix;
    mix.main.setSize (2, total);
    mix.sidechain.setSize (2, total);
    mix.sidechain.clear();

    // Ruido rosa con el filtro de Paul Kellet (aproximación de 3 dB/oct con 7 polos), uno por canal.
    juce::Random random (1234);
    for (int ch = 0; ch < 2; ++ch)
    {
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        for (int i = 0; i < total; ++i)
        {
            const double white = random.nextDouble() * 2.0 - 1.0;
            b0 = 0.99886 * b0 + white * 0.0555179;
            b1 = 0.99332 * b1 + white * 0.0750759;
            b2 = 0.96900 * b2 + white * 0.1538520;
            b3 = 0.86650 * b3 + white * 0.3104856;
            b4 = 0.55000 * b4 + white * 0.5329522;
            b5 = -0.7616 * b5 - white * 0.0168980;
            const double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
            b6 = white * 0.115926;

            const double t = i / sampleRate;
            const double bass = 0.3 * std::sin (twoPi * 55.0 * t) + 0.06 * std::sin (twoPi * 110.0 * t) + 0.03 * std::sin (twoPi * 165.0 * t);
            double chord = 0.0;
            for (const double hz : { 220.0, 261.63, 329.63 })
                chord += 0.03 * std::sin (twoPi * hz * t + ch);
            mix.main.setSample (ch, i, static_cast<float> (bass + chord + 0.02 * pink));
        }
    }

    for (int start = 0; start < total; start += static_cast<int> (0.5 * sampleRate))
        for (int i = start; i < std::min (total, start + static_cast<int> (0.2 * sampleRate)); ++i)
        {
            const double t = (i - start) / sampleRate;
            const double sweep = 50.0 * t + (100.0 / 30.0) * (1.0 - std::exp (-30.0 * t));
            const auto sample = static_cast<float> (0.9 * std::exp (-t / 0.07) * std::sin (twoPi * sweep));
            for (int ch = 0; ch < 2; ++ch)
                mix.sidechain.setSample (ch, i, sample);
        }
    return mix;
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
    bool sidechain = true;
    bool offline = true; // offline los FIR de fase se diseñan en el audio (render determinista, con su memoria)
    std::function<void (int block)> beforeBlock;
};

RenderStats render (FathomAudioProcessor& processor, const Mix& input, double sampleRate, const RenderOptions& options = {})
{
    const int channels = options.channels;
    auto layout = processor.getBusesLayout();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.getReference (0) = set;
    layout.outputBuses.getReference (0) = set;
    layout.inputBuses.getReference (1) = options.sidechain ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    check (processor.setBusesLayout (layout), "layout aceptado");
    processor.setNonRealtime (options.offline);
    processor.setRateAndBufferSizeDetails (sampleRate, options.blockSize);
    processor.prepareToPlay (sampleRate, options.blockSize);

    const int total = input.main.getNumSamples();
    const int sidechainChannels = options.sidechain ? 2 : 0;
    RenderStats stats;
    stats.output.setSize (channels, total);
    juce::AudioBuffer<float> buffer (channels + sidechainChannels, options.blockSize);
    juce::MidiBuffer midi;

    for (int start = 0, block = 0; start < total; start += options.blockSize, ++block)
    {
        const int count = std::min (options.blockSize, total - start);
        buffer.setSize (channels + sidechainChannels, count, false, false, true);
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom (ch, 0, input.main, ch, start, count);
        for (int ch = 0; ch < sidechainChannels; ++ch)
            buffer.copyFrom (channels + ch, 0, input.sidechain, ch, start, count);
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

// La mayor diferencia entre muestras consecutivas (un clic es un salto).
float largestStep (const juce::AudioBuffer<float>& buffer, int from = 1)
{
    float largest = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = std::max (1, from); i < buffer.getNumSamples(); ++i)
            largest = std::max (largest, std::abs (buffer.getSample (ch, i) - buffer.getSample (ch, i - 1)));
    return largest;
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
            FathomAudioProcessor processor;
            for (const auto& preset : processor.getPresetManager().getPresets())
                std::printf ("===== %s (%s) =====\n%s\n\n", preset.name.toRawUTF8(), preset.category.toRawUTF8(), preset.notes.toRawUTF8());
            return 0;
        }
    }

    runTest ("Presets de fabrica: se leen, nombres unicos, categorias validas, notas y valores correctos", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        const auto& presets = manager.getPresets();
        const auto categories = fathom::presets::categoryNames();
        check (presets.size() >= 20, "al menos 20 presets de fabrica (hay " + juce::String (static_cast<int> (presets.size())) + ")");

        std::set<juce::String> names;
        std::vector<int> perCategory (static_cast<size_t> (categories.size()), 0);
        for (const auto& preset : presets)
        {
            check (preset.factory, preset.name + ": deberia ser de fabrica");
            check (names.insert (preset.name.toLowerCase()).second, "nombre repetido: " + preset.name);
            check (preset.categoryIndex() < categories.size() - 1, preset.name + ": categoria desconocida " + preset.category);
            check (preset.author == "Fathom", preset.name + ": autor");
            ++perCategory[static_cast<size_t> (preset.categoryIndex())];
            if (preset.categoryIndex() != 0)
            {
                check (preset.notes.length() > 600, preset.name + ": las notas tienen que explicar el preset");
                check (preset.notes.contains (utf8 ("C\xc3\xb3mo se construy\xc3\xb3:")), preset.name + ": como se construyo");
                check (preset.notes.contains (utf8 ("Pru\xc3\xa9" "balo:")), preset.name + ": que probar");
                check (preset.notes.contains (utf8 ("Cu\xc3\xa1ndo NO usarlo:")), preset.name + ": cuando NO usarlo");
                check (preset.notes.contains ("Viene de: Fase"), preset.name + ": de que fase viene");
            }

            juce::StringArray problems;
            (void) manager.resolve (preset, &problems);
            check (problems.isEmpty(), preset.name + ": " + problems.joinIntoString ("; "));
            for (const auto& [parameterId, value] : preset.values)
                check (parameterId != id::bypass, preset.name + ": un preset no debe tocar el bypass");

            const auto loadProblems = manager.load (preset);
            check (loadProblems.isEmpty(), preset.name + " al cargar: " + loadProblems.joinIntoString ("; "));
            check (! manager.isModified(), preset.name + ": recien cargado no esta modificado");

            // Cada banda encendida es una de las que dice el preset, y las notas hablan de cada una ("Banda N").
            int enabled = 0;
            for (int b = 0; b < id::numBands; ++b)
                if (processor->getBandSettings (b).enabled)
                {
                    ++enabled;
                    check (preset.notes.contains ("Banda " + juce::String (b + 1)), preset.name + ": las notas no explican la banda " + juce::String (b + 1));
                }
            check (enabled > 0 || preset.categoryIndex() == 0, preset.name + ": sin bandas encendidas");
        }
        check (perCategory[0] == 1, "un solo Init");
        for (int c = 0; c < categories.size() - 1; ++c)
            check (perCategory[static_cast<size_t> (c)] > 0, "categoria vacia: " + categories[c]);
        std::printf ("    %d presets de fabrica en %d categorias\n", static_cast<int> (presets.size()), categories.size() - 1);
    });

    runTest ("Sonoridad: los correctivos cambian poco el volumen (ruido rosa con peso K, <= 3 dB) y la SALIDA queda en 0 dB", [] {
        // La SALIDA no se compensa en los presets: el modelo del AUTO GAIN (ruido rosa) engañaría en un 808 o un hi-hat
        // (un corte donde el instrumento no tiene nada baja el ruido rosa, no al instrumento). Para comparar, AUTO GAIN.
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        const int creative = fathom::presets::categoryNames().indexOf ("Creativo");
        if (verbose)
            std::printf ("    %-34s %14s\n", "preset", "sonoridad (dB)");
        for (const auto& preset : manager.getPresets())
        {
            manager.load (preset);
            // A 48 kHz (a 44.1 kHz da lo mismo ±0.1 dB: los puntos llegan a 18 kHz).
            const float change = -loudnessCompensationDb (*processor, 48000.0);
            if (verbose)
                std::printf ("    %-34s %+13.2f\n", preset.name.toRawUTF8(), change);
            check (std::abs (valueOf (*processor, id::outputGain)) < 1.0e-4f, preset.name + ": la salida no se toca");
            check (std::abs (change + loudnessCompensationDb (*processor, 44100.0)) < 0.1f, preset.name + ": 44.1 = 48 kHz");
            if (preset.categoryIndex() != creative)
                check (std::abs (change) <= 3.0f, preset.name + ": cambia la sonoridad " + juce::String (change, 2) + " dB");
        }
    });

    runTest ("Cargar: unidades reales y nombres de opciones, lo que falta vuelve al defecto; el bypass no se toca", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();

        Preset preset;
        preset.name = "Prueba";
        preset.values = { { id::band (4, id::enabled), "On" },    { id::band (4, id::shape), "Low Cut" },
                          { id::band (4, id::frequency), "120" }, { id::band (4, id::slope), "48 dB/oct" },
                          { id::band (4, id::stereo), "Side" },   { id::band (5, id::dynamicSource), "Sidechain" },
                          { id::band (5, id::attack), "2.5" },    { id::phaseMode, "Linear Phase" },
                          { id::analogColor, "Warm" },            { id::outputGain, "-1.5" } };
        setValue (*processor, id::band (7, id::gain), 6.0f); // no está en el preset: tiene que volver a 0
        setValue (*processor, id::bypass, 1.0f);             // excluido: tiene que quedarse como está
        const auto problems = manager.load (preset);
        check (problems.isEmpty(), "sin avisos: " + problems.joinIntoString ("; "));
        const auto band = processor->getBandSettings (4);
        check (band.enabled && band.shape == fathom::dsp::Shape::lowCut, "forma por nombre");
        check (std::abs (band.frequency - 120.0f) < 0.05f, "frecuencia en Hz");
        check (fathom::dsp::slopes[static_cast<size_t> (band.slopeIndex)] == 48, "pendiente por nombre (48 dB/oct)");
        check (band.placement == fathom::dsp::Placement::side, "canal por nombre");
        check (processor->getBandSettings (5).dynamics.external, "detector por nombre");
        check (std::abs (processor->getBandSettings (5).dynamics.attackMs - 2.5f) < 1.0e-3f, "ataque en ms");
        check (processor->getPhaseMode() == fathom::dsp::PhaseMode::linear, "modo de fase por nombre");
        check (processor->getColorMode() == fathom::dsp::ColorMode::warm, "color por nombre");
        check (std::abs (valueOf (*processor, id::outputGain) + 1.5f) < 1.0e-3f, "salida en dB");
        check (std::abs (processor->getBandSettings (7).gainDb) < 1.0e-4f, "lo que falta vuelve al defecto");
        check (std::abs (processor->getBandSettings (9).frequency - processor->getDefaultBandSettings (9).frequency) < 0.01f,
               "una banda que no aparece queda en su frecuencia de Init");
        check (valueOf (*processor, id::bypass) > 0.5f, "el bypass no cambia al cargar");
        check (manager.getCurrentName() == "Prueba" && ! manager.isModified(), "preset actual sin modificar");

        manager.loadInit();
        check (manager.getCurrentName() == "Init" && ! processor->getBandSettings (4).enabled, "Init");
        check (processor->getPhaseMode() == fathom::dsp::PhaseMode::zeroLatency, "Init vuelve a cero latencia");
        check (valueOf (*processor, id::bypass) > 0.5f, "Init tampoco toca el bypass");
    });

    runTest ("Modificado: cualquier parametro de una banda marca el preset; volver lo desmarca; el bypass no cuenta", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        check (manager.getCurrentName() == "Init" && ! manager.isModified(), "una instancia nueva es Init sin modificar");

        manager.load (*findPreset (manager, utf8 ("Voz Limpieza B\xc3\xa1sica")));
        check (! manager.isModified(), "recien cargado");
        setValue (*processor, id::bypass, 1.0f);
        check (! manager.isModified(), "el bypass no modifica el preset");
        const auto frequency = valueOf (*processor, id::band (1, id::frequency));
        setValue (*processor, id::band (1, id::frequency), 350.0f);
        check (manager.isModified(), "mover un nodo: modificado");
        setValue (*processor, id::band (1, id::frequency), frequency);
        check (! manager.isModified(), "de vuelta a su valor");
        setValue (*processor, id::band (20, id::release), 500.0f); // una banda apagada: también es parte del preset
        check (manager.isModified(), "un ajuste de una banda apagada: modificado");
    });

    runTest ("Deshacer: cargar un preset es UN paso (Ctrl+Z vuelve a lo de antes, Ctrl+Y al preset)", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        auto& history = processor->getEditHistory();
        check (! history.canUndo(), "instancia nueva: nada que deshacer");

        manager.load (*findPreset (manager, "Kick Dembow Con Pegada"));
        const auto kick = history.snapshot();
        check (history.canUndo(), "cargar deja un paso");
        manager.load (*findPreset (manager, "Master Sub Mono Fase Lineal"));
        const auto master = history.snapshot();
        check (processor->getPhaseMode() == fathom::dsp::PhaseMode::linear, "el master esta en fase lineal");

        // Con tolerancia: cada parámetro guarda su valor real y getValue() lo vuelve a pasar a 0..1 (redondea).
        const auto same = [] (const std::vector<float>& a, const std::vector<float>& b) {
            for (size_t i = 0; i < a.size(); ++i)
                if (std::abs (a[i] - b[i]) > 1.0e-5f)
                    return false;
            return a.size() == b.size();
        };
        check (history.undo() && same (history.snapshot(), kick), "deshacer vuelve al kick entero (todas las bandas)");
        check (processor->getPhaseMode() == fathom::dsp::PhaseMode::zeroLatency, "y a cero latencia");
        check (history.redo() && same (history.snapshot(), master), "rehacer vuelve al master");
        check (history.undo() && history.undo() && ! history.canUndo(), "dos pasos, uno por preset");
        check (! processor->getBandSettings (0).enabled, "de vuelta a Init");
        check (history.isIdle(), "sin transacciones abiertas");
    });

    runTest ("A/B: un preset se carga en el lado activo; el otro lado queda como estaba", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        manager.load (*findPreset (manager, "Hi-Hats Sin Aspereza"));
        check (processor->switchCompareSide(), "pasar a B (nace como copia de A)");
        manager.load (*findPreset (manager, utf8 ("Tel\xc3\xa9" "fono")));
        check (std::abs (processor->getBandSettings (0).frequency - 400.0f) < 0.1f, "B = Telefono");
        check (processor->switchCompareSide(), "volver a A");
        check (std::abs (processor->getBandSettings (0).frequency - 350.0f) < 0.1f, "A sigue siendo Hi-Hats");
    });

    runTest ("Guardar preset de usuario: archivo legible sin el bypass, ida y vuelta exacta de los 370 parametros, borrar", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        manager.load (*findPreset (manager, "Bus Graves Mono"));
        setValue (*processor, id::band (2, id::frequency), 233.0f);
        setValue (*processor, id::band (2, id::shape), 7.0f); // notch
        setValue (*processor, id::band (10, id::dynamic), 1.0f);
        setValue (*processor, id::band (10, id::spectral), 1.0f);
        setValue (*processor, id::band (10, id::threshold), -17.3f);
        setValue (*processor, id::linearQuality, 3.0f);
        std::vector<float> before;
        for (const auto* parameter : manager.getParameters())
            before.push_back (parameter->getValue());
        check (before.size() == static_cast<size_t> (id::numBands * 15 + 9), "todos menos el bypass: "
                                                                                 + juce::String (static_cast<int> (before.size())));

        const auto name = utf8 ("Mi Bus Ra\xc3\xadz");
        check (manager.save (name, "Buses", "Yo", "Notas\ncon dos lineas").wasOk(), "guardar");
        const auto file = temp.folder.getChildFile (name + ".fhpreset");
        check (file.existsAsFile(), "archivo .fhpreset en la carpeta del usuario");
        const auto text = file.loadFileAsString();
        check (text.contains ("<FathomPreset") && text.contains ("band3Frequency") && text.contains ("band24Spectral"), "XML legible");
        check (! text.contains ("\"bypass\""), "el bypass no se guarda");
        check (! manager.isModified() && manager.getCurrentName() == name, "guardado = preset actual sin modificar");

        manager.loadInit();
        const auto* saved = findPreset (manager, name);
        check (saved != nullptr && ! saved->factory && saved->category == "Buses" && saved->notes == "Notas\ncon dos lineas",
               "se relee con categoria y notas");
        if (saved != nullptr)
        {
            manager.load (*saved);
            for (size_t i = 0; i < before.size(); ++i)
                check (std::abs (manager.getParameters()[i]->getValue() - before[i]) < 1.0e-4f,
                       "ida y vuelta: " + manager.getParameters()[i]->paramID);
            check (! manager.isModified(), "recien cargado");
        }
        check (manager.save ("Init", "Buses", {}, {}).failed(), "no se puede usar el nombre de uno de fabrica");
        check (manager.save (name, "Buses", {}, "otra").wasOk(), "reemplazar");
        if (const auto* replaced = findPreset (manager, name))
        {
            check (replaced->notes == "otra", "reemplazado");
            check (manager.remove (*replaced).wasOk(), "borrar (a la papelera)");
        }
        check (findPreset (manager, name) == nullptr, "borrado");
    });

    runTest ("Proyecto de FL: el nombre y el 'modificado' viajan con el proyecto (con la vista, el A/B y el nombre de la instancia)", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        manager.load (*findPreset (manager, "808 Sub Limpio"));
        processor->setCustomName ("808");

        juce::MemoryBlock state;
        processor->getStateInformation (state);
        auto reopened = makeProcessor (temp.folder);
        reopened->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        check (reopened->getPresetManager().getCurrentName() == "808 Sub Limpio", "nombre restaurado");
        check (! reopened->getPresetManager().isModified(), "sin modificar");
        check (reopened->getCustomName() == "808", "el nombre de la instancia sigue ahi");

        setValue (*processor, id::band (2, id::gain), -5.0f);
        processor->getStateInformation (state);
        reopened = makeProcessor (temp.folder);
        reopened->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        check (reopened->getPresetManager().isModified(), "modificado: el '*' se restaura");

        // Un proyecto de la Fase 14 (sin las propiedades del preset): "Sin nombre", sin asterisco, con sus bandas.
        auto tree = processor->getParameters().copyState();
        tree.removeProperty ("presetName", nullptr);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);
        reopened = makeProcessor (temp.folder);
        reopened->setStateInformation (old.getData(), static_cast<int> (old.getSize()));
        check (reopened->getPresetManager().getCurrentName() == "Sin nombre", "proyecto antiguo: Sin nombre");
        check (! reopened->getPresetManager().isModified(), "proyecto antiguo: sin asterisco");
        check (std::abs (reopened->getBandSettings (2).gainDb + 5.0f) < 1.0e-3f, "proyecto antiguo: sus bandas");
    });

    runTest ("Cada preset de fabrica sobre una mezcla: sin NaN ni reservas, latencia segun su modo, sin picos que exploten", [] {
        constexpr double sampleRate = 48000.0;
        TemporaryFolder temp;
        const auto mix = makeMix (sampleRate, 3.0);
        std::vector<Preset> presets;
        {
            auto processor = makeProcessor (temp.folder);
            presets = processor->getPresetManager().getPresets();
        }
        if (verbose)
            std::printf ("    %-34s %9s %10s\n", "preset", "latencia", "pico");
        for (const auto& preset : presets)
        {
            auto processor = makeProcessor (temp.folder);
            processor->getPresetManager().load (preset);
            const auto stats = render (*processor, mix, sampleRate);
            check (stats.finite, preset.name + ": NaN o infinito");
            check (stats.allocations == 0, preset.name + ": processBlock reservo memoria " + juce::String (stats.allocations) + " veces");
            // Ningún preset sube más de 6 dB el pico (la mezcla tiene 808: las campanas en sus armónicos lo suben un poco).
            check (stats.outputPeak <= stats.inputPeak * 2.0f, preset.name + ": el pico sube a " + juce::String (toDb (stats.outputPeak), 1) + " dBFS");

            // Latencia: 0 en cero latencia sin color ni bandas espectrales; si no, la del motor.
            bool spectral = false;
            for (int b = 0; b < id::numBands; ++b)
                spectral = spectral || fathom::dsp::isSpectral (processor->getBandSettings (b));
            const bool latencyFree = processor->getPhaseMode() == fathom::dsp::PhaseMode::zeroLatency
                                     && processor->getColorMode() == fathom::dsp::ColorMode::clean && ! spectral;
            check (latencyFree == (stats.latency == 0), preset.name + ": latencia " + juce::String (stats.latency));
            if (verbose)
                std::printf ("    %-34s %6.1f ms %7.1f dBFS\n", preset.name.toRawUTF8(), 1000.0 * stats.latency / sampleRate, toDb (stats.outputPeak));
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
            const auto mix = makeMix (sampleRate, 0.6);
            for (const int blockSize : { 1, 7, 64, 256, 4096 })
                for (const int channels : { 1, 2 })
                    for (const bool sidechain : { true, false })
                    {
                        // Un preset distinto en cada combinación (recorre la biblioteca). En tiempo real: la memoria de
                        // la fase lineal/natural llega del temporizador (aquí no corre): se queda en cero latencia y avisa.
                        manager.load (presets[static_cast<size_t> (index++ % static_cast<int> (presets.size()))]);
                        RenderOptions options;
                        options.blockSize = blockSize;
                        options.channels = channels;
                        options.sidechain = sidechain;
                        options.offline = false;
                        const auto stats = render (*processor, mix, sampleRate, options);
                        const auto tag = juce::String (sampleRate / 1000.0, 1) + " kHz, bloques de " + juce::String (blockSize) + ", "
                                         + juce::String (channels) + " canal(es)" + (sidechain ? ", con sidechain" : ", sin sidechain");
                        check (stats.finite, tag + ": NaN");
                        check (stats.allocations == 0, tag + ": reservas de memoria: " + juce::String (stats.allocations));
                    }
        }
    });

    runTest ("Cambiar de preset con audio sonando (cada 150 ms, toda la biblioteca, con los modos de fase): sin NaN, saltos ni reservas", [] {
        constexpr double sampleRate = 48000.0;
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        const auto presets = manager.getPresets();
        RenderOptions options;
        const int blocksPerPreset = static_cast<int> (0.15 * sampleRate / options.blockSize);
        const double seconds = 0.15 * static_cast<double> (presets.size() * 2) + 0.5;

        // Un seno de 110 Hz a −12 dBFS (sin transitorios propios: cualquier salto es un clic) y el kick en el sidechain
        // para que las bandas con detector externo también se muevan.
        auto mix = makeMix (sampleRate, seconds);
        for (int i = 0; i < mix.main.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                mix.main.setSample (ch, i, 0.25f * static_cast<float> (std::sin (twoPi * 110.0 * i / sampleRate)));

        // Dos vueltas a la biblioteca en orden (cada preset después de otro distinto).
        options.beforeBlock = [&] (int block) {
            if (block % blocksPerPreset == 0)
                manager.load (presets[static_cast<size_t> ((block / blocksPerPreset) % static_cast<int> (presets.size()))]);
        };
        const auto stats = render (*processor, mix, sampleRate, options);
        check (stats.finite, "NaN");
        check (stats.allocations == 0, "reservas de memoria al cambiar de preset: " + juce::String (stats.allocations));

        // Un seno de 110 Hz a −12 dBFS cambia como mucho 0.0036 por muestra; con los presets que suben +3 dB en 110 Hz,
        // algo más. Un clic (un cambio sin fundido) es de 0.05 a 0.5.
        const float step = largestStep (stats.output);
        std::printf ("    pico %.1f dBFS, mayor salto entre muestras %.4f\n", toDb (stats.outputPeak), step);
        check (step < 0.02f, "salto entre muestras de " + juce::String (step, 4));
    });

    std::printf ("\n%s (%d fallos)\n", failures == 0 ? "TODOS LOS TESTS PASAN" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}
