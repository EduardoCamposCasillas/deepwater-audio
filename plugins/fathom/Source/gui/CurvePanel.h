#pragma once

#include <cmath>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "analysis/SpectrumMatch.h"
#include "gui/EqDisplay.h"
#include "gui/Format.h"
#include "gui/Theme.h"

namespace fathom::gui
{

// Fase 12: el panel de IGUALAR y DIBUJAR. Ocupa el lugar del panel de la banda mientras una de las dos herramientas está
// activa (el editor los alterna). Sus ajustes viven en el procesador (CurveTools): sobreviven a cerrar la ventana.
//
// IGUALAR: REFERENCIA (aprender, y de dónde: el sidechain u otra parte de la misma entrada), TU PISTA (aprender la
// entrada) y BORRAR; CANTIDAD (cuánto de la diferencia) y SUAVIZADO (cuánto detalle copia).
// DIBUJAR: BORRAR DIBUJO.
// Las dos: BANDAS (cuántas como mucho), REEMPLAZAR (borrar las que hay o sumarse a ellas), CREAR BANDAS y SALIR, y a la
// derecha qué haría la vista previa (cuántas bandas y cuánto se aleja todavía de lo pedido).
class CurvePanel final : public juce::Component
{
public:
    CurvePanel (FathomAudioProcessor& p, EqDisplay& d) : processor (p), display (d)
    {
        title.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::curveWanted);
        addAndMakeVisible (title);
        info.setJustificationType (juce::Justification::topLeft);
        info.setFont (juce::FontOptions (11.5f));
        info.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (info);

        // Aprender.
        for (auto* label : { &referenceLabel, &currentLabel, &smoothingLabel, &replaceLabel })
            setUpLabel (*label);
        referenceLabel.setText ("REFERENCIA", juce::dontSendNotification);
        currentLabel.setText ("TU PISTA", juce::dontSendNotification);
        smoothingLabel.setText ("SUAVIZADO", juce::dontSendNotification);
        replaceLabel.setText ("LAS DE AHORA", juce::dontSendNotification);
        for (auto* button : { &learnReferenceButton, &learnCurrentButton, &replaceButton })
        {
            button->setClickingTogglesState (true);
            button->setColour (juce::TextButton::buttonOnColourId, theme::curveWanted.withAlpha (0.5f));
            addAndMakeVisible (*button);
        }
        learnReferenceButton.setTooltip (juce::String::fromUTF8 ("Mientras est\xc3\xa1 encendido, Fathom suma el espectro "
                                                                 "de la referencia a su promedio. D\xc3\xa9jalo 10-30 "
                                                                 "segundos sobre la parte m\xc3\xa1s representativa (el "
                                                                 "coro, el drop): cuanto m\xc3\xa1s tiempo, m\xc3\xa1s "
                                                                 "estable. Los silencios no cuentan."));
        learnReferenceButton.onClick = [this] {
            tools().learningReference = learnReferenceButton.getToggleState();
            display.curveSettingsChanged();
        };
        learnCurrentButton.setTooltip (juce::String::fromUTF8 ("Lo mismo con tu pista: lo que entra a Fathom ANTES del EQ "
                                                               "(as\xc3\xad lo que ya hacen las bandas no cambia lo "
                                                               "aprendido). Puede aprender a la vez que la referencia."));
        learnCurrentButton.onClick = [this] {
            tools().learningCurrent = learnCurrentButton.getToggleState();
            display.curveSettingsChanged();
        };
        sourceBox.addItem ("del sidechain", 1);
        sourceBox.addItem ("de la entrada", 2);
        sourceBox.setTooltip (juce::String::fromUTF8 ("De d\xc3\xb3nde se aprende la referencia. Sidechain: otra pista de "
                                                      "FL mandada a Fathom (en el mixer: la pista de la referencia "
                                                      "\xe2\x86\x92 \"Sidechain to this track\"). Entrada: la misma pista "
                                                      "de Fathom en otro momento (aprende la referencia mientras suena "
                                                      "ella, ap\xc3\xa1galo y aprende tu pista despu\xc3\xa9s)."));
        sourceBox.onChange = [this] {
            tools().referenceSource = sourceBox.getSelectedId() - 1;
            display.curveSettingsChanged();
        };
        addAndMakeVisible (sourceBox);
        clearLearnedButton.setTooltip (juce::String::fromUTF8 ("Borrar lo aprendido de las dos (para empezar con otra "
                                                               "referencia u otra parte)."));
        clearLearnedButton.onClick = [this] {
            tools().reference.clear();
            tools().current.clear();
            display.curveSettingsChanged();
        };
        addAndMakeVisible (clearLearnedButton);

        // Cuánto y con cuánto detalle.
        setUpKnob (amount, "CANTIDAD",
                   juce::String::fromUTF8 ("Cu\xc3\xa1nto de la diferencia se aplica. 100 % = que tu pista suene con el "
                                           "balance de la referencia; 50-70 % suele bastar: acercarse al color sin perder "
                                           "el tuyo."));
        amount.slider.setRange (0.0, 100.0, 1.0);
        amount.slider.setTextValueSuffix (" %");
        amount.slider.setDoubleClickReturnValue (true, 100.0);
        amount.slider.onValueChange = [this] {
            tools().amount = static_cast<float> (amount.slider.getValue() * 0.01);
            display.curveSettingsChanged();
        };
        for (size_t i = 0; i < analysis::matchSmoothingNames.size(); ++i)
            smoothingBox.addItem (analysis::matchSmoothingNames[i], static_cast<int> (i) + 1);
        smoothingBox.setTooltip (juce::String::fromUTF8 ("Cu\xc3\xa1nto detalle copia la curva. 1 octava = solo el balance "
                                                         "grueso (graves, medios, agudos); 1/3 = lo habitual para hablar "
                                                         "de \"color\"; 1/6 = sigue detalles finos (m\xc3\xa1s riesgo de "
                                                         "copiar notas y resonancias de la otra canci\xc3\xb3n en vez de "
                                                         "su balance)."));
        smoothingBox.onChange = [this] {
            tools().smoothingIndex = smoothingBox.getSelectedId() - 1;
            display.curveSettingsChanged();
        };
        addAndMakeVisible (smoothingBox);

        // Dibujar.
        clearDrawingButton.setTooltip (juce::String::fromUTF8 ("Borrar todo lo dibujado (tambi\xc3\xa9n: Supr con el mouse "
                                                               "sobre el visor)."));
        clearDrawingButton.onClick = [this] { display.clearDrawing(); };
        addAndMakeVisible (clearDrawingButton);

        // Las dos.
        setUpKnob (maxBands, "BANDAS",
                   juce::String::fromUTF8 ("Cu\xc3\xa1ntas bandas como mucho. Fathom usa las que hagan falta (se "
                                           "detiene cuando otra banda casi no mejora): pocas = una curva "
                                           "m\xc3\xa1s simple y f\xc3\xa1" "cil de retocar; muchas = m\xc3\xa1s fiel."));
        maxBands.slider.setRange (1.0, edit::CurveTools::maxBandsLimit, 1.0);
        maxBands.slider.setDoubleClickReturnValue (true, 8.0);
        maxBands.slider.onValueChange = [this] {
            tools().maxBands = juce::roundToInt (maxBands.slider.getValue());
            display.curveSettingsChanged();
        };
        replaceButton.setTooltip (juce::String::fromUTF8 ("Encendido: las bandas que ya tienes se borran y las nuevas "
                                                          "hacen toda la curva. Apagado: las nuevas se suman a las de "
                                                          "ahora (en las bandas libres) y solo agregan lo que falta. Las "
                                                          "bandas de un solo canal (L, R, mid, side) no se cuentan."));
        replaceButton.onClick = [this] {
            tools().replace = replaceButton.getToggleState();
            replaceButton.setButtonText (tools().replace ? "REEMPLAZAR" : "SUMAR");
            display.curveSettingsChanged();
        };
        createButton.setColour (juce::TextButton::buttonColourId, theme::curvePreview.withAlpha (0.35f));
        createButton.setTooltip (juce::String::fromUTF8 ("Crear las bandas de la vista previa (la l\xc3\xadnea verde). Es "
                                                         "un solo paso de deshacer y quedan elegidas: arrastrar una las "
                                                         "mueve todas, la rueda cambia el Q de todas."));
        createButton.onClick = [this] {
            const int created = display.createBandsFromCurve();
            if (created == 0)
                message = juce::String::fromUTF8 ("No hay bandas que crear: lo pedido ya est\xc3\xa1, o faltan bandas "
                                                  "libres.");
        };
        addAndMakeVisible (createButton);
        exitButton.setTooltip (juce::String::fromUTF8 ("Volver a la edici\xc3\xb3n normal (Esc). Lo aprendido y lo "
                                                       "dibujado se guardan mientras el proyecto est\xc3\xa9 abierto."));
        exitButton.onClick = [this] { display.setTool (EqDisplay::Tool::none); };
        addAndMakeVisible (exitButton);

        status.setJustificationType (juce::Justification::topLeft);
        status.setFont (juce::FontOptions (11.5f));
        status.setColour (juce::Label::textColourId, theme::text);
        addAndMakeVisible (status);
    }

    // El editor la muestra al elegir una herramienta.
    void showTool (EqDisplay::Tool tool)
    {
        shownTool = tool;
        const bool match = tool == EqDisplay::Tool::match;
        title.setText (match ? "IGUALAR A UNA REFERENCIA" : "DIBUJAR LA CURVA", juce::dontSendNotification);
        info.setText (match ? juce::String::fromUTF8 ("Aprende el espectro promedio de una referencia y el de tu "
                                                      "pista; la curva naranja es la diferencia (d\xc3\xb3nde a tu "
                                                      "pista le falta o le sobra), sin contar el volumen. "
                                                      "\xc3\x81mbar = la referencia aprendida, gris = tu pista. "
                                                      "Verde = lo que har\xc3\xad" "an las bandas.")
                            : juce::String::fromUTF8 ("Arrastra sobre el visor la curva que quieres (escala del "
                                                      "EQ, la de la derecha); clic derecho o Alt + arrastrar "
                                                      "borra. Lo que no dibujes se queda como est\xc3\xa1. Verde "
                                                      "= lo que har\xc3\xad" "an las bandas."),
                      juce::dontSendNotification);
        for (auto* component : std::initializer_list<juce::Component*> { &referenceLabel, &currentLabel, &learnReferenceButton,
                                                                         &learnCurrentButton, &sourceBox, &clearLearnedButton,
                                                                         &amount.slider, &amount.label, &smoothingLabel, &smoothingBox })
            component->setVisible (match);
        clearDrawingButton.setVisible (! match);

        const auto& t = tools();
        learnReferenceButton.setToggleState (t.learningReference, juce::dontSendNotification);
        learnCurrentButton.setToggleState (t.learningCurrent, juce::dontSendNotification);
        sourceBox.setSelectedId (t.referenceSource + 1, juce::dontSendNotification);
        amount.slider.setValue (t.amount * 100.0, juce::dontSendNotification);
        smoothingBox.setSelectedId (t.smoothingIndex + 1, juce::dontSendNotification);
        maxBands.slider.setValue (t.maxBands, juce::dontSendNotification);
        replaceButton.setToggleState (t.replace, juce::dontSendNotification);
        replaceButton.setButtonText (t.replace ? "REEMPLAZAR" : "SUMAR");
        message.clear();
        refresh();
    }

    // En cada cuadro (lo llama el editor): lo aprendido y la vista previa.
    void refresh()
    {
        if (! isVisible())
            return;
        const auto& t = tools();
        const bool match = shownTool == EqDisplay::Tool::match;
        // Lo que se apaga desde otro lado (salir de la herramienta lo apaga) se refleja.
        learnReferenceButton.setToggleState (t.learningReference, juce::dontSendNotification);
        learnCurrentButton.setToggleState (t.learningCurrent, juce::dontSendNotification);
        learnReferenceButton.setButtonText (learnText (t.reference, t.learningReference));
        learnCurrentButton.setButtonText (learnText (t.current, t.learningCurrent));

        const auto s = display.getCurveStatus();
        juce::String text;
        if (match && ! t.reference.hasData())
            text = juce::String::fromUTF8 ("Falta aprender la REFERENCIA (enciende APRENDER mientras suena).");
        else if (match && ! t.current.hasData())
            text = juce::String::fromUTF8 ("Falta aprender TU PISTA.");
        else if (match && (t.learningReference || t.learningCurrent))
            text = juce::String::fromUTF8 ("Aprendiendo... La vista previa aparece al apagar APRENDER.");
        else if (! s.hasWanted)
            text = match ? juce::String::fromUTF8 ("Nada que comparar todav\xc3\xad" "a.")
                         : juce::String ("Dibuja una curva sobre el visor.");
        else if (s.freeBands == 0)
            text = juce::String::fromUTF8 ("No quedan bandas libres: borra alguna o elige REEMPLAZAR.");
        else if (! s.previewReady)
            text = juce::String::fromUTF8 ("Calculando la vista previa...");
        else if (s.bands == 0)
            text = juce::String::fromUTF8 ("Lo pedido ya est\xc3\xa1: no hace falta ninguna banda.");
        else
            text = "Vista previa: " + juce::String (s.bands) + (s.bands == 1 ? " banda" : " bandas")
                   + juce::String::fromUTF8 ("  \xc2\xb7  se aleja ") + juce::String (s.maxErrorDb, 1)
                   + " dB como mucho (sin bandas: " + juce::String (s.startErrorDb, 1) + " dB)";
        if (message.isNotEmpty())
            text << "\n" << message;
        if (text != status.getText())
            status.setText (text, juce::dontSendNotification);
        createButton.setEnabled (s.previewReady && s.bands > 0);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (theme::curveWanted);
        g.fillRoundedRectangle (bounds.removeFromLeft (4.0f).reduced (0.0f, 10.0f), 2.0f);
        g.setColour (theme::panelOutline);
        g.fillRect (juce::Rectangle<float> (14.0f, dividerY, static_cast<float> (getWidth()) - 28.0f, 1.0f));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14, 8);
        auto bottom = area.removeFromBottom (72);
        area.removeFromBottom (10);
        dividerY = static_cast<float> (bottom.getY()) - 5.0f;

        // Arriba: qué es (a la izquierda), aprender (o borrar el dibujo) y el estado de la vista previa.
        auto left = area.removeFromLeft (300);
        title.setBounds (left.removeFromTop (18));
        info.setBounds (left);
        area.removeFromLeft (12);
        auto reference = area.removeFromLeft (150);
        referenceLabel.setBounds (reference.removeFromTop (16));
        learnReferenceButton.setBounds (reference.removeFromTop (26));
        reference.removeFromTop (6);
        sourceBox.setBounds (reference.removeFromTop (24));
        area.removeFromLeft (10);
        auto current = area.removeFromLeft (150);
        currentLabel.setBounds (current.removeFromTop (16));
        learnCurrentButton.setBounds (current.removeFromTop (26));
        current.removeFromTop (6);
        clearLearnedButton.setBounds (current.removeFromTop (24));
        clearDrawingButton.setBounds (juce::Rectangle<int> (reference.getX(), referenceLabel.getBottom(), 150, 26));
        area.removeFromLeft (12);
        status.setBounds (area);

        // Abajo: cuánto, con cuánto detalle, cuántas bandas, sumar o reemplazar, crear y salir.
        auto row = bottom;
        for (auto* knob : { &amount, &maxBands })
        {
            auto column = row.removeFromLeft (80);
            knob->label.setBounds (column.removeFromTop (16));
            knob->slider.setBounds (column);
            row.removeFromLeft (10);
        }
        auto smoothing = row.removeFromLeft (110);
        smoothingLabel.setBounds (smoothing.removeFromTop (16));
        smoothingBox.setBounds (smoothing.removeFromTop (26));
        row.removeFromLeft (12);
        auto replace = row.removeFromLeft (110);
        replaceLabel.setBounds (replace.removeFromTop (16));
        replaceButton.setBounds (replace.removeFromTop (26));
        exitButton.setBounds (row.removeFromRight (80).withHeight (34).withY (row.getY() + 16));
        row.removeFromRight (10);
        createButton.setBounds (row.removeFromRight (150).withHeight (34).withY (row.getY() + 16));
    }

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
    };

    edit::CurveTools& tools() const noexcept { return processor.getCurveTools(); }

    // "APRENDER" / "APRENDIENDO 12 s" / "APRENDIDO 12 s".
    static juce::String learnText (const analysis::AverageSpectrum& spectrum, bool learning)
    {
        const auto seconds = juce::String (juce::roundToInt (spectrum.seconds())) + " s";
        if (learning)
            return "APRENDIENDO " + seconds;
        return spectrum.hasData() ? "APRENDIDO " + seconds : "APRENDER";
    }

    void setUpLabel (juce::Label& label)
    {
        label.setJustificationType (juce::Justification::centredLeft);
        label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (label);
    }

    void setUpKnob (Knob& knob, const juce::String& name, const juce::String& tooltip)
    {
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        knob.slider.setColour (juce::Slider::rotarySliderFillColourId, theme::curveWanted);
        knob.slider.setTooltip (tooltip);
        addAndMakeVisible (knob.slider);
        knob.label.setText (name, juce::dontSendNotification);
        knob.label.setJustificationType (juce::Justification::centred);
        knob.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        knob.label.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (knob.label);
    }

    FathomAudioProcessor& processor;
    EqDisplay& display;
    EqDisplay::Tool shownTool = EqDisplay::Tool::none;
    juce::Label title, info, status;
    juce::Label referenceLabel, currentLabel, smoothingLabel, replaceLabel;
    juce::TextButton learnReferenceButton { "APRENDER" }, learnCurrentButton { "APRENDER" };
    juce::ComboBox sourceBox, smoothingBox;
    juce::TextButton clearLearnedButton { "BORRAR" }, clearDrawingButton { "BORRAR DIBUJO" };
    Knob amount, maxBands;
    juce::TextButton replaceButton { "SUMAR" };
    juce::TextButton createButton { "CREAR BANDAS" }, exitButton { "SALIR" };
    juce::String message; // lo que pasó al apretar CREAR BANDAS (si no se creó nada)
    float dividerY = 0.0f;
};

} // namespace fathom::gui
