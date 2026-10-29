#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gui/Theme.h"
#include "gui/TheoryNames.h"

namespace tidechart::gui
{

// Fase 7: un botón que se ARRASTRA (no se pulsa) fuera del plugin, al Piano Roll o a la Playlist de FL. Al empezar el
// arrastre pide el archivo (makeFile lo escribe con lo de ese momento) y se lo pasa al sistema como un arrastre de
// archivos normal, igual que si se arrastrara un .mid desde el Explorador de Windows: FL no necesita saber nada de
// Tidechart. Un clic sin arrastrar llama a onClick (para explicar qué hacer).
class MidiDragButton final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<juce::File()> makeFile; // vacío = no hay nada que arrastrar
    std::function<void()> onClick;

    MidiDragButton() { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }

    void setText (const juce::String& newText)
    {
        text = newText;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const bool active = isEnabled();
        g.setColour (pressed ? theme::accent.withAlpha (0.55f) : (isMouseOver() && active ? theme::track : theme::control));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (active ? theme::accent.withAlpha (0.8f) : theme::panelOutline);
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        // Un asa de arrastre (seis puntos) a la izquierda: dice "esto se arrastra" sin palabras.
        auto area = getLocalBounds().reduced (8, 0);
        const auto grip = area.removeFromLeft (8).toFloat();
        g.setColour (active ? theme::text : theme::dimText);
        for (int column = 0; column < 2; ++column)
            for (int row = -1; row <= 1; ++row)
                g.fillEllipse (grip.getX() + column * 5.0f, grip.getCentreY() + row * 5.0f - 1.25f, 2.5f, 2.5f);
        area.removeFromLeft (6);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (text, area, juce::Justification::centredLeft, 1);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseDown (const juce::MouseEvent&) override
    {
        dragging = false;
        pressed = true;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || ! isEnabled() || e.getDistanceFromDragStart() < 5 || ! makeFile)
            return;
        dragging = true;
        const auto file = makeFile();
        if (! file.existsAsFile())
            return;
        // Copia, no mover: el archivo es nuestro y se reescribe en el próximo arrastre.
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                   [safe = juce::Component::SafePointer<MidiDragButton> (this)] {
                                                                       if (safe != nullptr)
                                                                       {
                                                                           safe->pressed = false;
                                                                           safe->repaint();
                                                                       }
                                                                   });
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        pressed = false;
        repaint();
        if (! dragging && onClick)
            onClick();
    }

    void enablementChanged() override { repaint(); }

private:
    juce::String text;
    bool dragging = false, pressed = false;
};

} // namespace tidechart::gui
