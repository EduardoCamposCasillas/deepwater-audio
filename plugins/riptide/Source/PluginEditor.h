#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Saturator.h"

#include "gui/LevelMeterView.h"
#include "gui/RiptideLookAndFeel.h"

class RiptideAudioProcessor;

// GUI funcional (la profesional llega en la Fase 8). Medidor IN | modos, tono (cortes con PRE/POST, emphasis, tilt) y
// perillas Input (drive) y Output | medidor OUT; en la cabecera, el selector de oversampling y el bypass.
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
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void setUpKnob (Knob& knob, const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void setUpPositionButton (juce::TextButton& button, std::unique_ptr<ButtonAttachment>& attachment, const char* parameterId);
    void setUpSectionLabel (juce::Label& label, const juce::String& text);
    void showMode (int index);

    RiptideAudioProcessor& processor;
    riptide::gui::RiptideLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    riptide::gui::LevelMeterView inputMeter;
    riptide::gui::LevelMeterView outputMeter;
    Knob inputKnob;
    Knob outputKnob;
    juce::Label levelSection;

    // Tono (Fase 4). Cada corte tiene su interruptor: apagado = PRE (antes de la curva), encendido = POST (después).
    juce::Label toneSection;
    Knob lowCutKnob, highCutKnob, emphasisKnob, emphasisFreqKnob, tiltKnob;
    juce::TextButton lowCutPositionButton, highCutPositionButton;
    std::unique_ptr<ButtonAttachment> lowCutPositionAttachment, highCutPositionAttachment;
    juce::TextButton bypassButton { "BYPASS" };
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    juce::Label oversamplingLabel;
    juce::ComboBox oversamplingBox;
    std::unique_ptr<ComboBoxAttachment> oversamplingAttachment;

    // Un botón por modo (funcionan como radio: solo uno encendido). JUCE no trae un attachment de "grupo de botones"
    // para un parámetro choice; ParameterAttachment hace de puente en los dos sentidos (clic → parámetro, y
    // automatización o preset → botones).
    std::array<juce::TextButton, riptide::dsp::numModes> modeButtons;
    juce::Label modeDescription;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;

    double lastFrameMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiptideAudioProcessorEditor)
};
