// Tests de la Fase 10 (presets). A diferencia de DspTests.cpp, estos usan JUCE: compilan el procesador completo para
// probar exactamente lo que hace el plugin (parámetros, estado del proyecto, XML).
//
//   UndertowPresetTests            todos los tests
//   UndertowPresetTests --verbose  además, la tabla de niveles de cada preset de fábrica
//   UndertowPresetTests --verbose --user "C:\ruta"   mide también los presets de esa carpeta (p. ej. los tuyos)
//   UndertowPresetTests --update-references   reescribe tests/reference/factory-presets.txt (ver "Huellas")
//
// Fase 11: además comprueba que processBlock no reserva memoria (operator new contado, abajo), mide la CPU de cada
// preset de fábrica y compara su sonido con una huella de referencia guardada en el repositorio.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "presets/PresetManager.h"

// --- Reservas de memoria en el hilo de audio -------------------------------------------------------------------------
//
// Regla del proyecto: processBlock no puede reservar ni liberar memoria (el reparto de memoria del sistema puede
// bloquearse esperando a otro hilo, y un bloqueo en el hilo de audio es un corte en el sonido). No hay forma de verlo
// escuchando: casi siempre es rápido. Así que este programa REEMPLAZA operator new / delete (C++ lo permite una vez
// por ejecutable) por versiones que, mientras 'countingAllocations' está activo, cuentan cada llamada.
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

// Cuenta las reservas (y liberaciones) que ocurren dentro de su vida.
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
using undertow::presets::Preset;
using undertow::presets::PresetManager;

int failures = 0;
bool verbose = false;
juce::File measuredUserFolder; // --user: presets del usuario que también se miden
bool updateReferences = false;  // --update-references

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

float valueOf (UndertowAudioProcessor& processor, const char* id)
{
    return processor.getParameters().getRawParameterValue (id)->load();
}

void setValue (UndertowAudioProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.getParameters().getParameter (id);
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
                      .getChildFile ("UndertowPresetTests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())))
    {
        folder.createDirectory();
    }
    ~TemporaryFolder() { folder.deleteRecursively(); }
    juce::File folder;
};

std::unique_ptr<UndertowAudioProcessor> makeProcessor (const juce::File& userFolder)
{
    auto processor = std::make_unique<UndertowAudioProcessor>();
    processor->getPresetManager().setUserFolder (userFolder);
    return processor;
}

// --- Render ----------------------------------------------------------------------------------------------------------

struct Note
{
    int note;
    double start, end; // segundos
    int velocity = 100;
};

// Una frase típica de cada categoría (notas MIDI: 60 = C5 de FL).
std::vector<Note> phraseFor (const juce::String& category)
{
    if (category == "Bass")
        return { { 41, 0.0, 1.0 }, { 44, 1.0, 1.5 }, { 41, 1.5, 2.5 } }; // F3 (87 Hz), G#3
    if (category == "Lead")
        return { { 72, 0.0, 0.5 }, { 76, 0.5, 1.0 }, { 79, 1.0, 1.5 }, { 84, 1.5, 2.5 } };
    if (category == "Pad")
        return { { 57, 0.0, 3.0, 90 }, { 60, 0.0, 3.0, 90 }, { 64, 0.0, 3.0, 90 }, { 67, 0.0, 3.0, 90 } };
    if (category == "Pluck")
    {
        std::vector<Note> notes;
        constexpr std::array<int, 4> arpeggio { 60, 64, 67, 69 }; // C5-E5-G5-A5, como dicen las notas de los plucks
        for (int i = 0; i < 12; ++i)
            notes.push_back ({ arpeggio[static_cast<size_t> (i % 4)], i * 0.25, i * 0.25 + 0.2 });
        return notes;
    }
    if (category == "Keys")
        return { { 60, 0.0, 2.0 }, { 64, 0.0, 2.0 }, { 67, 0.0, 2.0 }, { 71, 0.0, 2.0 } };
    if (category == "FX")
        return { { 60, 0.0, 8.0 } }; // el riser tarda 8 s en abrirse
    return { { 60, 0.0, 2.0 } };
}

struct RenderStats
{
    float peak = 0.0f;
    float loudestRms = 0.0f; // RMS máximo en ventanas de 300 ms (≈ "volumen momentáneo")
    bool finite = true;
    int allocations = 0;          // reservas o liberaciones de memoria dentro de processBlock (debe ser 0)
    double processSeconds = 0.0;  // tiempo total dentro de processBlock
    double audioSeconds = 0.0;    // duración del audio generado
    std::vector<float> mono;      // (L + R) / 2, solo si se pidió

    // Fracción de un núcleo que hace falta para generar el audio en tiempo real.
    [[nodiscard]] double cpu() const { return processSeconds / audioSeconds; }
};

RenderStats render (UndertowAudioProcessor& processor, const std::vector<Note>& notes, double sampleRate, double seconds,
                    const std::function<void (int block)>& beforeBlock = {}, bool keepAudio = false, int blockSize = 256)
{
    processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    const int total = static_cast<int> (seconds * sampleRate);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    RenderStats stats;

    const int window = static_cast<int> (0.3 * sampleRate);
    double windowSum = 0.0;
    int windowCount = 0;

    for (int start = 0, block = 0; start < total; start += blockSize, ++block)
    {
        const int count = std::min (blockSize, total - start);
        buffer.setSize (2, count, false, false, true);
        midi.clear();
        for (const auto& n : notes)
        {
            const int on = static_cast<int> (n.start * sampleRate);
            const int off = static_cast<int> (n.end * sampleRate);
            if (on >= start && on < start + count)
                midi.addEvent (juce::MidiMessage::noteOn (1, n.note, static_cast<juce::uint8> (n.velocity)), on - start);
            if (off >= start && off < start + count)
                midi.addEvent (juce::MidiMessage::noteOff (1, n.note), off - start);
        }
        if (beforeBlock)
            beforeBlock (block);

        allocation::count = 0;
        const auto started = std::chrono::steady_clock::now();
        {
            const allocation::Scope countAllocations;
            processor.processBlock (buffer, midi);
        }
        stats.processSeconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
        stats.allocations += allocation::count.load();

        for (int i = 0; i < count; ++i)
        {
            const float l = buffer.getSample (0, i);
            const float r = buffer.getSample (1, i);
            if (keepAudio)
                stats.mono.push_back (0.5f * (l + r));
            if (! std::isfinite (l) || ! std::isfinite (r))
            {
                stats.finite = false;
                continue;
            }
            stats.peak = std::max ({ stats.peak, std::abs (l), std::abs (r) });
            windowSum += 0.5 * (static_cast<double> (l) * l + static_cast<double> (r) * r);
            if (++windowCount == window)
            {
                stats.loudestRms = std::max (stats.loudestRms, static_cast<float> (std::sqrt (windowSum / window)));
                windowSum = 0.0;
                windowCount = 0;
            }
        }
    }
    processor.releaseResources();
    stats.audioSeconds = total / sampleRate;
    return stats;
}

float toDb (float gain)
{
    return 20.0f * std::log10 (std::max (gain, 1.0e-9f));
}

// --- Huellas: cómo suena cada preset de fábrica ------------------------------------------------------------------
//
// Una "huella" resume el sonido de un render: la energía media en 24 bandas de 1/3 de octava (más o menos) entre 40 Hz
// y 20 kHz, en dB. Se guarda en tests/reference/factory-presets.txt y cada ejecución la compara con la de ahora. Sirve
// para cambios que NO deberían cambiar el sonido (optimizaciones, refactors): si una banda se mueve más de 0.05 dB, el
// test lo dice. Si el cambio de sonido es intencionado, se regeneran con --update-references (y se revisa el diff).
constexpr int fingerprintBands = 24;
using Fingerprint = std::array<float, fingerprintBands>;

Fingerprint fingerprintOf (const std::vector<float>& audio, double sampleRate)
{
    constexpr int order = 12, size = 1 << order;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> window (size, juce::dsp::WindowingFunction<float>::hann, false);

    std::vector<double> power (size / 2, 0.0);
    std::vector<float> frame (2 * size);
    int frames = 0;
    for (size_t start = 0; start + size <= audio.size(); start += size / 2, ++frames)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        std::copy_n (audio.begin() + static_cast<std::ptrdiff_t> (start), size, frame.begin());
        window.multiplyWithWindowingTable (frame.data(), size);
        fft.performFrequencyOnlyForwardTransform (frame.data());
        for (size_t k = 0; k < power.size(); ++k)
            power[k] += static_cast<double> (frame[k]) * frame[k];
    }

    Fingerprint bands {};
    const double lowest = 40.0, highest = 20000.0;
    for (int b = 0; b < fingerprintBands; ++b)
    {
        const double from = lowest * std::pow (highest / lowest, static_cast<double> (b) / fingerprintBands);
        const double to = lowest * std::pow (highest / lowest, static_cast<double> (b + 1) / fingerprintBands);
        double sum = 0.0;
        for (size_t k = 1; k < power.size(); ++k)
        {
            const double hz = static_cast<double> (k) * sampleRate / size;
            if (hz >= from && hz < to)
                sum += power[k];
        }
        bands[static_cast<size_t> (b)] = static_cast<float> (10.0 * std::log10 (std::max (sum / std::max (frames, 1), 1.0e-20)));
    }
    return bands;
}

juce::File referenceFile()
{
    return juce::File (UNDERTOW_SOURCE_DIR).getChildFile ("tests/reference/factory-presets.txt");
}

// Formato: una línea por preset, "nombre<TAB>banda1 banda2 ... banda24" (dB con 2 decimales).
std::map<juce::String, Fingerprint> readReferences()
{
    std::map<juce::String, Fingerprint> references;
    juce::StringArray lines;
    lines.addLines (referenceFile().loadFileAsString());
    for (const auto& line : lines)
    {
        if (line.startsWith ("#") || ! line.contains ("\t"))
            continue;
        const auto values = juce::StringArray::fromTokens (line.fromFirstOccurrenceOf ("\t", false, false), " ", "");
        if (values.size() != fingerprintBands)
            continue;
        Fingerprint bands {};
        for (int b = 0; b < fingerprintBands; ++b)
            bands[static_cast<size_t> (b)] = values[b].getFloatValue();
        references[line.upToFirstOccurrenceOf ("\t", false, false)] = bands;
    }
    return references;
}

// Diferencia máxima entre dos huellas, contando solo las bandas que suenan: las que están a menos de 60 dB de la más
// fuerte. (Una banda casi vacía cambia mucho en dB por diferencias de redondeo inaudibles.)
float fingerprintDifference (const Fingerprint& a, const Fingerprint& b)
{
    const float loudest = *std::max_element (a.begin(), a.end());
    float worst = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] > loudest - 60.0f)
            worst = std::max (worst, std::abs (a[i] - b[i]));
    return worst;
}

} // namespace

int main (int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        const juce::String argument (argv[i]);
        verbose = verbose || argument == "--verbose";
        updateReferences = updateReferences || argument == "--update-references";
        if (argument == "--user" && i + 1 < argc)
            measuredUserFolder = juce::File (juce::String (argv[++i]));
        if (argument == "--notes")
        {
            // Imprime las notas de los presets de fábrica tal como las muestra el navegador (y no corre los tests).
            const juce::ScopedJuceInitialiser_GUI init;
            UndertowAudioProcessor processor;
            for (const auto& preset : processor.getPresetManager().getPresets())
                if (preset.factory)
                    std::printf ("=== %s (%s)\n%s\n\n", preset.name.toRawUTF8(), preset.category.toRawUTF8(), preset.notes.toRawUTF8());
            return 0;
        }
    }

    const juce::ScopedJuceInitialiser_GUI juce;
    namespace id = undertow::params;

    runTest ("Presets de fabrica: se leen, nombres unicos, categorias validas, notas y valores correctos", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        const auto& presets = manager.getPresets();

        int factoryCount = 0;
        std::set<juce::String> names;
        std::map<int, int> perCategory;
        for (const auto& preset : presets)
        {
            if (! preset.factory)
                continue;
            ++factoryCount;
            check (names.insert (preset.name.toLowerCase()).second, "nombre repetido: " + preset.name);
            check (preset.categoryIndex() < static_cast<int> (undertow::presets::categoryNames.size()) - 1,
                   preset.name + ": categoria desconocida \"" + preset.category + "\"");
            check (preset.notes.length() > 200, preset.name + ": las notas deben explicar como se construyo");
            ++perCategory[preset.categoryIndex()];

            juce::StringArray problems;
            juce::ignoreUnused (manager.resolve (preset, &problems));
            for (const auto& problem : problems)
                check (false, preset.name + ": " + problem);

            // Cada id aparece una sola vez (si no, el segundo valor pisaría al primero sin que se note).
            std::set<juce::String> ids;
            for (const auto& [parameterId, value] : preset.values)
                check (ids.insert (parameterId).second, preset.name + ": " + parameterId + " repetido");
        }
        check (factoryCount >= 20, "se esperaban al menos 20 presets de fabrica, hay " + juce::String (factoryCount));
        for (int c = 0; c < static_cast<int> (undertow::presets::categoryNames.size()) - 1; ++c)
            check (perCategory[c] > 0, juce::String ("categoria vacia: ") + undertow::presets::categoryNames[static_cast<size_t> (c)]);
        check (findPreset (manager, "Init") != nullptr && findPreset (manager, "Init")->values.empty(), "falta Init vacio");
        std::printf ("    %d presets de fabrica en %d categorias\n", factoryCount, static_cast<int> (perCategory.size()));
    });

    runTest ("Cargar: aplica valores (unidades reales y nombres de opciones), lo que falta vuelve al defecto", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();

        const auto* moog = findPreset (manager, "Bajo Moog");
        check (moog != nullptr, "falta Bajo Moog");
        if (moog == nullptr)
            return;
        check (manager.load (*moog).isEmpty(), "Bajo Moog no deberia dar avisos");
        check (std::abs (valueOf (*processor, id::filter1Cutoff) - 450.0f) < 0.5f, "cutoff 450 Hz");
        check (valueOf (*processor, id::filter1On) > 0.5f, "filtro encendido");
        check (static_cast<int> (valueOf (*processor, id::filter1Type)) == 0, "Low Pass por nombre");
        check (static_cast<int> (valueOf (*processor, id::voices)) == 1, "voices 1");
        check (static_cast<int> (valueOf (*processor, id::modSlots[0].source)) == 2, "mod 1 = Env 2 por nombre");
        check (std::abs (valueOf (*processor, id::modSlots[0].amount) - 0.15f) < 1.0e-4f, "mod 1 amount 15 %");
        check (! manager.isModified(), "recien cargado no esta modificado");
        check (manager.getCurrentName() == "Bajo Moog", "nombre actual");

        // Supersaw tiene reverb y Osc B; Bajo Moog no: al cargarlo después, la reverb y Osc B deben apagarse.
        manager.load (*findPreset (manager, "Supersaw Trance"));
        check (valueOf (*processor, id::reverbOn) > 0.5f && valueOf (*processor, id::oscillators[1].on) > 0.5f,
               "Supersaw: reverb y Osc B");
        manager.load (*moog);
        check (valueOf (*processor, id::reverbOn) < 0.5f, "la reverb vuelve a su defecto (apagada)");
        check (valueOf (*processor, id::oscillators[1].on) < 0.5f, "Osc B vuelve a su defecto (apagado)");
        check (static_cast<int> (valueOf (*processor, id::oscillators[0].unison)) == 1, "unison vuelve a 1");

        // Init = todos los parámetros en su valor por defecto.
        manager.loadInit();
        int notDefault = 0;
        for (const auto* parameter : manager.getParameters())
            notDefault += std::abs (parameter->getValue() - parameter->getDefaultValue()) > 1.0e-6f ? 1 : 0;
        check (notDefault == 0, juce::String (notDefault) + " parametros no volvieron al defecto con Init");
        check (manager.getCurrentName() == "Init", "nombre Init");
    });

    runTest ("Modificado: tocar un parametro marca el preset; volver al valor lo desmarca", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        manager.load (*findPreset (manager, "Acid Squelch"));
        check (! manager.isModified(), "sin tocar");
        setValue (*processor, id::filter1Resonance, 0.5f);
        check (manager.isModified(), "tras cambiar la resonancia");
        setValue (*processor, id::filter1Resonance, 0.75f);
        check (! manager.isModified(), "al volver al valor del preset");
    });

    runTest ("Guardar preset de usuario: archivo legible, todos los parametros, ida y vuelta exacta", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();

        manager.load (*findPreset (manager, "Pad Juno"));
        setValue (*processor, id::filter1Cutoff, 1234.5f);
        setValue (*processor, id::oscillators[0].unison, 5.0f);
        setValue (*processor, id::modSlots[3].source, 4.0f);
        setValue (*processor, id::modSlots[3].destination, 3.0f);
        setValue (*processor, id::modSlots[3].amount, -0.37f);
        const auto saved = manager.getParameters();
        std::vector<float> expected;
        for (const auto* parameter : saved)
            expected.push_back (parameter->getValue());

        const auto result = manager.save (juce::String::fromUTF8 ("Mi Pad / Cálido"), "Pad", "Yo",
                                          juce::String::fromUTF8 ("Notas con tildes: ñ, á.\nSegunda línea."));
        check (result.wasOk(), "guardar: " + result.getErrorMessage());
        check (manager.getCurrentName() == juce::String::fromUTF8 ("Mi Pad / Cálido"), "el guardado pasa a ser el actual");
        check (! manager.isModified(), "recien guardado no esta modificado");

        const auto files = temp.folder.findChildFiles (juce::File::findFiles, true, "*.uwpreset");
        check (files.size() == 1, "un archivo en la carpeta");
        const auto* user = findPreset (manager, juce::String::fromUTF8 ("Mi Pad / Cálido"));
        check (user != nullptr && ! user->factory, "aparece en la biblioteca como preset del usuario");
        if (user == nullptr)
            return;
        check (user->notes == juce::String::fromUTF8 ("Notas con tildes: ñ, á.\nSegunda línea."), "notas intactas");
        check (user->values.size() == saved.size(), "guarda todos los parametros");
        check (user->category == "Pad" && user->author == "Yo", "categoria y autor");

        // Releerlo en otra instancia: los valores deben ser los mismos.
        manager.loadInit();
        auto other = makeProcessor (temp.folder);
        auto& otherManager = other->getPresetManager();
        check (otherManager.load (*findPreset (otherManager, juce::String::fromUTF8 ("Mi Pad / Cálido"))).isEmpty(), "sin avisos");
        int different = 0;
        for (size_t i = 0; i < otherManager.getParameters().size(); ++i)
            different += std::abs (otherManager.getParameters()[i]->getValue() - expected[i]) > 1.0e-5f ? 1 : 0;
        check (different == 0, juce::String (different) + " parametros distintos tras la ida y vuelta");

        // Reemplazar: mismo nombre = mismo archivo.
        setValue (*other, id::filter1Cutoff, 800.0f);
        check (otherManager.userPresetExists (juce::String::fromUTF8 ("mi pad / cálido")), "existe (sin distinguir mayusculas)");
        check (otherManager.save (juce::String::fromUTF8 ("Mi Pad / Cálido"), "Pad", "Yo", "v2").wasOk(), "reemplazar");
        check (temp.folder.findChildFiles (juce::File::findFiles, true, "*.uwpreset").size() == 1, "sigue habiendo un archivo");

        // Un nombre de fábrica no se puede usar, ni vacío.
        check (otherManager.save ("bajo moog", "Bass", {}, {}).failed(), "nombre de fabrica rechazado");
        check (otherManager.save ("   ", "Bass", {}, {}).failed(), "nombre vacio rechazado");

        // Borrar (a la papelera). Los de fábrica no.
        check (otherManager.remove (*findPreset (otherManager, "Bajo Moog")).failed(), "no se borra uno de fabrica");
        check (otherManager.remove (*findPreset (otherManager, juce::String::fromUTF8 ("Mi Pad / Cálido"))).wasOk(), "borrar");
        check (findPreset (otherManager, juce::String::fromUTF8 ("Mi Pad / Cálido")) == nullptr, "ya no esta en la lista");
        check (otherManager.isModified(), "el sonido borrado de la biblioteca se marca como modificado");
    });

    runTest ("Archivos raros: ids desconocidos, opciones inexistentes y XML ajeno no rompen nada", [] {
        TemporaryFolder temp;
        temp.folder.getChildFile ("Roto.uwpreset").replaceWithText ("esto no es XML");
        temp.folder.getChildFile ("Ajeno.uwpreset").replaceWithText ("<OtroPlugin name=\"x\"/>");
        temp.folder.getChildFile ("Sub").createDirectory();
        temp.folder.getChildFile ("Sub").getChildFile ("Raro.uwpreset").replaceWithText (
            "<UndertowPreset formatVersion=\"1\" name=\"Raro\" category=\"Inventada\"><Parameters>"
            "<PARAM id=\"noExiste\" value=\"1\"/><PARAM id=\"filter1Type\" value=\"Comb\"/>"
            "<PARAM id=\"filter1Cutoff\" value=\"999999\"/><PARAM id=\"filter1On\" value=\"on\"/>"
            "</Parameters></UndertowPreset>");
        temp.folder.getChildFile ("Futuro.uwpreset").replaceWithText ("<UndertowPreset formatVersion=\"99\" name=\"Futuro\"/>");

        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        int userCount = 0;
        for (const auto& preset : manager.getPresets())
            userCount += preset.factory ? 0 : 1;
        check (userCount == 1, "solo el preset valido de la subcarpeta, hay " + juce::String (userCount));

        const auto* weird = findPreset (manager, "Raro");
        check (weird != nullptr && weird->category == "Inventada" && weird->categoryIndex() == 7, "categoria desconocida → Otros");
        if (weird == nullptr)
            return;
        const auto problems = manager.load (*weird);
        check (problems.size() == 3, "3 avisos (id, opcion, rango), hay " + juce::String (problems.size()));
        check (valueOf (*processor, id::filter1On) > 0.5f, "\"on\" se entiende");
        check (std::abs (valueOf (*processor, id::filter1Cutoff) - 20000.0f) < 1.0f, "fuera de rango → se limita");
        check (static_cast<int> (valueOf (*processor, id::filter1Type)) == 0, "opcion desconocida → defecto");
    });

    runTest ("Restaurar el estado deja los on/off y las listas en su valor exacto (encontrado con pluginval)", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto* on = processor->getParameters().getParameter (id::filter1On);
        auto* type = processor->getParameters().getParameter (id::filter1Type);

        // El host puede dejar valores intermedios: 0.34 en un on/off es "Off"; 0.2 en una lista de 3 es la opción 0.
        on->setValueNotifyingHost (0.34f);
        type->setValueNotifyingHost (0.2f);
        juce::MemoryBlock state;
        processor->getStateInformation (state);
        on->setValueNotifyingHost (0.3f); // sigue siendo Off: el valor del estado no cambia al restaurar
        type->setValueNotifyingHost (0.1f);
        processor->setStateInformation (state.getData(), static_cast<int> (state.getSize()));

        check (on->getValue() == 0.0f, "on/off restaurado a " + juce::String (on->getValue()) + " (debe ser 0)");
        check (type->getValue() == 0.0f, "lista restaurada a " + juce::String (type->getValue()) + " (debe ser 0)");
    });

    runTest ("Proyecto de FL: el nombre y el estado 'modificado' se guardan con el proyecto", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();

        manager.load (*findPreset (manager, "Reese DnB"));
        juce::MemoryBlock clean;
        processor->getStateInformation (clean);

        setValue (*processor, id::filter1Cutoff, 900.0f);
        juce::MemoryBlock modified;
        processor->getStateInformation (modified);

        auto reopened = makeProcessor (temp.folder);
        reopened->setStateInformation (clean.getData(), static_cast<int> (clean.getSize()));
        check (reopened->getPresetManager().getCurrentName() == "Reese DnB", "nombre restaurado");
        check (! reopened->getPresetManager().isModified(), "sin modificar");
        check (std::abs (valueOf (*reopened, id::filter1Cutoff) - 350.0f) < 0.5f, "valores restaurados");

        reopened->setStateInformation (modified.getData(), static_cast<int> (modified.getSize()));
        check (reopened->getPresetManager().isModified(), "modificado restaurado");
        check (std::abs (valueOf (*reopened, id::filter1Cutoff) - 900.0f) < 0.5f, "valor modificado restaurado");

        // Un proyecto de la Fase 9 (sin propiedades de preset) carga igual y se llama "Sin nombre".
        auto phase9 = makeProcessor (temp.folder);
        setValue (*phase9, id::filter1Cutoff, 700.0f);
        juce::MemoryBlock oldProject;
        if (auto xml = phase9->getParameters().copyState().createXml()) // el estado tal como lo guardaba la Fase 9
            juce::AudioProcessor::copyXmlToBinary (*xml, oldProject);
        reopened->setStateInformation (oldProject.getData(), static_cast<int> (oldProject.getSize()));
        check (reopened->getPresetManager().getCurrentName() == "Sin nombre", "proyecto antiguo: Sin nombre");
        check (! reopened->getPresetManager().isModified(), "proyecto antiguo: sin asterisco");
        check (std::abs (valueOf (*reopened, id::filter1Cutoff) - 700.0f) < 0.5f, "proyecto antiguo: valores intactos");

        // Siguiente / anterior dan la vuelta a la lista.
        auto& m = reopened->getPresetManager();
        const auto* first = m.step (1);
        check (first != nullptr && first->name == "Init", "desde 'Sin nombre', siguiente = el primero (Init)");
        const auto* last = m.step (-1);
        check (last != nullptr && last->sameAs (m.getPresets().back()), "anterior a Init = el ultimo");
        check (m.step (1)->name == "Init", "y vuelta al primero");
    });

    runTest ("Render de cada preset de fabrica: sin NaN, sin pasar de 0 dBFS y con volumenes parecidos", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (measuredUserFolder.isDirectory() ? measuredUserFolder : temp.folder);
        auto& manager = processor->getPresetManager();

        if (verbose)
            std::printf ("    %-24s %-6s %9s %9s %8s\n", "preset", "cat.", "pico dB", "RMS dB", "CPU");
        float quietest = 0.0f, loudest = -200.0f;
        double cpuSum = 0.0, cpuMax = 0.0;
        juce::String heaviest;
        int measured = 0;
        for (const auto& preset : manager.getPresets())
        {
            manager.load (preset);
            const auto notes = phraseFor (preset.category);
            double length = 0.0;
            for (const auto& n : notes)
                length = std::max (length, n.end);
            const auto stats = render (*processor, notes, 48000.0, length + 1.0);

            const float peakDb = toDb (stats.peak);
            const float rmsDb = toDb (stats.loudestRms);
            if (verbose)
                std::printf ("    %-24s %-6s %9.1f %9.1f %7.1f%%%s\n", preset.name.toRawUTF8(), preset.category.toRawUTF8(),
                             peakDb, rmsDb, 100.0 * stats.cpu(), preset.factory ? "" : "   (usuario)");
            check (stats.allocations == 0,
                   preset.name + ": processBlock reservo o libero memoria " + juce::String (stats.allocations) + " veces");
            if (! preset.factory)
                continue; // los del usuario solo se miden
            cpuSum += stats.cpu();
            ++measured;
            if (stats.cpu() > cpuMax)
            {
                cpuMax = stats.cpu();
                heaviest = preset.name;
            }
            check (stats.finite, preset.name + ": NaN o infinito");
            check (peakDb < -0.5f, preset.name + ": pico " + juce::String (peakDb, 1) + " dBFS (debe quedar por debajo de -0.5)");
            if (preset.category == "Pluck")
            {
                // Un sonido percusivo dura tan poco que su RMS siempre es bajo aunque suene igual de fuerte (factor de
                // cresta alto). En ellos se compara el pico.
                check (peakDb > -14.0f && peakDb < -4.0f, preset.name + ": pico " + juce::String (peakDb, 1) + " fuera de -14..-4");
            }
            else if (preset.category != "Init")
            {
                check (rmsDb > -22.0f && rmsDb < -14.0f, preset.name + ": RMS " + juce::String (rmsDb, 1) + " dBFS fuera de -22..-14");
                quietest = std::min (quietest, rmsDb);
                loudest = std::max (loudest, rmsDb);
            }
        }
        std::printf ("    RMS de los presets (sin Init): de %.1f a %.1f dBFS\n", quietest, loudest);
        std::printf ("    CPU a 48 kHz (bloques de 256, este equipo): media %.1f %% de un nucleo, maximo %.1f %% (%s)\n",
                     100.0 * cpuSum / std::max (measured, 1), 100.0 * cpuMax, heaviest.toRawUTF8());
    });

    runTest ("Huellas: cada preset de fabrica suena como su referencia (tests/reference)", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        const auto references = readReferences();

        juce::String written = "# Huellas de los presets de fabrica (UndertowPresetTests --update-references).\n"
                               "# Energia en 24 bandas de 40 Hz a 20 kHz (dB) de la frase de su categoria a 48 kHz.\n";
        float worst = 0.0f;
        for (const auto& preset : manager.getPresets())
        {
            if (! preset.factory)
                continue;
            manager.load (preset);
            const auto notes = phraseFor (preset.category);
            double length = 0.0;
            for (const auto& n : notes)
                length = std::max (length, n.end);
            const auto stats = render (*processor, notes, 48000.0, length + 1.0, {}, true);
            const auto bands = fingerprintOf (stats.mono, 48000.0);

            juce::StringArray values;
            for (const float band : bands)
                values.add (juce::String (band, 2));
            written << preset.name << "\t" << values.joinIntoString (" ") << "\n";

            if (updateReferences)
                continue;
            const auto reference = references.find (preset.name);
            check (reference != references.end(), preset.name + ": no tiene huella de referencia (--update-references)");
            if (reference == references.end())
                continue;
            const float difference = fingerprintDifference (reference->second, bands);
            worst = std::max (worst, difference);
            if (verbose)
                std::printf ("    %-24s diferencia maxima %.3f dB\n", preset.name.toRawUTF8(), difference);
            check (difference <= 0.05f, preset.name + ": suena distinto a su referencia (" + juce::String (difference, 2)
                                            + " dB en alguna banda)");
        }

        if (updateReferences)
        {
            referenceFile().getParentDirectory().createDirectory();
            check (referenceFile().replaceWithText (written, false, false, "\n"), "no se pudo escribir " + referenceFile().getFullPathName());
            std::printf ("    huellas escritas en %s\n", referenceFile().getFullPathName().toRawUTF8());
        }
        else
        {
            std::printf ("    peor diferencia con la referencia: %.3f dB\n", worst);
        }
    });

    runTest ("Tiempo real: cualquier tamano de bloque (1 a 4096 muestras), sin reservar memoria ni NaN", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        const std::vector<Note> chord { { 48, 0.0, 1.0 }, { 60, 0.1, 1.0 }, { 64, 0.2, 1.0 }, { 67, 0.3, 1.2 } };
        for (const auto& preset : manager.getPresets())
        {
            if (! preset.factory)
                continue;
            manager.load (preset);
            for (const int blockSize : { 1, 7, 64, 4096 })
            {
                const auto stats = render (*processor, chord, 48000.0, 1.5, {}, false, blockSize);
                check (stats.finite && stats.allocations == 0,
                       preset.name + " con bloques de " + juce::String (blockSize) + ": " + juce::String (stats.allocations)
                           + " reservas de memoria" + (stats.finite ? "" : ", NaN"));
            }
        }
    });

    runTest ("Otros sample rates y cambiar de preset con notas sonando: sin NaN ni picos", [] {
        TemporaryFolder temp;
        auto processor = makeProcessor (temp.folder);
        auto& manager = processor->getPresetManager();
        std::vector<const Preset*> factory;
        for (const auto& preset : manager.getPresets())
            if (preset.factory)
                factory.push_back (&preset);

        for (const double rate : { 44100.0, 96000.0 })
            for (const auto* preset : factory)
            {
                manager.load (*preset);
                const auto stats = render (*processor, phraseFor (preset->category), rate, 1.5);
                check (stats.finite && toDb (stats.peak) < 0.0f,
                       preset->name + " a " + juce::String (rate / 1000.0, 1) + " kHz: pico " + juce::String (toDb (stats.peak), 1));
            }

        // Un acorde mantenido mientras se cambia de preset cada ~100 ms (como al recorrer la lista con las flechas).
        const std::vector<Note> chord { { 48, 0.0, 6.0 }, { 60, 0.0, 6.0 }, { 64, 0.0, 6.0 }, { 67, 0.0, 6.0 } };
        size_t next = 0;
        const auto stats = render (*processor, chord, 48000.0, 6.0, [&] (int block) {
            if (block % 19 == 0)
                manager.load (*factory[next++ % factory.size()]);
        });
        check (stats.finite, "cambiando de preset: NaN");
        check (toDb (stats.peak) < 3.0f, "cambiando de preset: pico " + juce::String (toDb (stats.peak), 1) + " dBFS");
        std::printf ("    cambiando de preset con un acorde: pico %.1f dBFS\n", toDb (stats.peak));
    });

    std::printf ("\n%s (%d fallos)\n", failures == 0 ? "TODOS LOS TESTS PASAN" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}
