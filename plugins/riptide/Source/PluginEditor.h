#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "gui/LevelMeterView.h"
#include "gui/RiptideLookAndFeel.h"

class RiptideAudioProcessor;

// Fase 1: GUI funcional. Medidor IN | perillas Input y Output | medidor OUT, y el bypass en la cabecera.
// Un temporizador a 30 cuadros por segundo recoge los niveles que publicó el audio y redibuja los medidores.
class RiptideAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RiptideAudioProcessorEditor (RiptideAudioProcessor&);
    ~RiptideAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void setUpKnob (Knob& knob, const char* parameterId, const juce::String& name, const juce::String& tooltip);

    RiptideAudioProcessor& processor;
    riptide::gui::RiptideLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    riptide::gui::LevelMeterView inputMeter;
    riptide::gui::LevelMeterView outputMeter;
    Knob inputKnob;
    Knob outputKnob;
    juce::TextButton bypassButton { "BYPASS" };
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    double lastFrameMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiptideAudioProcessorEditor)
};
