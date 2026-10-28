#pragma once

#include <cmath>
#include <optional>
#include <tuple>

#include <juce_gui_basics/juce_gui_basics.h>

#include "TransportSnapshot.h"
#include "gui/Theme.h"

namespace tidechart::gui
{

// Lo que Tidechart sabe del host, para comprobar a ojo que sigue a FL (la misma tira que Ebb): estado, posición
// compás.tiempo.semicorchea (debería ir a la par con la de FL), una luz por tiempo del compás (la primera, más
// brillante) y los detalles (tempo, compás, paso B:S de FL, loop, saltos y audio).
class TransportView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    TransportView()
    {
        setTooltip (juce::String::fromUTF8 ("El reloj del host tal como lo ve Tidechart. La posici\xc3\xb3n (comp\xc3\xa1s . tiempo . "
                                            "semicorchea) y B:S deben ir a la par con FL. Saltos cuenta los loops y los "
                                            "saltos en la l\xc3\xadnea de tiempo: desde la Fase 4 la progresi\xc3\xb3n "
                                            "seguir\xc3\xa1 a esa posici\xc3\xb3n."));
    }

    // Solo se repinta si cambia algo de lo que se ve (con FL parado, nada: no gasta CPU).
    void setSnapshot (const TransportSnapshot& newSnapshot)
    {
        const auto before = shownState();
        snapshot = newSnapshot;
        if (shownState() != before)
            repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        const auto status = statusText();
        g.setColour (status.second);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (status.first, area.removeFromLeft (112), juce::Justification::centredLeft);

        const auto pos = position();
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText (juce::String (pos.bar) + " . " + juce::String (pos.beat) + " . " + juce::String (pos.sixteenth),
                    area.removeFromLeft (112), juce::Justification::centredLeft);

        // Una luz por tiempo del compás (como el metrónomo de FL). Con compases muy largos se achican.
        const int beats = snapshot.timeSignature.numerator;
        auto lights = area.removeFromLeft (110).toFloat();
        const float size = juce::jmin (12.0f, (lights.getWidth() - 4.0f * static_cast<float> (beats - 1)) / static_cast<float> (beats));
        const float gap = juce::jmin (4.0f, size * 0.3f);
        for (int b = 0; b < beats; ++b)
        {
            const auto light = juce::Rectangle<float> (lights.getX() + static_cast<float> (b) * (size + gap), lights.getCentreY() - size * 0.5f, size, size);
            const auto colour = b == 0 ? theme::accent : theme::accent.withMultipliedSaturation (0.55f);
            g.setColour (b + 1 == pos.beat ? colour : theme::track);
            g.fillEllipse (light);
        }

        const auto& sig = snapshot.timeSignature;
        const auto audio = snapshot.sampleRate > 0.0
                               ? juce::String (snapshot.sampleRate / 1000.0, 1) + " kHz / " + juce::String (snapshot.blockSize)
                               : juce::String ("-");
        const std::pair<juce::String, juce::String> details[] {
            { "", juce::String (snapshot.bpm, 2) + " BPM" },
            { "", juce::String (sig.numerator) + "/" + juce::String (sig.denominator) },
            { "B:S", juce::String (pos.bar) + ":" + juce::String (pos.step).paddedLeft ('0', 2) },
            { "loop", snapshot.looping ? juce::String::fromUTF8 ("s\xc3\xad") : juce::String ("no") },
            { "saltos", juce::String (static_cast<int> (snapshot.jumps)) },
            { "audio", audio },
        };
        for (const auto& [name, value] : details)
        {
            const juce::Font nameFont (juce::FontOptions (11.0f));
            const juce::Font valueFont (juce::FontOptions (12.0f, juce::Font::bold));
            const int nameWidth = name.isEmpty() ? 0 : juce::roundToInt (juce::GlyphArrangement::getStringWidth (nameFont, name)) + 4;
            const int valueWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (valueFont, value)) + 2;
            if (nameWidth + valueWidth > area.getWidth())
                break;
            g.setColour (theme::dimText);
            g.setFont (nameFont);
            g.drawText (name, area.removeFromLeft (nameWidth), juce::Justification::centredLeft);
            g.setColour (theme::text);
            g.setFont (valueFont);
            g.drawText (value, area.removeFromLeft (valueWidth), juce::Justification::centredLeft);
            area.removeFromLeft (14);
        }
    }

private:
    [[nodiscard]] deepwater::dsp::MusicalPosition position() const noexcept
    {
        const auto barStart = snapshot.hasLastBarStart ? std::optional<double> (snapshot.lastBarStart) : std::nullopt;
        return deepwater::dsp::musicalPosition (snapshot.ppq, snapshot.timeSignature, barStart);
    }

    // Todo lo que paint() escribe o enciende, tal como se ve (el tempo con 2 decimales).
    [[nodiscard]] auto shownState() const noexcept
    {
        const auto pos = position();
        return std::make_tuple (snapshot.source, pos.bar, pos.beat, pos.sixteenth, pos.step, std::lround (snapshot.bpm * 100.0),
                                snapshot.timeSignature.numerator, snapshot.timeSignature.denominator, snapshot.looping,
                                snapshot.jumps, std::lround (snapshot.sampleRate / 100.0), snapshot.blockSize);
    }

    [[nodiscard]] std::pair<juce::String, juce::Colour> statusText() const
    {
        switch (snapshot.source)
        {
            case deepwater::dsp::ClockSource::host: return { juce::String::fromUTF8 ("\xe2\x96\xb6  Reproduciendo"), theme::accent };
            case deepwater::dsp::ClockSource::stopped: return { juce::String::fromUTF8 ("\xe2\x96\xa0  Parado"), theme::dimText };
            case deepwater::dsp::ClockSource::freeRunning:
            default: return { juce::String::fromUTF8 ("\xe2\x97\x8b  Reloj libre"), theme::freeRunning };
        }
    }

    TransportSnapshot snapshot;
};

} // namespace tidechart::gui
