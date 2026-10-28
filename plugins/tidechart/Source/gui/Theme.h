#pragma once

#include <juce_graphics/juce_graphics.h>

// Colores de toda la interfaz en un solo sitio. La base es la de la suite (Undertow, Riptide, Ebb, Fathom); el acento
// es un azul lavanda: la tinta de una carta náutica de noche.
namespace tidechart::gui::theme
{
inline const juce::Colour background { 0xff0e1115 };
inline const juce::Colour panel { 0xff161b21 };
inline const juce::Colour panelOutline { 0xff252d36 };
inline const juce::Colour display { 0xff0a0d10 };
inline const juce::Colour control { 0xff222932 };
inline const juce::Colour track { 0xff2c3540 };
inline const juce::Colour text { 0xffe3e9ef };
inline const juce::Colour dimText { 0xff8794a1 };
inline const juce::Colour accent { 0xff8c9eff };

// Entrada / salida en el monitor, reloj libre (sin host: no sigue a FL) y avisos.
inline const juce::Colour input { 0xff80cbc4 };
inline const juce::Colour freeRunning { 0xffffb74d };
inline const juce::Colour warning { 0xffef5350 };
} // namespace tidechart::gui::theme
