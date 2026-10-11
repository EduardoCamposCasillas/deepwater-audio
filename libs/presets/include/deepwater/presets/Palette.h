#pragma once

#include <array>
#include <utility>

#include <juce_graphics/juce_graphics.h>

namespace deepwater::presets
{

// Colores de los componentes de presets (barra, navegador, panel de guardar). Cada plugin pasa los de su tema: la base
// de la suite es la misma y cambia el acento (Undertow frío, Riptide cálido).
struct Palette
{
    juce::Colour background;   // fondo de la ventana (oscurece la página detrás del panel de guardar)
    juce::Colour panel;        // fondo de los paneles
    juce::Colour panelOutline;
    juce::Colour display;      // fondo de los campos de texto y de la lista
    juce::Colour text;
    juce::Colour dimText;
    juce::Colour accent;       // categoría, preset actual, selección
    juce::Colour secondary;    // marca de los presets del usuario y mensajes de error
};

// String::toUpperCase depende de la configuración regional de C (en Windows deja "ó" en minúscula): las vocales con
// tilde y la ñ se pasan a mano.
[[nodiscard]] inline juce::String upperCase (const juce::String& text)
{
    auto result = text.toUpperCase();
    constexpr std::array<std::pair<juce::juce_wchar, juce::juce_wchar>, 6> accents { {
        { 0xe1, 0xc1 }, { 0xe9, 0xc9 }, { 0xed, 0xcd }, { 0xf3, 0xd3 }, { 0xfa, 0xda }, { 0xf1, 0xd1 } } };
    for (const auto& [lower, upper] : accents)
        result = result.replaceCharacter (lower, upper);
    return result;
}

} // namespace deepwater::presets
