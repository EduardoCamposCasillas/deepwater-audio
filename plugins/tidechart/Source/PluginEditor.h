#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "gui/ChordPaletteView.h"
#include "gui/ChordView.h"
#include "gui/KeyDetectView.h"
#include "gui/LinesView.h"
#include "gui/MidiMonitorView.h"
#include "gui/ProgressionView.h"
#include "gui/SoundingKeyboardView.h"
#include "gui/TransportView.h"

class TidechartAudioProcessor;

// Dos vistas con los mismos paneles (lo que se cambia en una se ve en la otra):
//
// SIMPLE (por defecto): lo necesario para que alguien que empieza encuentre acordes, progresiones y melodías.
//   cabecera:  TIDECHART | vista completa | pánico | bypass
//   izquierda, de arriba abajo, en el orden en que se usa:
//     TONALIDAD (tónica y modo; a la derecha, Escuchar para detectarla) y los acordes de la escala + los prestados
//     PROGRESIÓN: qué tocan las teclas, ritmo, ideas ("Otra idea"), armar con clic, los pasos y el patrón
//     una guía de qué hacer ahora (según el modo, si FL reproduce, si suena algo) y el ruteo de FL
//   derecha, MELODÍA Y BAJO: capas, estilo y "Otra" de cada línea, piano roll que explica cada nota y, abajo, arrastrar
//     cada capa al Piano Roll de FL (Fase 7)
//
// COMPLETA: todo lo construido en las Fases 1–6.
//   cabecera:  TIDECHART | vista simple | pánico | bypass
//   reloj del host (posición, tempo, compás, loop, saltos)
//   PALETA: tonalidad, modo, tríadas/séptimas, qué teclas disparan los acordes, bajo; los 7 acordes de la escala (Fase 3)
//           y los prestados de otros modos (Fase 4)
//   PROGRESIÓN: teclas o progresión, ritmo, voicing, plantillas, texto romano, los pasos y el patrón rítmico (Fase 4)
//   TONALIDAD: escuchar lo que entra y detectar la tonalidad, con el porqué y las alternativas (Fase 5)
//   SUENA AHORA: teclado con las notas que suenan en el receptor y cuántas; debajo, el ACORDE que forman (Fase 2)
//   monitor MIDI: ENTRADA | SALIDA (los últimos eventos de cada lado)
//   derecha, MELODÍA Y BAJO: además densidad, rango, capturar, fijar y variar (Fase 6)
//   pie: cómo rutear la salida en FL
//
// La vista elegida se guarda en el proyecto (propiedad "view" del árbol de parámetros; no es un parámetro).
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
    void setSimple (bool shouldBeSimple);
    void layoutSimple();
    void layoutFull();
    void paintFull (juce::Graphics&);
    [[nodiscard]] juce::String guideText() const;

    TidechartAudioProcessor& processor;

    tidechart::gui::TransportView transportView;
    tidechart::gui::ChordPaletteView paletteView;
    tidechart::gui::ProgressionView progressionView;
    tidechart::gui::KeyDetectView keyDetectView;
    tidechart::gui::LinesView linesView;
    tidechart::gui::SoundingKeyboardView keyboardView;
    tidechart::gui::ChordView chordView;
    tidechart::gui::MidiMonitorView inputMonitor;
    tidechart::gui::MidiMonitorView outputMonitor;
    juce::TextButton viewButton;
    juce::TextButton panicButton;
    juce::TextButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    juce::TooltipWindow tooltips { this, 600 };

    bool simple = true;
    TransportSnapshot transport;
    std::uint32_t seenProgressionChanges = 0;
    std::uint32_t seenLinesChanges = 0;
    juce::Rectangle<int> keyboardTitleArea, guideArea;
    juce::String guide;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TidechartAudioProcessorEditor)
};
