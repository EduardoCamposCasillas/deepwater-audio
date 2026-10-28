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

// Fase 4: voicings, ritmos y modo de tocar, en el orden de sus parámetros.
inline juce::String voicingName (int index)
{
    static constexpr std::array<const char*, 3> names { "Estado fundamental", "Conducci\xc3\xb3n de voces", "Abierto (drop 2)" };
    return utf8 (names[static_cast<std::size_t> (index)]);
}

inline juce::String rhythmName (int index)
{
    static constexpr std::array<const char*, 9> names {
        "Sostenido", "Negras", "Corcheas", "Contratiempo", "Dembow (3+3+2)", "Dembow largo", "Dembow a contratiempo",
        "Dembow desplazado", "Dembow con huecos",
    };
    return utf8 (names[static_cast<std::size_t> (index)]);
}

// Qué hace cada ritmo y para qué sirve, en una frase.
inline juce::String rhythmDescription (int index)
{
    static constexpr std::array<const char*, 9> text {
        "el acorde dura todo su paso: pads, colchones, la base para escuchar la armon\xc3\xad" "a",
        "un golpe por tiempo: el piano de balada, el pulso m\xc3\xa1s simple",
        "dos golpes por tiempo, el primero acentuado: empuje constante (pop, synthwave)",
        "el \"y\" de cada tiempo: el skank del reggae y del house, los acordes entre los bombos",
        "3+3+2 en cada medio comp\xc3\xa1s (0 3 6 | 8 11 14): el boom-ch-boom-chick del reggaet\xc3\xb3n",
        "el mismo 3+3+2 en corcheas, una vez por comp\xc3\xa1s: acordes que respiran (el tresillo de la habanera)",
        "solo el \"ch\" del dembow: los acordes contestan al bombo en vez de pisarlo",
        "el dembow movido una corchea: mismo dibujo, otro apoyo; suena a que tropieza",
        "el 3+3 de la primera mitad y una anticipaci\xc3\xb3n al final: deja aire para la voz",
    };
    return utf8 (text[static_cast<std::size_t> (index)]);
}

inline juce::String playModeName (int index)
{
    return index == 0 ? utf8 ("Teclas: un acorde cada una") : utf8 ("Progresi\xc3\xb3n (sigue a FL)");
}

// "de dórico", "de menor (eólico)": el modo del que se tomó prestado un acorde.
inline juce::String borrowedFrom (th::Mode mode)
{
    return "de " + names::modeName (mode); // calificado: libs/theory también tiene un modeName (en inglés)
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
