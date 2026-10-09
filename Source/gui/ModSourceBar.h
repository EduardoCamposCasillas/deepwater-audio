#pragma once

#include <array>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gui/ModKnob.h"
#include "gui/Theme.h"

namespace undertow::gui
{

// Barra inferior con las fuentes de modulación. Cada fuente es una "ficha" que se arrastra y se suelta sobre una
// perilla con destino en la matriz. A la derecha, una línea de mensajes (qué se conectó, avisos, pistas).
class ModSourceBar final : public juce::Component
{
public:
    ModSourceBar()
    {
        for (size_t i = 0; i < chips.size(); ++i)
        {
            chips[i] = std::make_unique<Chip> (static_cast<synth::ModSource> (i + 1));
            addAndMakeVisible (*chips[i]);
        }
    }

    // Cuántas rutas usa cada fuente (se muestra junto a su nombre).
    void setUsage (const ModSlots& slots)
    {
        for (auto& chip : chips)
        {
            int count = 0;
            for (const auto& slot : slots)
                count += slot.isRouted() && slot.source == chip->source ? 1 : 0;
            chip->setRouteCount (count);
        }
    }

    [[nodiscard]] const juce::String& getMessage() const noexcept { return message; }

    void setMessage (const juce::String& newMessage)
    {
        if (newMessage != message)
        {
            message = newMessage;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("MOD", getLocalBounds().removeFromLeft (labelWidth).withTrimmedLeft (10), juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (message.isNotEmpty() ? message : defaultHint(), messageArea, juce::Justification::centredLeft, 2, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6, 5);
        area.removeFromLeft (labelWidth - 6);
        for (auto& chip : chips)
        {
            chip->setBounds (area.removeFromLeft (chipWidth));
            area.removeFromLeft (4);
        }
        messageArea = area.withTrimmedLeft (8).withTrimmedRight (4);
    }

private:
    static constexpr int labelWidth = 44;
    static constexpr int chipWidth = 60;

    static juce::String defaultHint()
    {
        return juce::String::fromUTF8 ("Arrastra una fuente sobre una perilla para modularla. El anillo de color "
                                       "muestra cu\xc3\xa1nto la mueve.");
    }

    class Chip final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit Chip (synth::ModSource s) : source (s)
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            setTooltip (juce::String::fromUTF8 ("Arrastra ") + name()
                        + juce::String::fromUTF8 (" sobre una perilla (Position, Cutoff, Level, Warp\xe2\x80\xa6) para modularla."));
        }

        void setRouteCount (int count)
        {
            if (count != routeCount)
            {
                routeCount = count;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
            const auto colour = theme::sourceColour (source);
            g.setColour (isMouseOver() ? colour.withAlpha (0.25f) : theme::control);
            g.fillRoundedRectangle (bounds, 5.0f);
            g.setColour (colour.withAlpha (routeCount > 0 ? 0.9f : 0.45f));
            g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

            g.setColour (colour);
            g.fillRoundedRectangle (bounds.withWidth (4.0f).reduced (0.0f, 5.0f).translated (5.0f, 0.0f), 2.0f);

            g.setColour (theme::text);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            auto textArea = bounds.withTrimmedLeft (12.0f).withTrimmedRight (2.0f);
            if (routeCount > 0)
            {
                g.setColour (colour);
                g.drawText (juce::String (routeCount), textArea.removeFromRight (12.0f), juce::Justification::centred);
                g.setColour (theme::text);
            }
            g.drawFittedText (shortName(), textArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        }

        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (e.getDistanceFromDragStart() < 4)
                return;
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
                if (! container->isDragAndDropActive())
                    container->startDragging (ModKnob::dragDescription (source), this);
        }

        const synth::ModSource source;

    private:
        [[nodiscard]] juce::String name() const { return synth::modSourceNames[static_cast<size_t> (source)]; }

        [[nodiscard]] juce::String shortName() const
        {
            switch (source)
            {
                case synth::ModSource::env1: return "ENV 1";
                case synth::ModSource::env2: return "ENV 2";
                case synth::ModSource::env3: return "ENV 3";
                case synth::ModSource::lfo1: return "LFO 1";
                case synth::ModSource::lfo2: return "LFO 2";
                case synth::ModSource::velocity: return "VEL";
                case synth::ModSource::key: return "KEY";
                case synth::ModSource::modWheel: return "M.WHEEL";
                case synth::ModSource::aftertouch: return "AFTERT.";
                case synth::ModSource::none: break;
            }
            return {};
        }

        int routeCount = 0;
    };

    std::array<std::unique_ptr<Chip>, synth::numModSources - 1> chips;
    juce::Rectangle<int> messageArea;
    juce::String message;
};

} // namespace undertow::gui
