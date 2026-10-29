#include "presets/TidechartPresets.h"

#include <algorithm>
#include <string>
#include <vector>

#include <BinaryData.h>

#include "deepwater/theory/Text.h"
#include "Parameters.h"

namespace tidechart::presets
{
namespace
{
namespace th = deepwater::theory;

// Una línea, legible: <Note at="0" length="3" note="A2" velocity="100"/> (en semicorcheas desde el principio de la
// vuelta; la nota con el nombre de FL, C5 = 60). Así un preset o el servidor MCP la pueden escribir.
void writeLine (juce::XmlElement& parent, const th::Line& line)
{
    parent.setAttribute ("length", line.length);
    for (const auto& n : line.notes)
    {
        auto* note = parent.createNewChildElement ("Note");
        note->setAttribute ("at", n.start);
        note->setAttribute ("length", n.length);
        note->setAttribute ("note", juce::String (th::midiNoteName (n.note)));
        note->setAttribute ("velocity", n.velocity);
    }
}

th::Line readLine (const juce::XmlElement& parent)
{
    th::Line line;
    line.length = juce::jlimit (0, th::maxLineSixteenths, parent.getIntAttribute ("length", 0));
    if (line.length <= 0)
        return {};
    std::vector<th::LineNote> notes;
    for (auto* note : parent.getChildWithTagNameIterator ("Note"))
    {
        const auto pitch = th::parseMidiNote (note->getStringAttribute ("note").toStdString());
        if (! pitch || *pitch < 0 || *pitch > 127)
            continue;
        const int start = juce::jlimit (0, line.length - 1, note->getIntAttribute ("at", 0));
        notes.push_back ({ start, juce::jlimit (1, line.length - start, note->getIntAttribute ("length", 1)), *pitch,
                           juce::jlimit (1, 127, note->getIntAttribute ("velocity", 100)) });
    }
    std::sort (notes.begin(), notes.end(), [] (const th::LineNote& a, const th::LineNote& b) {
        return a.start != b.start ? a.start < b.start : a.note < b.note;
    });
    for (const auto& n : notes)
        line.notes.push_back (n);
    return line;
}

// Un estilo por su nombre ("dembow"); si no se reconoce, por su índice; si tampoco, el de por defecto.
template <std::size_t N>
int styleIndex (const std::array<std::string_view, N>& names, const juce::String& text, int fallback)
{
    for (std::size_t i = 0; i < N; ++i)
        if (text == juce::String (std::string (names[i])))
            return static_cast<int> (i);
    if (text.isNotEmpty() && text.containsOnly ("0123456789"))
        return juce::jlimit (0, static_cast<int> (N) - 1, text.getIntValue());
    return fallback;
}
} // namespace

void writeProgression (juce::XmlElement& parent, const th::Progression& progression)
{
    auto* element = parent.createNewChildElement (progressionTag);
    for (const auto& step : progression.steps)
    {
        auto* stepElement = element->createNewChildElement ("Step");
        stepElement->setAttribute ("chord", juce::String (th::romanNumeralText (step.numeral)));
        stepElement->setAttribute ("beats", step.beats);
    }
}

th::Progression readProgression (const juce::XmlElement& element, juce::StringArray* problems)
{
    th::Progression result;
    for (auto* stepElement : element.getChildWithTagNameIterator ("Step"))
    {
        const auto text = stepElement->getStringAttribute ("chord");
        if (const auto numeral = th::parseRomanNumeral (text.toStdString()))
            result.steps.push_back ({ *numeral, th::Progression::clampBeats (stepElement->getIntAttribute ("beats", 4)) });
        else if (problems != nullptr)
            problems->add (juce::String::fromUTF8 ("progresi\xc3\xb3n: no se entiende el grado \"") + text + "\"");
    }
    return result;
}

void writeLines (juce::XmlElement& parent, const tidechart::Lines& lines, bool withNotes)
{
    auto* linesElement = parent.createNewChildElement (linesTag);
    auto* bass = linesElement->createNewChildElement ("Bass");
    const auto& b = lines.bassSettings.generator;
    bass->setAttribute ("style", juce::String (std::string (th::bassStyleNames[static_cast<std::size_t> (b.style)])));
    bass->setAttribute ("density", b.density);
    bass->setAttribute ("low", b.low);
    bass->setAttribute ("high", b.high);
    bass->setAttribute ("seed", static_cast<int> (b.seed));
    if (withNotes)
        writeLine (*bass, lines.bass);

    auto* melody = linesElement->createNewChildElement ("Melody");
    const auto& m = lines.melodySettings;
    melody->setAttribute ("style", juce::String (std::string (th::melodyStyleNames[static_cast<std::size_t> (m.generator.style)])));
    melody->setAttribute ("density", m.generator.density);
    melody->setAttribute ("low", m.generator.low);
    melody->setAttribute ("high", m.generator.high);
    melody->setAttribute ("seed", static_cast<int> (m.generator.seed));
    melody->setAttribute ("variation", m.variation);
    melody->setAttribute ("variationSeed", static_cast<int> (m.variationSeed));
    if (m.hasSource)
        writeLine (*melody->createNewChildElement ("Source"), m.source);
    if (withNotes)
        writeLine (*melody, lines.melody);
}

ReadLines readLines (const juce::XmlElement& element)
{
    ReadLines result;
    if (auto* bass = element.getChildByName ("Bass"))
    {
        auto& b = result.lines.bassSettings.generator;
        b.style = static_cast<th::BassStyle> (styleIndex (th::bassStyleNames, bass->getStringAttribute ("style"), static_cast<int> (b.style)));
        b.density = juce::jlimit (0.0, 1.0, bass->getDoubleAttribute ("density", b.density));
        b.low = juce::jlimit (0, 127, bass->getIntAttribute ("low", b.low));
        b.high = juce::jlimit (0, 127, bass->getIntAttribute ("high", b.high));
        b.seed = static_cast<std::uint32_t> (bass->getIntAttribute ("seed", 1));
        result.lines.bass = readLine (*bass);
        result.hasNotes = bass->hasAttribute ("length");
    }
    if (auto* melody = element.getChildByName ("Melody"))
    {
        auto& m = result.lines.melodySettings;
        m.generator.style = static_cast<th::MelodyStyle> (
            styleIndex (th::melodyStyleNames, melody->getStringAttribute ("style"), static_cast<int> (m.generator.style)));
        m.generator.density = juce::jlimit (0.0, 1.0, melody->getDoubleAttribute ("density", m.generator.density));
        m.generator.low = juce::jlimit (0, 127, melody->getIntAttribute ("low", m.generator.low));
        m.generator.high = juce::jlimit (0, 127, melody->getIntAttribute ("high", m.generator.high));
        m.generator.seed = static_cast<std::uint32_t> (melody->getIntAttribute ("seed", 1));
        m.variation = juce::jlimit (0.0, 1.0, melody->getDoubleAttribute ("variation", 0.0));
        m.variationSeed = static_cast<std::uint32_t> (melody->getIntAttribute ("variationSeed", 1));
        if (auto* source = melody->getChildByName ("Source"))
        {
            m.hasSource = true;
            m.source = readLine (*source);
        }
        result.lines.melody = readLine (*melody);
        result.hasNotes = result.hasNotes || melody->hasAttribute ("length");
    }
    return result;
}

juce::ValueTree compositionToTree (const th::Progression& progression, const tidechart::Lines& lines)
{
    juce::XmlElement xml (compositionTag);
    writeProgression (xml, progression);
    writeLines (xml, lines, false);
    return juce::ValueTree::fromXml (xml);
}

Composition compositionFromTree (const juce::ValueTree& tree, const th::Progression& initProgression, juce::StringArray* problems)
{
    Composition result { initProgression, {}, {} };
    if (! tree.isValid())
        return result;
    const auto xml = tree.createXml();
    if (xml == nullptr || ! xml->hasTagName (compositionTag))
    {
        if (problems != nullptr)
            problems->add (juce::String ("se esperaba <") + compositionTag + ">");
        return result;
    }
    if (auto* element = xml->getChildByName (progressionTag))
        result.progression = readProgression (*element, problems);
    if (auto* element = xml->getChildByName (linesTag))
    {
        const auto read = readLines (*element);
        result.bass = read.lines.bassSettings;
        result.melody = read.lines.melodySettings;
    }
    return result;
}

juce::StringArray categoryNames()
{
    // "Init" primero y "Otros" al final (convención de libs/presets). Las de la hoja de ruta: por género, más "Oscuro"
    // (oscuro / experimental: lo que se sale, frigio, cromático, pedales raros; el nombre corto cabe en el navegador).
    return { "Init", juce::String::fromUTF8 ("Reggaet\xc3\xb3n"), "Oscuro", "R&B", "Trap", "Otros" };
}

deepwater::presets::PresetFormat format (std::function<juce::ValueTree()> capture, std::function<void (const Composition&)> apply,
                                         th::Progression initProgression)
{
    deepwater::presets::PresetFormat result;
    result.productName = "Tidechart";
    result.rootTag = "TidechartPreset";
    result.fileExtension = fileExtension;
    result.categories = categoryNames();
    result.userFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Tidechart").getChildFile ("Presets");
    result.excludedParameters.addArray ({ tidechart::params::bypass, tidechart::params::keyTonic, tidechart::params::keyMapping });

    result.extraStateTag = compositionTag;
    result.captureExtraState = std::move (capture);
    result.applyExtraState = [apply = std::move (apply), initProgression] (const juce::ValueTree& tree) {
        juce::StringArray problems;
        apply (compositionFromTree (tree, initProgression, &problems));
        return problems;
    };

    result.factoryFiles = [] {
        std::vector<deepwater::presets::FactoryFile> files;
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            int size = 0;
            const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
            files.push_back ({ BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i]),
                               juce::String::fromUTF8 (data, size) });
        }
        return files;
    };
    return result;
}

} // namespace tidechart::presets
