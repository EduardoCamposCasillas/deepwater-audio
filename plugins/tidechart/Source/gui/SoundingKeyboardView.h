#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gui/Theme.h"
#include "midi/MidiText.h"

namespace tidechart::gui
{

// Las 128 notas MIDI como un teclado (de C0 a G10, los nombres de FL), con las que SUENAN en el receptor encendidas:
// lo que salió de Tidechart con note-on y todavía no recibió su note-off. Al soltar todo, el teclado tiene que quedar
// apagado: una tecla que se queda encendida es una nota colgada.
class SoundingKeyboardView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SoundingKeyboardView()
    {
        setTooltip (juce::String::fromUTF8 ("Las notas que suenan en el instrumento que recibe la salida de Tidechart. Al soltar "
                                            "las teclas o parar FL tiene que quedar todo apagado."));
    }

    // Un bit por canal para cada nota (0 = no suena).
    void setSounding (const std::uint16_t (&channels)[128])
    {
        if (std::equal (std::begin (channels), std::end (channels), sounding.begin()))
            return;
        std::copy (std::begin (channels), std::end (channels), sounding.begin());
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        auto labels = area.removeFromBottom (14.0f);
        g.setColour (theme::display);
        g.fillRect (area);

        // 75 teclas blancas de C0 a G10.
        constexpr int whiteCount = 75;
        const float whiteWidth = area.getWidth() / static_cast<float> (whiteCount);
        int white = 0;
        for (int note = 0; note < 128; ++note)
        {
            if (isBlack (note))
                continue;
            const auto key = juce::Rectangle<float> (area.getX() + static_cast<float> (white) * whiteWidth, area.getY(), whiteWidth, area.getHeight());
            g.setColour (sounding[static_cast<size_t> (note)] != 0 ? theme::accent : juce::Colour (0xffc9d1d9));
            g.fillRect (key.reduced (0.5f, 0.0f));
            if (note % 12 == 0)
            {
                g.setColour (note == 60 ? theme::accent : theme::dimText);
                g.setFont (juce::FontOptions (9.5f, note == 60 ? juce::Font::bold : juce::Font::plain));
                g.drawText (midi::flNoteName (note), juce::Rectangle<float> (key.getX(), labels.getY(), whiteWidth * 3.0f, labels.getHeight()),
                            juce::Justification::centredLeft);
            }
            ++white;
        }

        // Las negras encima, centradas en el borde entre dos blancas.
        white = 0;
        for (int note = 0; note < 128; ++note)
        {
            if (! isBlack (note))
            {
                ++white;
                continue;
            }
            const float x = area.getX() + static_cast<float> (white) * whiteWidth - whiteWidth * 0.32f;
            const auto key = juce::Rectangle<float> (x, area.getY(), whiteWidth * 0.64f, area.getHeight() * 0.62f);
            g.setColour (sounding[static_cast<size_t> (note)] != 0 ? theme::accent.brighter (0.2f) : theme::background);
            g.fillRect (key);
        }
    }

private:
    [[nodiscard]] static bool isBlack (int note) noexcept
    {
        const int pc = note % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    std::array<std::uint16_t, 128> sounding {};
};

} // namespace tidechart::gui
