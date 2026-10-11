#include "PluginEditor.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace
{
constexpr int editorWidth = 900;
constexpr int editorHeight = 760;
constexpr int headerHeight = 44;
constexpr int padding = 12;
constexpr int meterWidth = 96;
constexpr int knobSize = 104;
constexpr int toneKnobSize = 70;
constexpr int sectionLabelHeight = 18;
constexpr int positionButtonHeight = 22;
constexpr int framesPerSecond = 30;
constexpr int modeButtonHeight = 26;
constexpr int modeDescriptionHeight = 34;
constexpr int signalFlowHeight = 28;
constexpr int toggleWidth = 96;
constexpr int toggleHeight = 26;
constexpr int crossoverColumnWidth = 76;
constexpr int bandKnobSize = 54;
constexpr int bandStripHeight = 186;
constexpr int levelKnobsHeight = 132;

// "80 Hz", "2.5 kHz" o "20 kHz".
juce::String formatHz (float hz)
{
    if (hz < 999.5f)
        return juce::String (juce::roundToInt (hz)) + " Hz";
    return hz < 9950.0f ? juce::String (hz / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (hz / 1000.0f)) + " kHz";
}

juce::String formatDb (float db)
{
    const float rounded = std::round (db * 10.0f) / 10.0f;
    return (rounded > 0.0f ? "+" : "") + juce::String (rounded == 0.0f ? 0.0f : rounded, 1) + " dB";
}

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
               juce::String::fromUTF8 ("Drive: nivel con el que la se\xc3\xb1" "al entra a la curva (lo que mide IN, despu\xc3\xa9s de los cortes PRE y el emphasis). Subirlo = saturar m\xc3\xa1s: m\xc3\xa1s arm\xc3\xb3nicos, m\xc3\xa1s densidad, menos pegada."));
    setUpKnob (outputKnob, id::outputGain, "Output",
               juce::String::fromUTF8 ("Volumen final. Sirve para igualar el volumen con el bypass y comparar a "
                                       "o\xc3\xad" "do justo."));

    setUpKnob (mixKnob, id::mix, "Mix",
               juce::String::fromUTF8 ("Mezcla en paralelo: cu\xc3\xa1nto de la se\xc3\xb1" "al saturada se suma a la original. 100 % = solo saturada; 30-50 % = la original conserva su pegada y la saturada le agrega densidad y arm\xc3\xb3nicos. Las dos llegan alineadas (misma latencia y misma fase)."));
    setUpToggle (autoGainButton, autoGainAttachment, id::autoGain,
                 juce::String::fromUTF8 ("Iguala el volumen de la se\xc3\xb1" "al saturada con el de la original: mover Input o el modo cambia el car\xc3\xa1" "cter, no el volumen. Debajo, la compensaci\xc3\xb3n que aplica. Sigue al nivel de la m\xc3\xba" "sica despacio (1 s) y queda un poco por debajo, nunca por encima."));
    setUpToggle (deltaButton, deltaAttachment, id::delta,
                 juce::String::fromUTF8 ("Escuchar solo lo que Riptide le agrega (o le quita) a la se\xc3\xb1" "al: la salida menos la original. Sin drive, silencio. Sirve para o\xc3\xad" "r los arm\xc3\xb3nicos, el fizz o el barro que agrega cada ajuste. No lo dejes puesto al exportar."));
    autoGainReadout.setJustificationType (juce::Justification::centred);
    autoGainReadout.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (autoGainReadout);
    updateAutoGainReadout();

    setUpSectionLabel (levelSection, "NIVEL Y MEZCLA");
    setUpSectionLabel (toneSection, "TONO");
    setUpKnob (lowCutKnob, id::lowCut, "Low Cut", juce::String::fromUTF8 ("Low Cut (high-pass, 12 dB/oct): quita graves. En PRE los graves ya no llegan a la curva: no la empujan ni la ensucian (m\xc3\xa1s claridad, menos \"barro\"). En POST el bajo satura con fuerza y despu\xc3\xa9s se le quita la fundamental, pero sus arm\xc3\xb3nicos se quedan. Al m\xc3\xadnimo: apagado."));
    setUpKnob (highCutKnob, id::highCut, "High Cut", juce::String::fromUTF8 ("High Cut (low-pass, 12 dB/oct): quita agudos. En POST suaviza el \"fizz\" (los arm\xc3\xb3nicos agudos que crea la saturaci\xc3\xb3n). En PRE los agudos no se saturan: la saturaci\xc3\xb3n queda en graves y medios, m\xc3\xa1s oscura. Al m\xc3\xa1ximo: apagado."));
    setUpKnob (emphasisKnob, id::emphasis, "Emphasis", juce::String::fromUTF8 ("Emphasis: sube (o baja) una zona ANTES de la curva y la devuelve a su sitio DESPU\xc3\x89S. Sin drive no cambia nada; con drive, esa zona es la que m\xc3\xa1s se satura (positivo) o la que se mantiene limpia (negativo)."));
    setUpKnob (emphasisFreqKnob, id::emphasisFreq, "Emph Freq", juce::String::fromUTF8 ("Centro de la zona de emphasis (campana ancha, unas 2 octavas). Ejemplos: 100 Hz = saturar el cuerpo del bajo; 3 kHz = la presencia de una voz o una guitarra."));
    setUpKnob (tiltKnob, id::tilt, "Tilt", juce::String::fromUTF8 ("Tilt: inclina el espectro de la salida alrededor de 1 kHz. Positivo = agudos arriba y graves abajo (m\xc3\xa1s brillante); negativo = m\xc3\xa1s oscuro y c\xc3\xa1lido. Va despu\xc3\xa9s de la curva: cambia el color, no lo que se satura."));
    setUpPositionButton (lowCutPositionButton, lowCutPositionAttachment, id::lowCutPosition);
    setUpPositionButton (highCutPositionButton, highCutPositionAttachment, id::highCutPosition);

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

    // Bandas.
    setUpSectionLabel (bandsSection, "BANDAS");
    auto* bandsParameter = processor.getParameters().getParameter (id::bands);
    bandCountAttachment = std::make_unique<juce::ParameterAttachment> (
        *bandsParameter, [this] (float index) { showBandCount (static_cast<int> (std::lround (index)) + 1); });
    for (size_t i = 0; i < bandCountButtons.size(); ++i)
    {
        auto& button = bandCountButtons[i];
        button.setButtonText (juce::String (i + 1));
        button.setRadioGroupId (2);
        button.setColour (juce::TextButton::buttonOnColourId, riptide::gui::theme::accent.withAlpha (0.55f));
        button.setTooltip (juce::String::fromUTF8 ("N\xc3\xba" "mero de bandas. 1 = toda la se\xc3\xb1" "al junta (como hasta la Fase 5). Con m\xc3\xa1s, cada banda se satura por separado: lo fuerte de una (el bombo, el bajo) ya no empuja ni ensucia a las otras. Cambiarlo hace un fundido corto a silencio."));
        button.onClick = [this, i] { bandCountAttachment->setValueAsCompleteGesture (static_cast<float> (i)); };
        addAndMakeVisible (button);
    }

    const auto modeChoices = processor.getParameters().getParameter (id::bandMode[0])->getAllValueStrings();
    for (size_t b = 0; b < bandStrips.size(); ++b)
    {
        auto& strip = bandStrips[b];
        strip.title.setText ("BANDA " + juce::String (b + 1), juce::dontSendNotification);
        strip.title.setJustificationType (juce::Justification::centred);
        strip.title.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        strip.title.setColour (juce::Label::textColourId, riptide::gui::theme::text);
        addAndMakeVisible (strip.title);
        strip.range.setJustificationType (juce::Justification::centred);
        strip.range.setFont (juce::FontOptions (11.0f));
        strip.range.setColour (juce::Label::textColourId, riptide::gui::theme::dimText);
        addAndMakeVisible (strip.range);

        strip.mode.addItemList (modeChoices, 1);
        strip.mode.setTooltip (juce::String::fromUTF8 ("Modo de esta banda. Global = el de los botones de arriba. Ejemplo: graves en Tube (cuerpo, pares) y agudos en Tape (suaves) o en Transistor (mordida)."));
        addAndMakeVisible (strip.mode);
        strip.modeAttachment = std::make_unique<ComboBoxAttachment> (processor.getParameters(), id::bandMode[b], strip.mode);

        strip.drive.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        strip.drive.setTooltip (juce::String::fromUTF8 ("Drive de esta banda, sumado al Input. Subirlo satura m\xc3\xa1s solo esta zona; bajarlo la deja m\xc3\xa1s limpia que el resto. Con AUTO GAIN el volumen de la banda se mantiene."));
        addAndMakeVisible (strip.drive);
        strip.driveAttachment = std::make_unique<SliderAttachment> (processor.getParameters(), id::bandDrive[b], strip.drive);
        strip.drive.setDoubleClickReturnValue (true, 0.0);

        strip.solo.setClickingTogglesState (true);
        strip.solo.setColour (juce::TextButton::buttonOnColourId, riptide::gui::theme::meterHot.withAlpha (0.6f));
        strip.solo.setTooltip (juce::String::fromUTF8 ("Solo: escuchar solo esta banda (tambi\xc3\xa9n en la original del mix y en el delta). Sirve para ajustar el drive y los cruces de cada banda. Varias en solo = suenan todas ellas."));
        addAndMakeVisible (strip.solo);
        strip.soloAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), id::bandSolo[b], strip.solo);
        strip.mute.setClickingTogglesState (true);
        strip.mute.setColour (juce::TextButton::buttonOnColourId, riptide::gui::theme::meterClip.withAlpha (0.6f));
        strip.mute.setTooltip (juce::String::fromUTF8 ("Mute: quitar esta banda de la salida. Con una banda en solo, el solo manda."));
        addAndMakeVisible (strip.mute);
        strip.muteAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), id::bandMute[b], strip.mute);

        strip.autoGain.setJustificationType (juce::Justification::centred);
        strip.autoGain.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        strip.autoGain.setTooltip (juce::String::fromUTF8 ("Compensaci\xc3\xb3n de AUTO GAIN de esta banda (tenue si AUTO GAIN est\xc3\xa1 apagado)."));
        addAndMakeVisible (strip.autoGain);
    }
    for (size_t i = 0; i < crossoverKnobs.size(); ++i)
        setUpKnob (crossoverKnobs[i], id::crossover[i], "Cruce " + juce::String (i + 1),
                   juce::String::fromUTF8 ("Frecuencia que separa las dos bandas vecinas (Linkwitz-Riley, 24 dB/oct: sin saturaci\xc3\xb3n la suma es plana). Si dos cruces quedan a menos de media octava, Riptide los separa: lo que suena se ve debajo del nombre de cada banda."));
    bandCountAttachment->sendInitialUpdate();
    updateBandReadouts();

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
    addAndMakeVisible (knob.slider);
    knob.attachment = std::make_unique<SliderAttachment> (processor.getParameters(), parameterId, knob.slider);
    // Doble clic = valor por defecto (0 dB en las ganancias, apagado en los cortes).
    auto* parameter = processor.getParameters().getParameter (parameterId);
    knob.slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    knob.label.setColour (juce::Label::textColourId, riptide::gui::theme::dimText);
    addAndMakeVisible (knob.label);
}

void RiptideAudioProcessorEditor::setUpPositionButton (juce::TextButton& button, std::unique_ptr<ButtonAttachment>& attachment,
                                                       const char* parameterId)
{
    button.setClickingTogglesState (true);
    button.setColour (juce::TextButton::buttonOnColourId, riptide::gui::theme::accent.withAlpha (0.35f));
    button.setTooltip (juce::String::fromUTF8 ("PRE = el filtro act\xc3\xba" "a antes de la curva (decide qu\xc3\xa9 se satura). POST = act\xc3\xba" "a despu\xc3\xa9s (solo cambia lo que se oye). Cambiarlo cruza los dos en 20 ms, sin clics."));
    // El texto sigue al estado (también cuando lo cambia la automatización o un preset).
    button.onStateChange = [&button] { button.setButtonText (button.getToggleState() ? "POST" : "PRE"); };
    addAndMakeVisible (button);
    attachment = std::make_unique<ButtonAttachment> (processor.getParameters(), parameterId, button);
    button.onStateChange();
}

void RiptideAudioProcessorEditor::setUpToggle (juce::TextButton& button, std::unique_ptr<ButtonAttachment>& attachment,
                                               const char* parameterId, const juce::String& tooltip)
{
    button.setClickingTogglesState (true);
    button.setColour (juce::TextButton::buttonOnColourId, riptide::gui::theme::accent.withAlpha (0.55f));
    button.setTooltip (tooltip);
    addAndMakeVisible (button);
    attachment = std::make_unique<ButtonAttachment> (processor.getParameters(), parameterId, button);
}

void RiptideAudioProcessorEditor::updateAutoGainReadout()
{
    // Encendido: la compensación que se aplica. Apagado: la que se aplicaría, tenue (útil para ver cuánto cambia el
    // volumen el drive).
    // Con varias bandas cada una tiene la suya (en su columna).
    autoGainReadout.setText (shownBands > 1 ? "por banda" : formatDb (processor.getAutoGainDb()), juce::dontSendNotification);
    autoGainReadout.setColour (juce::Label::textColourId, autoGainButton.getToggleState() ? riptide::gui::theme::accent
                                                                                           : riptide::gui::theme::dimText.withAlpha (0.6f));
}

void RiptideAudioProcessorEditor::showBandCount (int count)
{
    shownBands = juce::jlimit (1, riptide::dsp::maxBands, count);
    bandCountButtons[static_cast<size_t> (shownBands - 1)].setToggleState (true, juce::dontSendNotification);
    // Las bandas y los cruces que no existen se ven apagados (y no se pueden tocar).
    for (size_t b = 0; b < bandStrips.size(); ++b)
    {
        const bool active = static_cast<int> (b) < shownBands;
        auto& strip = bandStrips[b];
        for (juce::Component* component : { static_cast<juce::Component*> (&strip.title), static_cast<juce::Component*> (&strip.range),
                                            static_cast<juce::Component*> (&strip.mode), static_cast<juce::Component*> (&strip.drive),
                                            static_cast<juce::Component*> (&strip.solo), static_cast<juce::Component*> (&strip.mute),
                                            static_cast<juce::Component*> (&strip.autoGain) })
        {
            component->setEnabled (active);
            component->setAlpha (active ? 1.0f : 0.3f);
        }
    }
    for (size_t i = 0; i < crossoverKnobs.size(); ++i)
    {
        const bool active = static_cast<int> (i) < shownBands - 1;
        for (juce::Component* component : { static_cast<juce::Component*> (&crossoverKnobs[i].slider),
                                            static_cast<juce::Component*> (&crossoverKnobs[i].label) })
        {
            component->setEnabled (active);
            component->setAlpha (active ? 1.0f : 0.3f);
        }
    }
    updateBandReadouts();
    repaint();
}

void RiptideAudioProcessorEditor::updateBandReadouts()
{
    // El rango de cada banda con los cruces que suenan (ya separados), y la compensación de su auto gain.
    const auto colour = autoGainButton.getToggleState() ? riptide::gui::theme::accent : riptide::gui::theme::dimText.withAlpha (0.6f);
    for (size_t b = 0; b < bandStrips.size(); ++b)
    {
        auto& strip = bandStrips[b];
        const int band = static_cast<int> (b);
        const float low = band == 0 ? 20.0f : processor.getCrossoverHz (band - 1);
        const float high = band >= shownBands - 1 ? 20000.0f : processor.getCrossoverHz (band);
        strip.range.setText (static_cast<int> (b) < shownBands ? formatHz (low) + " - " + formatHz (high) : juce::String ("-"),
                             juce::dontSendNotification);
        strip.autoGain.setText ("auto " + formatDb (processor.getAutoGainDb (band)), juce::dontSendNotification);
        strip.autoGain.setColour (juce::Label::textColourId, colour);
    }
}

void RiptideAudioProcessorEditor::setUpSectionLabel (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, riptide::gui::theme::accent);
    addAndMakeVisible (label);
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
    updateAutoGainReadout();
    updateBandReadouts();
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
    g.drawText ("saturador  -  Fase 6", header.withTrimmedRight (oversamplingLabel.getRight() - oversamplingLabel.getX() + 180),
                juce::Justification::centredLeft);

    // Panel de las perillas, entre los dos medidores.
    const auto body = getLocalBounds().withTrimmedTop (headerHeight).reduced (padding);
    auto knobsPanel = body.reduced (meterWidth + padding, 0).toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (knobsPanel, 8.0f);
    g.setColour (theme::panelOutline);
    g.drawRoundedRectangle (knobsPanel.reduced (0.5f), 8.0f, 1.0f);
    paintBands (g);

    // Flecha del recorrido de la señal: IN → perillas → OUT.
    g.setColour (theme::dimText.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String::fromUTF8 ("input \xe2\x86\x92 PRE \xe2\x86\x92 emphasis \xe2\x86\x92 BANDAS (drive \xc2\xb7 modo \xc2\xb7 auto gain) \xe2\x86\x92 de-emph. \xe2\x86\x92 POST \xe2\x86\x92 tilt \xe2\x86\x92 mix \xe2\x86\x92 delta \xe2\x86\x92 output"),
                knobsPanel.removeFromBottom (static_cast<float> (signalFlowHeight)), juce::Justification::centred);
}

void RiptideAudioProcessorEditor::paintBands (juce::Graphics& g)
{
    using namespace riptide::gui;
    // Fondo de cada columna: las que suenan, más claras; las apagadas, apenas marcadas.
    for (size_t b = 0; b < bandStrips.size(); ++b)
    {
        const auto area = bandStrips[b].area.toFloat();
        const bool active = static_cast<int> (b) < shownBands;
        g.setColour (active ? theme::control.withAlpha (0.7f) : theme::control.withAlpha (0.2f));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (active ? theme::panelOutline : theme::panelOutline.withAlpha (0.4f));
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);
    }
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

    // Tono: cinco perillas en el orden de la señal; debajo de los cortes, su interruptor PRE/POST.
    panel.removeFromTop (4);
    toneSection.setBounds (panel.removeFromTop (sectionLabelHeight));
    auto toneRow = panel.removeFromTop (18 + toneKnobSize + 20 + 4 + positionButtonHeight);
    const int toneWidth = toneRow.getWidth() / 5;
    for (auto* knob : { &lowCutKnob, &highCutKnob, &emphasisKnob, &emphasisFreqKnob, &tiltKnob })
    {
        auto column = toneRow.removeFromLeft (toneWidth);
        knob->label.setBounds (column.removeFromTop (18));
        knob->slider.setBounds (column.removeFromTop (toneKnobSize + 20));
        column.removeFromTop (4);
        if (knob == &lowCutKnob)
            lowCutPositionButton.setBounds (column.withSizeKeepingCentre (56, positionButtonHeight));
        else if (knob == &highCutKnob)
            highCutPositionButton.setBounds (column.withSizeKeepingCentre (56, positionButtonHeight));
    }

    // Bandas: el título con el selector 1-4 a la derecha; debajo, banda | cruce | banda | cruce | ...
    panel.removeFromTop (6);
    auto bandsHeader = panel.removeFromTop (22);
    for (auto it = bandCountButtons.rbegin(); it != bandCountButtons.rend(); ++it)
        it->setBounds (bandsHeader.removeFromRight (30).reduced (2, 0));
    bandsSection.setBounds (bandsHeader);
    panel.removeFromTop (4);
    auto bandRow = panel.removeFromTop (bandStripHeight);
    const int stripWidth = (bandRow.getWidth() - riptide::dsp::maxCrossovers * crossoverColumnWidth) / riptide::dsp::maxBands;
    for (size_t b = 0; b < bandStrips.size(); ++b)
    {
        auto& strip = bandStrips[b];
        strip.area = bandRow.removeFromLeft (stripWidth);
        auto column = strip.area.reduced (6, 6);
        strip.title.setBounds (column.removeFromTop (16));
        strip.range.setBounds (column.removeFromTop (16));
        column.removeFromTop (4);
        strip.mode.setBounds (column.removeFromTop (22));
        column.removeFromTop (4);
        strip.drive.setBounds (column.removeFromTop (bandKnobSize + 18));
        column.removeFromTop (4);
        auto buttons = column.removeFromTop (22);
        strip.solo.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
        strip.mute.setBounds (buttons.reduced (2, 0));
        strip.autoGain.setBounds (column.removeFromTop (18));

        if (b < crossoverKnobs.size())
        {
            auto crossoverColumn = bandRow.removeFromLeft (crossoverColumnWidth);
            auto content = crossoverColumn.withSizeKeepingCentre (crossoverColumnWidth, 18 + bandKnobSize + 20);
            crossoverKnobs[b].label.setBounds (content.removeFromTop (18));
            crossoverKnobs[b].slider.setBounds (content);
        }
    }

    // Nivel y mezcla: Input (drive), Mix, Output y la columna de AUTO GAIN (con su lectura) y DELTA.
    panel.removeFromTop (6);
    levelSection.setBounds (panel.removeFromTop (sectionLabelHeight));
    auto knobs = panel.removeFromTop (levelKnobsHeight);
    const int knobWidth = knobs.getWidth() / 4;
    for (auto* knob : { &inputKnob, &mixKnob, &outputKnob })
    {
        auto column = knobs.removeFromLeft (knobWidth);
        auto content = column.withSizeKeepingCentre (column.getWidth(), juce::jmin (column.getHeight(), knobSize + 20 + 24));
        knob->label.setBounds (content.removeFromTop (20));
        knob->slider.setBounds (content);
    }
    auto toggles = knobs.withSizeKeepingCentre (toggleWidth, toggleHeight * 2 + 20 + 16);
    autoGainButton.setBounds (toggles.removeFromTop (toggleHeight));
    autoGainReadout.setBounds (toggles.removeFromTop (20));
    toggles.removeFromTop (16);
    deltaButton.setBounds (toggles.removeFromTop (toggleHeight));
}
