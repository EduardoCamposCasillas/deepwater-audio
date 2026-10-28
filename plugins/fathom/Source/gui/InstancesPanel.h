#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "analysis/Masking.h"
#include "gui/EqDisplay.h"
#include "gui/Format.h"
#include "gui/Theme.h"

namespace fathom::gui
{

// Fase 14: el panel de INSTANCIAS. Ocupa el lugar del panel de la banda mientras la herramienta está activa (como
// IGUALAR y DIBUJAR). Lo que se elige aquí sigue valiendo al salir: las otras instancias se siguen viendo y editando
// mientras el proyecto esté abierto (vive en el procesador, LinkSettings).
//
// Izquierda: qué es, el nombre de esta instancia (como la ven las demás), CHOQUES y su SENSIBILIDAD, y SALIR.
// Derecha: la lista de las demás instancias de Fathom del proceso: su color, su nombre, si mandan audio, VER (su
// espectro y sus choques con esta), EDITAR (sus nodos en el visor; una a la vez) y dónde chocan.
class InstancesPanel final : public juce::Component
{
public:
    InstancesPanel (FathomAudioProcessor& p, EqDisplay& d) : processor (p), display (d)
    {
        title.setText ("OTRAS INSTANCIAS", juce::dontSendNotification);
        title.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::accent);
        addAndMakeVisible (title);
        info.setText (juce::String::fromUTF8 ("Las dem\xc3\xa1s pistas con Fathom: su salida (una l\xc3\xadnea de su color) "
                                              "sobre la tuya. En coral, d\xc3\xb3nde chocan: las dos tienen su energ\xc3\xad" "a "
                                              "importante en la misma zona del o\xc3\xad" "do y una tapa a la otra. Pasa el mouse "
                                              "por un choque para ver qui\xc3\xa9n tapa a qui\xc3\xa9n."),
                      juce::dontSendNotification);
        info.setJustificationType (juce::Justification::topLeft);
        info.setFont (juce::FontOptions (11.5f));
        info.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (info);

        for (auto* label : { &nameLabel, &collisionsLabel, &listName, &listStatus, &listShow, &listEdit, &listClashes })
        {
            label->setFont (juce::FontOptions (11.0f, juce::Font::bold));
            label->setColour (juce::Label::textColourId, theme::dimText);
            addAndMakeVisible (*label);
        }
        nameLabel.setText ("ESTA INSTANCIA", juce::dontSendNotification);
        collisionsLabel.setText ("CHOQUES", juce::dontSendNotification);
        listName.setText ("INSTANCIA", juce::dontSendNotification);
        listStatus.setText ("AUDIO", juce::dontSendNotification);
        listShow.setText ("VER", juce::dontSendNotification);
        listEdit.setText ("EDITAR", juce::dontSendNotification);
        listClashes.setText ("CHOCA CON ESTA EN", juce::dontSendNotification);

        nameEditor.setFont (juce::FontOptions (12.0f));
        nameEditor.setColour (juce::TextEditor::backgroundColourId, theme::control);
        nameEditor.setColour (juce::TextEditor::outlineColourId, theme::panelOutline);
        nameEditor.setColour (juce::TextEditor::textColourId, theme::text);
        nameEditor.setTooltip (juce::String::fromUTF8 ("C\xc3\xb3mo ven esta instancia las dem\xc3\xa1s (\"Voz\", \"808\", "
                                                       "\"Bus bater\xc3\xad" "a\"). Vac\xc3\xad" "o = el nombre de la pista del "
                                                       "mixer si FL lo manda, o \"Fathom N\". Se guarda con el proyecto."));
        nameEditor.onTextChange = [this] { processor.setCustomName (nameEditor.getText()); };
        nameEditor.onReturnKey = [this] { unfocusAllComponents(); };
        addAndMakeVisible (nameEditor);

        collisionsButton.setClickingTogglesState (true);
        collisionsButton.setColour (juce::TextButton::buttonOnColourId, theme::collision.withAlpha (0.45f));
        collisionsButton.setTooltip (juce::String::fromUTF8 (
            "Marcar d\xc3\xb3nde choca esta pista con las que ves. Se mide con lo que suena durante unos segundos (no con "
            "un golpe), en bandas cr\xc3\xadticas: el ancho en que el o\xc3\xad" "do junta las frecuencias (casi media octava "
            "en graves, 1/6 en agudos). Lo que cambia esta instancia se ve al momento: corta en un choque y mira c\xc3\xb3mo "
            "se apaga."));
        collisionsButton.onClick = [this] {
            processor.getLinkSettings().collisions = collisionsButton.getToggleState();
            display.instanceSettingsChanged();
        };
        addAndMakeVisible (collisionsButton);
        for (size_t i = 0; i < analysis::maskingSensitivities.size(); ++i)
            sensitivityBox.addItem (juce::String::fromUTF8 (analysis::maskingSensitivities[i].name), static_cast<int> (i) + 1);
        sensitivityBox.setTooltip (juce::String::fromUTF8 (
            "Desde cu\xc3\xa1ndo un choque se marca. Baja = solo donde las dos pistas tienen su zona principal (los choques "
            "graves de verdad: kick y 808 en el sub); alta = tambi\xc3\xa9n donde una de las dos tiene algo secundario."));
        sensitivityBox.onChange = [this] {
            processor.getLinkSettings().sensitivityIndex = sensitivityBox.getSelectedId() - 1;
            display.instanceSettingsChanged();
        };
        addAndMakeVisible (sensitivityBox);

        exitButton.setTooltip (juce::String::fromUTF8 ("Volver al panel de la banda (Esc). Lo que ves y editas de las otras "
                                                       "instancias sigue en el visor."));
        exitButton.onClick = [this] { display.setTool (EqDisplay::Tool::none); };
        addAndMakeVisible (exitButton);

        empty.setJustificationType (juce::Justification::topLeft);
        empty.setFont (juce::FontOptions (11.5f));
        empty.setColour (juce::Label::textColourId, theme::dimText);
        empty.setText (juce::String::fromUTF8 (
                           "No hay otras instancias de Fathom en este proyecto. Ponle Fathom a otra pista del mixer y aparece "
                           "aqu\xc3\xad. Si ya hay y no salen, una de las dos corre en otro proceso (en FL: el plugin "
                           "\"bridged\" o \"Make bridged\" en las opciones del wrapper): ah\xc3\xad no se pueden ver."),
                       juce::dontSendNotification);
        addChildComponent (empty);

        viewport.setViewedComponent (&rows, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
    }

    // El editor lo muestra al elegir la herramienta.
    void showPanel()
    {
        const auto& settings = processor.getLinkSettings();
        nameEditor.setText (processor.getCustomName(), juce::dontSendNotification);
        collisionsButton.setToggleState (settings.collisions, juce::dontSendNotification);
        sensitivityBox.setSelectedId (settings.sensitivityIndex + 1, juce::dontSendNotification);
        refresh();
    }

    // En cada cuadro (lo llama el editor): la lista, quién suena y dónde choca.
    void refresh()
    {
        if (! isVisible())
            return;
        auto placeholder = processor.getTrackName().isNotEmpty() ? processor.getTrackName() : "Fathom " + juce::String (processor.getInstanceId());
        if (placeholder != shownPlaceholder)
        {
            shownPlaceholder = placeholder;
            nameEditor.setTextToShowWhenEmpty (placeholder, theme::dimText.withAlpha (0.7f));
        }
        if (! nameEditor.hasKeyboardFocus (true) && nameEditor.getText() != processor.getCustomName()) // un proyecto cargado
            nameEditor.setText (processor.getCustomName(), juce::dontSendNotification);

        auto& link = display.getInstanceLink();
        const auto available = link.available();
        std::vector<int> ids;
        for (const auto& a : available)
            ids.push_back (a.id);
        if (ids != rowIds)
            rebuildRows (ids);
        empty.setVisible (available.empty());
        viewport.setVisible (! available.empty());

        const auto& settings = processor.getLinkSettings();
        for (size_t i = 0; i < available.size() && i < rowList.size(); ++i)
        {
            auto& row = *rowList[i];
            const auto& a = available[i];
            row.colour = a.colour;
            if (row.name.getText() != a.name)
                row.name.setText (a.name, juce::dontSendNotification);
            row.status.setText (a.sounding ? "suena" : "sin audio", juce::dontSendNotification);
            row.status.setColour (juce::Label::textColourId, a.sounding ? theme::text : theme::dimText);
            row.show.setToggleState (settings.isShown (a.id), juce::dontSendNotification);
            row.edit.setToggleState (settings.editable == a.id, juce::dontSendNotification);
            const auto text = clashesText (link, a.id, settings);
            if (row.clashes.getText() != text)
                row.clashes.setText (text, juce::dontSendNotification);
            row.repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (theme::accent);
        g.fillRoundedRectangle (bounds.removeFromLeft (4.0f).reduced (0.0f, 10.0f), 2.0f);
        g.setColour (theme::panelOutline);
        g.fillRect (juce::Rectangle<float> (dividerX, 12.0f, 1.0f, static_cast<float> (getHeight()) - 24.0f));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14, 8);
        auto left = area.removeFromLeft (300);
        title.setBounds (left.removeFromTop (18));
        info.setBounds (left.removeFromTop (62));
        left.removeFromTop (4);
        auto nameRow = left.removeFromTop (18);
        nameLabel.setBounds (nameRow);
        nameEditor.setBounds (left.removeFromTop (24));
        left.removeFromTop (8);
        collisionsLabel.setBounds (left.removeFromTop (16));
        auto controls = left.removeFromTop (26);
        collisionsButton.setBounds (controls.removeFromLeft (70));
        controls.removeFromLeft (6);
        sensitivityBox.setBounds (controls.removeFromLeft (110));
        exitButton.setBounds (controls.removeFromRight (80));

        area.removeFromLeft (12);
        dividerX = static_cast<float> (area.getX());
        area.removeFromLeft (12);
        auto header = area.removeFromTop (18);
        listName.setBounds (header.removeFromLeft (Row::nameWidth + Row::swatchWidth));
        listStatus.setBounds (header.removeFromLeft (Row::statusWidth));
        listShow.setBounds (header.removeFromLeft (Row::buttonWidth + 6));
        listEdit.setBounds (header.removeFromLeft (Row::editWidth + 10));
        listClashes.setBounds (header);
        empty.setBounds (area);
        viewport.setBounds (area);
        layoutRows();
    }

private:
    // Una fila de la lista: color, nombre, audio, VER, EDITAR y dónde choca.
    struct Row final : public juce::Component
    {
        static constexpr int swatchWidth = 16, nameWidth = 150, statusWidth = 64, buttonWidth = 44, editWidth = 60;
        static constexpr int height = 26;

        juce::Colour colour;
        juce::Label name, status, clashes;
        juce::TextButton show { "VER" }, edit { "EDITAR" };

        Row()
        {
            for (auto* label : { &name, &status, &clashes })
            {
                label->setFont (juce::FontOptions (11.5f));
                label->setColour (juce::Label::textColourId, theme::text);
                addAndMakeVisible (*label);
            }
            name.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            for (auto* button : { &show, &edit })
            {
                button->setClickingTogglesState (true);
                button->setColour (juce::TextButton::buttonOnColourId, theme::accent.withAlpha (0.45f));
                addAndMakeVisible (*button);
            }
            show.setTooltip (juce::String::fromUTF8 ("Ver su salida sobre la tuya y d\xc3\xb3nde chocan."));
            edit.setTooltip (juce::String::fromUTF8 ("Mostrar sus nodos en este visor y moverlos desde aqu\xc3\xad (arrastrar, "
                                                     "rueda = Q, doble clic = apagar). Una instancia a la vez. Lo que cambies "
                                                     "se deshace desde su propia ventana."));
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (colour);
            g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, 8.0f, 10.0f, 10.0f), 2.0f);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (0, 2);
            area.removeFromLeft (swatchWidth);
            name.setBounds (area.removeFromLeft (nameWidth));
            status.setBounds (area.removeFromLeft (statusWidth));
            show.setBounds (area.removeFromLeft (buttonWidth));
            area.removeFromLeft (6);
            edit.setBounds (area.removeFromLeft (editWidth));
            area.removeFromLeft (10);
            clashes.setBounds (area);
        }
    };

    void rebuildRows (const std::vector<int>& ids)
    {
        rowIds = ids;
        rowList.clear();
        for (const int id : ids)
        {
            auto row = std::make_unique<Row>();
            row->show.onClick = [this, id, button = &row->show] {
                processor.getLinkSettings().setShown (id, button->getToggleState());
                display.instanceSettingsChanged();
            };
            row->edit.onClick = [this, id, button = &row->edit] {
                processor.getLinkSettings().setEditable (button->getToggleState() ? id : -1);
                display.instanceSettingsChanged();
            };
            rows.addAndMakeVisible (*row);
            rowList.push_back (std::move (row));
        }
        layoutRows();
    }

    void layoutRows()
    {
        const int width = viewport.getMaximumVisibleWidth();
        rows.setSize (width, Row::height * static_cast<int> (rowList.size()));
        for (size_t i = 0; i < rowList.size(); ++i)
            rowList[i]->setBounds (0, Row::height * static_cast<int> (i), width, Row::height);
    }

    // "60-120 Hz (ella +5 dB) · 2.5-4 kHz" o lo que falta para saberlo.
    static juce::String clashesText (InstanceLink& link, int id, const link::LinkSettings& settings)
    {
        if (! settings.isShown (id))
            return {};
        if (! settings.collisions)
            return juce::String::fromUTF8 ("(CHOQUES apagado)");
        for (const auto& other : link.getShown())
            if (other.id == id)
            {
                if (other.collision.empty())
                    return juce::String::fromUTF8 ("(esperando audio de las dos)");
                if (other.zones.empty())
                    return juce::String::fromUTF8 ("ning\xc3\xban choque");
                juce::String text;
                for (const auto& zone : other.zones)
                {
                    if (text.isNotEmpty())
                        text << juce::String::fromUTF8 (" \xc2\xb7 ");
                    text << rangeText (zone.lowHz, zone.highHz);
                    if (std::abs (zone.louderDb) >= 1.5f)
                        text << (zone.louderDb > 0.0f ? juce::String::fromUTF8 (" (t\xc3\xba ") + format::db (zone.louderDb)
                                                      : " (ella " + format::db (-zone.louderDb))
                             << ")";
                }
                return text;
            }
        return {};
    }

    // "60-120 Hz", "2.5-4.0 kHz".
    static juce::String rangeText (double lowHz, double highHz)
    {
        if (highHz < 1000.0)
            return juce::String (juce::roundToInt (lowHz)) + "-" + juce::String (juce::roundToInt (highHz)) + " Hz";
        const auto khz = [] (double hz) { return juce::String (hz / 1000.0, hz < 9950.0 ? 1 : 0); };
        return (lowHz < 1000.0 ? juce::String (juce::roundToInt (lowHz)) + " Hz-" : khz (lowHz) + "-") + khz (highHz) + " kHz";
    }

    FathomAudioProcessor& processor;
    EqDisplay& display;
    juce::Label title, info, nameLabel, collisionsLabel, empty;
    juce::Label listName, listStatus, listShow, listEdit, listClashes;
    juce::TextEditor nameEditor;
    juce::TextButton collisionsButton { "MARCAR" }, exitButton { "SALIR" };
    juce::ComboBox sensitivityBox;
    juce::Component rows; // antes que el viewport: se destruye después de él
    std::vector<std::unique_ptr<Row>> rowList;
    juce::Viewport viewport;
    std::vector<int> rowIds;
    juce::String shownPlaceholder;
    float dividerX = 0.0f;
};

} // namespace fathom::gui
