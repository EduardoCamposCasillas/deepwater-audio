#pragma once

#include <functional>
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
// Es el contenido a su tamaño lógico (el de cada vista); el editor de abajo lo escala para que quepa en la pantalla.
class TidechartMainView final : public juce::Component, private juce::Timer
{
public:
    explicit TidechartMainView (TidechartAudioProcessor&);
    ~TidechartMainView() override;

    // Al cambiar de vista cambia el tamaño lógico: el editor ajusta la ventana.
    std::function<void()> onLogicalSizeChanged;

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

    bool simple = true;
    TransportSnapshot transport;
    std::uint32_t seenProgressionChanges = 0;
    std::uint32_t seenLinesChanges = 0;
    juce::Rectangle<int> keyboardTitleArea, guideArea;
    juce::String guide;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TidechartMainView)
};

// El editor que ve el host: el contenido escalado. El dibujo es vectorial, así que escalar no pixela.
// - Al abrir, la escala es la que el usuario eligió (1 = tamaño original), pero nunca más grande que lo que cabe en la
//   pantalla: la vista completa (1360 × 960) no entra en una pantalla de 1080 px de alto con la barra de FL encima.
// - La esquina de abajo a la derecha cambia el tamaño, con la proporción de la vista fija (del 50 % al 150 %). La escala
//   elegida se guarda en el proyecto (propiedad "scale" del árbol de parámetros) y vale para las dos vistas.
class TidechartAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit TidechartAudioProcessorEditor (TidechartAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    // La escala de ahora (tamaño de la ventana / tamaño lógico de la vista).
    [[nodiscard]] float currentScale() const noexcept { return scale; }

private:
    void applyScale();
    [[nodiscard]] double fitScale() const;

    TidechartAudioProcessor& tidechart;
    TidechartMainView view;
    juce::TooltipWindow tooltips { this, 600 };
    double userScale = 1.0;
    float scale = 1.0f;
    bool applying = false; // el tamaño lo puso applyScale, no el usuario

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TidechartAudioProcessorEditor)
};
