#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "gui/CurveView.h"
#include "gui/EbbLookAndFeel.h"
#include "gui/TransportView.h"

class EbbAudioProcessor;

// GUI funcional. Fase 1: el visor del reloj del host y el bypass en la cabecera. Fase 2: la curva del modo tempo con
// sus controles y un medidor de cuánto baja el volumen. Un temporizador a 30 cuadros por segundo recoge lo que publicó
// el audio.
class EbbAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EbbAudioProcessorEditor (EbbAudioProcessor&);
    ~EbbAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void setUpKnob (Knob& knob, const char* parameterId, const juce::String& name, const juce::String& tooltip);
    [[nodiscard]] juce::Rectangle<int> controlsArea() const;

    EbbAudioProcessor& processor;
    ebb::gui::EbbLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    ebb::gui::TransportView transportView;
    ebb::gui::CurveView curveView;
    TransportSnapshot lastSnapshot;

    juce::Label divisionLabel;
    juce::ComboBox divisionBox;
    std::unique_ptr<ComboBoxAttachment> divisionAttachment;
    Knob depthKnob, durationKnob, smoothingKnob;

    juce::TextButton bypassButton { "BYPASS" };
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EbbAudioProcessorEditor)
};
