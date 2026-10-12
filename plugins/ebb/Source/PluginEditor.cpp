#include "PluginEditor.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace
{
constexpr int editorWidth = 580;
constexpr int editorHeight = 270;
constexpr int headerHeight = 44;
constexpr int footerHeight = 28;
constexpr int padding = 12;
constexpr int framesPerSecond = 30;
} // namespace

EbbAudioProcessorEditor::EbbAudioProcessorEditor (EbbAudioProcessor& p) : AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (transportView);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip (juce::String::fromUTF8 ("Escuchar la se\xc3\xb1" "al original. Es el mismo bypass que el de FL "
                                                     "y entra con un fundido de 20 ms (sin clics)."));
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), ebb::params::bypass, bypassButton);

    setSize (editorWidth, editorHeight);
    timerCallback(); // sin esperar al primer cuadro
    startTimerHz (framesPerSecond);
}

EbbAudioProcessorEditor::~EbbAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void EbbAudioProcessorEditor::timerCallback()
{
    TransportSnapshot snapshot;
    if (processor.readTransport (snapshot))
        transportView.setSnapshot (snapshot);
}

void EbbAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace ebb::gui;
    g.fillAll (theme::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 0);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("EBB", header.removeFromLeft (60), juce::Justification::centredLeft);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("ducker  -  Fase 1: reloj del host", header, juce::Justification::centredLeft);

    g.setColour (theme::dimText.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String::fromUTF8 ("La se\xc3\xb1" "al pasa sin cambios. Fase 2: la curva de ducking sincronizada a "
                                        "este reloj."),
                getLocalBounds().removeFromBottom (footerHeight), juce::Justification::centred);
}

void EbbAudioProcessorEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 8);
    bypassButton.setBounds (header.removeFromRight (90));

    transportView.setBounds (getLocalBounds().withTrimmedTop (headerHeight).withTrimmedBottom (footerHeight).reduced (padding, 0));
}
