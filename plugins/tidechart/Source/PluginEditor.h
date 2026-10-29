#pragma once

#include <array>
#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "deepwater/presets/PresetPanel.h"
#include "gui/ChordPaletteView.h"
#include "gui/ChordView.h"
#include "gui/KeyDetectView.h"
#include "gui/LinesView.h"
#include "gui/MidiMonitorView.h"
#include "gui/ProgressionView.h"
#include "gui/SoundingKeyboardView.h"
#include "gui/TransportView.h"

class TidechartAudioProcessor;

// Tres pestañas con los mismos paneles (lo que se cambia en una se ve en las otras), todas del mismo tamaño
// (1180 × 560): caben enteras en una laptop de 13" (1080p al 150 %: 1280 × 672 útiles) sin achicarlas.
//
// CREAR (por defecto): lo necesario para que alguien que empieza encuentre acordes, progresiones y melodías.
//   izquierda, de arriba abajo, en el orden en que se usa:
//     TONALIDAD (tónica y modo; a la derecha, Escuchar para detectarla) y los acordes de la escala + los prestados
//     PROGRESIÓN: qué tocan las teclas, ritmo, ideas ("Otra idea"), armar con clic, los pasos y el patrón
//     una guía de qué hacer ahora (según el modo, si FL reproduce, si suena algo) y el ruteo de FL
//   derecha, MELODÍA Y BAJO: capas, estilo y "Otra" de cada línea, piano roll que explica cada nota y, abajo, arrastrar
//     cada capa al Piano Roll de FL (Fase 7)
//
// AJUSTES: el ajuste fino de lo mismo, sin repetir el piano roll.
//   izquierda: PALETA con tríadas/séptimas, qué teclas disparan los acordes y + Bajo; PROGRESIÓN con voicing y texto
//     romano; debajo, el ACORDE que suena (para oír y ver el voicing)
//   derecha: MELODÍA Y BAJO con densidad, rango, capturar, fijar y variar
//
// ANÁLISIS: lo que pasa por dentro (Fases 1, 2 y 5).
//   reloj del host; TONALIDAD detectada con el porqué, el histograma y las alternativas; SUENA AHORA (teclado y acorde);
//   monitor MIDI: ENTRADA | SALIDA
//
// La pestaña elegida se guarda en el proyecto (propiedad "page" del árbol de parámetros; no es un parámetro).
// Un temporizador a 30 cuadros por segundo recoge lo que publicó el audio.
// Es el contenido a su tamaño lógico; el editor de abajo lo escala (esquina para redimensionar, y nunca más grande que
// la pantalla).
class TidechartMainView final : public juce::Component, private juce::Timer
{
public:
    explicit TidechartMainView (TidechartAudioProcessor&);
    ~TidechartMainView() override;

    enum class Page
    {
        create,
        settings,
        analysis
    };

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setPage (Page newPage);
    void layoutCreate();
    void layoutSettings();
    void layoutAnalysis();
    void paintAnalysis (juce::Graphics&);
    [[nodiscard]] juce::String guideText() const;
    void showMessage (const juce::String& text);

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
    std::array<juce::TextButton, 3> pageButtons;
    juce::TextButton panicButton;
    juce::TextButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    Page page = Page::create;
    TransportSnapshot transport;
    std::uint32_t seenProgressionChanges = 0;
    std::uint32_t seenLinesChanges = 0;
    juce::Rectangle<int> keyboardTitleArea, guideArea;
    juce::String guide;

    // Presets (Fase 9): la barra en la cabecera; el navegador y el panel de guardar tapan la página. Los mensajes
    // ("Preset: …", avisos al cargar) reemplazan la guía unos segundos (hasta messageExpiry, ms del sistema).
    std::unique_ptr<juce::PropertiesFile> preferences; // %APPDATA%\Deepwater\Tidechart.settings: el último autor al guardar
    deepwater::presets::PresetPanel presets;
    juce::String message;
    juce::uint32 messageExpiry = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TidechartMainView)
};

// El editor que ve el host: el contenido escalado. El dibujo es vectorial, así que escalar no pixela.
// - Al abrir, la escala es la que el usuario eligió (1 = tamaño original), pero nunca más grande que lo que cabe en la
//   pantalla (con la barra de la ventana de FL encima).
// - La esquina de abajo a la derecha cambia el tamaño, con la proporción fija (del 50 % al 150 %). La escala elegida se
//   guarda en el proyecto (propiedad "scale" del árbol de parámetros).
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
