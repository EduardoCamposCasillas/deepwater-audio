#pragma once

#include "deepwater/presets/PresetManager.h"

namespace riptide::presets
{

// Fase 9: presets. El sistema (formato, biblioteca, preset actual, navegador) es el de la suite (libs/presets); aquí va
// lo propio de Riptide:
//   <RiptidePreset formatVersion="1" name="…" category="Bajo" author="Riptide"> … </RiptidePreset>   (.rtpreset)
//   Fábrica: Presets/Factory/<Categoría>/*.rtpreset, compilados dentro del plugin.
//   Usuario: Documentos\Riptide\Presets.
//   El bypass no es parte del sonido: un preset no lo guarda ni lo cambia.
inline constexpr const char* fileExtension = ".rtpreset";

// Orden del navegador (las categorías se guardan por nombre en cada preset: cambiar el orden no rompe nada).
[[nodiscard]] juce::StringArray categoryNames();

[[nodiscard]] deepwater::presets::PresetFormat format();

} // namespace riptide::presets
