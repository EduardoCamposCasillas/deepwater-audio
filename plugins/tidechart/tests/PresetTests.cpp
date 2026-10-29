// Tests de la Fase 9 (presets). Usan JUCE: compilan el procesador completo para probar exactamente lo que hace el plugin
// (parámetros, la progresión y las líneas como estado extra, el estado del proyecto, XML).
//
//   TidechartPresetTests           todos los tests
//   TidechartPresetTests --notes   imprime las notas de los presets de fábrica como las muestra el navegador
//
// Lo que se comprueba: cada preset de fábrica se lee sin avisos, trae notas y una categoría conocida, y al cargarlo suena
// (con un host a 120 BPM y una tecla abajo salen los acordes, el bajo y la melodía que pide, sin notas colgadas al
// soltar); la tónica NO cambia (decisión de la Fase 9) y el modo sí; el "*" ve los cambios de la progresión y de las
// líneas; Init deja todo como una instancia nueva; un preset del usuario con una melodía capturada vuelve igual; el
// nombre del preset y el "*" viajan con el proyecto.

#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "deepwater/theory/Text.h"
#include "Parameters.h"
#include "PluginProcessor.h"
#include "presets/TidechartPresets.h"

namespace
{
int failures = 0;

void check (bool condition, const juce::String& what)
{
    if (! condition)
    {
        ++failures;
        std::printf ("    FALLO: %s\n", what.toRawUTF8());
    }
}

namespace th = deepwater::theory;
namespace id = tidechart::params;

class TestHead final : public juce::AudioPlayHead
{
public:
    double ppq = 0.0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (true);
        info.setBpm (120.0);
        info.setTimeSignature (TimeSignature { 4, 4 });
        info.setPpqPosition (ppq);
        info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        return info;
    }
};

std::unique_ptr<TidechartAudioProcessor> makeProcessor()
{
    auto processor = std::make_unique<TidechartAudioProcessor>();
    processor->setRateAndBufferSizeDetails (48000.0, 512);
    processor->prepareToPlay (48000.0, 512);
    return processor;
}

int choiceIndex (TidechartAudioProcessor& processor, const char* parameterId)
{
    auto* parameter = processor.getParameters().getParameter (parameterId);
    return juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue()));
}

void setChoice (TidechartAudioProcessor& processor, const char* parameterId, int index)
{
    auto* parameter = processor.getParameters().getParameter (parameterId);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (index)));
}

juce::File tempUserFolder()
{
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TidechartPresetTests");
    folder.deleteRecursively();
    folder.createDirectory();
    return folder;
}

// Dos compases a 120 BPM con una tecla (C5) abajo y después soltada: cuántas notas salieron de cada nota MIDI y si quedó
// alguna sonando. Lo que sale por el canal 1 son acordes, bajo y melodía mezclados: se cuentan los note-on por altura.
struct Played
{
    int noteOns = 0;
    int lowest = 128, highest = -1;
    int hung = 0;
};

Played play (TidechartAudioProcessor& processor)
{
    TestHead head;
    processor.setPlayHead (&head);
    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    std::array<int, 128> sounding {};
    Played result;
    const int blocks = 2 * 4 * 24000 / 512; // 2 compases de negras de 24000 muestras
    for (int b = 0; b <= blocks + 1; ++b)
    {
        midi.clear();
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0);
        if (b == blocks)
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        processor.processBlock (audio, midi);
        for (const auto metadata : midi)
        {
            const auto m = metadata.getMessage();
            if (m.isNoteOn())
            {
                ++result.noteOns;
                ++sounding[static_cast<std::size_t> (m.getNoteNumber())];
                result.lowest = std::min (result.lowest, m.getNoteNumber());
                result.highest = std::max (result.highest, m.getNoteNumber());
            }
            else if (m.isNoteOff() && sounding[static_cast<std::size_t> (m.getNoteNumber())] > 0)
                --sounding[static_cast<std::size_t> (m.getNoteNumber())];
        }
        head.ppq += 512.0 / 24000.0;
    }
    for (const int n : sounding)
        result.hung += n;
    processor.setPlayHead (nullptr);
    return result;
}

const deepwater::presets::Preset* findPreset (deepwater::presets::PresetManager& manager, const juce::String& name)
{
    for (const auto& preset : manager.getPresets())
        if (preset.name == name)
            return &preset;
    return nullptr;
}

// --- Tests -----------------------------------------------------------------------------------------------------------

void testFactoryPresets()
{
    std::printf ("Presets de fabrica: se leen sin avisos, con notas y categoria; al cargarlos la tonica se queda, suena lo "
                 "que piden y nada queda colgado\n");

    auto processor = makeProcessor();
    auto& manager = processor->getPresetManager();
    const auto& presets = manager.getPresets();
    const auto categories = tidechart::presets::categoryNames();
    int factory = 0;
    std::set<juce::String> names;
    for (const auto& preset : presets)
        if (preset.factory)
        {
            ++factory;
            const auto tag = "\"" + preset.name + "\"";
            check (names.insert (preset.name).second, tag + ": nombre repetido");
            check (categories.contains (preset.category) && preset.category != "Otros", tag + ": categoria desconocida " + preset.category);
            check (preset.notes.trim().length() > (preset.category == "Init" ? 100 : 600), tag + ": notas demasiado cortas");
        }
    check (factory >= 5, "al menos Init y uno por categoria (hay " + juce::String (factory) + ")");
    for (const auto& category : categories)
        if (category != "Otros")
        {
            bool found = false;
            for (const auto& preset : presets)
                found = found || (preset.factory && preset.category == category);
            check (found, "ningun preset en la categoria " + category);
        }

    for (const auto& preset : presets)
    {
        if (! preset.factory)
            continue;
        const auto tag = "\"" + preset.name + "\"";
        setChoice (*processor, id::keyTonic, 4); // Mi: la tónica de "tu canción"
        const auto problems = manager.load (preset);
        check (problems.isEmpty(), tag + ": avisos al cargar: " + problems.joinIntoString ("; "));
        check (choiceIndex (*processor, id::keyTonic) == 4, tag + ": cambio la tonica (no debe)");
        check (! manager.isModified() && manager.getCurrentName() == preset.name, tag + ": recien cargado tiene \"*\" o otro nombre");

        // La progresión y las líneas: las del archivo, generadas con la tónica de ahora.
        const auto composition = tidechart::presets::compositionFromTree (preset.extraState, TidechartAudioProcessor::defaultProgression(), nullptr);
        check (processor->getProgression() == composition.progression, tag + ": la progresion no es la del preset");
        const auto lines = processor->getLines();
        check (lines.bassSettings == composition.bass && lines.melodySettings == composition.melody, tag + ": los ajustes de las lineas");
        const auto key = processor->currentChordSettings (false).key;
        check (key.tonic.pitchClass() == 4, tag + ": la tonalidad del procesador no esta en Mi");
        check (lines.bass == tidechart::computeBass (lines.bassSettings, composition.progression, key)
                   && lines.melody == tidechart::computeMelody (lines.melodySettings, composition.progression, key),
               tag + ": las lineas no se generaron con la tonalidad de ahora");

        // Fase 9.2: cada acorde es de la escala o de un modo paralelo (las notas lo explican como "prestado"): nada que no
        // se pueda encontrar en la paleta o entender con el teclado de la escala.
        for (const auto& step : composition.progression.steps)
        {
            const auto chord = th::chordFor (step.numeral, key);
            check (th::isDiatonic (chord, key) || ! th::modesContaining (chord, key.tonic).empty(),
                   tag + ": el " + juce::String (th::romanNumeralText (step.numeral)) + " no es de ningun modo con esta tonica");
        }

        // Suena: en el modo progresión, al menos un acorde por compás; con bajo, algo por debajo de C4 (48).
        const bool progressionMode = choiceIndex (*processor, id::playMode) == 1;
        const auto played = play (*processor);
        check (played.hung == 0, tag + ": quedaron " + juce::String (played.hung) + " notas colgadas");
        check (played.noteOns >= (progressionMode ? 6 : 3), tag + ": casi no sono (" + juce::String (played.noteOns) + " notas)");
        if (choiceIndex (*processor, id::playBass) == 1 && progressionMode)
            check (played.lowest < 48, tag + ": el bajo no sono (la mas grave fue " + juce::String (played.lowest) + ")");
        if (preset.category != "Init")
            check (progressionMode, tag + ": un preset de fabrica deberia sonar en el modo progresion");
    }
}

void testModifiedAndInit()
{
    std::printf ("El \"*\": cambiar la progresion o las lineas lo enciende, volver a cargar lo apaga; Init = una instancia nueva\n");

    auto processor = makeProcessor();
    auto& manager = processor->getPresetManager();
    check (manager.getCurrentName() == "Init" && ! manager.isModified(), "una instancia nueva es Init sin \"*\"");

    const auto* frigio = findPreset (manager, "Perreo Frigio");
    check (frigio != nullptr, "existe Perreo Frigio");
    if (frigio == nullptr)
        return;
    manager.load (*frigio);
    check (choiceIndex (*processor, id::keyMode) == static_cast<int> (th::Mode::phrygian), "Perreo Frigio pone el modo frigio");

    auto progression = processor->getProgression();
    progression.steps[1].beats = 4;
    processor->setProgression (progression);
    check (manager.isModified(), "cambiar la duracion de un paso pone el \"*\"");
    manager.load (*frigio);
    check (! manager.isModified(), "cargarlo otra vez lo quita");

    auto melody = processor->getLines().melodySettings;
    melody.generator.seed = 99;
    processor->setMelodySettings (melody);
    check (manager.isModified(), "otra melodia (Otra) pone el \"*\"");
    manager.load (*frigio);

    auto bass = processor->getLines().bassSettings;
    bass.generator.density = 0.9;
    processor->setBassSettings (bass);
    check (manager.isModified(), "otra densidad del bajo pone el \"*\"");

    // Init: todo como una instancia nueva, menos la tónica (que ningún preset toca).
    setChoice (*processor, id::keyTonic, 2);
    manager.loadInit();
    auto fresh = makeProcessor();
    check (processor->getProgression() == TidechartAudioProcessor::defaultProgression(), "Init: la progresion de fabrica");
    const auto now = processor->getLines(), then = fresh->getLines();
    check (now.bassSettings == then.bassSettings && now.melodySettings == then.melodySettings, "Init: las lineas por defecto");
    for (const auto* parameterId : { id::keyMode, id::chordSize, id::playMode, id::voicing, id::rhythm, id::playChords, id::playBass, id::playMelody })
        check (choiceIndex (*processor, parameterId) == choiceIndex (*fresh, parameterId), juce::String ("Init: ") + parameterId + " no volvio a su valor");
    check (choiceIndex (*processor, id::keyTonic) == 2, "Init no cambia la tonica");
    check (! manager.isModified(), "Init recien cargado: sin \"*\"");
}

void testUserPresetRoundTrip()
{
    std::printf ("Preset del usuario: progresion de 16 pasos, lineas y una melodia capturada vuelven iguales; el XML es legible\n");

    auto processor = makeProcessor();
    auto& manager = processor->getPresetManager();
    manager.setUserFolder (tempUserFolder());

    th::Progression progression;
    const auto parsed = th::parseProgression ("i bVI bIII bVII iv v i bII bVI bVII i iv9 bIIImaj7 bVII7 bIIsus2 i");
    for (std::size_t k = 0; k < parsed->size(); ++k)
        progression.steps.push_back ({ (*parsed)[k], 1 + static_cast<int> (k % 4) });
    processor->setProgression (progression);
    setChoice (*processor, id::rhythm, 7);
    setChoice (*processor, id::keyMode, static_cast<int> (th::Mode::dorian));
    auto bass = processor->getLines().bassSettings;
    bass.generator.style = th::BassStyle::octaves;
    bass.generator.seed = 5;
    processor->setBassSettings (bass);
    th::Line captured;
    captured.length = 64;
    captured.notes.push_back ({ 0, 4, 69, 100 });
    captured.notes.push_back ({ 6, 2, 72, 90 });
    processor->captureMelody (captured);

    const auto result = manager.save ("Mi Perreo", juce::String::fromUTF8 ("Reggaet\xc3\xb3n"), "Yo", "Notas de prueba");
    check (result.wasOk(), "guardar: " + result.getErrorMessage());
    const auto file = manager.getUserFolder().getChildFile ("Mi Perreo.tcpreset");
    check (file.existsAsFile(), "el archivo quedo en la carpeta del usuario");
    const auto text = file.loadFileAsString();
    check (text.contains ("<Composition>") && text.contains ("chord=\"bIIsus2\"") && text.contains ("style=\"octaves\"")
               && text.contains ("<Source length=\"64\">") && text.contains ("note=\"A5\""),
           "el XML trae la progresion, el bajo y la melodia capturada, legibles");
    check (! text.contains ("keyTonic") && ! text.contains ("\"bypass\"") && ! text.contains ("keyMapping"), "ni la tonica, ni el bypass, ni las teclas");
    check (! text.contains ("<Bass style=\"octaves\" density=\"0.5\" low=\"28\" high=\"50\" seed=\"5\" length"),
           "las notas generadas del bajo no se guardan (solo sus ajustes)");

    // Otra instancia, en la misma carpeta: el preset vuelve igual.
    auto other = makeProcessor();
    auto& otherManager = other->getPresetManager();
    otherManager.setUserFolder (manager.getUserFolder());
    otherManager.rescan();
    const auto* mine = findPreset (otherManager, "Mi Perreo");
    check (mine != nullptr && ! mine->factory, "el preset del usuario aparece en la biblioteca");
    if (mine == nullptr)
        return;
    const auto problems = otherManager.load (*mine);
    check (problems.isEmpty(), "sin avisos: " + problems.joinIntoString ("; "));
    check (other->getProgression() == progression, "la progresion de 16 pasos vuelve igual");
    check (choiceIndex (*other, id::rhythm) == 7 && choiceIndex (*other, id::keyMode) == static_cast<int> (th::Mode::dorian), "ritmo y modo");
    const auto lines = other->getLines();
    check (lines.bassSettings == processor->getLines().bassSettings, "los ajustes del bajo");
    check (lines.melodySettings.hasSource && lines.melodySettings.source == captured && lines.melody == captured, "la melodia capturada, nota a nota");
    check (! otherManager.isModified(), "recien cargado: sin \"*\"");

    // Un grado que no se entiende: aviso, y el resto se carga.
    juce::XmlElement broken ("Composition");
    auto* steps = broken.createNewChildElement ("Progression");
    steps->createNewChildElement ("Step")->setAttribute ("chord", "i");
    steps->createNewChildElement ("Step")->setAttribute ("chord", "xyz");
    juce::StringArray warnings;
    const auto composition = tidechart::presets::compositionFromTree (juce::ValueTree::fromXml (broken), {}, &warnings);
    check (composition.progression.steps.size() == 1 && warnings.size() == 1, "un grado roto: un aviso y se salta");
    manager.getUserFolder().deleteRecursively();
}

void testProjectState()
{
    std::printf ("Estado del proyecto: el preset cargado y su \"*\" viajan con el proyecto; el XML del proyecto no cambio\n");

    auto processor = makeProcessor();
    auto& manager = processor->getPresetManager();
    const auto* trap = findPreset (manager, juce::String::fromUTF8 ("Trap Menor Arm\xc3\xb3nica"));
    check (trap != nullptr, "existe Trap Menor Armonica");
    if (trap == nullptr)
        return;
    manager.load (*trap);
    juce::MemoryBlock clean;
    processor->getStateInformation (clean);

    auto reloaded = makeProcessor();
    reloaded->setStateInformation (clean.getData(), static_cast<int> (clean.getSize()));
    check (reloaded->getPresetManager().getCurrentName() == trap->name && ! reloaded->getPresetManager().isModified(),
           "el nombre del preset viaja con el proyecto, sin \"*\"");
    check (reloaded->getProgression() == processor->getProgression() && reloaded->getLines().melody == processor->getLines().melody,
           "la progresion y las notas de las lineas, iguales");

    auto progression = processor->getProgression();
    progression.steps.push_back ({ *th::parseRomanNumeral ("bVII"), 4 });
    processor->setProgression (progression);
    juce::MemoryBlock edited;
    processor->getStateInformation (edited);
    reloaded->setStateInformation (edited.getData(), static_cast<int> (edited.getSize()));
    check (reloaded->getPresetManager().isModified(), "un proyecto guardado con cambios vuelve con \"*\"");

    // El XML del proyecto: progresión y líneas como antes de la Fase 9 (con las notas), más las propiedades del preset.
    const auto xml = juce::AudioProcessor::getXmlFromBinary (edited.getData(), static_cast<int> (edited.getSize()));
    check (xml != nullptr && xml->getChildByName ("Progression") != nullptr && xml->getChildByName ("Lines") != nullptr
               && xml->getChildByName ("Lines")->getChildByName ("Melody")->getChildByName ("Note") != nullptr,
           "el proyecto sigue guardando la progresion y las notas de las lineas");
    check (xml != nullptr && xml->getChildByName ("Composition") == nullptr, "el proyecto no guarda un <Composition> aparte");
}

void printNotes()
{
    auto processor = makeProcessor();
    for (const auto& preset : processor->getPresetManager().getPresets())
        if (preset.factory)
            std::printf ("=== %s  (%s)\n%s\n\n", preset.name.toRawUTF8(), preset.category.toRawUTF8(), preset.notes.toRawUTF8());
}
} // namespace

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI init;
    if (argc == 2 && juce::String (argv[1]) == "--notes")
    {
        printNotes();
        return 0;
    }

    testFactoryPresets();
    testModifiedAndInit();
    testUserPresetRoundTrip();
    testProjectState();

    if (failures == 0)
        std::printf ("\nTodos los tests de presets de Tidechart pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);
    return failures == 0 ? 0 : 1;
}
