// Tests de la Fase 14 (otras instancias). A diferencia de EngineTests.cpp, estos usan JUCE: crean varios procesadores
// COMPLETOS en el mismo proceso, como FL con Fathom en varias pistas, y prueban lo que solo se ve así: la lista del
// proceso, que una instancia con la ventana cerrada deja su salida cuando alguien mira, los choques entre dos pistas de
// verdad, editar una desde otra (con su deshacer) y que borrar una a mitad de una edición no rompa nada.

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "gui/BandEdits.h"
#include "gui/InstanceLink.h"

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

constexpr double testRate = 48000.0;
constexpr int blockSize = 512;

std::unique_ptr<FathomAudioProcessor> makeInstance()
{
    auto processor = std::make_unique<FathomAudioProcessor>();
    processor->setPlayConfigDetails (2, 2, testRate, blockSize);
    processor->prepareToPlay (testRate, blockSize);
    return processor;
}

// Un bloque de senos (en los dos canales) con la fase que sigue de un bloque al otro.
void playBlock (FathomAudioProcessor& processor, const std::vector<std::pair<double, double>>& sines, double& phase)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int i = 0; i < blockSize; ++i)
    {
        double x = 0.0;
        for (const auto& [hz, amplitude] : sines)
            x += amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * hz * (phase + i) / testRate);
        buffer.setSample (0, i, static_cast<float> (x));
        buffer.setSample (1, i, static_cast<float> (x));
    }
    phase += blockSize;
    processor.processBlock (buffer, midi);
}

float parameterValue (FathomAudioProcessor& processor, const std::string& id)
{
    return processor.getParameters().getRawParameterValue (id)->load();
}

void testRegistryAndNames()
{
    std::printf ("Instancias: cada una se anota en la lista del proceso y tiene su nombre\n");
    auto& registry = FathomAudioProcessor::Registry::shared();
    const auto before = registry.ids().size();
    auto a = makeInstance();
    auto b = makeInstance();
    CHECK (registry.ids().size() == before + 2);
    CHECK (a->getInstanceId() != b->getInstanceId());
    CHECK (registry.find (a->getInstanceId()) == a.get() && registry.find (b->getInstanceId()) == b.get());
    CHECK (a->getInstanceName() == "Fathom " + juce::String (a->getInstanceId()));

    // El nombre de la pista (si el host lo manda) y el puesto a mano, que gana y se guarda con el proyecto.
    juce::AudioProcessor::TrackProperties track;
    track.name = "808";
    track.colourARGB = 0xff3080ffu;
    b->updateTrackProperties (track);
    CHECK (b->getInstanceName() == "808" && b->getTrackColour().has_value());
    b->setCustomName ("  Bajo  ");
    CHECK (b->getInstanceName() == "Bajo");
    juce::MemoryBlock state;
    b->getStateInformation (state);
    auto c = makeInstance();
    c->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (c->getCustomName() == "Bajo");

    const int idB = b->getInstanceId();
    b.reset();
    CHECK (registry.find (idB) == nullptr);
    CHECK (registry.ids().size() == before + 2); // a y c
}

void testFeedsWhenWatched()
{
    std::printf ("Instancias: con la ventana cerrada solo se copia la salida si otra instancia esta mirando\n");
    auto& registry = FathomAudioProcessor::Registry::shared();
    auto a = makeInstance();
    double phase = 0.0;
    for (int i = 0; i < 4; ++i)
        playBlock (*a, { { 440.0, 0.3 } }, phase);
    CHECK (! registry.isWatched());
    CHECK (a->getOutputTap().getWritten() == 0); // nadie mira: no gasta en eso

    registry.startWatching();
    for (int i = 0; i < 4; ++i)
        playBlock (*a, { { 440.0, 0.3 } }, phase);
    registry.stopWatching();
    CHECK (a->getOutputTap().getWritten() == static_cast<std::uint64_t> (4 * blockSize));
}

// Varios cuadros de la GUI de 'watcher' mientras las dos instancias suenan.
void runFrames (fathom::gui::InstanceLink& link, FathomAudioProcessor& watcher, double& watcherPhase, const std::vector<std::pair<double, double>>& watcherSines,
                FathomAudioProcessor& other, double& otherPhase, const std::vector<std::pair<double, double>>& otherSines, int frames)
{
    const fathom::analysis::LogAxis axis { 20.0, 20000.0, 400 };
    const fathom::analysis::ViewSettings view;
    for (int frame = 0; frame < frames; ++frame)
    {
        for (int block = 0; block < 3; ++block) // ~32 ms de audio por cuadro
        {
            playBlock (watcher, watcherSines, watcherPhase);
            playBlock (other, otherSines, otherPhase);
        }
        link.update (axis, view, 1.0 / 30.0);
    }
}

void testCollisionsBetweenInstances()
{
    std::printf ("Instancias: los choques entre dos pistas de verdad (y que cortar en una los apaga)\n");
    auto a = makeInstance(); // la que mira
    auto b = makeInstance();
    fathom::gui::InstanceLink link (*a);
    auto& settings = a->getLinkSettings();
    settings.setShown (b->getInstanceId(), true);
    settings.setShown (a->getInstanceId(), true); // a sí misma no se ve (se quita sola)

    double phaseA = 0.0, phaseB = 0.0;
    const std::vector<std::pair<double, double>> trackA { { 100.0, 0.4 }, { 2000.0, 0.02 } }, trackB { { 100.0, 0.3 } };
    runFrames (link, *a, phaseA, trackA, *b, phaseB, trackB, 120); // 4 s
    CHECK (! settings.isShown (a->getInstanceId()));
    CHECK (link.getShown().size() == 1);
    if (link.getShown().size() == 1)
    {
        const auto& other = link.getShown().front();
        CHECK (other.id == b->getInstanceId() && other.sounding);
        CHECK (other.zones.size() == 1);
        if (! other.zones.empty())
        {
            const auto& zone = other.zones.front();
            std::printf ("  choque %.0f-%.0f Hz (%.2f), esta instancia %+.1f dB\n", zone.lowHz, zone.highHz, static_cast<double> (zone.amount),
                         static_cast<double> (zone.louderDb));
            CHECK (zone.lowHz < 100.0 && zone.highHz > 100.0);
            CHECK (std::abs (zone.louderDb - 2.5f) < 1.0f); // 0.4 contra 0.3: +2.5 dB
        }
    }

    // Cortar 100 Hz en la que mira (como haría el usuario): el choque se apaga en unos segundos.
    fathom::gui::BandEdits edits (*a);
    edits.begin();
    edits.set (0, fathom::params::enabled, 1.0f);
    edits.set (0, fathom::params::frequency, 100.0f);
    edits.set (0, fathom::params::gain, -20.0f);
    edits.end();
    runFrames (link, *a, phaseA, trackA, *b, phaseB, trackB, 240); // 8 s: el promedio lento se olvida del antes
    CHECK (link.getShown().size() == 1 && link.getShown().front().zones.empty());
}

void testEditingAnotherInstance()
{
    std::printf ("Instancias: editar otra desde aqui (su deshacer) y borrarla a mitad de la edicion\n");
    auto a = makeInstance();
    auto b = makeInstance();
    {
        fathom::gui::InstanceLink link (*a);
        a->getLinkSettings().setEditable (b->getInstanceId());
        double phaseA = 0.0, phaseB = 0.0;
        runFrames (link, *a, phaseA, { { 200.0, 0.2 } }, *b, phaseB, { { 300.0, 0.2 } }, 3);
        CHECK (link.getEditable() != nullptr);

        const auto band1Freq = fathom::params::band (0, fathom::params::frequency);
        auto* edits = link.editsForEditable();
        CHECK (edits != nullptr);
        if (edits != nullptr)
        {
            edits->begin();
            edits->set (0, fathom::params::enabled, 1.0f);
            edits->set (0, fathom::params::frequency, 250.0f);
            edits->end();
        }
        CHECK (std::abs (parameterValue (*b, band1Freq) - 250.0f) < 0.5f);
        CHECK (b->getBandSettings (0).enabled);
        CHECK (b->getEditHistory().canUndo());      // el paso quedó en SU historial
        CHECK (! a->getEditHistory().canUndo());    // no en el de la que mira
        b->getEditHistory().undo();
        CHECK (! b->getBandSettings (0).enabled);

        // La curva y los nodos de la editable siguen a sus parámetros.
        if (edits != nullptr)
        {
            edits->begin();
            edits->set (0, fathom::params::enabled, 1.0f);
            edits->set (0, fathom::params::gain, 6.0f);
            edits->end();
        }
        runFrames (link, *a, phaseA, { { 200.0, 0.2 } }, *b, phaseB, { { 300.0, 0.2 } }, 1);
        if (const auto* editable = link.getEditable(); editable != nullptr)
        {
            const auto at = static_cast<size_t> (std::round (fathom::analysis::LogAxis { 20.0, 20000.0, 400 }.position (b->getBandSettings (0).frequency)));
            std::printf ("  su curva en su banda 1: %+.2f dB (nodo %+.2f dB)\n", static_cast<double> (editable->curveDb[at]),
                         static_cast<double> (editable->nodeDb[0]));
            CHECK (std::abs (editable->nodeDb[0] - 6.0f) < 0.05f);
            CHECK (std::abs (editable->curveDb[at] - 6.0f) < 0.3f);
            CHECK (fathom::gui::InstanceLink::inUse (*editable, 0) && ! fathom::gui::InstanceLink::inUse (*editable, 5));
        }

        // A mitad de un arrastre, la otra se borra (se quita el plugin de la pista): la edición se cierra antes de que
        // desaparezca y la que mira sigue como si nada.
        edits = link.editsForEditable();
        if (edits != nullptr)
        {
            edits->begin();
            edits->set (0, fathom::params::frequency, 500.0f);
        }
        b.reset();
        CHECK (link.editsForEditable() == nullptr);
        CHECK (a->getLinkSettings().editable == -1 && a->getLinkSettings().shown.empty());
        const fathom::analysis::LogAxis axis { 20.0, 20000.0, 400 };
        link.update (axis, {}, 1.0 / 30.0);
        CHECK (link.getShown().empty() && link.getEditable() == nullptr);
    }
    // Sin la GUI que miraba, nadie mira.
    CHECK (! FathomAudioProcessor::Registry::shared().isWatched());
}

void testEditorWithOtherInstances()
{
    std::printf ("Instancias: abrir y cerrar la ventana con otras instancias vivas (y una que se va)\n");
    auto a = makeInstance();
    auto b = makeInstance();
    a->getLinkSettings().setEditable (b->getInstanceId());
    std::unique_ptr<juce::AudioProcessorEditor> editor (a->createEditor());
    CHECK (editor != nullptr);
    b.reset(); // con la ventana abierta y la otra marcada para editar
    editor.reset();
    CHECK (! FathomAudioProcessor::Registry::shared().isWatched());
}
} // namespace

int main()
{
    const juce::ScopedJuceInitialiser_GUI init;
    testRegistryAndNames();
    testFeedsWhenWatched();
    testCollisionsBetweenInstances();
    testEditingAnotherInstance();
    testEditorWithOtherInstances();
    if (failures > 0)
    {
        std::printf ("\n%d FALLO(S)\n", failures);
        return 1;
    }
    std::printf ("\nTodos los tests pasaron\n");
    return 0;
}
