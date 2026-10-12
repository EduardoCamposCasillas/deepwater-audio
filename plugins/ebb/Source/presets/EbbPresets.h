#pragma once

#include <functional>

#include "deepwater/dsp/BreakpointCurve.h"
#include "deepwater/presets/PresetManager.h"

namespace ebb::presets
{

// Fase 8: presets. El sistema (formato, biblioteca, preset actual, navegador) es el de la suite (libs/presets); aquí va
// lo propio de Ebb:
//   <EbbPreset formatVersion="1" name="…" category="Dembow" author="Ebb"> … <Curve shape="Retención"/> </EbbPreset>
//   (.ebpreset). Fábrica: Presets/Factory/<Categoría>/*.ebpreset, compilados dentro del plugin.
//   Usuario: Documentos\Ebb\Presets.
//   El bypass no es parte del sonido: un preset no lo guarda ni lo cambia.
//   La curva NO es un parámetro: viaja como el elemento <Curve> (el "estado extra" de libs/presets).
inline constexpr const char* fileExtension = ".ebpreset";

// La curva en el estado del proyecto y en los presets: <Curve><Point x="0.25" y="0.156" tension="0.034"/>…</Curve>.
// Un preset de fábrica también puede nombrar una forma de fábrica: <Curve shape="Lenta"/> (se lee como una receta).
inline constexpr const char* curveTag = "Curve";

[[nodiscard]] juce::ValueTree curveToTree (const deepwater::dsp::BreakpointCurve& curve);

// Árbol → curva. Inválido (sin <Curve>) = la forma por defecto, sin aviso (un proyecto de la Fase 2 o un preset que no
// cambia la curva). Una forma desconocida o menos de dos puntos = la forma por defecto, con un aviso en 'problems'.
[[nodiscard]] deepwater::dsp::BreakpointCurve curveFromTree (const juce::ValueTree& tree, juce::StringArray* problems);

// Orden del navegador (las categorías se guardan por nombre en cada preset: cambiar el orden no rompe nada).
[[nodiscard]] juce::StringArray categoryNames();

// La curva vive en el procesador: el formato la lee (getCurve) y la cambia (setCurve) a través de estas funciones.
[[nodiscard]] deepwater::presets::PresetFormat format (std::function<deepwater::dsp::BreakpointCurve()> getCurve,
                                                       std::function<void (const deepwater::dsp::BreakpointCurve&)> setCurve);

} // namespace ebb::presets
