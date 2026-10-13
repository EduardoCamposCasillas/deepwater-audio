#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "gui/AnalyzerPanel.h"
#include "gui/BandPanel.h"
#include "gui/EqDisplay.h"
#include "gui/FathomLookAndFeel.h"

class FathomAudioProcessor;

// El visor (lienzo del EQ con el analizador, la curva y los nodos editables de las bandas), debajo el panel de la banda
// elegida, en la cabecera deshacer/rehacer y el bypass, y a la derecha los ajustes del analizador y la perilla de salida.
// Un temporizador a 30 cuadros por segundo actualiza el analizador, la curva, los paneles y los botones de deshacer.
class FathomAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FathomAudioProcessorEditor (FathomAudioProcessor&);
    ~FathomAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override; // Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z) desde cualquier control

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void selectBand (int band);
    void showAnalyzerView (const fathom::analysis::ViewSettings&);

    FathomAudioProcessor& processor;
    fathom::gui::FathomLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    fathom::gui::EqDisplay display;
    fathom::gui::BandPanel bandPanel;
    fathom::gui::AnalyzerPanel analyzerPanel;
    fathom::analysis::ViewSettings shownView; // la vista que muestran el panel y el visor (para notar un proyecto cargado)
    juce::TextButton undoButton { "DESHACER" };
    juce::TextButton redoButton { "REHACER" };
    juce::TextButton bypassButton { "BYPASS" };
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    juce::Slider outputKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label outputLabel;
    std::unique_ptr<SliderAttachment> outputAttachment;
    double lastFrameSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FathomAudioProcessorEditor)
};
