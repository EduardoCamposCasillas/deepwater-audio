#pragma once

#include <cmath>
#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/Solo.h"
#include "gui/Format.h"
#include "gui/Theme.h"

namespace fathom::gui
{

// El panel de la banda elegida: cuál de las 24 (el selector o un clic en su nodo del visor), encendido, SOLO, FORMA,
// PENDIENTE (solo los cortes), CANAL (estéreo, L, R, mid o side: Fase 8), FRECUENCIA, GANANCIA y Q, y debajo lo que esos números significan para la forma
// elegida. Los controles que la forma no usa se atenúan. Lo mismo se edita arrastrando el nodo en el visor (Fase 4).
// El SOLO sigue a la banda del panel: con él encendido, elegir otra banda escucha la zona de la nueva.
// Abajo, la dinámica (Fase 9): DINÁMICO, un medidor (nivel de la zona, umbral y cuánto se mueve la banda ahora),
// DETECTOR (la banda o el sidechain), umbral AUTO, RANGO, UMBRAL, ATAQUE y LIBERACIÓN.
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

        soloButton.setClickingTogglesState (true);
        soloButton.setColour (juce::TextButton::buttonOnColourId, theme::solo.withAlpha (0.45f));
        soloButton.setTooltip (juce::String::fromUTF8 (
            "Escuchar solo la zona de la banda (lo que llega, sin el EQ): campana, notch y pasa banda = la franja de su "
            "frecuencia y su Q; shelf o corte de graves = lo de abajo; de agudos = lo de arriba. Sirve para encontrar "
            "una resonancia (Q alto + barrer la frecuencia) y para o\xc3\xadr qu\xc3\xa9 quita un corte. Los tilts no tienen "
            "zona. No se guarda: se apaga al cerrar la ventana."));
        soloButton.onClick = [this] { processor.setSoloBand (soloButton.getToggleState() ? band : -1); };
        addAndMakeVisible (soloButton);

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

        static_assert (format::placementNames.size() == dsp::numPlacements);
        for (size_t i = 0; i < format::placementNames.size(); ++i)
            placementBox.addItem (juce::String::fromUTF8 (format::placementNames[i]), static_cast<int> (i) + 1);
        placementBox.setTooltip (juce::String::fromUTF8 (
            "D\xc3\xb3nde act\xc3\xba" "a la banda en el est\xc3\xa9reo:\n"
            "- Est\xc3\xa9reo: los dos lados igual.\n"
            "- Izquierda / Derecha: un solo lado (corregir una toma que suena distinta de un lado).\n"
            "- Mid: solo lo del CENTRO (lo que suena igual en los dos lados: voz, kick, bajo, caja).\n"
            "- Side: solo lo que es DISTINTO entre los lados: el ancho (reverbs, dobles, pads, guitarras abiertas). "
            "Subir agudos en el side abre la mezcla sin tocar la voz; cortar graves en el side la deja firme en "
            "mono.\n"
            "Con el analizador en Side ves lo que tocas. En una pista mono el side no hace nada. Cambiar sonando no "
            "hace clic: la banda se funde."));
        addAndMakeVisible (placementBox);

        for (auto* label : { &shapeLabel, &slopeLabel, &placementLabel })
            setUpLabel (*label);
        shapeLabel.setText ("FORMA", juce::dontSendNotification);
        slopeLabel.setText ("PENDIENTE", juce::dontSendNotification);
        placementLabel.setText ("CANAL", juce::dontSendNotification);

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

        // Fase 9: la dinámica.
        dynamicButton.setClickingTogglesState (true);
        dynamicButton.setTooltip (juce::String::fromUTF8 ("EQ din\xc3\xa1mico: la banda se mueve sola seg\xc3\xban cu\xc3\xa1nto suena su zona (la misma que "
                                      "escuchas en SOLO). Con la zona debajo del UMBRAL queda en su GANANCIA; cuando la "
                                      "pasa, se aleja hasta el RANGO: 1 dB por cada dB que la pasa. Sirve para lo que "
                                      "molesta solo a ratos: las eses de una voz, la resonancia de una nota del 808, el "
                                      "grave que se acumula en un acorde. Solo las campanas, shelves y tilts."));
        addAndMakeVisible (dynamicButton);

        autoButton.setClickingTogglesState (true);
        autoButton.setColour (juce::TextButton::buttonOnColourId, theme::accent.withAlpha (0.45f));
        autoButton.setTooltip (juce::String::fromUTF8 ("Umbral autom\xc3\xa1tico: el umbral queda 3 dB sobre el nivel T\xc3\x8dPICO de la zona (el "
                                   "promedio de los \xc3\xbaltimos ~1.5 s). Lo de siempre no se toca y solo act\xc3\xba" "an los momentos "
                                   "en que la zona suena m\xc3\xa1s fuerte que de costumbre, aunque la pista suba o baje. La "
                                   "perilla UMBRAL no se usa; el RANGO dice cu\xc3\xa1nto."));
        addAndMakeVisible (autoButton);

        sourceBox.addItem ("Banda", 1);
        sourceBox.addItem ("Sidechain", 2);
        sourceBox.setTooltip (juce::String::fromUTF8 ("Qu\xc3\xa9 escucha el detector:\n"
                                  "- Banda: la zona en la propia pista (lo normal: cortar lo que sobra).\n"
                                  "- Sidechain: la zona en otra pista (en FL, \"Sidechain to this track\" y el puerto "
                                  "en Processing). Para tallar espacio solo cuando el otro suena: bajar 60-100 Hz del "
                                  "808 cuando pega el kick, o 2-4 kHz de los acordes cuando canta la voz."));
        addAndMakeVisible (sourceBox);

        for (auto* label : { &sourceLabel, &autoLabel })
            setUpLabel (*label);
        sourceLabel.setText ("DETECTOR", juce::dontSendNotification);
        autoLabel.setText ("UMBRAL", juce::dontSendNotification);

        setUpKnob (range, "RANGO", true,
                   juce::String::fromUTF8 ("Hasta d\xc3\xb3nde se mueve la banda con la zona fuerte, sumado a su GANANCIA: \xe2\x88\x92" "6 = corta hasta 6 dB "
                       "(lo t\xc3\xadpico); + sube (que algo aparezca solo cuando suena fuerte). La l\xc3\xadnea vertical en el nodo "
                       "del visor marca hasta d\xc3\xb3nde llega. Doble clic = \xe2\x88\x92" "6 dB."));
        setUpKnob (threshold, "UMBRAL", false,
                   juce::String::fromUTF8 ("Desde qu\xc3\xa9 nivel de la zona act\xc3\xba" "a (pico de un seno; el medidor de la izquierda muestra el nivel "
                       "y el umbral). B\xc3\xa1jalo hasta que la banda se mueva solo en los momentos que molestan. Doble clic "
                       "= \xe2\x88\x92" "20 dB."));
        setUpKnob (attack, "ATAQUE", false,
                   juce::String::fromUTF8 ("Cu\xc3\xa1nto tarda la banda en llegar (63 %) a su corte cuando la zona pasa el umbral. Corto (0.5-5 "
                       "ms) = agarra eses y golpes; largo (20-100 ms) = deja pasar el ataque y doma lo que sigue. "
                       "Doble clic = 10 ms."));
        setUpKnob (release, juce::String::fromUTF8 ("LIBERACI\xc3\x93N"), false,
                   juce::String::fromUTF8 ("Cu\xc3\xa1nto tarda en volver a su ganancia cuando la zona baja. Corto = sigue cada s\xc3\xadlaba (puede "
                       "sonar nervioso); largo = m\xc3\xa1s suave, pero tarda en soltar. Doble clic = 150 ms."));

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
        if (processor.getSoloBand() >= 0)
            processor.setSoloBand (band); // el solo sigue a la banda del panel
        enabledAttachment.reset();
        shapeAttachment.reset();
        slopeAttachment.reset();
        placementAttachment.reset();
        dynamicAttachment.reset();
        autoAttachment.reset();
        sourceAttachment.reset();
        for (auto* knob : { &frequency, &gain, &q, &range, &threshold, &attack, &release })
            knob->attachment.reset();

        enabledAttachment = std::make_unique<ButtonAttachment> (parameters, params::band (band, params::enabled), enabledButton);
        shapeAttachment = std::make_unique<ComboBoxAttachment> (parameters, params::band (band, params::shape), shapeBox);
        slopeAttachment = std::make_unique<ComboBoxAttachment> (parameters, params::band (band, params::slope), slopeBox);
        placementAttachment = std::make_unique<ComboBoxAttachment> (parameters, params::band (band, params::stereo), placementBox);
        attachKnob (frequency, params::band (band, params::frequency));
        attachKnob (gain, params::band (band, params::gain));
        attachKnob (q, params::band (band, params::q));
        dynamicAttachment = std::make_unique<ButtonAttachment> (parameters, params::band (band, params::dynamic), dynamicButton);
        autoAttachment = std::make_unique<ButtonAttachment> (parameters, params::band (band, params::autoThreshold), autoButton);
        sourceAttachment = std::make_unique<ComboBoxAttachment> (parameters, params::band (band, params::dynamicSource), sourceBox);
        attachKnob (range, params::band (band, params::dynamicRange));
        attachKnob (threshold, params::band (band, params::threshold));
        attachKnob (attack, params::band (band, params::attack));
        attachKnob (release, params::band (band, params::release));

        const auto colour = theme::bandColour (band);
        enabledButton.setColour (juce::TextButton::buttonOnColourId, colour.withAlpha (0.45f));
        dynamicButton.setColour (juce::TextButton::buttonOnColourId, colour.withAlpha (0.45f));
        for (auto* knob : { &frequency, &gain, &q, &range, &threshold, &attack, &release })
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
        // El solo también se puede encender o apagar desde el menú del visor.
        soloButton.setToggleState (processor.getSoloBand() >= 0, juce::dontSendNotification);

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

        // El medidor de la dinámica se mueve con el audio: se repinta solo si cambió de forma visible.
        const auto live = processor.getDynamicState (band);
        const auto movedDb = [] (float a, float b) { return std::abs (a - b) >= 0.1f; };
        if (movedDb (live.gainDb, shownDynamics.gainDb) || movedDb (live.levelDb, shownDynamics.levelDb)
            || movedDb (live.thresholdDb, shownDynamics.thresholdDb) || settings.dynamics.enabled != shown.dynamics.enabled)
        {
            shownDynamics = live;
            repaint (meterArea.expanded (2).getSmallestIntegerContainer());
        }

        if (sameSettings (settings, shown) && ! describeAgain)
            return;
        shown = settings;
        describeAgain = false;
        gain.setUsed (dsp::usesGain (settings.shape));
        q.setUsed (dsp::usesQ (settings.shape, settings.slopeIndex));
        slopeBox.setEnabled (dsp::usesSlope (settings.shape));
        slopeLabel.setEnabled (slopeBox.isEnabled());
        soloButton.setAlpha (dsp::canSolo (settings.shape) ? 1.0f : 0.35f); // un tilt no tiene zona: el solo no hace nada

        // La dinámica: atenuada entera si la forma no tiene ganancia; sus ajustes, si está apagada; el umbral, en AUTO.
        const bool canMove = dsp::canBeDynamic (settings.shape);
        const bool moving = canMove && settings.dynamics.enabled;
        dynamicButton.setAlpha (canMove ? 1.0f : 0.35f);
        for (auto* component : std::initializer_list<juce::Component*> { &autoButton, &sourceBox, &sourceLabel, &autoLabel })
            component->setAlpha (moving ? 1.0f : 0.45f);
        for (auto* knob : { &range, &attack, &release })
            knob->setUsed (moving);
        threshold.setUsed (moving && ! settings.dynamics.autoThreshold);
        info.setText (describe (settings), juce::dontSendNotification);
        repaint (meterArea.expanded (2).getSmallestIntegerContainer());
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
        // La línea que separa la fila de la dinámica.
        g.setColour (theme::panelOutline);
        g.fillRect (juce::Rectangle<float> (14.0f, dividerY, static_cast<float> (getWidth()) - 28.0f, 1.0f));
        paintMeter (g);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14, 8);
        dynamicsRow = area.removeFromBottom (72);
        area.removeFromBottom (10);
        auto left = area.removeFromLeft (200);
        auto top = left.removeFromTop (26);
        bandSelector.setBounds (top.removeFromLeft (104));
        top.removeFromLeft (6);
        enabledButton.setBounds (top.removeFromLeft (42));
        top.removeFromLeft (6);
        soloButton.setBounds (top.removeFromLeft (42));
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
        auto channel = area.removeFromLeft (92);
        placementLabel.setBounds (channel.removeFromTop (16));
        placementBox.setBounds (channel.removeFromTop (26));

        area.removeFromLeft (12);
        for (auto* knob : { &frequency, &gain, &q })
        {
            auto column = area.removeFromLeft (100);
            knob->label.setBounds (column.removeFromTop (16));
            knob->slider.setBounds (column);
        }

        // La fila de la dinámica, con las mismas columnas que la de arriba.
        dividerY = static_cast<float> (dynamicsRow.getY()) - 5.0f;
        auto row = dynamicsRow;
        auto dynamicLeft = row.removeFromLeft (200);
        dynamicButton.setBounds (dynamicLeft.removeFromTop (26).removeFromLeft (104));
        dynamicLeft.removeFromTop (6);
        meterArea = dynamicLeft.removeFromTop (34).toFloat();
        row.removeFromLeft (12);
        auto source = row.removeFromLeft (140);
        sourceLabel.setBounds (source.removeFromTop (16));
        sourceBox.setBounds (source.removeFromTop (26));
        row.removeFromLeft (12);
        auto automatic = row.removeFromLeft (92);
        autoLabel.setBounds (automatic.removeFromTop (16));
        autoButton.setBounds (automatic.removeFromTop (26));
        row.removeFromLeft (12);
        for (auto* knob : { &range, &threshold, &attack, &release })
        {
            auto column = row.removeFromLeft (75);
            knob->label.setBounds (column.removeFromTop (16));
            knob->slider.setBounds (column);
        }
    }

    // La altura que necesita el panel: la fila de la banda y la de la dinámica.
    static constexpr int preferredHeight = 186;

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
               && a.slopeIndex == b.slopeIndex && a.placement == b.placement && a.dynamics.enabled == b.dynamics.enabled
               && a.dynamics.rangeDb == b.dynamics.rangeDb && a.dynamics.autoThreshold == b.dynamics.autoThreshold
               && a.dynamics.external == b.dynamics.external;
    }

    // El medidor de la dinámica: una barra con el nivel de la zona (de −60 a +6 dB), una marca en el umbral y, a la
    // derecha, cuánto se está moviendo la banda ahora. Apagada o con una forma sin ganancia: un texto que lo dice.
    void paintMeter (juce::Graphics& g) const
    {
        if (meterArea.isEmpty())
            return;
        const auto settings = processor.getBandSettings (band);
        auto area = meterArea;
        g.setFont (juce::FontOptions (11.0f));
        if (! dsp::canBeDynamic (settings.shape) || ! settings.dynamics.enabled)
        {
            g.setColour (theme::dimText.withAlpha (0.8f));
            g.drawText (dsp::canBeDynamic (settings.shape) ? juce::String::fromUTF8 ("Est\xc3\xa1tica (sin din\xc3\xa1mica)")
                                                            : juce::String::fromUTF8 ("Esta forma no tiene ganancia que mover"),
                        area, juce::Justification::centredLeft);
            return;
        }
        const auto colour = theme::bandColour (band);
        auto readout = area.removeFromRight (64.0f);
        g.setColour (std::abs (shownDynamics.gainDb) >= 0.05f ? colour : theme::dimText);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (format::db (shownDynamics.gainDb), readout, juce::Justification::centredRight);

        auto bar = area.withSizeKeepingCentre (area.getWidth(), 8.0f).withTrimmedRight (6.0f);
        const auto xFor = [&bar] (float db) {
            const float t = juce::jlimit (0.0f, 1.0f, (db - meterBottomDb) / (meterTopDb - meterBottomDb));
            return bar.getX() + t * bar.getWidth();
        };
        g.setColour (theme::track);
        g.fillRoundedRectangle (bar, 2.0f);
        const float levelX = xFor (shownDynamics.levelDb);
        const float thresholdX = xFor (shownDynamics.thresholdDb);
        // Debajo del umbral, gris; lo que lo pasa, del color de la banda (lo que la hace moverse).
        g.setColour (theme::dimText.withAlpha (0.7f));
        g.fillRoundedRectangle (bar.withRight (std::min (levelX, thresholdX)), 2.0f);
        if (levelX > thresholdX)
        {
            g.setColour (colour);
            g.fillRect (bar.withLeft (thresholdX).withRight (levelX));
        }
        g.setColour (theme::text);
        g.fillRect (juce::Rectangle<float> (thresholdX - 1.0f, bar.getY() - 4.0f, 2.0f, bar.getHeight() + 8.0f));
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ((settings.dynamics.autoThreshold ? "auto " : "umbral ") + format::db (shownDynamics.thresholdDb),
                    juce::Rectangle<float> (bar.getX(), bar.getBottom() + 3.0f, bar.getWidth(), 11.0f), juce::Justification::centredLeft);
    }

    void setUpLabel (juce::Label& label)
    {
        label.setJustificationType (juce::Justification::centredLeft);
        label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (label);
    }

    void setUpKnob (Knob& knob, const juce::String& name, bool bipolar, const juce::String& tooltip)
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
        static constexpr std::array<const char*, dsp::numPlacements> placedText { "", "\nSolo el lado izquierdo", "\nSolo el lado derecho",
                                                                                  "\nSolo el centro (mid)", "\nSolo el ancho (side)" };
        text << placedText[static_cast<size_t> (s.placement)];
        if (s.dynamics.enabled && dsp::canBeDynamic (s.shape))
            text << juce::String::fromUTF8 ("\nDin\xc3\xa1mica: ") << format::db (s.dynamics.rangeDb) << (s.dynamics.external ? " con el sidechain" : "");
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
    juce::TextButton soloButton { "SOLO" };
    std::unique_ptr<ButtonAttachment> enabledAttachment;
    juce::ComboBox shapeBox, slopeBox;
    juce::Label shapeLabel, slopeLabel;
    juce::ComboBox placementBox;
    juce::Label placementLabel;
    std::unique_ptr<ComboBoxAttachment> shapeAttachment, slopeAttachment, placementAttachment;
    Knob frequency, gain, q;
    juce::Label info;

    // Fase 9.
    static constexpr float meterBottomDb = -60.0f, meterTopDb = 6.0f;
    juce::TextButton dynamicButton { juce::String::fromUTF8 ("DIN\xc3\x81MICO") };
    juce::TextButton autoButton { "AUTO" };
    juce::ComboBox sourceBox;
    juce::Label sourceLabel, autoLabel;
    std::unique_ptr<ButtonAttachment> dynamicAttachment, autoAttachment;
    std::unique_ptr<ComboBoxAttachment> sourceAttachment;
    Knob range, threshold, attack, release;
    juce::Rectangle<int> dynamicsRow;
    juce::Rectangle<float> meterArea;
    float dividerY = 0.0f;
    FathomAudioProcessor::DynamicState shownDynamics;
};

} // namespace fathom::gui
