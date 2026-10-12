#include "PluginEditor.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace
{
constexpr int editorWidth = 960;
constexpr int editorHeight = 620;
constexpr int bandPanelHeight = 112;
constexpr int headerHeight = 44;
constexpr int footerHeight = 26;
constexpr int controlsWidth = 110;
constexpr int padding = 12;
constexpr int framesPerSecond = 30;
} // namespace

FathomAudioProcessorEditor::FathomAudioProcessorEditor (FathomAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p), display (p), bandPanel (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (display);
    addAndMakeVisible (bandPanel);
    // Elegir una banda (en el selector del panel o con un clic en su nodo) la muestra en los dos.
    display.onBandClicked = [this] (int band) { selectBand (band); };
    bandPanel.onBandChosen = [this] (int band) { selectBand (band); };

    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip (juce::String::fromUTF8 ("Escuchar la se\xc3\xb1" "al original. Es el mismo bypass que el de FL "
                                                     "y entra con un fundido de 20 ms (sin clics)."));
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), fathom::params::bypass, bypassButton);

    outputKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    outputKnob.getProperties().set ("bipolar", true);
    outputKnob.setTooltip (juce::String::fromUTF8 ("Volumen de salida (\xc2\xb1" "24 dB). Sirve para compensar lo que suben o "
                                                   "bajan las bandas: al comparar con el BYPASS, lo m\xc3\xa1s fuerte siempre "
                                                   "parece mejor, as\xc3\xad que iguala el volumen antes de juzgar. Doble clic "
                                                   "= 0 dB."));
    addAndMakeVisible (outputKnob);
    outputAttachment = std::make_unique<SliderAttachment> (processor.getParameters(), fathom::params::outputGain, outputKnob);
    outputKnob.setDoubleClickReturnValue (true, 0.0);
    outputLabel.setText ("SALIDA", juce::dontSendNotification);
    outputLabel.setJustificationType (juce::Justification::centred);
    outputLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    outputLabel.setColour (juce::Label::textColourId, fathom::gui::theme::dimText);
    addAndMakeVisible (outputLabel);

    setSize (editorWidth, editorHeight);
    processor.setAnalyzerActive (true);
    lastFrameSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (framesPerSecond);
}

FathomAudioProcessorEditor::~FathomAudioProcessorEditor()
{
    processor.setAnalyzerActive (false);
    setLookAndFeel (nullptr);
}

void FathomAudioProcessorEditor::timerCallback()
{
    // El tiempo real entre cuadros (el temporizador no es exacto): la caída del analizador se mide en dB por segundo.
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double elapsed = juce::jlimit (0.0, 0.2, now - lastFrameSeconds);
    lastFrameSeconds = now;
    display.update (elapsed);
    bandPanel.refresh();
}

void FathomAudioProcessorEditor::selectBand (int band)
{
    bandPanel.showBand (band);
    display.setSelectedBand (band);
}

void FathomAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace fathom::gui;
    g.fillAll (theme::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 0);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("FATHOM", header.removeFromLeft (100), juce::Justification::centredLeft);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String::fromUTF8 ("EQ param\xc3\xa9trico  -  Fase 3: todas las formas y 24 bandas"), header, juce::Justification::centredLeft);

    g.setColour (theme::dimText.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String::fromUTF8 ("Elige una banda en el panel (o con un clic en su nodo), enci\xc3\xa9ndela y "
                                        "prueba sus formas. Pasa el mouse por los controles para ver qu\xc3\xa9 hace cada uno. "
                                        "Fase 4: editar sobre el visor."),
                getLocalBounds().removeFromBottom (footerHeight), juce::Justification::centred);
}

void FathomAudioProcessorEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 8);
    bypassButton.setBounds (header.removeFromRight (90));

    auto content = getLocalBounds().withTrimmedTop (headerHeight).withTrimmedBottom (footerHeight).reduced (padding, 0);
    auto controls = content.removeFromRight (controlsWidth);
    content.removeFromRight (padding);
    bandPanel.setBounds (content.removeFromBottom (bandPanelHeight));
    content.removeFromBottom (padding);
    display.setBounds (content);

    // La salida abajo a la derecha, junto al final de la cadena (como en una consola).
    auto knobArea = controls.removeFromBottom (130);
    outputLabel.setBounds (knobArea.removeFromTop (18));
    outputKnob.setBounds (knobArea);
}
