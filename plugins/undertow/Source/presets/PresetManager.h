#pragma once

#include <array>

#include "deepwater/presets/PresetManager.h"

namespace undertow::presets
{

// Fase 10: presets. El sistema (formato .uwpreset, biblioteca, preset actual, navegador) vive desde Riptide Fase 9 en
// la librería compartida libs/presets (deepwater::presets); aquí queda solo lo propio de Undertow.
using deepwater::presets::Preset;
using deepwater::presets::PresetManager;

// El orden es el de la lista del navegador. "Otros" recoge las categorías desconocidas.
inline constexpr std::array<const char*, 8> categoryNames { "Init", "Bass", "Lead", "Pad", "Pluck", "Keys", "FX", "Otros" };

inline constexpr const char* fileExtension = ".uwpreset";

// <UndertowPreset …> con extensión .uwpreset, en Documentos\Undertow\Presets; los de fábrica, de BinaryData.
[[nodiscard]] deepwater::presets::PresetFormat format();

} // namespace undertow::presets
