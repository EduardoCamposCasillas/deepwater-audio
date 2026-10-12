#pragma once

#include <cmath>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "gui/Format.h"
#include "gui/Theme.h"

namespace fathom::gui
{

// El panel de la banda elegida (Fase 2: la única, una campana): encendido, FRECUENCIA, GANANCIA y Q, y debajo lo que
// esos números significan (ancho en octavas, nota del centro). En la Fase 4 este panel pasa a mostrar la banda que se
// elija en el visor.
class BandPanel final : public juce::Component
{
public:
    explicit BandPanel (juce::AudioProcessorValueTreeState& parameters)
    {
        enabledButton.setClickingTogglesState (true);
        enabledButton.setColour (juce::TextButton::buttonOnColourId, theme::accent.withAlpha (0.45f));
        enabledButton.setTooltip (juce::String::fromUTF8 ("Encender o apagar la banda. Sin clic: al apagarla, su ganancia "
                                                          "baja a 0 dB en 20 ms. Sirve para comparar con y sin ella."));
        addAndMakeVisible (enabledButton);
        enabledAttachment = std::make_unique<ButtonAttachment> (parameters, params::band1Enabled, enabledButton);
        enabledButton.onStateChange = [this] { updateInfo(); };

        setUpKnob (frequency, parameters, params::band1Frequency, "FRECUENCIA", 1000.0, false,
                   juce::String::fromUTF8 ("El centro de la campana: la zona que sube o baja. La franja de abajo del visor "
                                           "dice qu\xc3\xa9 vive ah\xc3\xad. Arrastra con la ganancia alta para \"barrer\" "
                                           "y encontrar lo que molesta. Doble clic = 1 kHz."));
        setUpKnob (gain, parameters, params::band1Gain, "GANANCIA", 0.0, true,
                   juce::String::fromUTF8 ("Cu\xc3\xa1nto sube (+) o baja (\xe2\x88\x92) el centro, hasta \xc2\xb1" "30 dB. "
                                           "Restar antes que sumar: cortes angostos para quitar lo que molesta, subidas "
                                           "anchas y suaves para dar car\xc3\xa1" "cter. En 0 dB la banda no toca el "
                                           "sonido. Doble clic = 0 dB."));
        setUpKnob (q, parameters, params::band1Q, "Q", 1.0, false,
                   juce::String::fromUTF8 ("Lo angosta que es la campana. Q bajo (0.3-1) = ancha y musical; Q alto (5-40) "
                                           "= angosta y quir\xc3\xbargica, para cazar una resonancia. Q 1.41 = una octava "
                                           "de ancho. Doble clic = 1."));

        info.setJustificationType (juce::Justification::centredLeft);
        info.setFont (juce::FontOptions (12.0f));
        info.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (info);
        updateInfo();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("BANDA 1", titleArea, juce::Justification::centredLeft);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String::fromUTF8 ("CAMPANA (peaking)"), titleArea.withTrimmedLeft (60), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 8);
        auto left = area.removeFromLeft (190);
        titleArea = left.removeFromTop (22);
        left.removeFromTop (6);
        enabledButton.setBounds (left.removeFromTop (26).withWidth (60));
        left.removeFromTop (6);
        info.setBounds (left);

        for (auto* knob : { &frequency, &gain, &q })
        {
            auto column = area.removeFromLeft (110);
            knob->label.setBounds (column.removeFromTop (16));
            knob->slider.setBounds (column);
        }
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

    void setUpKnob (Knob& knob, juce::AudioProcessorValueTreeState& parameters, const char* id, const char* name,
                    double defaultValue, bool bipolar, const juce::String& tooltip)
    {
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
        knob.slider.getProperties().set ("bipolar", bipolar);
        knob.slider.setTooltip (tooltip);
        knob.slider.onValueChange = [this] { updateInfo(); };
        addAndMakeVisible (knob.slider);
        knob.attachment = std::make_unique<SliderAttachment> (parameters, id, knob.slider);
        knob.slider.setDoubleClickReturnValue (true, defaultValue);

        knob.label.setText (name, juce::dontSendNotification);
        knob.label.setJustificationType (juce::Justification::centred);
        knob.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        knob.label.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (knob.label);
    }

    // Traducción de los números a lo que se oye: el ancho en octavas (con la definición de Q del Cookbook, medido donde
    // la campana vale la mitad de su ganancia en dB) y la nota de FL del centro.
    void updateInfo()
    {
        const double qValue = juce::jmax (0.001, q.slider.getValue());
        const double octaves = 2.0 / std::log (2.0) * std::asinh (1.0 / (2.0 * qValue));
        const auto hz = static_cast<float> (frequency.slider.getValue());
        auto text = juce::String::fromUTF8 ("Ancho: ") + juce::String (octaves, octaves < 1.0 ? 2 : 1) + " oct\n"
                    + juce::String::fromUTF8 ("Centro: ") + format::noteName (hz);
        if (! enabledButton.getToggleState())
            text << "\n(apagada)";
        info.setText (text, juce::dontSendNotification);
    }

    juce::TextButton enabledButton { "ON" };
    std::unique_ptr<ButtonAttachment> enabledAttachment;
    Knob frequency, gain, q;
    juce::Label info;
    juce::Rectangle<int> titleArea;
};

} // namespace fathom::gui
