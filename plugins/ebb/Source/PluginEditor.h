#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "gui/EbbLookAndFeel.h"
#include "gui/TransportView.h"

class EbbAudioProcessor;

// Fase 1: GUI funcional. El visor del reloj del host y el bypass en la cabecera. Un temporizador a 30 cuadros por
// segundo recoge el último estado que publicó el audio.
class EbbAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EbbAudioProcessorEditor (EbbAudioProcessor&);
    ~EbbAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;

    EbbAudioProcessor& processor;
    ebb::gui::EbbLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    ebb::gui::TransportView transportView;
    juce::TextButton bypassButton { "BYPASS" };
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EbbAudioProcessorEditor)
};
