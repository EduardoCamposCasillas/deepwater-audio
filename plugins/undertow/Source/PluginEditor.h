#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "gui/FilterResponseDisplay.h"
#include "gui/LfoDisplay.h"
#include "gui/ModKnob.h"
#include "gui/ModSourceBar.h"
#include "gui/ModulationRouter.h"
#include "gui/PresetBar.h"
#include "gui/PresetBrowser.h"
#include "gui/PresetSaveDialog.h"
#include "gui/UndertowLookAndFeel.h"
#include "gui/WavetableView.h"
#include "synth/Modulation.h"
#include "synth/SourceSettings.h"

class UndertowAudioProcessor;

// Fase 10: barra de presets en la cabecera, navegador y panel de guardar (encima de las páginas).
//
// Fase 9: GUI escalable con aspecto propio, visor 3D del wavetable con el warp aplicado y "arrastrar para modular".
//
// Escalado: toda la interfaz vive en 'content', que siempre mide lo mismo (baseWidth × baseHeight) y se dibuja con
// una transformación de escala. Así el diseño se hace una sola vez en píxeles fijos y cualquier zoom (70–200 %) se ve
// nítido, porque todo se dibuja con vectores. El zoom se guarda en el proyecto y como preferencia para instancias nuevas.
class UndertowAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit UndertowAudioProcessorEditor (UndertowAudioProcessor&);
    ~UndertowAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Page { oscillators, warp, sound, modulation, effects, string };

    void timerCallback() override;
    void showPage (Page page);
    void layoutContent();
    void setZoom (float zoom);
    void showZoomMenu();
    void showMessage (const juce::String& message);
    [[nodiscard]] float initialZoom() const;

    // --- Fase 10: presets ---
    void loadPreset (const undertow::presets::Preset& preset);
    void showPresetBrowser (bool show);
    void showSaveDialog (bool show);
    void showPresetMenu (juce::Component& anchor);
    void updatePresetBar();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        undertow::gui::ModKnob slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    // Crea la perilla, su etiqueta y su attachment con el parámetro. Si el parámetro es un destino de la matriz,
    // la perilla acepta fuentes de modulación arrastradas.
    void setUpKnob (Knob& knob, juce::Component& page, const char* parameterId, const juce::String& name, int width);
    // Rellena las opciones desde el propio parámetro (así la GUI nunca se desincroniza de la lista guardada).
    void setUpComboBox (juce::ComboBox& box, juce::Component& page, const char* parameterId,
                        std::unique_ptr<ComboBoxAttachment>& attachment);
    static void layoutKnob (Knob& knob, juce::Rectangle<int> column);
    static void styleCaption (juce::Label& label); // título pequeño sobre perillas y selectores

    // El contenedor escalado. Es también el "contenedor de arrastre": las fuentes se arrastran dentro de él.
    class Content final : public juce::Component, public juce::DragAndDropContainer
    {
    public:
        std::function<void (std::optional<undertow::synth::ModSource>)> onModulationDrag;
        void paint (juce::Graphics&) override;

    private:
        void dragOperationStarted (const juce::DragAndDropTarget::SourceDetails& details) override
        {
            if (onModulationDrag)
                onModulationDrag (undertow::gui::ModKnob::sourceFromDescription (details.description));
        }
        void dragOperationEnded (const juce::DragAndDropTarget::SourceDetails&) override
        {
            if (onModulationDrag)
                onModulationDrag (std::nullopt);
        }
    };

    // Pestaña que se abre sola al pasar por encima arrastrando una fuente (para soltarla en otra página).
    class TabButton final : public juce::TextButton, public juce::DragAndDropTarget
    {
    public:
        using juce::TextButton::TextButton;
        bool isInterestedInDragSource (const SourceDetails& details) override
        {
            return undertow::gui::ModKnob::sourceFromDescription (details.description).has_value();
        }
        void itemDragEnter (const SourceDetails&) override
        {
            if (! getToggleState())
                triggerClick();
        }
        void itemDropped (const SourceDetails&) override {}
    };

    static constexpr size_t numEnvelopeKnobs = 4;
    static constexpr size_t numVoiceKnobs = 3;

    UndertowAudioProcessor& processor;

    // Va primero: los componentes que lo usan se destruyen antes que él.
    undertow::gui::UndertowLookAndFeel lookAndFeel;
    undertow::gui::ModulationRouter router;
    std::unique_ptr<juce::PropertiesFile> settings; // preferencias del usuario (zoom para instancias nuevas)
    float zoom = 1.0f;
    bool rememberZoom = false; // hasta terminar el constructor los cambios de tamaño no son una elección del usuario

    Content content;
    juce::TooltipWindow tooltipWindow { &content, 700 };

    juce::Component oscillatorPage, warpPage, soundPage, modulationPage, effectsPage, stringPage;
    TabButton oscillatorTab { "Osciladores" };
    TabButton warpTab { "Warp y FM" };
    TabButton soundTab { "Filtro y Amp" };
    TabButton modulationTab;
    TabButton effectsTab { "Efectos" };
    TabButton stringTab { "Cuerda" };
    juce::TextButton zoomButton;
    undertow::gui::PresetBar presetBar;
    undertow::gui::PresetBrowser presetBrowser;
    undertow::gui::PresetSaveDialog saveDialog;
    undertow::gui::ModSourceBar modBar;
    std::vector<undertow::gui::ModKnob*> modulationKnobs; // las que son destino de la matriz
    juce::uint32 messageExpiry = 0;
    std::array<Knob, numEnvelopeKnobs + numVoiceKnobs> knobs;

    // --- Fase 6: osciladores A y B, sub y ruido ---
    static constexpr size_t numOscillatorKnobs = 9; // Position, Octave, Semi, Fine, Level, Pan, Unison, Detune, Width

    struct OscillatorControls
    {
        juce::GroupComponent group;
        juce::ToggleButton onButton { "On" };
        std::unique_ptr<ButtonAttachment> onAttachment;
        juce::ComboBox tableBox;
        std::unique_ptr<ComboBoxAttachment> tableAttachment;
        std::array<Knob, numOscillatorKnobs> knobs;
        undertow::gui::WavetableView view { undertow::gui::WavetableView::View::stack };
        std::atomic<float>* onParam = nullptr;
        std::atomic<float>* tableParam = nullptr;
        std::atomic<float>* positionParam = nullptr;
    };
    std::array<OscillatorControls, undertow::synth::numOscillators> oscillators;

    // --- Fase 7: warp y FM/RM de cada oscilador ---
    struct WarpControls
    {
        juce::GroupComponent group;
        undertow::gui::WavetableView view { undertow::gui::WavetableView::View::single };
        juce::Label warpLabel, fmLabel;
        juce::ComboBox warpBox, fmBox;
        std::unique_ptr<ComboBoxAttachment> warpAttachment, fmAttachment;
        Knob warpKnob, fmKnob;
        std::atomic<float>* warpModeParam = nullptr;
        std::atomic<float>* warpAmountParam = nullptr;
    };
    std::array<WarpControls, undertow::synth::numOscillators> warps;

    juce::GroupComponent subGroup { {}, "Sub" };
    juce::ToggleButton subOnButton { "On" };
    std::unique_ptr<ButtonAttachment> subOnAttachment;
    juce::ComboBox subShapeBox;
    std::unique_ptr<ComboBoxAttachment> subShapeAttachment;
    std::array<Knob, 2> subKnobs;

    juce::GroupComponent noiseGroup { {}, "Ruido" };
    juce::ToggleButton noiseOnButton { "On" };
    std::unique_ptr<ButtonAttachment> noiseOnAttachment;
    std::array<Knob, 2> noiseKnobs;

    static constexpr size_t numFilterKnobs = 4;

    juce::GroupComponent filterGroup { {}, "Filtro" };
    juce::ToggleButton filterOnButton { "On" };
    std::unique_ptr<ButtonAttachment> filterOnAttachment;
    juce::ComboBox filterTypeBox;
    std::unique_ptr<ComboBoxAttachment> filterTypeAttachment;
    juce::ComboBox filterSlopeBox;
    std::unique_ptr<ComboBoxAttachment> filterSlopeAttachment;
    std::array<Knob, numFilterKnobs> filterKnobs;
    undertow::gui::FilterResponseDisplay filterDisplay;

    juce::GroupComponent envelopeGroup { {}, "Env 1 (amplitud)" };
    juce::GroupComponent voiceGroup { {}, "Voz" };
    juce::Label activeVoicesLabel;
    int lastShownVoiceCount = -1;

    // --- Fase 5: modulación ---
    struct ModEnvelopeControls
    {
        juce::GroupComponent group;
        std::array<Knob, 4> knobs;
    };
    std::array<ModEnvelopeControls, 2> modEnvelopes;

    struct LfoControls
    {
        juce::GroupComponent group;
        juce::ComboBox shapeBox, modeBox, divisionBox;
        std::unique_ptr<ComboBoxAttachment> shapeAttachment, modeAttachment, divisionAttachment;
        juce::ToggleButton syncButton { "Sync (tempo)" };
        std::unique_ptr<ButtonAttachment> syncAttachment;
        Knob rateKnob;
        juce::Label divisionLabel;
        undertow::gui::LfoDisplay display;
        std::atomic<float>* shapeParam = nullptr;
        std::atomic<float>* syncParam = nullptr;
    };
    std::array<LfoControls, undertow::synth::numLfos> lfos;

    struct ModSlotControls
    {
        juce::Label number;
        juce::ComboBox sourceBox, destinationBox;
        std::unique_ptr<ComboBoxAttachment> sourceAttachment, destinationAttachment;
        juce::Label arrow;
        juce::Slider amount { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<SliderAttachment> amountAttachment;
    };
    juce::GroupComponent matrixGroup { {}, "Matriz de modulación" };
    std::array<ModSlotControls, undertow::synth::numModSlots> modSlots;

    // --- Fase 8: efectos ---
    struct EffectGroup
    {
        juce::GroupComponent group;
        juce::ToggleButton onButton { "On" };
        std::unique_ptr<ButtonAttachment> onAttachment;
    };
    EffectGroup distortionGroup, chorusGroup, delayGroup, reverbGroup;

    juce::ComboBox distortionModeBox;
    std::unique_ptr<ComboBoxAttachment> distortionModeAttachment;
    std::array<Knob, 3> distortionKnobs; // Drive, Tone, Mix
    std::array<Knob, 4> chorusKnobs;     // Rate, Depth, Feedback, Mix

    juce::ToggleButton delaySyncButton { "Sync (tempo)" };
    juce::ToggleButton delayPingPongButton { "Ping-Pong" };
    std::unique_ptr<ButtonAttachment> delaySyncAttachment, delayPingPongAttachment;
    Knob delayTimeKnob;
    juce::Label delayDivisionLabel;
    juce::ComboBox delayDivisionBox;
    std::unique_ptr<ComboBoxAttachment> delayDivisionAttachment;
    std::array<Knob, 3> delayKnobs;  // Feedback, Tone, Mix
    std::atomic<float>* delaySyncParam = nullptr;

    std::array<Knob, 5> reverbKnobs; // Size, Decay, Damping, Pre-Delay, Mix

    // --- Fase 12: cuerda pulsada ---
    juce::GroupComponent stringGroup;
    juce::ToggleButton stringOnButton { "On" };
    std::unique_ptr<ButtonAttachment> stringOnAttachment;
    std::array<Knob, 6> stringKnobs; // Octave, Semi, Fine, Level, Decay, Damping
    juce::Label stringHelp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndertowAudioProcessorEditor)
};
