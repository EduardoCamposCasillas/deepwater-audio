#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "deepwater/theory/Text.h"
#include "gui/Theme.h"
#include "gui/TheoryNames.h"

namespace tidechart::gui
{

// Fase 8.2: un teclado de 2 octavas (de C a B) con la escala resaltada: las teclas de la escala teñidas, la tónica en
// el color de la tónica con su letra. Encima, el acorde que se está mirando (el pad bajo el ratón, el pulsado o el que
// suena): sus notas como puntos del color de su función, apiladas desde la fundamental; las que no son de la escala
// (las que trae un prestado), en naranja y con un aro. Así se ve por qué un acorde "es de la tonalidad" (todas sus notas
// caen en teclas teñidas) y qué color trae uno prestado. Solo dibuja: la paleta le dice la tonalidad y el acorde.
class ScaleKeyboard final : public juce::Component, public juce::TooltipClient
{
public:
    void setKey (const deepwater::theory::Key& newKey)
    {
        if (newKey == key && hasKey)
            return;
        key = newKey;
        hasKey = true;
        repaint();
    }

    // El acorde a mostrar (o ninguno) y el color de su función.
    void setChord (const std::optional<deepwater::theory::Chord>& newChord, juce::Colour colour)
    {
        if (newChord == chord && colour == chordColour)
            return;
        chord = newChord;
        chordColour = colour;
        repaint();
    }

    juce::String getTooltip() override
    {
        namespace th = deepwater::theory;
        const int note = noteAt (getMouseXYRelative());
        if (note < 0)
            return {};
        const th::Notation notation { true, 5 };
        const int pc = note % 12;
        const auto name = juce::String::fromUTF8 (th::noteName (th::spellInKey (pc, key), notation).c_str());
        const int degree = key.degreeOf (pc);
        juce::String text = degree == 0 ? name + names::utf8 (": la t\xc3\xb3nica, la nota de casa")
                            : degree > 0 ? name + names::utf8 (": grado ") + juce::String (degree + 1) + " de la escala"
                                         : name + names::utf8 (": fuera de la escala");
        if (chord && ((th::chordMask (chord->root.pitchClass(), chord->type) >> pc) & 1) != 0)
            text << names::utf8 (" \xc2\xb7 est\xc3\xa1 en ") << juce::String::fromUTF8 (th::chordSymbol (*chord, notation).c_str());
        return text;
    }

    void paint (juce::Graphics& g) override
    {
        namespace th = deepwater::theory;
        const auto chordNotes = chordKeys();
        const int tonicPc = key.tonic.pitchClass();
        const float whiteWidth = static_cast<float> (getWidth()) / static_cast<float> (whiteCount);
        const float height = static_cast<float> (getHeight());

        // Blancas.
        for (int note = 0, white = 0; note < keyCount; ++note)
        {
            if (isBlack (note))
                continue;
            const auto bounds = juce::Rectangle<float> (white * whiteWidth, 0.0f, whiteWidth, height).reduced (0.5f, 0.0f);
            g.setColour (keyColour (note % 12, tonicPc, false));
            g.fillRoundedRectangle (bounds, 2.0f);
            if (note % 12 == tonicPc)
            {
                g.setColour (theme::display);
                g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                g.drawText (juce::String::fromUTF8 (th::noteName (key.tonic, { true, 5 }).c_str()), bounds.withTrimmedTop (height * 0.62f),
                            juce::Justification::centred);
            }
            drawDot (g, bounds, note, chordNotes, false);
            ++white;
        }
        // Negras, encima.
        for (int note = 0; note < keyCount; ++note)
        {
            if (! isBlack (note))
                continue;
            const auto bounds = blackBounds (note, whiteWidth);
            g.setColour (keyColour (note % 12, tonicPc, true));
            g.fillRoundedRectangle (bounds, 1.5f);
            drawDot (g, bounds, note, chordNotes, true);
        }
    }

private:
    static constexpr int keyCount = 24;
    static constexpr int whiteCount = 14;

    [[nodiscard]] static bool isBlack (int note) noexcept
    {
        constexpr std::array<bool, 12> black { false, true, false, true, false, false, true, false, true, false, true, false };
        return black[static_cast<std::size_t> (note % 12)];
    }

    [[nodiscard]] static int whiteIndex (int note) noexcept
    {
        constexpr std::array<int, 12> index { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
        return (note / 12) * 7 + index[static_cast<std::size_t> (note % 12)];
    }

    [[nodiscard]] juce::Rectangle<float> blackBounds (int note, float whiteWidth) const
    {
        const float width = whiteWidth * 0.62f;
        const float centre = static_cast<float> (whiteIndex (note) + 1) * whiteWidth; // entre su blanca y la siguiente
        return { centre - width * 0.5f, 0.0f, width, static_cast<float> (getHeight()) * 0.6f };
    }

    [[nodiscard]] int noteAt (juce::Point<int> point) const
    {
        const float whiteWidth = static_cast<float> (getWidth()) / static_cast<float> (whiteCount);
        for (int note = 0; note < keyCount; ++note)
            if (isBlack (note) && blackBounds (note, whiteWidth).contains (point.toFloat()))
                return note;
        const int white = static_cast<int> (static_cast<float> (point.x) / whiteWidth);
        for (int note = 0; note < keyCount; ++note)
            if (! isBlack (note) && whiteIndex (note) == white)
                return note;
        return -1;
    }

    [[nodiscard]] juce::Colour keyColour (int pc, int tonicPc, bool black) const
    {
        // En grises (de la escala = claras, fuera = oscuras): el color queda para la tónica y las notas del acorde.
        if (pc == tonicPc)
            return black ? theme::input.darker (0.3f) : theme::input;
        if (key.contains (pc))
            return black ? juce::Colour (0xff6b7682) : juce::Colour (0xffd5dce3);
        return black ? theme::background : juce::Colour (0xff4a535d);
    }

    // Las notas del acorde en el teclado: en estado fundamental desde la fundamental en la 1.ª octava; lo que se pasa de
    // las 2 octavas (una 13.ª) baja una octava. Bit n = tecla n.
    [[nodiscard]] std::uint32_t chordKeys() const
    {
        if (! chord)
            return 0;
        std::uint32_t mask = 0;
        for (int note : chord->withInversion (0).midiNotes (chord->root.pitchClass()))
        {
            while (note >= keyCount)
                note -= 12;
            if (note >= 0)
                mask |= 1u << note;
        }
        return mask;
    }

    void drawDot (juce::Graphics& g, juce::Rectangle<float> bounds, int note, std::uint32_t chordNotes, bool black) const
    {
        if (((chordNotes >> note) & 1u) == 0)
            return;
        const bool outside = ! key.contains (note % 12);
        const float radius = std::min (bounds.getWidth() * 0.32f, 4.0f);
        const auto centre = juce::Point<float> (bounds.getCentreX(), black ? bounds.getBottom() - radius - 3.0f : bounds.getY() + bounds.getHeight() * 0.78f - (note % 12 == key.tonic.pitchClass() ? bounds.getHeight() * 0.42f : 0.0f));
        const auto dot = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
        // Un aro oscuro para que se vea sobre cualquier tecla (también sobre la tónica, del mismo color que la función
        // tónica); las de fuera de la escala, en naranja con un segundo aro claro.
        g.setColour (theme::display);
        g.fillEllipse (dot.expanded (1.5f));
        g.setColour (outside ? theme::freeRunning : chordColour);
        g.fillEllipse (dot);
        if (outside)
        {
            g.setColour (theme::text);
            g.drawEllipse (dot.expanded (2.5f), 1.0f);
        }
    }

    deepwater::theory::Key key;
    bool hasKey = false;
    std::optional<deepwater::theory::Chord> chord;
    juce::Colour chordColour = theme::accent;
};

} // namespace tidechart::gui
