#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "gui/CurveEditor.h"
#include "gui/CurveView.h"
#include "gui/EbbLookAndFeel.h"
#include "gui/MidiActivityView.h"
#include "gui/TransportView.h"

class EbbAudioProcessor;

// GUI funcional. Fase 1: el visor del reloj del host y el bypass en la cabecera. Fase 2: la curva del modo tempo con
// sus controles y un medidor de cuánto baja el volumen. Fase 3: el menú de formas y el editor de la curva. Fase 4: el
// modo (tempo / MIDI), el filtro de nota, la velocidad y la luz de actividad MIDI. Un temporizador a 30 cuadros por
// segundo recoge lo que publicó el audio.
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
    [[nodiscard]] juce::Rectangle<int> editorPanelArea() const;
    void showCurve();
    void setUpComboLabel (juce::Label& label, const juce::String& text);
    void setUpCombo (juce::ComboBox& box, const char* parameterId, const juce::String& tooltip,
                     std::unique_ptr<ComboBoxAttachment>& attachment);
    void useLastMidiNote();
    void updateModeControls (ebb::dsp::TriggerMode mode);

    EbbAudioProcessor& processor;
    ebb::gui::EbbLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    ebb::gui::TransportView transportView;
    ebb::gui::CurveView curveView;
    ebb::gui::CurveEditor curveEditor;
    int shownCurveVersion = -1;
    TransportSnapshot lastSnapshot;

    juce::Label modeLabel;
    juce::ComboBox modeBox;
    std::unique_ptr<ComboBoxAttachment> modeAttachment;
    juce::Label noteLabel;
    juce::ComboBox noteBox;
    std::unique_ptr<ComboBoxAttachment> noteAttachment;
    juce::Label midiLabel;
    ebb::gui::MidiActivityView midiActivity;
    juce::TextButton useLastNoteButton;
    juce::TextButton velocityButton;
    std::unique_ptr<ButtonAttachment> velocityAttachment;
    int lastMidiNote = -1;
    ebb::dsp::TriggerMode shownMode = ebb::dsp::TriggerMode::tempo;

    juce::Label divisionLabel;
    juce::ComboBox divisionBox;
    std::unique_ptr<ComboBoxAttachment> divisionAttachment;
    juce::Label shapeLabel;
    juce::ComboBox shapeBox;
    Knob depthKnob, durationKnob, smoothingKnob;

    juce::TextButton bypassButton { "BYPASS" };
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EbbAudioProcessorEditor)
};
