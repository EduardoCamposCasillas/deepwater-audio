#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "gui/Theme.h"

namespace ebb::gui
{

// Aspecto de los controles de JUCE. Fase 1: solo colores (hay un botón y tooltips); las perillas llegan en la Fase 2.
class EbbLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    EbbLookAndFeel()
    {
        using namespace theme;
        setColourScheme ({ background, panel, control, panelOutline, text, accent, juce::Colours::black, control, text });

        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::Label::textColourId, text);
        setColour (juce::TextButton::buttonColourId, control);
        setColour (juce::TextButton::buttonOnColourId, bypassOn.withAlpha (0.55f));
        setColour (juce::TextButton::textColourOffId, dimText);
        setColour (juce::TextButton::textColourOnId, text);
        setColour (juce::TooltipWindow::backgroundColourId, panel.brighter (0.15f));
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, panelOutline);
    }
};

} // namespace ebb::gui
