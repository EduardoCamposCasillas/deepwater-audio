#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "gui/Format.h"
#include "gui/Theme.h"

namespace fathom::gui
{

// La columna de SALIDA, el final de la cadena (como el canal de una consola, de arriba a abajo): ESCALA de la ganancia
// de todas las bandas, AUTO GAIN (con lo que está compensando), BALANCE, invertir la polaridad (Ø) y la GANANCIA de salida.
class OutputPanel final : public juce::Component
{
public:
    explicit OutputPanel (FathomAudioProcessor& p) : processor (p)
    {
        auto& parameters = p.getParameters();
        title.setText ("SALIDA", juce::dontSendNotification);
        title.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::dimText);
        title.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (title);

        setUpKnob (scale, "ESCALA", false,
                   juce::String::fromUTF8 ("Cu\xc3\xa1nto de la ganancia de TODAS las bandas se aplica: 100 % = tal cual, "
                                           "50 % = la mitad de los dB, 0 % = plano, 200 % = el doble. Para responder "
                                           "\"\xc2\xbfme pas\xc3\xa9?\" sin tocar las bandas. Los cortes, el notch y el pasa "
                                           "banda no tienen ganancia: no cambian. Doble clic = 100 %."));
        scale.attachment = std::make_unique<SliderAttachment> (parameters, params::gainScale, scale.slider);
        scale.slider.setDoubleClickReturnValue (true, 100.0);

        autoGainButton.setClickingTogglesState (true);
        autoGainButton.setColour (juce::TextButton::buttonOnColourId, theme::accent.withAlpha (0.4f));
        autoGainButton.setTooltip (juce::String::fromUTF8 (
            "Compensa el volumen que sube o baja el EQ, para comparar con el BYPASS a volumen igual (lo m\xc3\xa1s fuerte "
            "siempre parece mejor). Se calcula de la curva, no midiendo la m\xc3\xbasica: no bombea. Una campana angosta "
            "casi no compensa; un shelf ancho, s\xc3\xad. Abajo, cu\xc3\xa1nto est\xc3\xa1 compensando. Es para comparar: "
            "despu\xc3\xa9s de decidir, puedes apagarlo y ajustar la GANANCIA a mano."));
        addAndMakeVisible (autoGainButton);
        autoGainAttachment = std::make_unique<ButtonAttachment> (parameters, params::autoGain, autoGainButton);
        autoGainReadout.setJustificationType (juce::Justification::centred);
        autoGainReadout.setFont (juce::FontOptions (11.0f));
        autoGainReadout.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (autoGainReadout);

        setUpKnob (pan, "BALANCE", true,
                   juce::String::fromUTF8 ("Balance de salida (solo en est\xc3\xa9reo): al centro no cambia nada; hacia un "
                                           "lado baja el otro (L 100 = solo el izquierdo). No es un paneo de potencia "
                                           "constante: el lado al que giras no sube. Doble clic = centro."));
        pan.attachment = std::make_unique<SliderAttachment> (parameters, params::outputPan, pan.slider);
        pan.slider.setDoubleClickReturnValue (true, 0.0);

        invertButton.setClickingTogglesState (true);
        invertButton.setColour (juce::TextButton::buttonOnColourId, theme::bypassOn.withAlpha (0.45f));
        invertButton.setTooltip (juce::String::fromUTF8 (
            "Invierte la polaridad (la onda al rev\xc3\xa9s, \xc3\x97 \xe2\x88\x92" "1). Sola no se oye distinta; importa "
            "al sumarla con otra pista parecida (dos micr\xc3\xb3" "fonos, un kick en capas, un bajo con su DI): si al "
            "invertir suena M\xc3\x81S lleno, estaban cancel\xc3\xa1ndose. Sin clic: pasa por 0 en 20 ms."));
        addAndMakeVisible (invertButton);
        invertAttachment = std::make_unique<ButtonAttachment> (parameters, params::phaseInvert, invertButton);

        setUpKnob (gain, "GANANCIA", true,
                   juce::String::fromUTF8 ("Volumen de salida (\xc2\xb1" "24 dB). Sirve para compensar lo que suben o "
                                           "bajan las bandas: al comparar con el BYPASS, lo m\xc3\xa1s fuerte siempre "
                                           "parece mejor, as\xc3\xad que iguala el volumen antes de juzgar (o usa AUTO "
                                           "GAIN). Doble clic = 0 dB."));
        gain.attachment = std::make_unique<SliderAttachment> (parameters, params::outputGain, gain.slider);
        gain.slider.setDoubleClickReturnValue (true, 0.0);
        refresh();
    }

    // Una vez por cuadro: lo que compensa el auto gain.
    void refresh()
    {
        const bool on = autoGainButton.getToggleState();
        const float db = processor.getAutoGainDb();
        const auto text = on ? format::db (std::abs (db) < 0.05f ? 0.0f : db) : juce::String ("apagado");
        if (text != autoGainReadout.getText())
            autoGainReadout.setText (text, juce::dontSendNotification);
        autoGainReadout.setColour (juce::Label::textColourId, on ? theme::text : theme::dimText.withAlpha (0.6f));
    }

    void resized() override
    {
        auto area = getLocalBounds().withTrimmedTop (8); // alineado con el panel del analizador
        title.setBounds (area.removeFromTop (18));
        area.removeFromTop (10);
        const auto placeKnob = [&area] (Knob& knob, int height) {
            knob.label.setBounds (area.removeFromTop (15));
            knob.slider.setBounds (area.removeFromTop (height));
        };
        placeKnob (scale, 84);
        area.removeFromTop (12);
        autoGainButton.setBounds (area.removeFromTop (24));
        autoGainReadout.setBounds (area.removeFromTop (18));
        area.removeFromTop (12);
        placeKnob (pan, 84);
        area.removeFromTop (10);
        invertButton.setBounds (area.removeFromTop (24));
        area.removeFromTop (14);
        gain.label.setBounds (area.removeFromTop (15));
        gain.slider.setBounds (area.removeFromTop (juce::jmin (area.getHeight(), 104)));
    }

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setUpKnob (Knob& knob, const char* name, bool bipolar, const juce::String& tooltip)
    {
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        knob.slider.getProperties().set ("bipolar", bipolar);
        knob.slider.setTooltip (tooltip);
        addAndMakeVisible (knob.slider);
        knob.label.setText (name, juce::dontSendNotification);
        knob.label.setJustificationType (juce::Justification::centred);
        knob.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        knob.label.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (knob.label);
    }

    FathomAudioProcessor& processor;
    juce::Label title;
    Knob scale, pan, gain;
    juce::TextButton autoGainButton { "AUTO GAIN" };
    juce::Label autoGainReadout;
    std::unique_ptr<ButtonAttachment> autoGainAttachment;
    juce::TextButton invertButton { juce::String::fromUTF8 ("\xc3\x98  POLARIDAD") };
    std::unique_ptr<ButtonAttachment> invertAttachment;
};

} // namespace fathom::gui
