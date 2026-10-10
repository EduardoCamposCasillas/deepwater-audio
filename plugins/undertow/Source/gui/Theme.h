#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

#include "synth/Modulation.h"

// Colores de toda la interfaz en un solo sitio: cambiar el aspecto no obliga a tocar cada componente.
namespace undertow::gui::theme
{
inline const juce::Colour background { 0xff0e1115 };
inline const juce::Colour panel { 0xff161b21 };
inline const juce::Colour panelOutline { 0xff252d36 };
inline const juce::Colour display { 0xff0a0d10 }; // fondo de los visores (onda, filtro, LFO)
inline const juce::Colour control { 0xff222932 };  // cuerpo de perillas, selectores y botones
inline const juce::Colour track { 0xff2c3540 };    // recorrido "vacío" de las perillas
inline const juce::Colour text { 0xffe3e9ef };
inline const juce::Colour dimText { 0xff8794a1 };
inline const juce::Colour accent { 0xff4fc3f7 };   // valor de las perillas, onda actual
inline const juce::Colour warm { 0xffffb74d };     // onda deformada, curva del filtro

// Un color por fuente de modulación: el mismo en la barra de fuentes, en los anillos de las perillas y en la matriz.
inline const std::array<juce::Colour, synth::numModSources> sourceColours {
    juce::Colour (0xff6b7785), // None
    juce::Colour (0xfff06292), // Env 1
    juce::Colour (0xffffa94d), // Env 2
    juce::Colour (0xffffd54f), // Env 3
    juce::Colour (0xff4dd0e1), // LFO 1
    juce::Colour (0xff81c784), // LFO 2
    juce::Colour (0xffba68c8), // Velocity
    juce::Colour (0xff90a4ae), // Key
    juce::Colour (0xff7986cb), // Mod Wheel
    juce::Colour (0xffa1887f), // Aftertouch
};

[[nodiscard]] inline juce::Colour sourceColour (synth::ModSource source)
{
    return sourceColours[static_cast<size_t> (source)];
}
} // namespace undertow::gui::theme
