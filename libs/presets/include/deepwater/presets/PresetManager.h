#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace deepwater::presets
{

// Presets de la suite (nació en Undertow, Fase 10; pasó a la librería con Riptide, Fase 9).
//
// Un preset es una "foto" de los parámetros que dan forma al sonido, con un nombre, una categoría y unas notas que
// explican cómo se construyó. Se guarda como XML legible; la etiqueta raíz y la extensión son de cada plugin:
//
//   <UndertowPreset formatVersion="1" name="Bajo Moog" category="Bass" author="Undertow">
//     <Notes>Cómo se construyó…</Notes>
//     <Parameters>
//       <PARAM id="filter1Cutoff" value="450"/>       ← unidades reales (Hz, s, %…), no el 0..1 interno
//       <PARAM id="filter1Type" value="Low Pass"/>    ← las listas aceptan el índice o el nombre de la opción
//     </Parameters>
//   </UndertowPreset>
//
// Los parámetros que NO aparecen toman su valor por defecto (el de "Init"). Así los presets de fábrica solo escriben lo
// que cambian y se leen como una receta; y un preset antiguo sigue funcionando cuando se añaden parámetros nuevos (los
// nuevos quedan "apagados", que es su valor por defecto). Los ids desconocidos se ignoran.
//
// Todo esto se usa en el hilo de mensajes (GUI, carga del proyecto), nunca en el de audio: cargar un preset solo
// cambia parámetros, y el audio ya suaviza cada cambio.

inline constexpr int formatVersion = 1;

// Un preset de fábrica tal como viene compilado dentro del plugin (juce_add_binary_data).
struct FactoryFile
{
    juce::String filename; // el nombre original; solo se leen los que terminan en la extensión del formato
    juce::String content;
};

// Lo que cambia de un plugin a otro. Lo arma cada plugin una vez (ver p. ej. plugins/riptide/Source/presets/).
struct PresetFormat
{
    juce::String productName;   // "Undertow": aparece en los errores ("no es un preset de Undertow")
    juce::String rootTag;       // "UndertowPreset"
    juce::String fileExtension; // ".uwpreset" (con el punto)

    // Orden de la lista del navegador. Convención: la primera es "Init" (el punto de partida, uno solo, de fábrica) y la
    // última recoge las categorías desconocidas ("Otros").
    juce::StringArray categories;

    juce::File userFolder; // Documentos\<Plugin>\Presets (se recorre con subcarpetas)

    // Parámetros que un preset no guarda ni cambia al cargarse. P. ej. el bypass de un efecto: elegir un preset no
    // debe apagar ni encender el plugin.
    juce::StringArray excludedParameters;

    std::function<std::vector<FactoryFile>()> factoryFiles;

    // Estado que forma parte del sonido pero NO es un parámetro (nació con Ebb, Fase 8: su curva por puntos). Opcional:
    // si 'extraStateTag' está vacío no se usa. En el archivo es un elemento hijo con esa etiqueta, escrito tal cual lo
    // devuelve captureExtraState (un ValueTree → XML) y leído con ValueTree::fromXml.
    //   capture: lo que suena ahora (al guardar, y para el "*": se compara con lo capturado al cargar).
    //   apply:   al cargar un preset; un árbol inválido (el preset no lo trae) = el valor de Init. Devuelve los avisos.
    juce::String extraStateTag;
    std::function<juce::ValueTree()> captureExtraState;
    std::function<juce::StringArray (const juce::ValueTree&)> applyExtraState;

    // Posición de una categoría en 'categories' (las desconocidas, en la última).
    [[nodiscard]] int categoryIndex (const juce::String& category) const;
};

struct Preset
{
    juce::String name;
    juce::String category = "Otros";
    juce::String author;
    juce::String notes;
    bool factory = false;
    juce::File file; // solo los del usuario

    // id del parámetro → valor tal como está escrito en el archivo (número o nombre de una opción).
    std::vector<std::pair<juce::String, juce::String>> values;

    // PresetFormat::extraStateTag (inválido si el archivo no lo trae).
    juce::ValueTree extraState;

    // Posición de la categoría en PresetFormat::categories (la pone quien lee o crea el preset).
    int categoryOrder = 0;

    [[nodiscard]] int categoryIndex() const noexcept { return categoryOrder; }
    [[nodiscard]] bool sameAs (const Preset& other) const noexcept { return factory == other.factory && name == other.name; }
};

// Lectura y escritura del formato (sin estado; también lo usan los tests).
[[nodiscard]] std::optional<Preset> parsePreset (const juce::XmlElement& xml, const PresetFormat& format, juce::String& error);
[[nodiscard]] std::unique_ptr<juce::XmlElement> presetToXml (const Preset& preset, const PresetFormat& format);

class PresetManager
{
public:
    PresetManager (juce::AudioProcessorValueTreeState& state, PresetFormat format);

    [[nodiscard]] const PresetFormat& getFormat() const noexcept { return format; }

    // --- Biblioteca ---
    // Fábrica (dentro del plugin) + carpeta del usuario. Se lee la primera vez que se pide y con rescan().
    const std::vector<Preset>& getPresets();
    void rescan();
    [[nodiscard]] const juce::File& getUserFolder() const noexcept { return format.userFolder; }
    void setUserFolder (const juce::File& folder); // los tests usan una carpeta temporal

    // --- Cargar ---
    // Devuelve los avisos (ids o valores que no se entendieron); el resto del preset se aplica igual.
    juce::StringArray load (const Preset& preset);
    juce::StringArray loadInit();
    // Siguiente / anterior en el orden del navegador (da la vuelta). Devuelve el preset cargado.
    const Preset* step (int delta);

    // --- Guardar y borrar (solo presets del usuario) ---
    [[nodiscard]] bool userPresetExists (const juce::String& name);
    [[nodiscard]] bool isFactoryName (const juce::String& name);
    juce::Result save (const juce::String& name, const juce::String& category, const juce::String& author,
                       const juce::String& notes);
    juce::Result remove (const Preset& preset);

    // --- Preset actual ---
    [[nodiscard]] juce::String getCurrentName() const;
    // El preset de la biblioteca que está cargado (nullptr si ya no existe, p. ej. si se borró su archivo).
    [[nodiscard]] const Preset* getCurrentPreset();
    // ¿Se tocó algún parámetro desde que se cargó? (el navegador muestra un "*").
    [[nodiscard]] bool isModified() const;

    // --- Estado del proyecto del host ---
    // El nombre del preset y si estaba modificado viajan con el proyecto (propiedades del estado, no parámetros).
    void writeStateProperties (juce::ValueTree& stateCopy) const;
    void stateRestored();

    // Valores (0..1) que tendría cada parámetro con este preset, en el orden de getParameters(). Lo usan los tests.
    [[nodiscard]] std::vector<float> resolve (const Preset& preset, juce::StringArray* problems) const;
    // Los parámetros que manejan los presets (todos menos los excluidos por el formato).
    [[nodiscard]] const std::vector<juce::RangedAudioParameter*>& getParameters() const noexcept { return parameters; }

private:
    void ensureScanned();
    void apply (const std::vector<float>& normalised);
    void setCurrent (const juce::String& name, bool factory, bool modified);
    [[nodiscard]] std::vector<float> currentValues() const;
    [[nodiscard]] juce::ValueTree currentExtraState() const;
    [[nodiscard]] int indexOfCurrent() const;

    juce::AudioProcessorValueTreeState& state;
    PresetFormat format;
    std::vector<juce::RangedAudioParameter*> parameters;
    std::vector<Preset> presets;
    bool scanned = false;

    // Lo puede leer getStateInformation desde otro hilo que no sea el de mensajes (algunos hosts lo hacen).
    mutable juce::CriticalSection lock;
    juce::String currentName { "Init" };
    bool currentFactory = true;
    bool modifiedWhenRestored = false;
    std::vector<float> snapshot; // valores al cargar: con ellos se detecta si el preset se modificó
    juce::ValueTree extraSnapshot; // lo mismo para el estado extra

    JUCE_DECLARE_NON_COPYABLE (PresetManager)
};

} // namespace deepwater::presets
