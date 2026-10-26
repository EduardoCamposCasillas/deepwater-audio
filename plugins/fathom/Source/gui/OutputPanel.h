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
// Abajo, el modo de FASE (Fase 10) con la calidad de la fase lineal y la latencia que agrega.
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

        // Fase 10: los modos de fase. El orden de las listas es el de los parámetros (dsp::PhaseMode y las calidades).
        setUpLabel (phaseLabel, "FASE");
        phaseBox.addItem ("Cero latencia", 1);
        phaseBox.addItem ("Natural", 2);
        phaseBox.addItem ("Lineal", 3);
        phaseBox.setTooltip (juce::String::fromUTF8 (
            "C\xc3\xb3mo trata el EQ la FASE (el retardo de cada frecuencia), adem\xc3\xa1s del volumen.\n"
            "CERO LATENCIA: filtros como los de una consola; cerca de lo que tocan retrasan un poco unas frecuencias "
            "respecto de otras. Casi nunca se oye. Para grabar y tocar en vivo, y en casi todo.\n"
            "NATURAL: la magnitud Y la fase de un EQ anal\xc3\xb3gico tambi\xc3\xa9n en los agudos (cerca de Nyquist el "
            "de cero latencia se aparta). 3.6 ms de latencia, casi sin pre-eco. Para buses y el master.\n"
            "LINEAL: todas las frecuencias se retrasan IGUAL (la forma de onda no se deforma): para capas en paralelo del "
            "mismo sonido, mid/side o un kick con su sub. Cuesta latencia, CPU y PRE-ECO: lo que una campana angosta o "
            "un corte empinado hace sonar tambi\xc3\xa9n suena ANTES del golpe (se oye como un \"chorro\" antes del "
            "kick). En los graves, mira la curva: con poca CALIDAD no alcanza a hacer una campana angosta."));
        addAndMakeVisible (phaseBox);
        phaseAttachment = std::make_unique<ComboBoxAttachment> (parameters, params::phaseMode, phaseBox);

        setUpLabel (qualityLabel, "CALIDAD");
        for (const auto* name : { "Baja", "Media", "Alta", "Muy alta", "M\xc3\xa1xima" })
            qualityBox.addItem (juce::String::fromUTF8 (name), qualityBox.getNumItems() + 1);
        qualityBox.setTooltip (juce::String::fromUTF8 (
            "Solo en fase LINEAL: el largo del filtro. M\xc3\xa1s largo = m\xc3\xa1s detalle en los graves (una campana "
            "de Q 4 en 40 Hz: con BAJA llega a +3 de +9 dB; con M\xc3\xa1XIMA, a +8.7), pero m\xc3\xa1s latencia y CPU "
            "(BAJA 35 ms, MEDIA 58, ALTA 104, MUY ALTA 197, M\xc3\xa1XIMA 383 ms). La curva muestra lo que de verdad "
            "suena con la calidad elegida."));
        addAndMakeVisible (qualityBox);
        qualityAttachment = std::make_unique<ComboBoxAttachment> (parameters, params::linearQuality, qualityBox);

        latencyReadout.setJustificationType (juce::Justification::centred);
        latencyReadout.setFont (juce::FontOptions (11.0f));
        latencyReadout.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (latencyReadout);
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

        // La calidad solo cuenta en fase lineal; abajo, la latencia que agrega el modo (la que FL compensa), más la de la
        // dinámica espectral si alguna banda la usa (Fase 11).
        const auto mode = processor.getPhaseMode();
        qualityBox.setEnabled (mode == dsp::PhaseMode::linear);
        qualityLabel.setAlpha (mode == dsp::PhaseMode::linear ? 1.0f : 0.4f);
        const double sampleRate = processor.getAnalyzerSampleRate();
        int latency = dsp::PhaseSizes::latency (mode, processor.getLinearQuality(), sampleRate);
        for (int b = 0; b < dsp::Engine::numBands; ++b)
            if (dsp::isSpectral (processor.getBandSettings (b)))
            {
                latency += dsp::SpectralSizes::latency (sampleRate);
                break;
            }
        const auto latencyText = latency == 0 ? juce::String ("sin latencia")
                                              : "latencia " + juce::String (1000.0 * latency / sampleRate, latency * 1000.0 < 10.0 * sampleRate ? 1 : 0) + " ms";
        if (latencyText != latencyReadout.getText())
            latencyReadout.setText (latencyText, juce::dontSendNotification);
        latencyReadout.setColour (juce::Label::textColourId, latency == 0 ? theme::dimText.withAlpha (0.6f) : theme::text);
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
        area.removeFromTop (14);
        phaseLabel.setBounds (area.removeFromTop (15));
        phaseBox.setBounds (area.removeFromTop (24));
        area.removeFromTop (6);
        qualityLabel.setBounds (area.removeFromTop (15));
        qualityBox.setBounds (area.removeFromTop (24));
        latencyReadout.setBounds (area.removeFromTop (18));
    }

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setUpLabel (juce::Label& label, const char* name)
    {
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centredLeft);
        label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (label);
    }

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
    juce::Label phaseLabel, qualityLabel, latencyReadout;
    juce::ComboBox phaseBox, qualityBox;
    std::unique_ptr<ComboBoxAttachment> phaseAttachment, qualityAttachment;
};

} // namespace fathom::gui
