#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Bands.h"
#include "dsp/Saturator.h"

#include "gui/LevelMeterView.h"
#include "gui/RiptideLookAndFeel.h"

class RiptideAudioProcessor;

// GUI funcional (la profesional llega en la Fase 8). Medidor IN | modos, tono (cortes con PRE/POST, emphasis, tilt),
// bandas (número de bandas, cruces entre ellas y, por banda, modo, drive, solo, mute y su auto gain), nivel y mezcla
// (Input, Mix, Output, AUTO GAIN, DELTA) | medidor OUT; en la cabecera, el selector de oversampling y el bypass.
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
    void setUpToggle (juce::TextButton& button, std::unique_ptr<ButtonAttachment>& attachment, const char* parameterId,
                      const juce::String& tooltip);
    void updateAutoGainReadout();
    void showMode (int index);
    void showBandCount (int count);
    void updateBandReadouts();
    void paintBands (juce::Graphics& g);

    RiptideAudioProcessor& processor;
    riptide::gui::RiptideLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    riptide::gui::LevelMeterView inputMeter;
    riptide::gui::LevelMeterView outputMeter;
    Knob inputKnob;
    Knob outputKnob;
    juce::Label levelSection;

    // Mezcla (Fase 5).
    Knob mixKnob;
    juce::TextButton autoGainButton { "AUTO GAIN" };
    juce::TextButton deltaButton { "DELTA" };
    std::unique_ptr<ButtonAttachment> autoGainAttachment, deltaAttachment;
    juce::Label autoGainReadout;

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

    // Bandas (Fase 6). Una columna por banda y, entre dos columnas, la perilla del cruce que las separa. Las bandas que
    // no existen (más allá del número elegido) se ven apagadas.
    struct BandStrip
    {
        juce::Label title, range; // "BANDA 2" y "120 Hz - 1.0 kHz" (los cruces que suenan de verdad)
        juce::ComboBox mode;
        std::unique_ptr<ComboBoxAttachment> modeAttachment;
        juce::Slider drive { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<SliderAttachment> driveAttachment;
        juce::TextButton solo { "S" }, mute { "M" };
        std::unique_ptr<ButtonAttachment> soloAttachment, muteAttachment;
        juce::Label autoGain; // la compensación de esta banda
        juce::Rectangle<int> area; // fondo de la columna (paint)
    };
    juce::Label bandsSection;
    std::array<juce::TextButton, riptide::dsp::maxBands> bandCountButtons;
    std::unique_ptr<juce::ParameterAttachment> bandCountAttachment;
    std::array<BandStrip, riptide::dsp::maxBands> bandStrips;
    std::array<Knob, riptide::dsp::maxCrossovers> crossoverKnobs;
    int shownBands = 1;

    double lastFrameMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiptideAudioProcessorEditor)
};
