#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fathom::gui
{

// La letra de un atajo con Ctrl ('z' para Ctrl+Z), o 0 si la tecla no es Ctrl + letra (o lleva Alt).
//
// En Windows, JUCE a veces entrega Ctrl+letra como el carácter de control ASCII que genera el teclado (Ctrl+A = 1 ...
// Ctrl+Z = 26) y no como la letra: comparar con KeyPress ('z', ctrl, 0) falla. Aquí se aceptan las dos formas.
inline juce::juce_wchar commandLetter (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (! mods.isCommandDown() || mods.isAltDown())
        return 0;
    const int code = key.getKeyCode();
    if (code >= 1 && code <= 26)
        return static_cast<juce::juce_wchar> ('a' + code - 1);
    const auto letter = juce::CharacterFunctions::toLowerCase (static_cast<juce::juce_wchar> (code));
    return letter >= 'a' && letter <= 'z' ? letter : 0;
}

} // namespace fathom::gui
