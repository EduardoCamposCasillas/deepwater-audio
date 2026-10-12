#pragma once

#include <array>
#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "deepwater/presets/PresetPanel.h"

#include "gui/BandView.h"
#include "gui/CurveEditor.h"
#include "gui/CurveView.h"
#include "gui/EbbLookAndFeel.h"
#include "gui/LiveView.h"
#include "gui/MidiActivityView.h"
#include "gui/SidechainMeterView.h"
#include "gui/TransportView.h"

class EbbAudioProcessor;

// GUI. Fase 1: el reloj del host y el bypass. Fase 2: la vista previa de la curva y sus controles, y el medidor de
// reducción. Fase 3: el menú de formas y el editor de la curva. Fase 4: el modo, el filtro de nota, la velocidad y la luz
// de actividad MIDI. Fase 5: el sidechain (medidor con el umbral, retención y lookahead). Fase 6: las bandas.
//
// Fase 7: el visor en vivo (la señal, la ganancia y los golpes, compás a compás) y el cursor de la curva, en una
// distribución más ancha y baja (1160 × 590):
//   cabecera:  EBB | reloj del host | zoom | bypass
//   fila 1:    visor EN VIVO                                    | vista PREVIA + medidor de reducción
//   fila 2:    FORMA DE LA CURVA (editor, con el menú de formas)  | BANDAS
//   fila 3:    MODO y DIVISIÓN | profundidad, duración, suavizado | MIDI o SIDECHAIN (según el modo) | lookahead
// Fase 8: la barra de presets en la cabecera (EBB | presets | reloj | zoom | bypass); el navegador y el panel de
// guardar tapan las tres filas; los mensajes de los presets ocupan el pie unos segundos.
// Escalado (como Riptide y Undertow): todo vive en 'content', que siempre mide lo mismo y se dibuja con una
// transformación de escala (70–200 %). El zoom se guarda en el proyecto (propiedad "uiZoom") y como preferencia; la
// primera vez se elige el mayor (≤ 100 %) que cabe en la pantalla.
// Un temporizador a 30 cuadros por segundo recoge lo que publicó el audio.
class EbbAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EbbAudioProcessorEditor (EbbAudioProcessor&);
    ~EbbAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // El lienzo de tamaño fijo que se escala; su paint lo hace el editor.
    struct Content final : public juce::Component
    {
        std::function<void (juce::Graphics&)> onPaint;
        void paint (juce::Graphics& g) override
        {
            if (onPaint)
                onPaint (g);
        }
    };

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void layOutContent();
    void paintContent (juce::Graphics& g);
    void setZoom (float newZoom);
    void showZoomMenu();
    [[nodiscard]] float initialZoom() const;
    void setUpKnob (Knob& knob, const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void setUpComboLabel (juce::Label& label, const juce::String& text);
    void setUpCombo (juce::ComboBox& box, const char* parameterId, const juce::String& tooltip,
                     std::unique_ptr<ComboBoxAttachment>& attachment);
    void showCurve();
    void useLastMidiNote();
    void updateModeControls (ebb::dsp::TriggerMode mode);
    void updateBandControls (ebb::dsp::DuckBand band);
    void showMessage (const juce::String& text);

    EbbAudioProcessor& processor;
    ebb::gui::EbbLookAndFeel lookAndFeel;
    std::unique_ptr<juce::PropertiesFile> settings; // preferencias del usuario (el zoom para instancias nuevas)
    Content content;
    juce::TooltipWindow tooltips { &content, 600 }; // hija del contenido: se escala con él
    float zoom = 1.0f;
    bool rememberZoom = false; // hasta terminar el constructor los cambios de tamaño no son una elección del usuario
    juce::TextButton zoomButton;

    ebb::gui::TransportView transportView;
    ebb::gui::LiveView liveView;
    ebb::gui::CurveView curveView;
    ebb::gui::CurveEditor curveEditor;
    int shownCurveVersion = -1;
    TransportSnapshot lastSnapshot;

    juce::Label modeLabel;
    juce::ComboBox modeBox;
    std::unique_ptr<ComboBoxAttachment> modeAttachment;
    juce::Label divisionLabel;
    juce::ComboBox divisionBox;
    std::unique_ptr<ComboBoxAttachment> divisionAttachment;
    juce::ComboBox shapeBox;
    Knob depthKnob, durationKnob, smoothingKnob;

    // MIDI (en los modos Tempo y MIDI).
    juce::Label noteLabel;
    juce::ComboBox noteBox;
    std::unique_ptr<ComboBoxAttachment> noteAttachment;
    juce::Label midiLabel;
    ebb::gui::MidiActivityView midiActivity;
    juce::TextButton useLastNoteButton;
    juce::TextButton velocityButton;
    std::unique_ptr<ButtonAttachment> velocityAttachment;
    int lastMidiNote = -1;
    ebb::dsp::TriggerMode shownMode = ebb::dsp::TriggerMode::tempo;
    juce::Rectangle<int> tempoHintArea; // en modo Tempo, una nota de por qué la nota MIDI está apagada
    std::array<int, 2> separators {};   // x de las líneas entre los grupos de la fila de controles

    // Sidechain (en el modo Audio).
    juce::Label sidechainLabel;
    ebb::gui::SidechainMeterView sidechainMeter;
    Knob thresholdKnob, holdKnob, lookaheadKnob;

    juce::Label bandLabel;
    juce::ComboBox bandBox;
    std::unique_ptr<ComboBoxAttachment> bandAttachment;
    ebb::gui::BandView bandView;
    Knob crossoverKnob, mixKnob;
    ebb::dsp::DuckBand shownBand = ebb::dsp::DuckBand::full;

    // Presets (Fase 8). Mensajes ("Preset: …", avisos al cargar) en el pie, hasta messageExpiry (ms del sistema).
    deepwater::presets::PresetPanel presets;
    juce::String message;
    juce::uint32 messageExpiry = 0;

    juce::TextButton bypassButton { "BYPASS" };
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EbbAudioProcessorEditor)
};
