#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Bands.h"
#include "dsp/Modulation.h"
#include "dsp/Saturator.h"

#include "gui/DisplayState.h"
#include "gui/LevelMeterView.h"
#include "gui/RiptideLookAndFeel.h"
#include "gui/SpectrumView.h"
#include "gui/TransferCurveView.h"

class RiptideAudioProcessor;

// Fase 8: GUI profesional y escalable. Medidor IN | arriba los visores (curva de transferencia y espectro antes/después
// con las bandas encima); debajo los modos, el tono (cortes con PRE/POST, emphasis, tilt) junto al nivel y la mezcla
// (Input, Mix, Output, AUTO GAIN, DELTA) y las bandas (número, cruces y, por banda, modo, drive, solo, mute y su auto
// gain) | modulación (envolvente y LFO) | medidor OUT; en la cabecera, el oversampling, el zoom y el bypass.
// Un temporizador a 30 cuadros por segundo recoge lo que publicó el audio y redibuja medidores y visores.
//
// Escalado (como Undertow): toda la interfaz vive en 'content', que siempre mide lo mismo (el diseño en píxeles fijos) y
// se dibuja con una transformación de escala. Todo es vectorial, así que cualquier zoom (70-200 %) se ve nítido. El zoom
// se guarda en el proyecto (propiedad "uiZoom" del estado) y como preferencia para instancias nuevas.
class RiptideAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RiptideAudioProcessorEditor (RiptideAudioProcessor&);
    ~RiptideAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
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
    [[nodiscard]] riptide::gui::DisplayState makeDisplayState() const;
    void setUpKnob (Knob& knob, const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void setUpPositionButton (juce::TextButton& button, std::unique_ptr<ButtonAttachment>& attachment, const char* parameterId);
    void setUpSectionLabel (juce::Label& label, const juce::String& text);
    void setUpToggle (juce::TextButton& button, std::unique_ptr<ButtonAttachment>& attachment, const char* parameterId,
                      const juce::String& tooltip);
    void updateAutoGainReadout();
    void showMode (int index);
    void showBandCount (int count);
    void updateBandReadouts();
    void paintBands (juce::Graphics& g);

    RiptideAudioProcessor& processor;
    riptide::gui::RiptideLookAndFeel lookAndFeel;
    std::unique_ptr<juce::PropertiesFile> settings; // preferencias del usuario (el zoom para instancias nuevas)
    Content content;
    juce::TooltipWindow tooltips { &content, 600 }; // hija del contenido: se escala con él
    float zoom = 1.0f;
    bool rememberZoom = false; // hasta terminar el constructor los cambios de tamaño no son una elección del usuario
    juce::TextButton zoomButton;

    // Visores (Fase 8).
    riptide::gui::TransferCurveView curveView;
    riptide::gui::SpectrumView spectrumView;

    riptide::gui::LevelMeterView inputMeter;
    riptide::gui::LevelMeterView outputMeter;
    Knob inputKnob;
    Knob outputKnob;
    juce::Label levelSection;

    // Mezcla (Fase 5).
    Knob mixKnob;
    juce::TextButton autoGainButton { "AUTO GAIN" };
    juce::TextButton deltaButton { "DELTA" };
    std::unique_ptr<ButtonAttachment> autoGainAttachment, deltaAttachment;
    juce::Label autoGainReadout;

    // Tono (Fase 4). Cada corte tiene su interruptor: apagado = PRE (antes de la curva), encendido = POST (después).
    juce::Label toneSection;
    Knob lowCutKnob, highCutKnob, emphasisKnob, emphasisFreqKnob, tiltKnob;
    juce::TextButton lowCutPositionButton, highCutPositionButton;
    std::unique_ptr<ButtonAttachment> lowCutPositionAttachment, highCutPositionAttachment;
    juce::TextButton bypassButton { "BYPASS" };
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    juce::Label oversamplingLabel;
    juce::ComboBox oversamplingBox;
    std::unique_ptr<ComboBoxAttachment> oversamplingAttachment;

    // Un botón por modo (funcionan como radio: solo uno encendido). JUCE no trae un attachment de "grupo de botones"
    // para un parámetro choice; ParameterAttachment hace de puente en los dos sentidos (clic → parámetro, y
    // automatización o preset → botones).
    std::array<juce::TextButton, riptide::dsp::numModes> modeButtons;
    juce::Label modeDescription;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;

    // Bandas (Fase 6). Una columna por banda y, entre dos columnas, la perilla del cruce que las separa. Las bandas que
    // no existen (más allá del número elegido) se ven apagadas.
    struct BandStrip
    {
        juce::Label title, range; // "BANDA 2" y "120 Hz - 1.0 kHz" (los cruces que suenan de verdad)
        juce::ComboBox mode;
        std::unique_ptr<ComboBoxAttachment> modeAttachment;
        juce::Slider drive { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<SliderAttachment> driveAttachment;
        juce::TextButton solo { "S" }, mute { "M" };
        std::unique_ptr<ButtonAttachment> soloAttachment, muteAttachment;
        juce::Label autoGain; // la compensación de esta banda
        juce::Rectangle<int> area; // fondo de la columna (paint)
    };
    juce::Label bandsSection;
    std::array<juce::TextButton, riptide::dsp::maxBands> bandCountButtons;
    std::unique_ptr<juce::ParameterAttachment> bandCountAttachment;
    std::array<BandStrip, riptide::dsp::maxBands> bandStrips;
    std::array<Knob, riptide::dsp::maxCrossovers> crossoverKnobs;
    int shownBands = 1;

    // Modulación (Fase 7). Una columna a la derecha: ENVOLVENTE (attack, release, sensibilidad, destino, amount) y LFO
    // (forma, sync, rate o división, destino, amount), cada una con una barra que muestra su valor en vivo. La perilla de
    // cada destino dibuja un arco celeste hasta el valor que suena (propiedad "modulation" del Slider, ver el LookAndFeel).
    struct RouteControls
    {
        juce::Label targetLabel;
        juce::ComboBox target;
        std::unique_ptr<ComboBoxAttachment> targetAttachment;
        Knob amount;
    };
    void setUpRoute (RouteControls& route, const char* targetId, const char* amountId, const juce::String& source);
    void setUpCaption (juce::Label& label, const juce::String& text);
    void showLfoSync (bool sync);
    void updateModulation();
    juce::Slider* targetSlider (riptide::dsp::ModTarget target) noexcept;
    void layOutRoute (RouteControls& route, juce::Rectangle<int> area);
    void paintModulation (juce::Graphics& g);

    juce::Label modulationSection, envelopeTitle, lfoTitle, modulationHelp;
    Knob envAttackKnob, envReleaseKnob, envSensitivityKnob, lfoRateKnob;
    RouteControls envelopeRoute, lfoRoute;
    juce::Label lfoShapeLabel, lfoDivisionLabel;
    juce::ComboBox lfoShapeBox, lfoDivisionBox;
    std::unique_ptr<ComboBoxAttachment> lfoShapeAttachment, lfoDivisionAttachment;
    juce::TextButton lfoSyncButton { "SYNC" };
    std::unique_ptr<ButtonAttachment> lfoSyncAttachment;
    juce::Rectangle<int> mainArea, modulationArea, envelopeBar, lfoBar; // fondos y barras (paint)
    juce::Rectangle<int> toneArea, levelArea;                           // separador entre TONO y NIVEL (paint)
    int separatorY = 0;
    float shownEnvelope = 0.0f, shownLfo = 0.0f;

    double lastFrameMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiptideAudioProcessorEditor)
};
