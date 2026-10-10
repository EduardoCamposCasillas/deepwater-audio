#include "PluginEditor.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace
{
constexpr int editorWidth = 520;
constexpr int editorHeight = 340;
constexpr int headerHeight = 44;
constexpr int padding = 12;
constexpr int meterWidth = 96;
constexpr int knobSize = 110;
constexpr int framesPerSecond = 30;
} // namespace

RiptideAudioProcessorEditor::RiptideAudioProcessorEditor (RiptideAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p), inputMeter ("IN", p.getInputMeter()), outputMeter ("OUT", p.getOutputMeter())
{
    namespace id = riptide::params;
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    setUpKnob (inputKnob, id::inputGain, "Input",
               juce::String::fromUTF8 ("Nivel con el que la se\xc3\xb1" "al entra al saturador (lo que mide IN). Desde la "
                                       "Fase 2, subirlo = saturar m\xc3\xa1s."));
    setUpKnob (outputKnob, id::outputGain, "Output",
               juce::String::fromUTF8 ("Volumen final. Sirve para igualar el volumen con el bypass y comparar a "
                                       "o\xc3\xad" "do justo."));

    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip (juce::String::fromUTF8 ("Escuchar la se\xc3\xb1" "al original. Es el mismo bypass que el de FL "
                                                     "y entra con un fundido de 20 ms (sin clics)."));
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), id::bypass, bypassButton);

    setSize (editorWidth, editorHeight);
    lastFrameMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (framesPerSecond);
}

RiptideAudioProcessorEditor::~RiptideAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void RiptideAudioProcessorEditor::setUpKnob (Knob& knob, const char* parameterId, const juce::String& name,
                                             const juce::String& tooltip)
{
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    knob.slider.setTooltip (tooltip);
    knob.slider.setDoubleClickReturnValue (true, 0.0); // doble clic = 0 dB
    addAndMakeVisible (knob.slider);
    knob.attachment = std::make_unique<SliderAttachment> (processor.getParameters(), parameterId, knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    knob.label.setColour (juce::Label::textColourId, riptide::gui::theme::dimText);
    addAndMakeVisible (knob.label);
}

void RiptideAudioProcessorEditor::timerCallback()
{
    // El tiempo real entre cuadros (no 1/30 s): si Windows retrasa el temporizador, los medidores caen igual de rápido.
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double elapsed = juce::jlimit (0.0, 0.5, (now - lastFrameMs) / 1000.0);
    lastFrameMs = now;

    const int channels = processor.getMeteredChannels();
    inputMeter.update (channels, elapsed);
    outputMeter.update (channels, elapsed);
}

void RiptideAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace riptide::gui;
    g.fillAll (theme::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 0);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("RIPTIDE", header.removeFromLeft (110), juce::Justification::centredLeft);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("saturador  -  Fase 1: passthrough", header, juce::Justification::centredLeft);

    // Panel de las perillas, entre los dos medidores.
    const auto body = getLocalBounds().withTrimmedTop (headerHeight).reduced (padding);
    auto knobsPanel = body.reduced (meterWidth + padding, 0).toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (knobsPanel, 8.0f);
    g.setColour (theme::panelOutline);
    g.drawRoundedRectangle (knobsPanel.reduced (0.5f), 8.0f, 1.0f);

    // Flecha del recorrido de la señal: IN → perillas → OUT.
    g.setColour (theme::dimText.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String::fromUTF8 ("entrada \xe2\x86\x92 input \xe2\x86\x92 (saturaci\xc3\xb3n, Fase 2) \xe2\x86\x92 output \xe2\x86\x92 salida"),
                knobsPanel.removeFromBottom (28.0f), juce::Justification::centred);
}

void RiptideAudioProcessorEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 8);
    bypassButton.setBounds (header.removeFromRight (90));

    auto body = getLocalBounds().withTrimmedTop (headerHeight).reduced (padding);
    inputMeter.setBounds (body.removeFromLeft (meterWidth));
    outputMeter.setBounds (body.removeFromRight (meterWidth));

    auto knobs = body.reduced (padding, 0).withTrimmedBottom (28);
    const int knobWidth = knobs.getWidth() / 2;
    for (auto* knob : { &inputKnob, &outputKnob })
    {
        auto column = knobs.removeFromLeft (knobWidth);
        auto content = column.withSizeKeepingCentre (column.getWidth(), knobSize + 20 + 24);
        knob->label.setBounds (content.removeFromTop (20));
        knob->slider.setBounds (content);
    }
}
