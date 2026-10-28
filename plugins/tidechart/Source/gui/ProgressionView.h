#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "deepwater/theory/Progression.h"
#include "deepwater/theory/Text.h"
#include "gui/ProgressionTemplates.h"
#include "gui/Theme.h"
#include "gui/TheoryNames.h"
#include "midi/ChordTrigger.h"
#include "Parameters.h"
#include "TransportSnapshot.h"

namespace tidechart::gui
{

// El constructor de progresiones (Fase 4). Arriba, cómo suena: qué hacen las teclas (cada una su acorde, o cualquiera la
// progresión), el ritmo de los acordes y el voicing. Debajo, la progresión: plantillas, el texto en números romanos
// (se puede escribir: "i bVI bIII bVII"), "Agregar con clic" (los clics en la paleta la van armando) y vaciar.
// Cada paso muestra su grado, su acorde, su función y cuántos tiempos dura; si no es de la escala, de qué modo se tomó
// prestado. Mantener pulsado un paso lo hace sonar; clic derecho = duración, tipo de acorde, mover o quitar; la rueda
// cambia la duración. El paso que corresponde a la posición de FL se enciende, y la tira de abajo dibuja el patrón
// rítmico con la semicorchea actual.
class ProgressionView final : public juce::Component, public juce::TooltipClient
{
public:
    struct Callbacks
    {
        std::function<void (const deepwater::theory::Progression&)> setProgression;
        std::function<void (const deepwater::theory::RomanNumeral&)> preview;
        std::function<void()> stopPreview;
        std::function<void (deepwater::theory::Mode)> setMode; // al elegir una plantilla
    };

    ProgressionView (juce::AudioProcessorValueTreeState& state, Callbacks callbacksToUse) : callbacks (std::move (callbacksToUse))
    {
        namespace id = tidechart::params;
        for (int i = 0; i < 2; ++i)
            playModeBox.addItem (names::playModeName (i), i + 1);
        for (int i = 0; i < deepwater::theory::rhythmPatternCount; ++i)
            rhythmBox.addItem (names::rhythmName (i), i + 1);
        for (int i = 0; i < deepwater::theory::voicingStyleCount; ++i)
            voicingBox.addItem (names::voicingName (i), i + 1);
        for (int i = 0; i < static_cast<int> (progressionTemplates.size()); ++i)
            templateBox.addItem (names::utf8 (progressionTemplates[static_cast<std::size_t> (i)].name), i + 1);
        templateBox.setTextWhenNothingSelected (names::utf8 ("Plantillas\xe2\x80\xa6"));
        templateBox.onChange = [this] { applyTemplate (templateBox.getSelectedId() - 1); };

        playModeBox.setTooltip (names::utf8 ("Teclas: cada tecla dispara su acorde (como en la Fase 3), con el ritmo y el voicing de aqu\xc3\xad. "
                                             "Progresi\xc3\xb3n: cualquier tecla abajo hace sonar la progresi\xc3\xb3n siguiendo a FL: en cada "
                                             "comp\xc3\xa1s suena el paso que cae ah\xc3\xad. La octava de la tecla elige el registro (C5 = el de la paleta)."));
        rhythmBox.setTooltip (names::utf8 ("El ritmo de los acordes, alineado con la cuadr\xc3\xad" "cula de FL (16 semicorcheas por comp\xc3\xa1s). "
                                           "Con FL parado los acordes suenan sostenidos."));
        voicingBox.setTooltip (names::utf8 ("C\xc3\xb3mo se reparten las notas. Estado fundamental: la fundamental abajo, cada acorde salta. "
                                            "Conducci\xc3\xb3n de voces: la inversi\xc3\xb3n que menos mueve las voces (las notas comunes quietas). "
                                            "Abierto: lo mismo, con la 2.\xc2\xaa voz una octava abajo (m\xc3\xa1s ancho, de pad)."));
        textEditor.setTooltip (names::utf8 ("La progresi\xc3\xb3n en n\xc3\xbameros romanos. Escribe y pulsa Enter: i bVI bIII bVII, i bII, "
                                            "Imaj7 IV7 iv9... (min\xc3\xbascula = menor, b = bemol, o = disminuido, + = aumentado)."));
        addButton.setTooltip (names::utf8 ("Encendido: cada clic en la paleta (tambi\xc3\xa9n en los prestados) agrega ese acorde al final de la progresi\xc3\xb3n."));
        clearButton.setTooltip (names::utf8 ("Vac\xc3\xad" "a la progresi\xc3\xb3n."));

        for (auto* box : { &playModeBox, &rhythmBox, &voicingBox, &templateBox })
        {
            box->setColour (juce::ComboBox::backgroundColourId, theme::control);
            box->setColour (juce::ComboBox::outlineColourId, theme::panelOutline);
            box->setColour (juce::ComboBox::textColourId, theme::text);
            addAndMakeVisible (*box);
        }
        textEditor.setColour (juce::TextEditor::backgroundColourId, theme::display);
        textEditor.setColour (juce::TextEditor::outlineColourId, theme::panelOutline);
        textEditor.setColour (juce::TextEditor::textColourId, theme::text);
        textEditor.setFont (juce::FontOptions (14.0f));
        textEditor.setSelectAllWhenFocused (true);
        textEditor.onReturnKey = [this] { applyText(); };
        textEditor.onFocusLost = [this] { applyText(); };
        textEditor.onEscapeKey = [this] { refreshText(); };
        addAndMakeVisible (textEditor);

        addButton.setButtonText ("Agregar con clic");
        addButton.setClickingTogglesState (true);
        clearButton.setButtonText ("Vaciar");
        clearButton.onClick = [this] {
            commit ({});
            info = names::utf8 ("Progresi\xc3\xb3n vac\xc3\xad" "a: enciende \xc2\xab" "Agregar con clic\xc2\xbb y elige acordes en la paleta.");
            repaint();
        };
        for (auto* button : { &addButton, &clearButton })
        {
            button->setColour (juce::TextButton::buttonColourId, theme::control);
            button->setColour (juce::TextButton::buttonOnColourId, theme::accent.withAlpha (0.55f));
            button->setColour (juce::TextButton::textColourOffId, theme::text);
            addAndMakeVisible (*button);
        }

        using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
        playModeAttachment = std::make_unique<ComboAttachment> (state, id::playMode, playModeBox);
        rhythmAttachment = std::make_unique<ComboAttachment> (state, id::rhythm, rhythmBox);
        voicingAttachment = std::make_unique<ComboAttachment> (state, id::voicing, voicingBox);
    }

    ~ProgressionView() override
    {
        if (pressedStep >= 0)
            callbacks.stopPreview();
    }

    // ¿Los clics en la paleta agregan acordes?
    [[nodiscard]] bool addsOnClick() const { return addButton.getToggleState(); }

    // Agrega un acorde al final (desde la paleta).
    void append (const deepwater::theory::RomanNumeral& numeral)
    {
        auto updated = progression;
        if (static_cast<int> (updated.steps.size()) >= deepwater::theory::maxProgressionSteps)
        {
            info = names::utf8 ("La progresi\xc3\xb3n tiene 8 pasos como m\xc3\xa1ximo: quita alguno (clic derecho) para agregar m\xc3\xa1s.");
            repaint();
            return;
        }
        updated.steps.push_back ({ numeral, 4 });
        commit (updated);
    }

    // Desde el editor: la progresión del procesador (al abrir, al cargar un proyecto, tras cada cambio).
    void setProgression (const deepwater::theory::Progression& newProgression)
    {
        progression = newProgression;
        rebuild();
    }

    // Desde el temporizador del editor: la tonalidad y dónde está FL.
    void update (const midi::ChordSettings& newSettings, const TransportSnapshot& transport)
    {
        const bool keyChanged = ! (newSettings.key == settings.key);
        const bool changed = keyChanged || newSettings.rhythm != settings.rhythm || newSettings.enabled != settings.enabled
                             || newSettings.playMode != settings.playMode || transport.progressionStep != step
                             || transport.progressionSounding != sounding || transport.rhythmStep != rhythmStep;
        settings = newSettings;
        step = transport.progressionStep;
        sounding = transport.progressionSounding;
        rhythmStep = transport.rhythmStep;
        if (keyChanged)
            rebuild();
        else if (changed)
            repaint();
    }

    juce::String getTooltip() override
    {
        if (tileAt (getMouseXYRelative()) >= 0)
            return names::utf8 ("Mant\xc3\xa9n pulsado para escucharlo. Clic derecho: duraci\xc3\xb3n, tipo de acorde, mover o quitar. "
                                "Rueda: m\xc3\xa1s o menos tiempos.");
        if (rhythmArea.contains (getMouseXYRelative()))
            return names::rhythmDescription (settings.rhythm);
        return {};
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto row = area.removeFromTop (26);
        row.removeFromLeft (100); // el título
        playModeBox.setBounds (row.removeFromLeft (220));
        row.removeFromLeft (8);
        rhythmBox.setBounds (row.removeFromLeft (220));
        row.removeFromLeft (8);
        voicingBox.setBounds (row.removeFromLeft (200));

        area.removeFromTop (6);
        row = area.removeFromTop (26);
        templateBox.setBounds (row.removeFromLeft (260));
        row.removeFromLeft (8);
        clearButton.setBounds (row.removeFromRight (70));
        row.removeFromRight (8);
        addButton.setBounds (row.removeFromRight (130));
        row.removeFromRight (8);
        textEditor.setBounds (row);

        area.removeFromTop (8);
        infoArea = area.removeFromBottom (16);
        area.removeFromBottom (4);
        rhythmArea = area.removeFromBottom (20);
        area.removeFromBottom (8);
        stepsArea = area;
        constexpr int gap = 6;
        const int width = (stepsArea.getWidth() - (deepwater::theory::maxProgressionSteps - 1) * gap) / deepwater::theory::maxProgressionSteps;
        for (int i = 0; i < deepwater::theory::maxProgressionSteps; ++i)
            tileBounds[static_cast<std::size_t> (i)] = { stepsArea.getX() + i * (width + gap), stepsArea.getY(), width, stepsArea.getHeight() };
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (names::utf8 ("PROGRESI\xc3\x93N"), getLocalBounds().removeFromTop (26).removeFromLeft (100), juce::Justification::centredLeft);

        const bool active = settings.enabled;
        const bool progressionMode = settings.playMode == midi::PlayMode::progression;
        for (int i = 0; i < deepwater::theory::maxProgressionSteps; ++i)
        {
            const auto bounds = tileBounds[static_cast<std::size_t> (i)].toFloat();
            if (i >= static_cast<int> (tiles.size()))
            {
                g.setColour (theme::panelOutline.withAlpha (0.6f));
                g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);
                if (i == static_cast<int> (tiles.size()))
                {
                    g.setColour (theme::dimText.withAlpha (0.7f));
                    g.setFont (juce::FontOptions (10.5f));
                    g.drawFittedText (names::utf8 ("+ un acorde\n(clic en la paleta)"), bounds.reduced (4.0f).toNearestInt(), juce::Justification::centred, 2);
                }
                continue;
            }
            const auto& tile = tiles[static_cast<std::size_t> (i)];
            const auto colour = names::functionColour (tile.function);
            const bool here = i == step;
            const bool lit = here && sounding;
            g.setColour (lit ? colour.withAlpha (0.28f) : (pressedStep == i ? theme::control : theme::display));
            g.fillRoundedRectangle (bounds, 5.0f);
            g.setColour (here ? (progressionMode ? colour : theme::dimText) : theme::panelOutline);
            g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, here ? 1.8f : 1.0f);
            g.setColour (colour.withAlpha (active ? 0.9f : 0.35f));
            g.fillRoundedRectangle (bounds.withHeight (4.0f).reduced (6.0f, 0.0f).translated (0.0f, 4.0f), 2.0f);

            auto inner = bounds.reduced (6.0f, 9.0f);
            g.setColour (active ? theme::text : theme::dimText);
            g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
            g.drawText (tile.roman, inner.removeFromTop (20.0f), juce::Justification::centred);
            g.setColour (active ? colour : theme::dimText);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (tile.symbol, inner.removeFromTop (16.0f), juce::Justification::centred);
            g.setColour (tile.borrowed ? theme::freeRunning : theme::dimText);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (tile.detail, inner.removeFromTop (13.0f), juce::Justification::centred);
        }

        drawRhythm (g);

        g.setFont (juce::FontOptions (11.5f));
        g.setColour (error ? theme::warning : theme::dimText);
        g.drawText (info, infoArea, juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int index = tileAt (e.getPosition());
        if (index < 0)
            return;
        if (e.mods.isPopupMenu())
        {
            showMenu (index);
            return;
        }
        pressedStep = index;
        callbacks.preview (progression.steps[static_cast<std::size_t> (index)].numeral);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (pressedStep < 0)
            return;
        pressedStep = -1;
        callbacks.stopPreview();
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const int index = tileAt (e.getPosition());
        if (index < 0 || wheel.deltaY == 0.0f)
            return;
        auto updated = progression;
        auto& beats = updated.steps[static_cast<std::size_t> (index)].beats;
        beats = deepwater::theory::Progression::clampBeats (beats + (wheel.deltaY > 0.0f ? 1 : -1));
        commit (updated);
    }

private:
    struct Tile
    {
        juce::String roman, symbol, detail;
        deepwater::theory::HarmonicFunction function = deepwater::theory::HarmonicFunction::tonic;
        bool borrowed = false;
    };

    [[nodiscard]] int tileAt (juce::Point<int> point) const
    {
        for (int i = 0; i < static_cast<int> (tiles.size()); ++i)
            if (tileBounds[static_cast<std::size_t> (i)].contains (point))
                return i;
        return -1;
    }

    void commit (const deepwater::theory::Progression& updated)
    {
        progression = updated;
        error = false;
        callbacks.setProgression (progression);
        rebuild();
    }

    // Los textos de cada paso y el texto romano, con la tonalidad actual.
    void rebuild()
    {
        namespace th = deepwater::theory;
        const th::Notation notation { true, 5 };
        const auto u = [] (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); };
        tiles.clear();
        for (const auto& s : progression.steps)
        {
            Tile tile;
            const auto chord = th::chordFor (s.numeral, settings.key);
            tile.roman = u (th::romanNumeralText (s.numeral, notation));
            tile.symbol = u (th::chordSymbol (chord, notation));
            tile.function = th::harmonicFunction (s.numeral);
            tile.detail = juce::String (s.beats) + (s.beats == 1 ? " tiempo" : " tiempos");
            if (! th::isDiatonic (chord, settings.key))
            {
                tile.borrowed = true;
                const auto modes = th::modesContaining (chord, settings.key.tonic);
                tile.detail << names::utf8 (" \xc2\xb7 ") << (modes.empty() ? names::utf8 ("crom\xc3\xa1tico") : names::borrowedFrom (modes[0]));
            }
            tiles.push_back (tile);
        }
        refreshText();
        repaint();
    }

    void refreshText()
    {
        if (textEditor.hasKeyboardFocus (true))
            return;
        std::vector<deepwater::theory::RomanNumeral> numerals;
        for (const auto& s : progression.steps)
            numerals.push_back (s.numeral);
        textEditor.setText (juce::String::fromUTF8 (deepwater::theory::progressionText (numerals, { true, 5 }).c_str()), false);
    }

    // El texto escrito → la progresión. Cada paso conserva su duración si ya existía; los nuevos, un compás.
    void applyText()
    {
        const auto text = textEditor.getText().trim();
        const auto parsed = deepwater::theory::parseProgression (text.toStdString());
        if (! parsed && text.isNotEmpty())
        {
            error = true;
            info = names::utf8 ("No entiendo \xc2\xab") + text + names::utf8 ("\xc2\xbb: escribe grados separados por espacios, como i bVI bIII bVII.");
            repaint();
            return;
        }
        deepwater::theory::Progression updated;
        if (parsed)
            for (std::size_t i = 0; i < parsed->size() && i < static_cast<std::size_t> (deepwater::theory::maxProgressionSteps); ++i)
                updated.steps.push_back ({ (*parsed)[i], i < progression.steps.size() ? progression.steps[i].beats : 4 });
        const bool truncated = parsed && parsed->size() > static_cast<std::size_t> (deepwater::theory::maxProgressionSteps);
        if (updated == progression)
        {
            refreshText();
            return;
        }
        commit (updated);
        info = truncated ? names::utf8 ("Solo caben 8 pasos: se qued\xc3\xb3 con los primeros.") : juce::String();
        refreshText();
    }

    void applyTemplate (int index)
    {
        if (index < 0 || index >= static_cast<int> (progressionTemplates.size()))
            return;
        const auto& t = progressionTemplates[static_cast<std::size_t> (index)];
        deepwater::theory::Progression updated;
        if (const auto parsed = deepwater::theory::parseProgression (t.numerals))
            for (const auto& numeral : *parsed)
                updated.steps.push_back ({ numeral, 4 });
        callbacks.setMode (t.mode);
        commit (updated);
        info = names::utf8 (t.description);
        templateBox.setSelectedId (0, juce::dontSendNotification);
        repaint();
    }

    void showMenu (int index)
    {
        namespace th = deepwater::theory;
        const auto current = progression.steps[static_cast<std::size_t> (index)];
        juce::PopupMenu beatsMenu, typeMenu, menu;
        constexpr std::array<int, 8> beatChoices { 1, 2, 3, 4, 6, 8, 12, 16 };
        for (const int beats : beatChoices)
            beatsMenu.addItem (juce::String (beats) + (beats == 1 ? " tiempo" : " tiempos") + (beats % 4 == 0 ? " (" + juce::String (beats / 4)
                               + (beats == 4 ? names::utf8 (" comp\xc3\xa1s)") : names::utf8 (" compases)")) : juce::String()),
                               true, current.beats == beats, [this, index, beats] { setBeats (index, beats); });

        // Las variantes de uso más común, sobre la misma fundamental.
        constexpr std::array<const char*, 22> types { "", "m", "dim", "aug", "sus2", "sus4", "5", "7", "maj7", "m7", "m7b5", "dim7",
                                                      "6", "m6", "add9", "madd9", "9", "maj9", "m9", "m11", "7sus4", "7b9" };
        const th::Notation notation { true, 5 };
        for (const auto* symbol : types)
        {
            auto numeral = current.numeral;
            numeral.type = th::chordType (symbol);
            const auto chord = th::chordFor (numeral, settings.key);
            typeMenu.addItem (juce::String::fromUTF8 ((th::romanNumeralText (numeral, notation) + "   " + th::chordSymbol (chord, notation)).c_str()),
                              true, numeral.type == current.numeral.type, [this, index, numeral] { setNumeral (index, numeral); });
        }

        menu.addSectionHeader (tiles[static_cast<std::size_t> (index)].roman + "  " + tiles[static_cast<std::size_t> (index)].symbol);
        menu.addSubMenu (names::utf8 ("Duraci\xc3\xb3n"), beatsMenu);
        menu.addSubMenu ("Tipo de acorde", typeMenu);
        menu.addSeparator();
        menu.addItem (names::utf8 ("Mover a la izquierda"), index > 0, false, [this, index] { swapSteps (index, index - 1); });
        menu.addItem (names::utf8 ("Mover a la derecha"), index + 1 < static_cast<int> (progression.steps.size()), false,
                      [this, index] { swapSteps (index, index + 1); });
        menu.addItem ("Duplicar", static_cast<int> (progression.steps.size()) < th::maxProgressionSteps, false, [this, index] {
            auto updated = progression;
            th::Progression result;
            for (int i = 0; i < static_cast<int> (updated.steps.size()); ++i)
            {
                result.steps.push_back (updated.steps[static_cast<std::size_t> (i)]);
                if (i == index)
                    result.steps.push_back (updated.steps[static_cast<std::size_t> (i)]);
            }
            commit (result);
        });
        menu.addItem ("Quitar", [this, index] {
            th::Progression result;
            for (int i = 0; i < static_cast<int> (progression.steps.size()); ++i)
                if (i != index)
                    result.steps.push_back (progression.steps[static_cast<std::size_t> (i)]);
            commit (result);
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
            localAreaToGlobal (tileBounds[static_cast<std::size_t> (index)])));
    }

    void setBeats (int index, int beats)
    {
        if (index >= static_cast<int> (progression.steps.size()))
            return;
        auto updated = progression;
        updated.steps[static_cast<std::size_t> (index)].beats = beats;
        commit (updated);
    }

    void setNumeral (int index, const deepwater::theory::RomanNumeral& numeral)
    {
        if (index >= static_cast<int> (progression.steps.size()))
            return;
        auto updated = progression;
        updated.steps[static_cast<std::size_t> (index)].numeral = numeral;
        commit (updated);
    }

    void swapSteps (int a, int b)
    {
        const int count = static_cast<int> (progression.steps.size());
        if (a < 0 || b < 0 || a >= count || b >= count)
            return;
        auto updated = progression;
        std::swap (updated.steps[static_cast<std::size_t> (a)], updated.steps[static_cast<std::size_t> (b)]);
        commit (updated);
    }

    // El patrón rítmico como 16 celdas: los golpes rellenos (más claros si están acentuados), la semicorchea actual
    // marcada, y una línea más fuerte en cada tiempo.
    void drawRhythm (juce::Graphics& g)
    {
        const auto& pattern = deepwater::theory::rhythmPattern (settings.rhythm);
        auto area = rhythmArea;
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("RITMO", area.removeFromLeft (52), juce::Justification::centredLeft);
        auto cells = area.removeFromLeft (16 * 22);
        area.removeFromLeft (10);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (names::rhythmDescription (settings.rhythm), area, juce::Justification::centredLeft);

        const float cellWidth = static_cast<float> (cells.getWidth()) / 16.0f;
        for (int s = 0; s < 16; ++s)
        {
            const auto cell = juce::Rectangle<float> (static_cast<float> (cells.getX()) + s * cellWidth, static_cast<float> (cells.getY()),
                                                      cellWidth, static_cast<float> (cells.getHeight()))
                                  .reduced (1.0f, 2.0f);
            const auto* hit = pattern.steps > 0 ? deepwater::theory::hitAt (pattern, s) : nullptr;
            const bool on = pattern.steps <= 0 || hit != nullptr;
            const bool start = hit != nullptr && hit->start == s;
            g.setColour (on ? theme::accent.withAlpha (start ? 0.35f + 0.5f * (static_cast<float> (hit->accent) - 70.0f) / 40.0f : 0.25f)
                            : theme::display);
            g.fillRoundedRectangle (cell, 2.0f);
            if (s == rhythmStep)
            {
                g.setColour (theme::text);
                g.drawRoundedRectangle (cell, 2.0f, 1.5f);
            }
            if (s % 4 == 0)
            {
                g.setColour (theme::dimText.withAlpha (0.6f));
                g.fillRect (cell.getX() - 1.5f, cell.getY(), 1.0f, cell.getHeight());
            }
        }
    }

    Callbacks callbacks;
    juce::ComboBox playModeBox, rhythmBox, voicingBox, templateBox;
    juce::TextEditor textEditor;
    juce::TextButton addButton, clearButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> playModeAttachment, rhythmAttachment, voicingAttachment;

    deepwater::theory::Progression progression;
    midi::ChordSettings settings;
    std::vector<Tile> tiles;
    std::array<juce::Rectangle<int>, deepwater::theory::maxProgressionSteps> tileBounds;
    juce::Rectangle<int> stepsArea, rhythmArea, infoArea;
    juce::String info;
    bool error = false;
    int step = -1, rhythmStep = -1, pressedStep = -1;
    bool sounding = false;
};

} // namespace tidechart::gui
