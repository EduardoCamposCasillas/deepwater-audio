#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "TransportSnapshot.h"
#include "gui/Theme.h"

namespace ebb::gui
{

// Fase 1: lo que Ebb sabe del host, para comprobar a ojo que sigue a FL. La posición grande (compás.tiempo.semicorchea)
// debería ir a la par con la de FL; las luces marcan el tiempo del compás (el primero, más brillante).
class TransportView final : public juce::Component
{
public:
    void setSnapshot (const TransportSnapshot& newSnapshot)
    {
        snapshot = newSnapshot;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        auto area = getLocalBounds().reduced (16, 12);
        auto details = area.removeFromRight (220);
        area.removeFromRight (16);
        paintPosition (g, area);
        paintDetails (g, details);
    }

private:
    static juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

    [[nodiscard]] dsp::MusicalPosition position() const noexcept
    {
        const auto barStart = snapshot.hasLastBarStart ? std::optional<double> (snapshot.lastBarStart) : std::nullopt;
        return dsp::musicalPosition (snapshot.ppq, snapshot.timeSignature, barStart);
    }

    void paintPosition (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        const auto pos = position();
        const auto status = statusText();

        g.setColour (status.second);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (status.first, area.removeFromTop (22), juce::Justification::centredLeft);

        area.removeFromTop (6);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (46.0f, juce::Font::bold));
        g.drawText (juce::String (pos.bar) + " . " + juce::String (pos.beat) + " . " + juce::String (pos.sixteenth),
                    area.removeFromTop (54), juce::Justification::centredLeft);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (utf8 ("comp\xc3\xa1" "s . tiempo . semicorchea"), area.removeFromTop (18),
                    juce::Justification::centredLeft);

        // Una luz por tiempo del compás (como el metrónomo de FL). Con compases muy largos se achican.
        area.removeFromTop (14);
        const int beats = snapshot.timeSignature.numerator;
        const auto lights = area.removeFromTop (26).toFloat();
        const float size = juce::jmin (22.0f, (lights.getWidth() - 6.0f * static_cast<float> (beats - 1)) / static_cast<float> (beats));
        const float gap = juce::jmin (6.0f, size * 0.3f);
        for (int b = 0; b < beats; ++b)
        {
            const auto light = juce::Rectangle<float> (lights.getX() + static_cast<float> (b) * (size + gap), lights.getY(), size, size);
            const bool current = b + 1 == pos.beat;
            const auto colour = b == 0 ? theme::accent : theme::accent.withMultipliedSaturation (0.55f);
            g.setColour (current ? colour : theme::track);
            g.fillEllipse (light);
        }
    }

    void paintDetails (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        const auto& sig = snapshot.timeSignature;
        const auto audio = snapshot.sampleRate > 0.0
                               ? juce::String (juce::roundToInt (snapshot.sampleRate)) + " Hz, " + juce::String (snapshot.blockSize)
                                     + utf8 (" muestras")
                               : juce::String ("-");

        const auto pos = position();
        const std::pair<juce::String, juce::String> rows[] {
            { "FL (B:S)", juce::String (pos.bar) + ":" + juce::String (pos.step).paddedLeft ('0', 2) },
            { "Tempo", juce::String (snapshot.bpm, 2) + " BPM" },
            { utf8 ("Comp\xc3\xa1" "s"), juce::String (sig.numerator) + "/" + juce::String (sig.denominator) },
            { utf8 ("Posici\xc3\xb3n"), juce::String (snapshot.ppq, 3) + " negras" },
            { "Loop", snapshot.looping ? utf8 ("s\xc3\xad") : juce::String ("no") },
            { "Saltos", juce::String (static_cast<int> (snapshot.jumps)) },
            { "Audio", audio },
        };

        const int rowHeight = juce::jmin (22, area.getHeight() / static_cast<int> (std::size (rows)));
        for (const auto& [name, value] : rows)
        {
            auto row = area.removeFromTop (rowHeight);
            g.setColour (theme::dimText);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (name, row.removeFromLeft (70), juce::Justification::centredLeft);
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (value, row, juce::Justification::centredLeft);
        }
    }

    [[nodiscard]] std::pair<juce::String, juce::Colour> statusText() const
    {
        switch (snapshot.source)
        {
            case dsp::ClockSource::host: return { utf8 ("\xe2\x96\xb6  Reproduciendo (sigue a FL)"), theme::accent };
            case dsp::ClockSource::stopped: return { utf8 ("\xe2\x96\xa0  Parado"), theme::dimText };
            case dsp::ClockSource::freeRunning:
            default: return { utf8 ("\xe2\x97\x8b  Reloj libre (el host no informa posici\xc3\xb3n)"), theme::freeRunning };
        }
    }

    TransportSnapshot snapshot;
};

} // namespace ebb::gui
