#pragma once

#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "deepwater/presets/PresetManager.h"
#include "deepwater/theory/Progression.h"
#include "Lines.h"

// Fase 9: presets. El sistema (formato, biblioteca, preset actual, navegador) es el de la suite (libs/presets); aquí va
// lo propio de Tidechart:
//   <TidechartPreset formatVersion="1" name="…" category="Reggaetón" author="Tidechart">
//     <Notes>…</Notes>
//     <Parameters><PARAM id="keyMode" value="Phrygian"/> …</Parameters>
//     <Composition>
//       <Progression><Step chord="i" beats="6"/><Step chord="bII" beats="2"/></Progression>
//       <Lines><Bass style="pedal" density="0.5" low="28" high="50" seed="1"/><Melody style="hook" …/></Lines>
//     </Composition>
//   </TidechartPreset>
//   (.tcpreset). Fábrica: Presets/Factory/<Categoría>/*.tcpreset, compilados dentro del plugin.
//   Usuario: Documentos\Tidechart\Presets.
// - Un preset NO cambia la tónica (la de tu canción; decidido con el usuario al empezar la Fase 9), ni el bypass, ni qué
//   teclas disparan (cómo tocas): esos tres quedan excluidos. Sí el modo: es parte del carácter (frigio, dórico...).
// - La progresión y las líneas no son parámetros: viajan como <Composition> (el "estado extra" de libs/presets). De las
//   líneas se guardan los AJUSTES (estilo, densidad, rango, semilla, variación), no las notas: al cargar se generan con
//   la tónica de tu canción. La excepción es una melodía fija o capturada (<Source>): esa es tuya y viaja nota a nota.
// - Los mismos elementos <Progression> y <Lines> son los del estado del proyecto (con las notas de las líneas, que ahí
//   mandan): las funciones de abajo son las de los dos.
namespace tidechart::presets
{
inline constexpr const char* fileExtension = ".tcpreset";
inline constexpr const char* compositionTag = "Composition";
inline constexpr const char* progressionTag = "Progression";
inline constexpr const char* linesTag = "Lines";

// --- XML compartido con el estado del proyecto ---------------------------------------------------------------------
// <Progression><Step chord="bVI" beats="4"/>…</Progression>, el grado en texto romano (ida y vuelta probada en
// libs/theory). Se agrega como hijo de 'parent'.
void writeProgression (juce::XmlElement& parent, const deepwater::theory::Progression& progression);
// Los pasos de un <Progression>. Un grado que no se entiende se salta (con un aviso en 'problems' si no es nullptr).
[[nodiscard]] deepwater::theory::Progression readProgression (const juce::XmlElement& element, juce::StringArray* problems);

// <Lines><Bass …/><Melody …/></Lines>. withNotes: también las notas que suenan (el proyecto); sin ellas, solo los
// ajustes y la melodía fija si la hay (los presets).
void writeLines (juce::XmlElement& parent, const tidechart::Lines& lines, bool withNotes);
// Lo que falta = el valor por defecto. 'hasNotes' dice si el elemento traía las notas que suenan.
struct ReadLines
{
    tidechart::Lines lines;
    bool hasNotes = false;
};
[[nodiscard]] ReadLines readLines (const juce::XmlElement& element);

// --- La composición de un preset -----------------------------------------------------------------------------------
struct Composition
{
    deepwater::theory::Progression progression;
    tidechart::BassSettings bass;
    tidechart::MelodySettings melody;
};

[[nodiscard]] juce::ValueTree compositionToTree (const deepwater::theory::Progression& progression, const tidechart::Lines& lines);
// Árbol inválido (el preset no trae <Composition>) = la de Init: la progresión de fábrica y las líneas por defecto, sin
// aviso. Un <Composition> sin <Progression> conserva la de fábrica; uno vacío, una progresión vacía.
[[nodiscard]] Composition compositionFromTree (const juce::ValueTree& tree, const deepwater::theory::Progression& initProgression,
                                               juce::StringArray* problems);

// Orden del navegador (las categorías se guardan por nombre en cada preset: cambiar el orden no rompe nada).
[[nodiscard]] juce::StringArray categoryNames();

// La composición vive en el procesador: el formato la lee (capture) y la cambia (apply) a través de estas funciones.
[[nodiscard]] deepwater::presets::PresetFormat format (std::function<juce::ValueTree()> capture, std::function<void (const Composition&)> apply,
                                                       deepwater::theory::Progression initProgression);
} // namespace tidechart::presets
