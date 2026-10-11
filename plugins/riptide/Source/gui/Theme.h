#pragma once

#include <array>

#include <juce_graphics/juce_graphics.h>

// Colores de toda la interfaz en un solo sitio. La base es la misma que la de Undertow (la suite se reconoce como
// familia); el acento es cálido porque saturar es "calentar" el sonido.
namespace riptide::gui::theme
{
inline const juce::Colour background { 0xff0e1115 };
inline const juce::Colour panel { 0xff161b21 };
inline const juce::Colour panelOutline { 0xff252d36 };
inline const juce::Colour display { 0xff0a0d10 }; // fondo de los medidores
inline const juce::Colour control { 0xff222932 };  // cuerpo de perillas y botones
inline const juce::Colour track { 0xff2c3540 };    // recorrido "vacío" de las perillas
inline const juce::Colour text { 0xffe3e9ef };
inline const juce::Colour dimText { 0xff8794a1 };
inline const juce::Colour accent { 0xffff7a45 };   // valor de las perillas
inline const juce::Colour modulation { 0xff4fc3d9 }; // lo que mueven la envolvente y el LFO (frío, para distinguirlo)

// Medidores: verde = nivel cómodo, ámbar = cerca del máximo, rojo = por encima de 0 dBFS (se recorta al exportar).
inline const juce::Colour meterSafe { 0xff4caf7d };
inline const juce::Colour meterHot { 0xffffb74d };
inline const juce::Colour meterClip { 0xffef5350 };

// Fase 8: los visores. Antes = lo que entra (gris, de referencia); después = lo que sale (el acento). Cada banda tiene su
// color, de cálido (graves) a frío (agudos), el mismo en el espectro, en la curva y en su columna.
inline const juce::Colour spectrumBefore { 0xff9aa7b4 };
inline const juce::Colour grid { 0xff1c232b };
inline const std::array<juce::Colour, 4> bands { juce::Colour { 0xffff8a5c }, juce::Colour { 0xffe8c35a },
                                                 juce::Colour { 0xff7fc98f }, juce::Colour { 0xff6fa8ff } };
} // namespace riptide::gui::theme
