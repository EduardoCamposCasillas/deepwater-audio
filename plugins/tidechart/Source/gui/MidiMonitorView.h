#pragma once

#include <deque>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "TransportSnapshot.h"
#include "gui/Theme.h"
#include "midi/EventLog.h"
#include "midi/MidiText.h"

namespace tidechart::gui
{

// Lista de los últimos eventos MIDI de un lado (lo que entra a Tidechart o lo que sale), el más nuevo arriba: dónde cayó
// (compás.tiempo.semicorchea), el canal y el mensaje. Sirve para comprobar el ruteo en FL: si la columna de ENTRADA se
// mueve, el Piano Roll le llega; si el instrumento no suena, el problema está entre la SALIDA y su puerto MIDI.
class MidiMonitorView final : public juce::Component
{
public:
    MidiMonitorView (juce::String titleText, juce::Colour colourToUse) : title (std::move (titleText)), colour (colourToUse) {}

    void add (const midi::LoggedEvent& event, const TransportSnapshot& transport)
    {
        const auto barStart = transport.hasLastBarStart ? std::optional<double> (transport.lastBarStart) : std::nullopt;
        const auto pos = deepwater::dsp::musicalPosition (event.ppq, transport.timeSignature, barStart);
        Line line;
        line.position = juce::String (pos.bar) + "." + juce::String (pos.beat) + "." + juce::String (pos.sixteenth);
        line.channel = event.message.status == 0 || event.message.kind() == 0xf0 ? juce::String ("-")
                                                                                : juce::String (event.message.channel() + 1);
        line.text = juce::String (midi::describe (event.message, event.size));
        line.noteOn = event.message.isNoteOn();
        lines.push_front (std::move (line));
        if (lines.size() > maxLines)
            lines.pop_back();
        ++total;
        dirty = true;
    }

    void setNoteCount (std::uint32_t notes)
    {
        if (notes != noteCount)
        {
            noteCount = notes;
            dirty = true;
        }
    }

    // Una vez por cuadro, después de los add.
    void refresh()
    {
        if (dirty)
            repaint();
        dirty = false;
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (area.toFloat(), 6.0f);
        area.reduce (10, 8);

        auto header = area.removeFromTop (18);
        g.setColour (colour);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (title, header, juce::Justification::centredLeft);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("notas: " + juce::String (static_cast<int> (noteCount)) + "   eventos: " + juce::String (static_cast<juce::int64> (total)),
                    header, juce::Justification::centredRight);
        area.removeFromTop (4);

        const auto mono = juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain);
        const int rowHeight = 17;
        auto columns = area.removeFromTop (rowHeight);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (juce::String::fromUTF8 ("posici\xc3\xb3n"), columns.removeFromLeft (70), juce::Justification::centredLeft);
        g.drawText ("canal", columns.removeFromLeft (44), juce::Justification::centredLeft);
        g.drawText ("mensaje", columns, juce::Justification::centredLeft);

        if (lines.empty())
        {
            g.setColour (theme::dimText);
            g.setFont (juce::FontOptions (11.5f));
            g.drawFittedText (emptyHint(), area.reduced (0, 6), juce::Justification::topLeft, 4);
            return;
        }

        g.setFont (mono);
        for (const auto& line : lines)
        {
            if (area.getHeight() < rowHeight)
                break;
            auto row = area.removeFromTop (rowHeight);
            g.setColour (theme::dimText);
            g.drawText (line.position, row.removeFromLeft (70), juce::Justification::centredLeft);
            g.drawText (line.channel, row.removeFromLeft (44), juce::Justification::centredLeft);
            g.setColour (line.noteOn ? theme::text : theme::text.withAlpha (0.6f));
            g.drawText (line.text, row, juce::Justification::centredLeft);
        }
    }

private:
    struct Line
    {
        juce::String position, channel, text;
        bool noteOn = false;
    };

    [[nodiscard]] juce::String emptyHint() const
    {
        return colour == theme::input
                   ? juce::String::fromUTF8 ("Todav\xc3\xad" "a no lleg\xc3\xb3 nada. Toca el teclado de FL o pon notas en el Piano "
                                             "Roll de Tidechart.")
                   : juce::String::fromUTF8 ("Lo que sale hacia el instrumento. En FL: engranaje de Tidechart \xe2\x86\x92 MIDI \xe2\x86\x92 "
                                             "Output port; en el instrumento, el mismo n\xc3\xbamero en Input port.");
    }

    static constexpr size_t maxLines = 40;
    juce::String title;
    juce::Colour colour;
    std::deque<Line> lines;
    std::uint64_t total = 0;
    std::uint32_t noteCount = 0;
    bool dirty = true;
};

} // namespace tidechart::gui
