#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "analysis/SpectrumView.h"
#include "gui/Theme.h"

namespace fathom::gui
{

// Los ajustes del analizador (Fase 5), en la columna de la derecha: qué espectros se ven (entrada, salida, sidechain) y
// cómo (canales mid/side de la Fase 8, inclinación, rango, resolución, velocidad, retención de picos), más CONGELAR. Nada de esto cambia el sonido.
// El editor guarda los ajustes en el proyecto (onViewChanged) y se los pasa al visor.
class AnalyzerPanel final : public juce::Component
{
public:
    AnalyzerPanel()
    {
        title.setText ("ANALIZADOR", juce::dontSendNotification);
        title.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::dimText);
        title.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (title);

        setUpToggle (inputButton, theme::spectrumIn,
                     juce::String::fromUTF8 ("Ver el espectro de lo que ENTRA al plugin (l\xc3\xadnea gris)."));
        setUpToggle (outputButton, theme::accent,
                     juce::String::fromUTF8 ("Ver el espectro de lo que SALE del plugin (relleno azul). Lo que cambie entre "
                                             "ENTRADA y SALIDA lo hizo el EQ."));
        setUpToggle (sidechainButton, theme::spectrumSidechain,
                     juce::String::fromUTF8 ("Ver el espectro de OTRA pista (l\xc3\xadnea \xc3\xa1mbar), la que llega por el "
                                             "sidechain: por ejemplo el kick mientras ecualizas el 808, para ver d\xc3\xb3nde "
                                             "chocan. En FL: en la pista del kick, clic derecho en la pista de Fathom > "
                                             "\"Sidechain to this track\"; despu\xc3\xa9s, en Fathom, el engranaje > "
                                             "Processing > el puerto de entrada del sidechain."));

        addChoice (channelsBox, channelsLabel, "CANALES",
                   juce::String::fromUTF8 ("Qu\xc3\xa9 parte del est\xc3\xa9reo se mide. Mid (L+R) = lo del centro (voz, "
                                           "kick, bajo; en mono, todo). Side (L\xe2\x88\x92R) = solo lo que es distinto entre "
                                           "los lados: el ancho (reverbs, dobles, pads abiertos); un sonido en el centro no "
                                           "aparece. Mid + Side = cada espectro en mid y adem\xc3\xa1s el side de la SALIDA "
                                           "(l\xc3\xadnea violeta): cu\xc3\xa1nto ancho hay en cada zona. Los graves casi "
                                           "siempre deber\xc3\xad" "an estar en el mid."));
        for (const auto* name : analysis::channelsNames)
            channelsBox.addItem (juce::String::fromUTF8 (name), channelsBox.getNumItems() + 1);

        addChoice (tiltBox, tiltLabel, juce::String::fromUTF8 ("INCLINACI\xc3\x93N"),
                   juce::String::fromUTF8 ("Endereza el espectro: suma esos dB por octava hacia los agudos (1 kHz no se "
                                           "mueve). La m\xc3\xbasica cae hacia los agudos; con 4.5 dB/oct una mezcla "
                                           "equilibrada se ve casi plana y lo que sobra en cualquier zona salta a la vista. "
                                           "0 = el espectro \"crudo\". Solo cambia la vista."));
        for (const float tilt : analysis::tilts)
            tiltBox.addItem (juce::String (tilt, tilt == std::floor (tilt) ? 0 : 1) + " dB/oct", tiltBox.getNumItems() + 1);

        addChoice (rangeBox, rangeLabel, "RANGO",
                   juce::String::fromUTF8 ("Cu\xc3\xa1ntos dB cubre la escala del analizador (los n\xc3\xbameros de la "
                                           "izquierda), de 0 dB hacia abajo. 48-72 = m\xc3\xa1s detalle en lo fuerte; 120 = "
                                           "ver tambi\xc3\xa9n el ruido de fondo y las colas de reverb."));
        for (const float range : analysis::ranges)
            rangeBox.addItem (juce::String (juce::roundToInt (range)) + " dB", rangeBox.getNumItems() + 1);

        addChoice (resolutionBox, resolutionLabel, juce::String::fromUTF8 ("RESOLUCI\xc3\x93N"),
                   juce::String::fromUTF8 ("El tama\xc3\xb1o de la FFT. Alta / m\xc3\xa1xima = m\xc3\xa1s detalle en graves "
                                           "(se distinguen las notas del 808) pero reacciona m\xc3\xa1s lento (mira un "
                                           "trozo m\xc3\xa1s largo: hasta 0.37 s). Baja = sigue cada golpe, pero los graves "
                                           "se ven como manchas anchas."));
        for (const auto* name : analysis::resolutionNames)
            resolutionBox.addItem (juce::String::fromUTF8 (name), resolutionBox.getNumItems() + 1);

        addChoice (speedBox, speedLabel, "VELOCIDAD",
                   juce::String::fromUTF8 ("Cu\xc3\xa1nto tarda la l\xc3\xadnea en subir y en caer. Lenta = un promedio "
                                           "estable (el balance tonal de la mezcla); r\xc3\xa1pida = cada golpe y cada nota "
                                           "(para ver qu\xc3\xa9 resuena y cu\xc3\xa1ndo)."));
        for (const auto& speed : analysis::speeds)
            speedBox.addItem (juce::String::fromUTF8 (speed.name), speedBox.getNumItems() + 1);

        addChoice (holdBox, holdLabel, "PICOS",
                   juce::String::fromUTF8 ("Retenci\xc3\xb3n de picos: una l\xc3\xadnea fina guarda el m\xc3\xa1ximo de "
                                           "cada frecuencia. 2 s = lo que acaba de pasar (el golpe del kick); Siempre = "
                                           "todo lo que son\xc3\xb3 hasta que lo borres: qu\xc3\xa9 nota del 808 llega "
                                           "m\xc3\xa1s alto, d\xc3\xb3nde aparece una resonancia de vez en cuando."));
        for (const auto* name : analysis::holdNames)
            holdBox.addItem (juce::String::fromUTF8 (name), holdBox.getNumItems() + 1);

        clearButton.setTooltip (juce::String::fromUTF8 ("Borrar la l\xc3\xadnea de picos y empezar a medir de nuevo."));
        clearButton.onClick = [this] {
            if (onClearPeaks)
                onClearPeaks();
        };
        addAndMakeVisible (clearButton);

        freezeButton.setClickingTogglesState (true);
        freezeButton.setColour (juce::TextButton::buttonOnColourId, theme::accent.withAlpha (0.45f));
        freezeButton.setTooltip (juce::String::fromUTF8 ("Congelar el espectro tal como est\xc3\xa1 (el sonido sigue): "
                                                         "para mirarlo con calma, medir un pico o compararlo con lo que "
                                                         "suena despu\xc3\xa9s. Agarrar un pico lo congela solo mientras "
                                                         "lo arrastras."));
        freezeButton.onClick = [this] {
            if (onFreezeChanged)
                onFreezeChanged (freezeButton.getToggleState());
        };
        addAndMakeVisible (freezeButton);

        setView ({});
    }

    // El usuario cambió algún ajuste (getView() trae todos).
    std::function<void()> onViewChanged;
    std::function<void (bool)> onFreezeChanged;
    std::function<void()> onClearPeaks;

    // Muestra unos ajustes sin avisar (al abrir la ventana o al cargar un proyecto).
    void setView (const analysis::ViewSettings& view)
    {
        inputButton.setToggleState (view.showInput, juce::dontSendNotification);
        outputButton.setToggleState (view.showOutput, juce::dontSendNotification);
        sidechainButton.setToggleState (view.showSidechain, juce::dontSendNotification);
        tiltBox.setSelectedItemIndex (view.tiltIndex, juce::dontSendNotification);
        rangeBox.setSelectedItemIndex (view.rangeIndex, juce::dontSendNotification);
        resolutionBox.setSelectedItemIndex (view.resolutionIndex, juce::dontSendNotification);
        speedBox.setSelectedItemIndex (view.speedIndex, juce::dontSendNotification);
        holdBox.setSelectedItemIndex (view.holdIndex, juce::dontSendNotification);
        channelsBox.setSelectedItemIndex (view.channelsIndex, juce::dontSendNotification);
        clearButton.setEnabled (view.holdMode() != analysis::Hold::off);
    }

    analysis::ViewSettings getView() const
    {
        analysis::ViewSettings view;
        view.showInput = inputButton.getToggleState();
        view.showOutput = outputButton.getToggleState();
        view.showSidechain = sidechainButton.getToggleState();
        view.tiltIndex = juce::jmax (0, tiltBox.getSelectedItemIndex());
        view.rangeIndex = juce::jmax (0, rangeBox.getSelectedItemIndex());
        view.resolutionIndex = juce::jmax (0, resolutionBox.getSelectedItemIndex());
        view.speedIndex = juce::jmax (0, speedBox.getSelectedItemIndex());
        view.holdIndex = juce::jmax (0, holdBox.getSelectedItemIndex());
        view.channelsIndex = juce::jmax (0, channelsBox.getSelectedItemIndex());
        return view;
    }

    void setFrozen (bool frozen) { freezeButton.setToggleState (frozen, juce::dontSendNotification); }

    void resized() override
    {
        auto area = getLocalBounds();
        title.setBounds (area.removeFromTop (18));
        area.removeFromTop (4);
        for (auto* button : { &inputButton, &outputButton, &sidechainButton })
        {
            button->setBounds (area.removeFromTop (24));
            area.removeFromTop (4);
        }
        area.removeFromTop (6);
        const std::array<std::pair<juce::Label*, juce::ComboBox*>, 6> choices { { { &channelsLabel, &channelsBox },
                                                                                  { &tiltLabel, &tiltBox },
                                                                                  { &rangeLabel, &rangeBox },
                                                                                  { &resolutionLabel, &resolutionBox },
                                                                                  { &speedLabel, &speedBox },
                                                                                  { &holdLabel, &holdBox } } };
        for (const auto& [label, box] : choices)
        {
            label->setBounds (area.removeFromTop (15));
            box->setBounds (area.removeFromTop (24));
            area.removeFromTop (6);
        }
        clearButton.setBounds (area.removeFromTop (22));
        area.removeFromTop (10);
        freezeButton.setBounds (area.removeFromTop (26));
    }

private:
    void setUpToggle (juce::TextButton& button, juce::Colour colour, const juce::String& tooltip)
    {
        button.setClickingTogglesState (true);
        button.setColour (juce::TextButton::buttonOnColourId, colour.withAlpha (0.35f));
        button.setTooltip (tooltip);
        button.onClick = [this] { changed(); };
        addAndMakeVisible (button);
    }

    void addChoice (juce::ComboBox& box, juce::Label& label, const juce::String& name, const juce::String& tooltip)
    {
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centredLeft);
        label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (label);
        box.setTooltip (tooltip);
        box.onChange = [this] { changed(); };
        addAndMakeVisible (box);
    }

    void changed()
    {
        clearButton.setEnabled (getView().holdMode() != analysis::Hold::off);
        if (onViewChanged)
            onViewChanged();
    }

    juce::Label title;
    juce::TextButton inputButton { "ENTRADA" };
    juce::TextButton outputButton { "SALIDA" };
    juce::TextButton sidechainButton { "SIDECHAIN" };
    juce::ComboBox channelsBox, tiltBox, rangeBox, resolutionBox, speedBox, holdBox;
    juce::Label channelsLabel, tiltLabel, rangeLabel, resolutionLabel, speedLabel, holdLabel;
    juce::TextButton clearButton { "BORRAR PICOS" };
    juce::TextButton freezeButton { "CONGELAR" };
};

} // namespace fathom::gui
