#pragma once

#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace undertow::presets
{

// Fase 10: presets.
//
// Un preset es una "foto" de todos los parámetros que dan forma al sonido, con un nombre, una categoría y unas notas
// que explican cómo se construyó. Se guarda como XML legible (extensión .uwpreset):
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

// El orden es el de la lista del navegador. "Otros" recoge las categorías desconocidas.
inline constexpr std::array<const char*, 8> categoryNames { "Init", "Bass", "Lead", "Pad", "Pluck", "Keys", "FX", "Otros" };

inline constexpr const char* fileExtension = ".uwpreset";
inline constexpr int formatVersion = 1;

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

    [[nodiscard]] int categoryIndex() const noexcept;
    [[nodiscard]] bool sameAs (const Preset& other) const noexcept { return factory == other.factory && name == other.name; }
};

// Lectura y escritura del formato (sin estado; también lo usan los tests).
[[nodiscard]] std::optional<Preset> parsePreset (const juce::XmlElement& xml, juce::String& error);
[[nodiscard]] std::unique_ptr<juce::XmlElement> presetToXml (const Preset& preset);

class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    // --- Biblioteca ---
    // Fábrica (dentro del plugin) + carpeta del usuario. Se lee la primera vez que se pide y con rescan().
    const std::vector<Preset>& getPresets();
    void rescan();
    [[nodiscard]] static juce::File defaultUserFolder(); // Documentos\Undertow\Presets
    [[nodiscard]] const juce::File& getUserFolder() const noexcept { return userFolder; }
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

    // --- Estado del proyecto de FL ---
    // El nombre del preset y si estaba modificado viajan con el proyecto (propiedades del estado, no parámetros).
    void writeStateProperties (juce::ValueTree& stateCopy) const;
    void stateRestored();

    // Valores (0..1) que tendría cada parámetro con este preset, en el orden de getParameters(). Lo usan los tests.
    [[nodiscard]] std::vector<float> resolve (const Preset& preset, juce::StringArray* problems) const;
    [[nodiscard]] const std::vector<juce::RangedAudioParameter*>& getParameters() const noexcept { return parameters; }

private:
    void ensureScanned();
    void apply (const std::vector<float>& normalised);
    void setCurrent (const juce::String& name, bool factory, bool modified);
    [[nodiscard]] std::vector<float> currentValues() const;
    [[nodiscard]] int indexOfCurrent() const;

    juce::AudioProcessorValueTreeState& state;
    std::vector<juce::RangedAudioParameter*> parameters;
    std::vector<Preset> presets;
    juce::File userFolder = defaultUserFolder();
    bool scanned = false;

    // Lo puede leer getStateInformation desde otro hilo que no sea el de mensajes (algunos hosts lo hacen).
    mutable juce::CriticalSection lock;
    juce::String currentName { "Init" };
    bool currentFactory = true;
    bool modifiedWhenRestored = false;
    std::vector<float> snapshot; // valores al cargar: con ellos se detecta si el preset se modificó

    JUCE_DECLARE_NON_COPYABLE (PresetManager)
};

} // namespace undertow::presets
