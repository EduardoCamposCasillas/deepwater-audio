#pragma once

#include <functional>

#include "deepwater/presets/PresetManager.h"

namespace fathom::presets
{

// Fase 15: presets. El sistema (formato, biblioteca, preset actual, navegador) es el de la suite (libs/presets); aquí va
// lo propio de Fathom:
//   <FathomPreset formatVersion="1" name="…" category="Voces" author="Fathom"> … </FathomPreset>  (.fhpreset)
//   Fábrica: Presets/Factory/<Categoría>/*.fhpreset, compilados dentro del plugin. Usuario: Documentos\Fathom\Presets.
//   Las bandas se escriben con sus IDs ("band3Frequency" = 300) y las listas por nombre ("Bell", "Low Cut",
//   "24 dB/oct", "Mid"): lo que no aparece vale lo de Init (bandas apagadas, cada una en su frecuencia).
//   El bypass no es parte del sonido: un preset no lo guarda ni lo cambia. Tampoco la vista del analizador, el nombre
//   de la instancia ni el lado del A/B (no son parámetros): un preset cambia el lado que está activo.
inline constexpr const char* fileExtension = ".fhpreset";

// Orden del navegador (las categorías se guardan por nombre en cada preset: cambiar el orden no rompe nada).
[[nodiscard]] juce::StringArray categoryNames();

// beforeLoad / afterLoad: lo que se hace alrededor de cargar un preset (el procesador abre ahí un paso de deshacer).
[[nodiscard]] deepwater::presets::PresetFormat format (std::function<void()> beforeLoad, std::function<void()> afterLoad);

} // namespace fathom::presets
