// Herramienta (no es un test de ctest): capturas de la GUI de Fathom en PNG, con varias instancias sonando en el mismo
// proceso. El Standalone solo abre una instancia y sale recortado por el escalado de pantalla; esto dibuja el editor
// fuera de pantalla, tal cual, para revisarlo sin abrir FL.
//
//   FathomUiSnapshot <carpeta>     guarda instancias-panel.png, instancias-banda.png, instancias-mouse.png,
//                                  presets-cargado.png y presets-navegador.png (Fase 15)
//
// Tres instancias ("808", "Kick" y "Voz") con señales sintéticas; la del 808 mira a las otras dos y edita la del kick.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <thread>
#include <tuple>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "deepwater/presets/PresetBar.h"
#include "gui/EqDisplay.h"

namespace
{
constexpr double rate = 48000.0;
constexpr int blockSize = 480; // 10 ms

std::unique_ptr<FathomAudioProcessor> makeInstance (const juce::String& name)
{
    auto processor = std::make_unique<FathomAudioProcessor>();
    processor->setPlayConfigDetails (2, 2, rate, blockSize);
    processor->prepareToPlay (rate, blockSize);
    processor->setCustomName (name);
    return processor;
}

void setParameter (FathomAudioProcessor& processor, const std::string& id, float value)
{
    auto* parameter = processor.getParameters().getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void setBand (FathomAudioProcessor& processor, int band, int shape, float hz, float gainDb, float q)
{
    namespace id = fathom::params;
    setParameter (processor, id::band (band, id::enabled), 1.0f);
    setParameter (processor, id::band (band, id::shape), static_cast<float> (shape));
    setParameter (processor, id::band (band, id::frequency), hz);
    setParameter (processor, id::band (band, id::gain), gainDb);
    setParameter (processor, id::band (band, id::q), q);
}

// Una "pista": una función del tiempo (segundos) a una muestra.
struct Track
{
    FathomAudioProcessor* processor;
    std::function<float (double)> signal;
    double time = 0.0;

    void play()
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (int i = 0; i < blockSize; ++i)
        {
            const float x = signal (time + i / rate);
            buffer.setSample (0, i, x);
            buffer.setSample (1, i, x);
        }
        time += blockSize / rate;
        processor->processBlock (buffer, midi);
    }
};

double sine (double hz, double t) { return std::sin (2.0 * juce::MathConstants<double>::pi * hz * t); }

template <typename Wanted>
Wanted* findChild (juce::Component& parent, std::function<bool (Wanted&)> matches = {})
{
    for (auto* child : parent.getChildren())
    {
        if (auto* wanted = dynamic_cast<Wanted*> (child); wanted != nullptr && (! matches || matches (*wanted)))
            return wanted;
        if (auto* deeper = findChild<Wanted> (*child, matches))
            return deeper;
    }
    return nullptr;
}

void save (juce::Component& editor, const juce::File& file)
{
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat().writeImageToStream (image, stream);
    std::printf ("  %s\n", file.getFullPathName().toRawUTF8());
}

// Mueve el "mouse" sobre el visor (sin ventana de verdad: el evento se arma a mano).
void hover (fathom::gui::EqDisplay& display, juce::Point<float> position)
{
    const juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), position, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &display,
                                  &display, juce::Time::getCurrentTime(), position, juce::Time::getCurrentTime(), 0, false);
    display.mouseMove (event);
}
} // namespace

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI init;
    const juce::File folder = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                       : juce::File::getCurrentWorkingDirectory();
    folder.createDirectory();

    auto bass = makeInstance ("808");
    auto kick = makeInstance ("Kick");
    auto voice = makeInstance ("Voz");
    setBand (*bass, 0, 5, 25.0f, 0.0f, 0.71f); // corte de graves
    setBand (*bass, 1, 0, 55.0f, 2.0f, 1.0f);
    setBand (*kick, 0, 5, 35.0f, 0.0f, 0.71f);
    setBand (*kick, 1, 0, 320.0f, -3.0f, 1.4f);
    setBand (*kick, 2, 0, 3500.0f, 3.0f, 1.0f);

    std::vector<Track> tracks {
        { bass.get(), [] (double t) { return static_cast<float> (0.35 * sine (55.0, t) + 0.08 * sine (110.0, t) + 0.03 * sine (165.0, t)); } },
        { kick.get(), [] (double t) {
             const double beat = std::fmod (t, 0.5); // un golpe cada medio segundo
             const double body = 0.6 * std::exp (-beat * 12.0) * sine (50.0 + 80.0 * std::exp (-beat * 40.0), beat);
             const double click = 0.15 * std::exp (-beat * 120.0) * sine (3200.0, beat);
             return static_cast<float> (body + click);
         } },
        { voice.get(), [] (double t) {
             double x = 0.0;
             for (int n = 1; n <= 18; ++n) // una "vocal" en 220 Hz con un formante en 2.5 kHz
             {
                 const double hz = 220.0 * n;
                 const double formant = 1.0 + 3.0 * std::exp (-std::pow (std::log2 (hz / 2500.0) * 3.0, 2.0));
                 x += 0.12 / n * formant * sine (hz, t);
             }
             return static_cast<float> (x);
         } },
    };

    // El audio, en su propio hilo y a tiempo real (como en FL).
    std::atomic<bool> running { true };
    std::thread audio ([&] {
        auto next = std::chrono::steady_clock::now();
        while (running)
        {
            for (auto& track : tracks)
                track.play();
            next += std::chrono::milliseconds (10);
            std::this_thread::sleep_until (next);
        }
    });

    auto& settings = bass->getLinkSettings();
    settings.setShown (kick->getInstanceId(), true);
    settings.setShown (voice->getInstanceId(), true);
    settings.setEditable (kick->getInstanceId());

    // Fase 16: siempre al 100 % y con el tamaño del diseño (sin depender de la pantalla ni de lo que se usó antes).
    const auto setSize = [&] (double zoom, int width, int height) {
        auto state = bass->getParameters().state;
        state.setProperty ("uiZoom", zoom, nullptr);
        state.setProperty ("uiWidth", width, nullptr);
        state.setProperty ("uiHeight", height, nullptr);
    };
    setSize (1.0, 1082, 714);
    std::unique_ptr<juce::AudioProcessorEditor> editor (bass->createEditor());
    editor->setVisible (true);
    auto* display = findChild<fathom::gui::EqDisplay> (*editor);
    auto* instancesButton = findChild<juce::TextButton> (*editor, [] (juce::TextButton& b) { return b.getButtonText() == "INSTANCIAS"; });
    auto& loop = *juce::MessageManager::getInstance();

    std::printf ("Capturas:\n");
    instancesButton->triggerClick();
    loop.runDispatchLoopUntil (5000); // los promedios lentos se llenan
    save (*editor, folder.getChildFile ("instancias-panel.png"));

    instancesButton->triggerClick(); // de vuelta al panel de la banda: lo de las otras sigue en el visor
    loop.runDispatchLoopUntil (500);
    save (*editor, folder.getChildFile ("instancias-banda.png"));

    // El mouse sobre el choque más fuerte con el kick y sobre un nodo del kick.
    const auto& shown = display->getInstanceLink().getShown();
    for (const auto& other : shown)
        std::printf ("  %s: %zu zonas%s\n", other.name.toRawUTF8(), other.zones.size(), other.sounding ? "" : " (sin audio)");
    const auto plotX = [&] (double hz) {
        const float left = 10.0f + 30.0f, width = static_cast<float> (display->getWidth()) - 20.0f - 60.0f;
        return left + width * static_cast<float> (std::log (hz / 20.0) / std::log (1000.0));
    };
    if (! shown.empty() && ! shown.front().zones.empty())
        hover (*display, { plotX (shown.front().zones.front().centreHz), static_cast<float> (display->getHeight()) * 0.5f });
    loop.runDispatchLoopUntil (300);
    save (*editor, folder.getChildFile ("instancias-mouse.png"));

    // Fase 15: un preset cargado (la barra con su categoría) y el navegador abierto con sus notas.
    auto& presets = bass->getPresetManager();
    for (const auto& preset : presets.getPresets())
        if (preset.name == "808 Sub Limpio")
            presets.load (preset);
    loop.runDispatchLoopUntil (500);
    save (*editor, folder.getChildFile ("presets-cargado.png"));
    if (auto* bar = findChild<deepwater::presets::PresetBar> (*editor); bar != nullptr && bar->onToggleBrowser)
        bar->onToggleBrowser();
    loop.runDispatchLoopUntil (300);
    save (*editor, folder.getChildFile ("presets-navegador.png"));

    // Fase 16: el contenido agrandado (el visor crece; la cabecera y las columnas no) y con zoom.
    if (auto* bar = findChild<deepwater::presets::PresetBar> (*editor); bar != nullptr && bar->onToggleBrowser)
        bar->onToggleBrowser(); // cierra el navegador
    for (const auto& [zoom, width, height, name] : { std::tuple { 1.0, 1500, 950, "tamano-grande.png" },
                                                     std::tuple { 1.3, 1082, 714, "zoom-130.png" } })
    {
        editor.reset();
        setSize (zoom, width, height);
        editor.reset (bass->createEditor());
        editor->setVisible (true);
        loop.runDispatchLoopUntil (1500);
        std::printf ("  %s: ventana %d x %d\n", name, editor->getWidth(), editor->getHeight());
        save (*editor, folder.getChildFile (name));
    }

    running = false;
    audio.join();
    editor.reset();
    return 0;
}
