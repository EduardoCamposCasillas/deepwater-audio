#include "PluginEditor.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace
{
constexpr int editorWidth = 640;
constexpr int editorHeight = 530;
constexpr int headerHeight = 44;
constexpr int transportHeight = 170;
constexpr int curveHeight = 150;
constexpr int footerHeight = 26;
constexpr int gap = 8;
constexpr int padding = 12;
constexpr int framesPerSecond = 30;

juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }
} // namespace

EbbAudioProcessorEditor::EbbAudioProcessorEditor (EbbAudioProcessor& p) : AudioProcessorEditor (p), processor (p)
{
    namespace id = ebb::params;
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (transportView);
    addAndMakeVisible (curveView);

    divisionLabel.setText (utf8 ("DIVISI\xc3\x93N"), juce::dontSendNotification);
    divisionLabel.setJustificationType (juce::Justification::centred);
    divisionLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    divisionLabel.setColour (juce::Label::textColourId, ebb::gui::theme::dimText);
    addAndMakeVisible (divisionLabel);
    divisionBox.addItemList (processor.getParameters().getParameter (id::division)->getAllValueStrings(), 1);
    divisionBox.setTooltip (utf8 ("Cada cu\xc3\xa1nto baja el volumen, en figuras musicales al tempo de FL. 1/4 = un golpe por "
                                  "tiempo (el kick del dembow cl\xc3\xa1sico). T = tresillo: tres golpes en el espacio de dos. "
                                  "1 bar = un golpe por comp\xc3\xa1s."));
    addAndMakeVisible (divisionBox);
    divisionAttachment = std::make_unique<ComboBoxAttachment> (processor.getParameters(), id::division, divisionBox);

    setUpKnob (depthKnob, id::depth, "PROFUNDIDAD",
               utf8 ("Cu\xc3\xa1nto baja el volumen en el golpe. 100 % = silencio. Entre par\xc3\xa9ntesis, lo mismo en dB: "
                     "para limpiar graves en la mezcla bastan 15\xe2\x80\x93" "30 % (1.5\xe2\x80\x93" "3 dB); el bombeo "
                     "como efecto empieza hacia el 60 %."));
    setUpKnob (durationKnob, id::duration, utf8 ("DURACI\xc3\x93N"),
               utf8 ("Qu\xc3\xa9 parte del ciclo tarda el volumen en volver. 100 % = vuelve justo cuando llega el "
                     "siguiente golpe (bombeo continuo); valores bajos = una ca\xc3\xad" "da corta y el resto del ciclo a "
                     "volumen completo."));
    setUpKnob (smoothingKnob, id::smoothing, "SUAVIZADO",
               utf8 ("Redondea los cambios bruscos de volumen (sobre todo la ca\xc3\xad" "da del golpe). 0 ms = ca\xc3\xad" "da "
                     "instant\xc3\xa1nea: en un bajo se oye un clic. 1\xe2\x80\x93" "5 ms = sin clic y con golpe. "
                     "M\xc3\xa1s = una entrada blanda, que se come el ataque del ducking."));

    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip (utf8 ("Escuchar la se\xc3\xb1" "al original. Es el mismo bypass que el de FL y entra con un "
                                   "fundido de 20 ms (sin clics)."));
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), id::bypass, bypassButton);

    setSize (editorWidth, editorHeight);
    timerCallback(); // sin esperar al primer cuadro
    startTimerHz (framesPerSecond);
}

EbbAudioProcessorEditor::~EbbAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void EbbAudioProcessorEditor::setUpKnob (Knob& knob, const char* parameterId, const juce::String& name, const juce::String& tooltip)
{
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 120, 18);
    knob.slider.setTooltip (tooltip);
    addAndMakeVisible (knob.slider);
    knob.attachment = std::make_unique<SliderAttachment> (processor.getParameters(), parameterId, knob.slider);
    // Doble clic = valor por defecto.
    auto* parameter = processor.getParameters().getParameter (parameterId);
    knob.slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    knob.label.setColour (juce::Label::textColourId, ebb::gui::theme::dimText);
    addAndMakeVisible (knob.label);
}

void EbbAudioProcessorEditor::timerCallback()
{
    if (processor.readTransport (lastSnapshot))
        transportView.setSnapshot (lastSnapshot);

    auto& parameters = processor.getParameters();
    ebb::dsp::TempoSettings settings;
    settings.division = static_cast<int> (parameters.getRawParameterValue (ebb::params::division)->load());
    settings.depth = parameters.getRawParameterValue (ebb::params::depth)->load() / 100.0f;
    settings.duration = parameters.getRawParameterValue (ebb::params::duration)->load() / 100.0f;
    settings.smoothingMs = parameters.getRawParameterValue (ebb::params::smoothing)->load();
    curveView.setCurve (settings, lastSnapshot.bpm, lastSnapshot.timeSignature);
    curveView.setMinGain (processor.takeMinGain());
}

juce::Rectangle<int> EbbAudioProcessorEditor::controlsArea() const
{
    return getLocalBounds()
        .withTrimmedTop (headerHeight + transportHeight + curveHeight + 2 * gap)
        .withTrimmedBottom (footerHeight)
        .reduced (padding, 0);
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
    g.drawText ("ducker  -  Fase 2: modo tempo", header, juce::Justification::centredLeft);

    const auto controls = controlsArea().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (controls, 8.0f);
    g.setColour (theme::panelOutline);
    g.drawRoundedRectangle (controls.reduced (0.5f), 8.0f, 1.0f);

    g.setColour (theme::dimText.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (utf8 ("Con FL parado no hay ducking. Fase 3: formas de curva y editor propio."),
                getLocalBounds().removeFromBottom (footerHeight), juce::Justification::centred);
}

void EbbAudioProcessorEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 8);
    bypassButton.setBounds (header.removeFromRight (90));

    auto area = getLocalBounds().withTrimmedTop (headerHeight).withTrimmedBottom (footerHeight).reduced (padding, 0);
    transportView.setBounds (area.removeFromTop (transportHeight));
    area.removeFromTop (gap);
    curveView.setBounds (area.removeFromTop (curveHeight));

    // Fila de controles: la división a la izquierda y las tres perillas repartidas en el resto.
    auto controls = controlsArea().reduced (14, 8);
    auto division = controls.removeFromLeft (150);
    divisionLabel.setBounds (division.removeFromTop (20));
    divisionBox.setBounds (division.withSizeKeepingCentre (division.getWidth(), 28));

    const int knobWidth = controls.getWidth() / 3;
    for (auto* knob : { &depthKnob, &durationKnob, &smoothingKnob })
    {
        auto column = controls.removeFromLeft (knobWidth);
        knob->label.setBounds (column.removeFromTop (20));
        knob->slider.setBounds (column);
    }
}
