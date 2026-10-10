#include "presets/PresetManager.h"

#include <algorithm>
#include <cmath>

#include <BinaryData.h>

namespace undertow::presets
{
namespace
{
constexpr const char* rootTag = "UndertowPreset";
constexpr const char* notesTag = "Notes";
constexpr const char* parametersTag = "Parameters";
constexpr const char* parameterTag = "PARAM";

// Propiedades del estado del proyecto (junto a "uiZoom"). No son parámetros: el host no las ve ni las automatiza.
constexpr const char* presetNameProperty = "presetName";
constexpr const char* presetFactoryProperty = "presetFactory";
constexpr const char* presetModifiedProperty = "presetModified";

// Por debajo de esto dos valores 0..1 se consideran iguales (escribir y releer un float en texto deja ~1e-7 de error).
constexpr float modifiedTolerance = 1.0e-4f;

// Las notas se escriben con sangría dentro del XML: se quita la de cada línea para mostrarlas limpias.
juce::String cleanNotes (const juce::String& text)
{
    juce::StringArray lines;
    lines.addLines (text);
    for (auto& line : lines)
        line = line.trim();
    return lines.joinIntoString ("\n").trim();
}

// Las notas de fábrica se escriben en el XML partidas a ~118 columnas. En el navegador (más estrecho) esos saltos
// cortarían los párrafos a la mitad: se juntan las líneas de cada párrafo y solo se respetan los saltos "de verdad":
// línea vacía, viñeta ("- "), lista numerada ("1. ") o una etiqueta corta al principio de una frase nueva ("Tócalo:",
// "Viene de:"). Las notas que escribe el usuario no pasan por aquí: se muestran tal como las escribió.
bool startsNewLine (const juce::String& line, const juce::String& previous)
{
    if (line.isEmpty() || previous.isEmpty() || line.startsWith ("- "))
        return true;
    const auto digits = line.initialSectionContainingOnly ("0123456789");
    if (digits.isNotEmpty() && line.substring (digits.length()).startsWith (". "))
        return true;

    // Etiqueta: hasta 4 palabras sin números ni signos, con mayúscula, y la línea anterior terminó la frase.
    // ("Mira el punto blanco del / Cutoff: va y viene" NO es una etiqueta: la frase anterior sigue abierta.)
    const int colon = line.indexOfChar (':');
    const auto label = line.substring (0, juce::jmax (0, colon));
    const bool previousEnded = previous.getLastCharacter() == '.' || previous.getLastCharacter() == ')'
                               || previous.getLastCharacter() == ':' || previous.getLastCharacter() == '?';
    return colon > 0 && previousEnded && juce::CharacterFunctions::isUpperCase (line[0])
           && ! label.containsAnyOf ("0123456789()[],.%+-=/\"") && juce::StringArray::fromTokens (label, " ", {}).size() <= 4;
}

juce::String reflowNotes (const juce::String& text)
{
    juce::StringArray lines;
    lines.addLines (text);
    juce::StringArray paragraphs;
    for (const auto& line : lines)
    {
        if (paragraphs.isEmpty() || startsNewLine (line, paragraphs[paragraphs.size() - 1]))
            paragraphs.add (line);
        else
            paragraphs.getReference (paragraphs.size() - 1) << " " << line;
    }
    return paragraphs.joinIntoString ("\n");
}

bool looksNumeric (const juce::String& text)
{
    return text.isNotEmpty() && text.containsOnly ("0123456789.-+eE");
}

// Texto del archivo → valor real del parámetro. Números tal cual; en listas, también el nombre de la opción;
// en interruptores, también On/Off.
std::optional<float> parseValue (const juce::RangedAudioParameter& parameter, const juce::String& text)
{
    const auto trimmed = text.trim();
    if (looksNumeric (trimmed))
        return trimmed.getFloatValue();

    if (const auto* choice = dynamic_cast<const juce::AudioParameterChoice*> (&parameter))
    {
        const int index = choice->choices.indexOf (trimmed, true);
        if (index >= 0)
            return static_cast<float> (index);
        return std::nullopt;
    }
    if (dynamic_cast<const juce::AudioParameterBool*> (&parameter) != nullptr)
    {
        const auto lower = trimmed.toLowerCase();
        if (lower == "on" || lower == "true")
            return 1.0f;
        if (lower == "off" || lower == "false")
            return 0.0f;
    }
    return std::nullopt;
}

// Números cortos y exactos para un humano: "450", "0.002", "0.6667".
juce::String formatValue (float value)
{
    return juce::String::formatted ("%.6g", static_cast<double> (value));
}

bool comesBefore (const Preset& a, const Preset& b)
{
    if (a.categoryIndex() != b.categoryIndex())
        return a.categoryIndex() < b.categoryIndex();
    if (a.factory != b.factory)
        return a.factory; // en cada categoría, primero los de fábrica
    return a.name.compareNatural (b.name) < 0;
}
} // namespace

int Preset::categoryIndex() const noexcept
{
    for (size_t i = 0; i < categoryNames.size(); ++i)
        if (category.equalsIgnoreCase (categoryNames[i]))
            return static_cast<int> (i);
    return static_cast<int> (categoryNames.size()) - 1;
}

std::optional<Preset> parsePreset (const juce::XmlElement& xml, juce::String& error)
{
    if (! xml.hasTagName (rootTag))
    {
        error = "no es un preset de Undertow";
        return std::nullopt;
    }
    if (xml.getIntAttribute ("formatVersion", 1) > formatVersion)
    {
        error = "lo guardó una versión más nueva de Undertow";
        return std::nullopt;
    }

    Preset preset;
    preset.name = xml.getStringAttribute ("name").trim();
    if (preset.name.isEmpty())
    {
        error = "el preset no tiene nombre";
        return std::nullopt;
    }
    preset.category = xml.getStringAttribute ("category", "Otros").trim();
    preset.author = xml.getStringAttribute ("author").trim();
    if (const auto* notes = xml.getChildByName (notesTag))
        preset.notes = cleanNotes (notes->getAllSubText());

    if (const auto* list = xml.getChildByName (parametersTag))
        for (const auto* parameter : list->getChildWithTagNameIterator (parameterTag))
            preset.values.emplace_back (parameter->getStringAttribute ("id"), parameter->getStringAttribute ("value"));
    return preset;
}

std::unique_ptr<juce::XmlElement> presetToXml (const Preset& preset)
{
    auto xml = std::make_unique<juce::XmlElement> (rootTag);
    xml->setAttribute ("formatVersion", formatVersion);
    xml->setAttribute ("name", preset.name);
    xml->setAttribute ("category", preset.category);
    if (preset.author.isNotEmpty())
        xml->setAttribute ("author", preset.author);
    xml->createNewChildElement (notesTag)->addTextElement (preset.notes);

    auto* list = xml->createNewChildElement (parametersTag);
    for (const auto& [id, value] : preset.values)
    {
        auto* parameter = list->createNewChildElement (parameterTag);
        parameter->setAttribute ("id", id);
        parameter->setAttribute ("value", value);
    }
    return xml;
}

// =====================================================================================================================

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : state (s)
{
    for (auto* parameter : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            parameters.push_back (ranged);

    snapshot = currentValues(); // una instancia nueva tiene los valores por defecto = "Init"
}

const std::vector<Preset>& PresetManager::getPresets()
{
    ensureScanned();
    return presets;
}

void PresetManager::ensureScanned()
{
    if (! scanned)
        rescan();
}

void PresetManager::rescan()
{
    scanned = true;
    presets.clear();

    // Fábrica: los .uwpreset de Presets/Factory, compilados dentro del plugin (juce_add_binary_data).
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const juce::String original = BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i]);
        if (! original.endsWithIgnoreCase (fileExtension))
            continue;

        int size = 0;
        const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
        juce::String error;
        if (auto xml = juce::parseXML (juce::String::fromUTF8 (data, size)))
            if (auto preset = parsePreset (*xml, error))
            {
                preset->factory = true;
                preset->notes = reflowNotes (preset->notes);
                presets.push_back (std::move (*preset));
            }
        jassert (error.isEmpty()); // un preset de fábrica roto es un bug: lo detecta UndertowPresetTests
    }

    // Usuario: cualquier .uwpreset dentro de la carpeta (también en subcarpetas que cree el usuario).
    const auto& folder = userFolder;
    if (folder.isDirectory())
        for (const auto& entry : juce::RangedDirectoryIterator (folder, true, juce::String ("*") + fileExtension))
        {
            juce::String error;
            if (auto xml = juce::parseXML (entry.getFile()))
                if (auto preset = parsePreset (*xml, error))
                {
                    preset->factory = false;
                    preset->file = entry.getFile();
                    presets.push_back (std::move (*preset));
                }
        }

    std::stable_sort (presets.begin(), presets.end(), comesBefore);
}

void PresetManager::setUserFolder (const juce::File& folder)
{
    userFolder = folder;
    rescan();
}

juce::File PresetManager::defaultUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Undertow").getChildFile ("Presets");
}

std::vector<float> PresetManager::resolve (const Preset& preset, juce::StringArray* problems) const
{
    // Primero todo a su valor por defecto; después, lo que dice el preset.
    std::vector<float> values;
    values.reserve (parameters.size());
    for (const auto* parameter : parameters)
        values.push_back (parameter->getDefaultValue());

    for (const auto& [id, text] : preset.values)
    {
        const auto it = std::find_if (parameters.begin(), parameters.end(),
                                      [&id] (const auto* parameter) { return parameter->paramID == id; });
        if (it == parameters.end())
        {
            if (problems != nullptr)
                problems->add ("parámetro desconocido: " + id);
            continue;
        }

        const auto& parameter = **it;
        const auto value = parseValue (parameter, text);
        if (! value.has_value())
        {
            if (problems != nullptr)
                problems->add (id + ": valor no válido \"" + text + "\"");
            continue;
        }

        const auto& range = parameter.getNormalisableRange();
        if (problems != nullptr && (*value < range.start - 1.0e-4f || *value > range.end + 1.0e-4f))
            problems->add (id + ": " + text + " fuera del rango " + formatValue (range.start) + " .. " + formatValue (range.end));
        values[static_cast<size_t> (std::distance (parameters.begin(), it))] = parameter.convertTo0to1 (*value);
    }
    return values;
}

void PresetManager::apply (const std::vector<float>& normalised)
{
    // setValueNotifyingHost: el parámetro cambia (el audio lo lee del atómico y lo suaviza) y FL se entera.
    // Solo se tocan los que cambian, para no llenar el historial de FL de cambios que no lo son.
    for (size_t i = 0; i < parameters.size(); ++i)
        if (std::abs (parameters[i]->getValue() - normalised[i]) > 1.0e-7f)
            parameters[i]->setValueNotifyingHost (normalised[i]);
}

juce::StringArray PresetManager::load (const Preset& preset)
{
    juce::StringArray problems;
    const auto values = resolve (preset, &problems);
    apply (values);
    setCurrent (preset.name, preset.factory, false);
    return problems;
}

juce::StringArray PresetManager::loadInit()
{
    ensureScanned();
    for (const auto& preset : presets)
        if (preset.factory && preset.category == "Init")
            return load (preset);

    // Sin el archivo de fábrica (no debería pasar), Init = todos los valores por defecto.
    Preset init;
    init.name = "Init";
    init.factory = true;
    return load (init);
}

int PresetManager::indexOfCurrent() const
{
    const juce::ScopedLock sl (lock);
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].factory == currentFactory && presets[i].name == currentName)
            return static_cast<int> (i);
    return -1;
}

const Preset* PresetManager::step (int delta)
{
    ensureScanned();
    if (presets.empty())
        return nullptr;

    const int count = static_cast<int> (presets.size());
    const int current = indexOfCurrent();
    const int next = current < 0 ? (delta > 0 ? 0 : count - 1) : ((current + delta) % count + count) % count;
    load (presets[static_cast<size_t> (next)]);
    return &presets[static_cast<size_t> (next)];
}

bool PresetManager::userPresetExists (const juce::String& name)
{
    ensureScanned();
    return std::any_of (presets.begin(), presets.end(),
                        [&name] (const Preset& p) { return ! p.factory && p.name.equalsIgnoreCase (name.trim()); });
}

bool PresetManager::isFactoryName (const juce::String& name)
{
    ensureScanned();
    return std::any_of (presets.begin(), presets.end(),
                        [&name] (const Preset& p) { return p.factory && p.name.equalsIgnoreCase (name.trim()); });
}

juce::Result PresetManager::save (const juce::String& name, const juce::String& category, const juce::String& author,
                                  const juce::String& notes)
{
    const auto cleanName = name.trim();
    if (cleanName.isEmpty())
        return juce::Result::fail ("Escribe un nombre para el preset.");
    if (isFactoryName (cleanName))
        return juce::Result::fail (juce::String::fromUTF8 ("Ese nombre ya lo usa un preset de f\xc3\xa1" "brica."));

    Preset preset;
    preset.name = cleanName;
    preset.category = category;
    preset.author = author.trim();
    preset.notes = notes.trim();

    // Un preset del usuario guarda TODOS los parámetros (no solo los que cambian): así no depende de cuáles sean los
    // valores por defecto de esta versión. Las listas se guardan por índice, que es lo que nunca cambia.
    for (const auto* parameter : parameters)
        preset.values.emplace_back (parameter->paramID, formatValue (parameter->convertFrom0to1 (parameter->getValue())));

    // Si ya existe con ese nombre (aunque esté en una subcarpeta) se reemplaza ese archivo.
    auto file = userFolder.getChildFile (juce::File::createLegalFileName (cleanName) + fileExtension);
    for (const auto& existing : presets)
        if (! existing.factory && existing.name.equalsIgnoreCase (cleanName))
            file = existing.file;

    if (const auto created = file.getParentDirectory().createDirectory(); created.failed())
        return juce::Result::fail ("No se pudo crear la carpeta de presets: " + created.getErrorMessage());

    auto xml = presetToXml (preset);
    if (! xml->writeTo (file))
        return juce::Result::fail ("No se pudo escribir " + file.getFullPathName());

    rescan();
    setCurrent (cleanName, false, false);
    return juce::Result::ok();
}

juce::Result PresetManager::remove (const Preset& preset)
{
    if (preset.factory)
        return juce::Result::fail (juce::String::fromUTF8 ("Los presets de f\xc3\xa1" "brica no se pueden borrar."));

    // A la papelera de reciclaje, no borrado definitivo: un error se puede deshacer desde Windows.
    if (! preset.file.moveToTrash())
        return juce::Result::fail ("No se pudo borrar " + preset.file.getFullPathName());

    // El sonido sigue sonando igual: solo deja de existir en la biblioteca (el "*" lo indica).
    const juce::String removedName = preset.name;
    rescan();
    {
        const juce::ScopedLock sl (lock);
        if (! currentFactory && currentName == removedName)
            modifiedWhenRestored = true;
    }
    return juce::Result::ok();
}

const Preset* PresetManager::getCurrentPreset()
{
    ensureScanned();
    const int index = indexOfCurrent();
    return index >= 0 ? &presets[static_cast<size_t> (index)] : nullptr;
}

juce::String PresetManager::getCurrentName() const
{
    const juce::ScopedLock sl (lock);
    return currentName;
}

bool PresetManager::isModified() const
{
    const auto now = currentValues();
    const juce::ScopedLock sl (lock);
    if (modifiedWhenRestored)
        return true;
    for (size_t i = 0; i < now.size() && i < snapshot.size(); ++i)
        if (std::abs (now[i] - snapshot[i]) > modifiedTolerance)
            return true;
    return false;
}

std::vector<float> PresetManager::currentValues() const
{
    std::vector<float> values;
    values.reserve (parameters.size());
    for (const auto* parameter : parameters)
        values.push_back (parameter->getValue());
    return values;
}

void PresetManager::setCurrent (const juce::String& name, bool factory, bool modified)
{
    auto values = currentValues();
    {
        const juce::ScopedLock sl (lock);
        currentName = name;
        currentFactory = factory;
        modifiedWhenRestored = modified;
        snapshot = std::move (values);
    }
    // También en el estado "vivo": así una copia del estado (p. ej. la de FL al clonar el canal) lo lleva.
    state.state.setProperty (presetNameProperty, name, nullptr);
    state.state.setProperty (presetFactoryProperty, factory, nullptr);
}

void PresetManager::writeStateProperties (juce::ValueTree& stateCopy) const
{
    const bool modified = isModified();
    const juce::ScopedLock sl (lock);
    stateCopy.setProperty (presetNameProperty, currentName, nullptr);
    stateCopy.setProperty (presetFactoryProperty, currentFactory, nullptr);
    stateCopy.setProperty (presetModifiedProperty, modified, nullptr);
}

void PresetManager::stateRestored()
{
    // Al abrir un proyecto, los valores guardados son la nueva referencia; si el preset estaba modificado al guardar el
    // proyecto, se recuerda (sigue mostrando el "*" aunque el preset no se vuelva a leer del disco).
    const auto& tree = state.state;
    if (! tree.hasProperty (presetNameProperty))
    {
        // Proyecto anterior a la Fase 10: su sonido no viene de ningún preset.
        setCurrent ("Sin nombre", false, false);
        return;
    }
    setCurrent (tree.getProperty (presetNameProperty).toString(), tree.getProperty (presetFactoryProperty, true),
                tree.getProperty (presetModifiedProperty, false));
}

} // namespace undertow::presets
