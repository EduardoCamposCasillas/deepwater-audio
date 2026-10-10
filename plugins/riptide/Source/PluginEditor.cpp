#include "PluginEditor.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace
{
constexpr int editorWidth = 580;
constexpr int editorHeight = 420;
constexpr int headerHeight = 44;
constexpr int padding = 12;
constexpr int meterWidth = 96;
constexpr int knobSize = 110;
constexpr int framesPerSecond = 30;
constexpr int modeButtonHeight = 26;
constexpr int modeDescriptionHeight = 34;
constexpr int signalFlowHeight = 28;

// Qué hace cada modo, en lenguaje musical (se muestra debajo de los botones y como tooltip). En UTF-8 escapado: MSVC
// no garantiza la codificación de los literales del código fuente.
const std::array<const char*, riptide::dsp::numModes> modeDescriptions {
    "Cinta: rodilla muy suave, comprime m\xc3\xa1s los agudos que los graves y suma cuerpo en 70 Hz. C\xc3\xa1lido, "
    "redondo, pega.",
    "Bulbo: curva asim\xc3\xa9trica, arm\xc3\xb3nicos pares (octavas). Llena y engorda sin sonar sucio; con drive "
    "alto, ronca.",
    "Transistor: limpio hasta cerca del l\xc3\xadmite y despu\xc3\xa9s \xc3\xa1spero. Arm\xc3\xb3nicos impares: "
    "mordida, presencia.",
    "Clip duro: recto hasta 0 dBFS y plano despu\xc3\xa9s. Recorta picos sin tocar lo dem\xc3\xa1s; pasado, suena "
    "a cuadrada.",
    "Rectificador: da vuelta al semiciclo negativo. La fundamental desaparece y sube una octava. Efecto extremo.",
    "Wavefold: en vez de aplanar los picos los dobla hacia abajo. M\xc3\xa1s drive = m\xc3\xa1s pliegues, timbre "
    "met\xc3\xa1lico.",
};
} // namespace

RiptideAudioProcessorEditor::RiptideAudioProcessorEditor (RiptideAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p), inputMeter ("IN", p.getInputMeter()), outputMeter ("OUT", p.getOutputMeter())
{
    namespace id = riptide::params;
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    setUpKnob (inputKnob, id::inputGain, "Input",
               juce::String::fromUTF8 ("Drive: nivel con el que la se\xc3\xb1" "al entra a la curva (lo que mide IN). "
                                       "Subirlo = saturar m\xc3\xa1s: m\xc3\xa1s arm\xc3\xb3nicos, m\xc3\xa1s densidad, "
                                       "menos pegada."));
    setUpKnob (outputKnob, id::outputGain, "Output",
               juce::String::fromUTF8 ("Volumen final. Sirve para igualar el volumen con el bypass y comparar a "
                                       "o\xc3\xad" "do justo."));

    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip (juce::String::fromUTF8 ("Escuchar la se\xc3\xb1" "al original. Es el mismo bypass que el de FL "
                                                     "y entra con un fundido de 20 ms (sin clics)."));
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), id::bypass, bypassButton);

    oversamplingLabel.setText ("OVERSAMPLING", juce::dontSendNotification);
    oversamplingLabel.setJustificationType (juce::Justification::centredRight);
    oversamplingLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    oversamplingLabel.setColour (juce::Label::textColourId, riptide::gui::theme::dimText);
    addAndMakeVisible (oversamplingLabel);
    // Las opciones salen del propio parámetro (Off, 2x, 4x, 8x, 16x): el attachment asocia la opción N con el índice N.
    oversamplingBox.addItemList (processor.getParameters().getParameter (id::oversampling)->getAllValueStrings(), 1);
    oversamplingBox.setTooltip (juce::String::fromUTF8 (
        "Calcula la saturaci\xc3\xb3n a 2, 4, 8 o 16 veces la frecuencia de muestreo para que los arm\xc3\xb3nicos que "
        "pasan de Nyquist no se reflejen como aliasing. M\xc3\xa1s factor = menos aliasing, m\xc3\xa1s CPU y algo "
        "m\xc3\xa1s de latencia (FL la compensa). Off = sin oversampling: \xc3\xbasalo para o\xc3\xad" "r el aliasing."));
    addAndMakeVisible (oversamplingBox);
    oversamplingAttachment = std::make_unique<ComboBoxAttachment> (processor.getParameters(), id::oversampling, oversamplingBox);

    auto* modeParameter = processor.getParameters().getParameter (id::mode);
    modeAttachment = std::make_unique<juce::ParameterAttachment> (
        *modeParameter, [this] (float index) { showMode (static_cast<int> (std::lround (index))); });
    for (size_t i = 0; i < modeButtons.size(); ++i)
    {
        auto& button = modeButtons[i];
        button.setButtonText (riptide::dsp::modeNames[i]);
        button.setRadioGroupId (1);
        button.setColour (juce::TextButton::buttonOnColourId, riptide::gui::theme::accent.withAlpha (0.55f));
        button.setTooltip (juce::String::fromUTF8 (modeDescriptions[i]));
        button.onClick = [this, i] { modeAttachment->setValueAsCompleteGesture (static_cast<float> (i)); };
        addAndMakeVisible (button);
    }
    modeDescription.setJustificationType (juce::Justification::centred);
    modeDescription.setFont (juce::FontOptions (12.0f));
    modeDescription.setColour (juce::Label::textColourId, riptide::gui::theme::dimText);
    modeDescription.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (modeDescription);
    modeAttachment->sendInitialUpdate();

    setSize (editorWidth, editorHeight);
    lastFrameMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (framesPerSecond);
}

RiptideAudioProcessorEditor::~RiptideAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void RiptideAudioProcessorEditor::setUpKnob (Knob& knob, const char* parameterId, const juce::String& name,
                                             const juce::String& tooltip)
{
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    knob.slider.setTooltip (tooltip);
    knob.slider.setDoubleClickReturnValue (true, 0.0); // doble clic = 0 dB
    addAndMakeVisible (knob.slider);
    knob.attachment = std::make_unique<SliderAttachment> (processor.getParameters(), parameterId, knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    knob.label.setColour (juce::Label::textColourId, riptide::gui::theme::dimText);
    addAndMakeVisible (knob.label);
}

void RiptideAudioProcessorEditor::showMode (int index)
{
    index = juce::jlimit (0, riptide::dsp::numModes - 1, index);
    modeButtons[static_cast<size_t> (index)].setToggleState (true, juce::dontSendNotification);
    modeDescription.setText (juce::String::fromUTF8 (modeDescriptions[static_cast<size_t> (index)]), juce::dontSendNotification);
}

void RiptideAudioProcessorEditor::timerCallback()
{
    // El tiempo real entre cuadros (no 1/30 s): si Windows retrasa el temporizador, los medidores caen igual de rápido.
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double elapsed = juce::jlimit (0.0, 0.5, (now - lastFrameMs) / 1000.0);
    lastFrameMs = now;

    const int channels = processor.getMeteredChannels();
    inputMeter.update (channels, elapsed);
    outputMeter.update (channels, elapsed);
}

void RiptideAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace riptide::gui;
    g.fillAll (theme::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 0);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("RIPTIDE", header.removeFromLeft (110), juce::Justification::centredLeft);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("saturador  -  Fase 3", header.withTrimmedRight (oversamplingLabel.getRight() - oversamplingLabel.getX() + 180),
                juce::Justification::centredLeft);

    // Panel de las perillas, entre los dos medidores.
    const auto body = getLocalBounds().withTrimmedTop (headerHeight).reduced (padding);
    auto knobsPanel = body.reduced (meterWidth + padding, 0).toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (knobsPanel, 8.0f);
    g.setColour (theme::panelOutline);
    g.drawRoundedRectangle (knobsPanel.reduced (0.5f), 8.0f, 1.0f);

    // Flecha del recorrido de la señal: IN → perillas → OUT.
    g.setColour (theme::dimText.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String::fromUTF8 ("entrada \xe2\x86\x92 input (drive) \xe2\x86\x92 modo \xe2\x86\x92 output \xe2\x86\x92 salida"),
                knobsPanel.removeFromBottom (static_cast<float> (signalFlowHeight)), juce::Justification::centred);
}

void RiptideAudioProcessorEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 8);
    bypassButton.setBounds (header.removeFromRight (90));
    header.removeFromRight (padding);
    oversamplingBox.setBounds (header.removeFromRight (70));
    oversamplingLabel.setBounds (header.removeFromRight (100));

    auto body = getLocalBounds().withTrimmedTop (headerHeight).reduced (padding);
    inputMeter.setBounds (body.removeFromLeft (meterWidth));
    outputMeter.setBounds (body.removeFromRight (meterWidth));

    // Modos: dos filas de tres botones y la descripción del activo.
    auto panel = body.reduced (padding, padding);
    for (int row = 0; row < 2; ++row)
    {
        auto line = panel.removeFromTop (modeButtonHeight);
        const int width = line.getWidth() / 3;
        for (int column = 0; column < 3; ++column)
            modeButtons[static_cast<size_t> (row * 3 + column)].setBounds (line.removeFromLeft (width).reduced (2, 0));
        panel.removeFromTop (4);
    }
    modeDescription.setBounds (panel.removeFromTop (modeDescriptionHeight));

    auto knobs = panel.withTrimmedBottom (signalFlowHeight - padding);
    const int knobWidth = knobs.getWidth() / 2;
    for (auto* knob : { &inputKnob, &outputKnob })
    {
        auto column = knobs.removeFromLeft (knobWidth);
        auto content = column.withSizeKeepingCentre (column.getWidth(), knobSize + 20 + 24);
        knob->label.setBounds (content.removeFromTop (20));
        knob->slider.setBounds (content);
    }
}
