#pragma once

#include <array>

#include <juce_graphics/juce_graphics.h>

// Colores de toda la interfaz en un solo sitio. La base es la de la suite (Undertow, Riptide, Ebb); el acento es un azul
// de sonar: Fathom "sondea" el espectro.
namespace fathom::gui::theme
{
inline const juce::Colour background { 0xff0e1115 };
inline const juce::Colour panel { 0xff161b21 };
inline const juce::Colour panelOutline { 0xff252d36 };
inline const juce::Colour display { 0xff0a0d10 };
inline const juce::Colour control { 0xff222932 };
inline const juce::Colour track { 0xff2c3540 };
inline const juce::Colour text { 0xffe3e9ef };
inline const juce::Colour dimText { 0xff8794a1 };
inline const juce::Colour accent { 0xff4f9dff };
inline const juce::Colour bypassOn { 0xffef5350 };
inline const juce::Colour solo { 0xffffd54f }; // escuchar una banda sola (Fase 6): amarillo, como el solo de una consola

// El visor: entrada = gris (referencia); salida = el acento, con relleno; sidechain = ámbar.
inline const juce::Colour spectrumIn { 0xff9aa7b4 };
inline const juce::Colour spectrumSidechain { 0xffe0a458 }; // otra pista (Fase 5): ámbar
inline const juce::Colour grid { 0xff1c232b };

// Fase 8: el side en el analizador (violeta) y la curva de cada canal cuando hay bandas en un solo canal (el mid es la
// curva blanca de siempre).
inline const juce::Colour spectrumSide { 0xffb48cff };
inline const juce::Colour curveLeft { 0xff6ec6ff };
inline const juce::Colour curveRight { 0xffff8a80 };
inline const juce::Colour curveSide { 0xffc6a6ff };

// Fase 12: la curva que se pide (lo que hay que igualar o lo dibujado), naranja; la vista previa de lo que harían las
// bandas, verde.
inline const juce::Colour curveWanted { 0xffff9f43 };
inline const juce::Colour curvePreview { 0xff6ee7a0 };

// Cada banda tiene su color (el nodo, su relleno y las perillas cuando está elegida). Ocho tonos que se repiten: con
// 24 bandas, la 1, la 9 y la 17 comparten color, pero rara vez se usan todas.
inline juce::Colour bandColour (int band)
{
    static const std::array<juce::Colour, 8> colours { juce::Colour (0xff4f9dff), juce::Colour (0xff26c6da), juce::Colour (0xff66bb6a),
                                                       juce::Colour (0xffd4e157), juce::Colour (0xffffb74d), juce::Colour (0xffef6c6c),
                                                       juce::Colour (0xffec6fb0), juce::Colour (0xffb388ff) };
    return colours[static_cast<size_t> (band) % colours.size()];
}
} // namespace fathom::gui::theme
