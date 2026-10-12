#pragma once

#include <cmath>
#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "gui/Format.h"
#include "gui/Theme.h"

namespace fathom::gui
{

// El panel de la banda elegida: cuál de las 24 (el selector o un clic en su nodo del visor), encendido, FORMA,
// PENDIENTE (solo los cortes), FRECUENCIA, GANANCIA y Q, y debajo lo que esos números significan para la forma
// elegida. Los controles que la forma no usa se atenúan. En la Fase 4 se agrega la edición sobre el visor.
class BandPanel final : public juce::Component
{
public:
    explicit BandPanel (FathomAudioProcessor& p) : processor (p), parameters (p.getParameters())
    {
        static_assert (format::shapeNames.size() == dsp::numShapes);

        for (int b = 0; b < params::numBands; ++b)
            bandSelector.addItem (bandItemText (b, false), b + 1);
        bandSelector.setTooltip (juce::String::fromUTF8 ("Qu\xc3\xa9 banda ves en el panel (24 en total). Tambi\xc3\xa9n se "
                                                         "elige con un clic en su nodo del visor. Las encendidas llevan "
                                                         "\xe2\x97\x8f."));
        bandSelector.onChange = [this] {
            if (const int chosen = bandSelector.getSelectedId() - 1; chosen >= 0 && chosen != band && onBandChosen)
                onBandChosen (chosen);
        };
        addAndMakeVisible (bandSelector);

        enabledButton.setClickingTogglesState (true);
        enabledButton.setTooltip (juce::String::fromUTF8 ("Encender o apagar la banda, sin clic (se funde en 20 ms). Sirve "
                                                          "para comparar con y sin ella. Apagada no gasta CPU."));
        addAndMakeVisible (enabledButton);

        for (size_t s = 0; s < format::shapeNames.size(); ++s)
            shapeBox.addItem (juce::String::fromUTF8 (format::shapeNames[s]), static_cast<int> (s) + 1);
        shapeBox.setTooltip (juce::String::fromUTF8 (
            "La forma de la banda:\n"
            "- Campana: sube o baja una zona.\n"
            "- Shelf graves / agudos: sube o baja todo lo que est\xc3\xa1 debajo / encima de la frecuencia (las perillas "
            "BASS y TREBLE de un equipo de sonido).\n"
            "- Tilt shelf: inclina el sonido alrededor de la frecuencia: los graves bajan lo que los agudos suben.\n"
            "- Flat tilt: una pendiente pareja en todo el espectro (10 dB de ganancia = 1 dB por octava): m\xc3\xa1s oscuro "
            "o m\xc3\xa1s brillante sin marcar ninguna zona.\n"
            "- Corte graves / agudos: quita todo lo que est\xc3\xa1 debajo / encima (el low cut de la voz o del hi-hat).\n"
            "- Notch: quita una sola frecuencia por completo (un zumbido, una resonancia que silba).\n"
            "- Pasa banda: deja solo la zona de la frecuencia (efecto tel\xc3\xa9" "fono o radio vieja).\n"
            "- All-pass: no cambia el volumen de nada, solo la fase (para alinear capas; se oye poco o nada solo).\n"
            "Cambiar de forma sonando no hace clic: la banda se funde."));
        addAndMakeVisible (shapeBox);

        for (size_t s = 0; s < dsp::slopes.size(); ++s)
            slopeBox.addItem (juce::String (dsp::slopes[s]) + " dB/oct", static_cast<int> (s) + 1);
        slopeBox.setTooltip (juce::String::fromUTF8 (
            "Solo en los cortes: lo empinado de la ca\xc3\xad" "da. 6 dB/oct = suave (cada octava m\xc3\xa1s all\xc3\xa1, la "
            "mitad de volumen); 12 y 24 = los m\xc3\xa1s usados; 48 a 96 = una pared que quita justo lo que pides, pero "
            "suena m\xc3\xa1s a \"filtro\" y gira m\xc3\xa1s la fase cerca del corte."));
        addAndMakeVisible (slopeBox);

        for (auto* label : { &shapeLabel, &slopeLabel })
            setUpLabel (*label);
        shapeLabel.setText ("FORMA", juce::dontSendNotification);
        slopeLabel.setText ("PENDIENTE", juce::dontSendNotification);

        setUpKnob (frequency, "FRECUENCIA", false,
                   juce::String::fromUTF8 ("D\xc3\xb3nde act\xc3\xba" "a la banda: el centro de la campana, el punto medio "
                                           "del shelf, el corte (\xe2\x88\x92" "3 dB) o el eje del tilt. La franja de abajo "
                                           "del visor dice qu\xc3\xa9 vive ah\xc3\xad. Doble clic = la frecuencia inicial "
                                           "de la banda."));
        setUpKnob (gain, "GANANCIA", true,
                   juce::String::fromUTF8 ("Cu\xc3\xa1nto sube (+) o baja (\xe2\x88\x92), hasta \xc2\xb1" "30 dB. En el flat "
                                           "tilt es la pendiente: 10 dB = 1 dB por octava. Los cortes, el notch, el pasa "
                                           "banda y el all-pass no tienen ganancia. Restar antes que sumar. Doble clic = "
                                           "0 dB."));
        setUpKnob (q, "Q", false,
                   juce::String::fromUTF8 ("Campana, notch y pasa banda: lo angosto (Q 1.41 = una octava; 5-40 = "
                                           "quir\xc3\xbargico). Shelves y cortes: la forma del codo. 0.71 = lo m\xc3\xa1s "
                                           "suave sin rebote (Butterworth en los cortes); m\xc3\xa1s alto = un pico de "
                                           "resonancia justo antes del corte. Doble clic = 1."));

        info.setJustificationType (juce::Justification::topLeft);
        info.setFont (juce::FontOptions (12.0f));
        info.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (info);

        showBand (0);
    }

    // Llamado cuando el usuario elige otra banda en el selector (el editor decide y llama a showBand).
    std::function<void (int)> onBandChosen;

    // Conecta los controles a los parámetros de otra banda.
    void showBand (int newBand)
    {
        band = newBand;
        enabledAttachment.reset();
        shapeAttachment.reset();
        slopeAttachment.reset();
        for (auto* knob : { &frequency, &gain, &q })
            knob->attachment.reset();

        enabledAttachment = std::make_unique<ButtonAttachment> (parameters, params::band (band, params::enabled), enabledButton);
        shapeAttachment = std::make_unique<ComboBoxAttachment> (parameters, params::band (band, params::shape), shapeBox);
        slopeAttachment = std::make_unique<ComboBoxAttachment> (parameters, params::band (band, params::slope), slopeBox);
        attachKnob (frequency, params::band (band, params::frequency));
        attachKnob (gain, params::band (band, params::gain));
        attachKnob (q, params::band (band, params::q));

        const auto colour = theme::bandColour (band);
        enabledButton.setColour (juce::TextButton::buttonOnColourId, colour.withAlpha (0.45f));
        for (auto* knob : { &frequency, &gain, &q })
            knob->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        bandSelector.setSelectedId (band + 1, juce::dontSendNotification);
        describeAgain = true;
        refresh();
        repaint();
    }

    [[nodiscard]] int getBand() const noexcept { return band; }

    // Una vez por cuadro: el texto de ayuda, los controles que la forma no usa y las marcas del selector.
    void refresh()
    {
        for (int b = 0; b < params::numBands; ++b)
        {
            const bool on = processor.getBandSettings (b).enabled;
            if (on != markedOn[static_cast<size_t> (b)])
            {
                markedOn[static_cast<size_t> (b)] = on;
                bandSelector.changeItemText (b + 1, bandItemText (b, on));
                if (b == band) // el texto a la vista no cambia solo: se vuelve a elegir la misma banda con el texto nuevo
                    bandSelector.setText (bandItemText (b, on), juce::dontSendNotification);
            }
        }

        const auto settings = processor.getBandSettings (band);
        if (sameSettings (settings, shown) && ! describeAgain)
            return;
        shown = settings;
        describeAgain = false;
        gain.setUsed (dsp::usesGain (settings.shape));
        q.setUsed (dsp::usesQ (settings.shape, settings.slopeIndex));
        slopeBox.setEnabled (dsp::usesSlope (settings.shape));
        slopeLabel.setEnabled (slopeBox.isEnabled());
        info.setText (describe (settings), juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);
        // Una franja del color de la banda a la izquierda: el mismo del nodo en el visor.
        g.setColour (theme::bandColour (band));
        g.fillRoundedRectangle (bounds.removeFromLeft (4.0f).reduced (0.0f, 10.0f), 2.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14, 8);
        auto left = area.removeFromLeft (200);
        auto top = left.removeFromTop (26);
        bandSelector.setBounds (top.removeFromLeft (128));
        top.removeFromLeft (8);
        enabledButton.setBounds (top.removeFromLeft (52));
        left.removeFromTop (8);
        info.setBounds (left);

        area.removeFromLeft (12);
        auto choices = area.removeFromLeft (140);
        shapeLabel.setBounds (choices.removeFromTop (16));
        shapeBox.setBounds (choices.removeFromTop (26));
        choices.removeFromTop (8);
        slopeLabel.setBounds (choices.removeFromTop (16));
        slopeBox.setBounds (choices.removeFromTop (26));

        area.removeFromLeft (12);
        for (auto* knob : { &frequency, &gain, &q })
        {
            auto column = area.removeFromLeft (108);
            knob->label.setBounds (column.removeFromTop (16));
            knob->slider.setBounds (column);
        }
    }

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;

        // Atenuado = la forma elegida no lo usa (se puede mover igual: cuenta al volver a una forma que sí lo usa).
        void setUsed (bool used)
        {
            slider.setAlpha (used ? 1.0f : 0.35f);
            label.setAlpha (used ? 1.0f : 0.35f);
        }
    };

    static juce::String bandItemText (int b, bool on)
    {
        auto text = "Banda " + juce::String (b + 1);
        if (on)
            text << juce::String::fromUTF8 ("  \xe2\x97\x8f");
        return text;
    }

    static bool sameSettings (const dsp::BandSettings& a, const dsp::BandSettings& b) noexcept
    {
        return a.enabled == b.enabled && a.frequency == b.frequency && a.gainDb == b.gainDb && a.q == b.q && a.shape == b.shape
               && a.slopeIndex == b.slopeIndex;
    }

    void setUpLabel (juce::Label& label)
    {
        label.setJustificationType (juce::Justification::centredLeft);
        label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (label);
    }

    void setUpKnob (Knob& knob, const char* name, bool bipolar, const juce::String& tooltip)
    {
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
        knob.slider.getProperties().set ("bipolar", bipolar);
        knob.slider.setTooltip (tooltip);
        addAndMakeVisible (knob.slider);

        knob.label.setText (name, juce::dontSendNotification);
        knob.label.setJustificationType (juce::Justification::centred);
        knob.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        knob.label.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (knob.label);
    }

    void attachKnob (Knob& knob, const std::string& id)
    {
        knob.attachment = std::make_unique<SliderAttachment> (parameters, id, knob.slider);
        // Doble clic = el valor inicial del parámetro (cada banda tiene su frecuencia inicial).
        if (auto* parameter = parameters.getParameter (id))
            knob.slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }

    // Traducción de los números a lo que se oye, según la forma.
    static juce::String describe (const dsp::BandSettings& s)
    {
        const auto note = format::noteName (s.frequency);
        const auto where = format::hz (s.frequency) + (note.isEmpty() ? juce::String() : " (" + note + ")");
        const auto octaves = format::octavesForQ (s.q);
        const auto width = juce::String (octaves, octaves < 1.0 ? 2 : 1) + " oct";
        juce::String text;
        switch (s.shape)
        {
            case dsp::Shape::bell: text = "Centro: " + where + "\nAncho: " + width; break;
            case dsp::Shape::lowShelf:
            case dsp::Shape::highShelf:
                text = juce::String::fromUTF8 ("Mitad de la subida: ") + where + "\n"
                       + (s.shape == dsp::Shape::lowShelf ? "Todo lo de abajo: " : "Todo lo de arriba: ") + format::db (s.gainDb);
                break;
            case dsp::Shape::tiltShelf:
                text = "Gira en: " + where + "\nGraves " + format::db (-s.gainDb / 2.0f) + ", agudos " + format::db (s.gainDb / 2.0f);
                break;
            case dsp::Shape::flatTilt:
            {
                const float slope = s.gainDb * dsp::BandSettings::flatTiltDbPerOctavePerDb;
                text = "Gira en: " + where + "\nPendiente: " + juce::String (slope > 0.0f ? "+" : "") + juce::String (slope, 2) + " dB/oct";
                break;
            }
            case dsp::Shape::lowCut:
            case dsp::Shape::highCut:
                text = juce::String::fromUTF8 ("Corte (\xe2\x88\x92" "3 dB): ") + where + "\n"
                       + (s.shape == dsp::Shape::lowCut ? "Quita lo de abajo, " : "Quita lo de arriba, ")
                       + juce::String (dsp::slopes[static_cast<size_t> (s.slopeIndex)]) + " dB/oct";
                break;
            case dsp::Shape::notch: text = "Hueco en: " + where + "\nAncho: " + width; break;
            case dsp::Shape::bandPass: text = "Deja pasar: " + where + "\nAncho: " + width; break;
            case dsp::Shape::allPass:
                text = juce::String::fromUTF8 ("Fase girada 180\xc2\xb0 en: ") + where + "\nEl volumen no cambia";
                break;
        }
        if (! s.enabled)
            text << "\n(apagada)";
        return text;
    }

    FathomAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& parameters;
    int band = 0;
    dsp::BandSettings shown;
    bool describeAgain = true;
    std::array<bool, params::numBands> markedOn {};

    juce::ComboBox bandSelector;
    juce::TextButton enabledButton { "ON" };
    std::unique_ptr<ButtonAttachment> enabledAttachment;
    juce::ComboBox shapeBox, slopeBox;
    juce::Label shapeLabel, slopeLabel;
    std::unique_ptr<ComboBoxAttachment> shapeAttachment, slopeAttachment;
    Knob frequency, gain, q;
    juce::Label info;
};

} // namespace fathom::gui
