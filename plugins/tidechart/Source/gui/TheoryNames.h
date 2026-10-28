#pragma once

#include <array>
#include <cstddef>

#include <juce_graphics/juce_graphics.h>

#include "deepwater/theory/Harmony.h"
#include "gui/Theme.h"

// La teoría en palabras para la GUI, en español (libs/theory escribe en inglés: es lo que se lee y escribe como texto
// de acordes y tonalidades, también desde el servidor MCP).
namespace tidechart::gui::names
{
namespace th = deepwater::theory;

inline juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

// Las 12 tónicas, en el orden del parámetro keyTonic.
inline juce::String tonicChoice (int index)
{
    static constexpr std::array<const char*, 12> names { "C", "C\xe2\x99\xaf / D\xe2\x99\xad", "D", "D\xe2\x99\xaf / E\xe2\x99\xad", "E", "F", "F\xe2\x99\xaf / G\xe2\x99\xad", "G", "G\xe2\x99\xaf / A\xe2\x99\xad", "A", "A\xe2\x99\xaf / B\xe2\x99\xad", "B" };
    return utf8 (names[static_cast<std::size_t> (index)]);
}

inline juce::String modeName (th::Mode mode)
{
    static constexpr std::array<const char*, th::modeCount> names {
        "mayor (j\xc3\xb3nico)", "d\xc3\xb3rico", "frigio", "lidio", "mixolidio", "menor (e\xc3\xb3lico)", "locrio",
        "menor arm\xc3\xb3nica", "menor mel\xc3\xb3" "dica", "frigio dominante",
    };
    return utf8 (names[static_cast<std::size_t> (mode)]);
}

// El carácter de cada modo en una frase, y la nota que lo distingue (respecto de la mayor o de la menor).
inline juce::String modeCharacter (th::Mode mode)
{
    static constexpr std::array<const char*, th::modeCount> text {
        "brillante y estable: el pop de siempre",
        "menor con la 6.\xc2\xaa mayor: oscuro pero con luz (R&B, funk, \xc2\xabOye como va\xc2\xbb)",
        "menor con el \xe2\x99\xad" "2: tenso y oscuro, el perreo m\xc3\xa1s sombr\xc3\xado (i\xe2\x80\x93\xe2\x99\xadII)",
        "mayor con el \xe2\x99\xaf" "4: flota, suena a sue\xc3\xb1o o a banda sonora",
        "mayor con la \xe2\x99\xad" "7: relajado, rockero (I\xe2\x80\x93\xe2\x99\xadVII)",
        "triste y oscura: la base del reggaet\xc3\xb3n (i\xe2\x80\x93\xe2\x99\xadVI\xe2\x80\x93\xe2\x99\xadIII\xe2\x80\x93\xe2\x99\xadVII)",
        "su i es disminuido: el m\xc3\xa1s inestable, casi solo para color",
        "menor con sensible: el V mayor vuelve a la i con fuerza; aire cl\xc3\xa1sico o \xc3\xa1rabe",
        "menor con 6.\xc2\xaa y 7.\xc2\xaa mayores: el menor del jazz",
        "el \xe2\x99\xad" "2 con 3.\xc2\xaa mayor: flamenco, \xc3\xa1rabe, el reggaet\xc3\xb3n m\xc3\xa1s ex\xc3\xb3tico",
    };
    return utf8 (text[static_cast<std::size_t> (mode)]);
}

inline juce::String functionName (th::HarmonicFunction function)
{
    switch (function)
    {
        case th::HarmonicFunction::tonic: return utf8 ("t\xc3\xb3nica");
        case th::HarmonicFunction::subdominant: return "subdominante";
        case th::HarmonicFunction::dominant: return "dominante";
    }
    return {};
}

// Reposo, alejarse, tensión: frío → templado → caliente.
inline juce::Colour functionColour (th::HarmonicFunction function)
{
    switch (function)
    {
        case th::HarmonicFunction::tonic: return theme::input;
        case th::HarmonicFunction::subdominant: return theme::accent;
        case th::HarmonicFunction::dominant: return theme::freeRunning;
    }
    return theme::text;
}
} // namespace tidechart::gui::names
