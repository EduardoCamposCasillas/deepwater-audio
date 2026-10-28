#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"

namespace theme = tidechart::gui::theme;

namespace
{
juce::String utf8 (const char* text)
{
    return juce::String::fromUTF8 (text);
}

constexpr int editorWidth = 900;
constexpr int editorHeight = 740;
} // namespace

TidechartAudioProcessorEditor::TidechartAudioProcessorEditor (TidechartAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      paletteView (p.getParameters(), [&p] (int degree) { p.previewChord (degree); }),
      inputMonitor (utf8 ("ENTRADA  \xe2\x80\x94  lo que llega a Tidechart"), theme::input),
      outputMonitor (utf8 ("SALIDA  \xe2\x80\x94  lo que va al instrumento"), theme::accent)
{
    addAndMakeVisible (transportView);
    addAndMakeVisible (paletteView);
    addAndMakeVisible (keyboardView);
    addAndMakeVisible (chordView);
    addAndMakeVisible (inputMonitor);
    addAndMakeVisible (outputMonitor);

    panicButton.setButtonText (utf8 ("P\xc3\xa1nico"));
    panicButton.setTooltip (utf8 ("Apaga todo lo que suena: un note-off por cada nota que sali\xc3\xb3 de aqu\xc3\xad y no se apag\xc3\xb3, "
                                  "y All Notes Off (CC 123) en los 16 canales por si el instrumento tiene notas colgadas "
                                  "de otro sitio."));
    panicButton.setColour (juce::TextButton::buttonColourId, theme::control);
    panicButton.setColour (juce::TextButton::textColourOffId, theme::text);
    panicButton.onClick = [this] { processor.panic(); };
    addAndMakeVisible (panicButton);

    bypassButton.setButtonText ("Bypass");
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip (utf8 ("Deja pasar el MIDI tal cual, sin acordes: cada tecla suena sola. Es el mismo bypass que el de FL. "
                                   "Al activarlo o quitarlo se apaga lo que sonaba."));
    bypassButton.setColour (juce::TextButton::buttonColourId, theme::control);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, theme::warning);
    bypassButton.setColour (juce::TextButton::textColourOffId, theme::text);
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.getParameters(),
                                                                                                tidechart::params::bypass, bypassButton);

    setSize (editorWidth, editorHeight);
    startTimerHz (30);
}

TidechartAudioProcessorEditor::~TidechartAudioProcessorEditor()
{
    stopTimer();
}

void TidechartAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto area = getLocalBounds().reduced (16, 0);
    auto header = area.removeFromTop (52);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("TIDECHART", header.removeFromLeft (150), juce::Justification::centredLeft);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (utf8 ("acordes y melod\xc3\xad" "as por tonalidad  \xc2\xb7  Fase 3: un acorde por tecla"), header, juce::Justification::centredLeft);

    // Paneles detrás del reloj y del teclado.
    g.setColour (theme::panel);
    g.fillRoundedRectangle (transportView.getBounds().expanded (10, 4).toFloat(), 6.0f);
    g.fillRoundedRectangle (paletteView.getBounds().expanded (10, 6).toFloat(), 6.0f);
    g.fillRoundedRectangle (keyboardView.getBounds().getUnion (chordView.getBounds()).withTop (keyboardTitleArea.getY()).expanded (10, 6).toFloat(), 6.0f);

    auto title = keyboardTitleArea;
    g.setColour (theme::accent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("SUENA AHORA", title.removeFromLeft (110), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (11.5f));
    const int sounding = transport.soundingCount;
    g.setColour (sounding > 0 ? theme::text : theme::dimText);
    juce::String summary = sounding == 0 ? utf8 ("nada (sin notas colgadas)")
                                         : juce::String (sounding) + (sounding == 1 ? " nota" : " notas");
    summary << utf8 ("   \xc2\xb7   todo apagado ") << static_cast<int> (transport.releases)
            << (transport.releases == 1 ? " vez" : " veces") << utf8 (" (p\xc3\xa1nico, reinicio del audio, cargar el proyecto)");
    g.drawText (summary, title, juce::Justification::centredLeft);
    if (transport.bypassed)
    {
        g.setColour (theme::warning);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText ("BYPASS", keyboardTitleArea, juce::Justification::centredRight);
    }

    auto footer = getLocalBounds().reduced (16, 0).removeFromBottom (34);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.5f));
    g.drawFittedText (utf8 ("Ruteo en FL: engranaje de Tidechart \xe2\x86\x92 MIDI \xe2\x86\x92 Output port = 1. En el instrumento que debe sonar "
                            "(Undertow, FLEX\xe2\x80\xa6): MIDI \xe2\x86\x92 Input port = 1. Las notas van en el Piano Roll de Tidechart."),
                      footer, juce::Justification::centredLeft, 2);
}

void TidechartAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16, 0);
    auto header = area.removeFromTop (52);
    bypassButton.setBounds (header.removeFromRight (90).reduced (0, 12));
    header.removeFromRight (8);
    panicButton.setBounds (header.removeFromRight (90).reduced (0, 12));

    area.removeFromTop (4);
    transportView.setBounds (area.removeFromTop (40).reduced (10, 0));
    area.removeFromTop (18);

    paletteView.setBounds (area.removeFromTop (146).reduced (10, 0));
    area.removeFromTop (18);

    auto keyboard = area.removeFromTop (136).reduced (10, 0);
    keyboardTitleArea = keyboard.removeFromTop (20);
    keyboard.removeFromTop (4);
    chordView.setBounds (keyboard.removeFromBottom (34));
    keyboard.removeFromBottom (6);
    keyboardView.setBounds (keyboard);
    area.removeFromTop (18);

    area.removeFromBottom (34);
    area.removeFromBottom (8);
    auto left = area.removeFromLeft ((area.getWidth() - 12) / 2);
    area.removeFromLeft (12);
    inputMonitor.setBounds (left);
    outputMonitor.setBounds (area);
}

void TidechartAudioProcessorEditor::timerCallback()
{
    const auto before = std::make_tuple (transport.soundingCount, transport.releases, transport.bypassed);
    if (processor.readTransport (transport))
    {
        transportView.setSnapshot (transport);
        keyboardView.setSounding (transport.soundingChannels);
        chordView.setSounding (transport.soundingChannels);
        paletteView.update (processor.currentChordSettings (transport.bypassed), transport.heldDegrees);
        if (std::make_tuple (transport.soundingCount, transport.releases, transport.bypassed) != before)
            repaint (keyboardTitleArea);
    }

    // La tonalidad para nombrar el acorde que suena (aunque el audio no publique nada nuevo, p. ej. con FL parado).
    chordView.setKey (processor.currentChordSettings (transport.bypassed).key);

    processor.drainMidiLog ([this] (const tidechart::midi::LoggedEvent& event) {
        (event.output ? outputMonitor : inputMonitor).add (event, transport);
    });
    inputMonitor.setNoteCount (processor.notesIn());
    outputMonitor.setNoteCount (processor.notesOut());
    inputMonitor.refresh();
    outputMonitor.refresh();
}
