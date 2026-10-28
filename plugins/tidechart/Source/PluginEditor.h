#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "gui/ChordPaletteView.h"
#include "gui/ChordView.h"
#include "gui/MidiMonitorView.h"
#include "gui/ProgressionView.h"
#include "gui/SoundingKeyboardView.h"
#include "gui/TransportView.h"

class TidechartAudioProcessor;

// GUI funcional (la definitiva llega en la Fase 8):
//   cabecera:  TIDECHART | pánico | bypass
//   reloj del host (posición, tempo, compás, loop, saltos)
//   PALETA: tonalidad, modo, tríadas/séptimas, qué teclas disparan los acordes, bajo; los 7 acordes de la escala (Fase 3)
//           y los prestados de otros modos (Fase 4)
//   PROGRESIÓN: teclas o progresión, ritmo, voicing, plantillas, los pasos y el patrón rítmico (Fase 4)
//   SUENA AHORA: teclado con las notas que suenan en el receptor y cuántas; debajo, el ACORDE que forman (Fase 2)
//   monitor MIDI: ENTRADA | SALIDA (los últimos eventos de cada lado)
//   pie: cómo rutear la salida en FL
// Un temporizador a 30 cuadros por segundo recoge lo que publicó el audio.
class TidechartAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TidechartAudioProcessorEditor (TidechartAudioProcessor&);
    ~TidechartAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    TidechartAudioProcessor& processor;

    tidechart::gui::TransportView transportView;
    tidechart::gui::ChordPaletteView paletteView;
    tidechart::gui::ProgressionView progressionView;
    tidechart::gui::SoundingKeyboardView keyboardView;
    tidechart::gui::ChordView chordView;
    tidechart::gui::MidiMonitorView inputMonitor;
    tidechart::gui::MidiMonitorView outputMonitor;
    juce::TextButton panicButton;
    juce::TextButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    juce::TooltipWindow tooltips { this, 600 };

    TransportSnapshot transport;
    std::uint32_t seenProgressionChanges = 0;
    juce::Rectangle<int> keyboardTitleArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TidechartAudioProcessorEditor)
};
