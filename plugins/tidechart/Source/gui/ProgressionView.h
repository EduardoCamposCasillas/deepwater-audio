#pragma once

#include <algorithm>
#include <array>
#include <cmath>
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
#include "ProgressionEdit.h"
#include "TransportSnapshot.h"

namespace tidechart::gui
{

// El constructor de progresiones (Fase 4). Arriba, cómo suena: qué hacen las teclas (cada una su acorde, o cualquiera la
// progresión), el ritmo de los acordes y el voicing. Debajo, la progresión: plantillas, el texto en números romanos
// (se puede escribir: "i bVI bIII bVII"), "Agregar con clic" (los clics en la paleta la van armando) y vaciar.
// Fase 8: la progresión es una línea de tiempo, como una pista de la Playlist de FL: cada paso mide lo que dura (al
// menos se ven 4 compases), con los compases numerados arriba y la posición de FL como una línea. Cada paso muestra su
// grado, su acorde y cuántos tiempos dura; si no es de la escala, de qué modo se tomó prestado. Mantener pulsado un
// paso lo hace sonar; arrastrarlo lo mueve; arrastrar su borde derecho lo alarga o acorta (de a un tiempo); la rueda
// también cambia la duración; clic derecho = cambiar el acorde, tipo, duración, duplicar o quitar. El "+" del final
// agrega un acorde de la escala o prestado. Lo arrastrado se aplica al soltar (un solo cambio para el audio y las
// líneas). La tira de abajo dibuja el patrón rítmico con la semicorchea actual.
// "Otra idea" pasa a la plantilla siguiente: la forma rápida de probar progresiones distintas sin saber teoría.
// Vista simple: sin el voicing ni el texto romano (siguen como estén; se cambian en la vista completa).
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
        ideaButton.setButtonText (names::utf8 ("Otra idea"));
        ideaButton.setTooltip (names::utf8 ("Pone la progresi\xc3\xb3n siguiente de las plantillas (con su modo): esc\xc3\xba" "chala con play en FL, "
                                            "y si te gusta, c\xc3\xa1mbiale el ritmo o encima ponle bajo y melod\xc3\xad" "a."));
        ideaButton.onClick = [this] { applyTemplate ((lastTemplate + 1) % static_cast<int> (progressionTemplates.size())); };
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
        for (auto* button : { &addButton, &clearButton, &ideaButton })
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
        if (previewing)
            callbacks.stopPreview();
    }

    // Vista simple o completa.
    void setSimple (bool shouldBeSimple)
    {
        simple = shouldBeSimple;
        voicingBox.setVisible (! simple);
        textEditor.setVisible (! simple);
        resized();
    }

    // ¿Los clics en la paleta agregan acordes?
    [[nodiscard]] bool addsOnClick() const { return addButton.getToggleState(); }

    // Agrega un acorde al final (desde la paleta).
    void append (const deepwater::theory::RomanNumeral& numeral)
    {
        auto updated = progression;
        if (static_cast<int> (updated.steps.size()) >= deepwater::theory::maxProgressionSteps)
        {
            info = names::utf8 ("La progresi\xc3\xb3n tiene ") + juce::String (deepwater::theory::maxProgressionSteps)
                   + names::utf8 (" pasos como m\xc3\xa1ximo: quita alguno (clic derecho) para agregar m\xc3\xa1s.");
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

        // La línea de FL: solo con un host (sin él, el reloj libre del Standalone no es la canción).
        const double beat = transport.source == deepwater::dsp::ClockSource::host ? loopBeat (progression, transport.ppq) : -1.0;
        const bool playheadMoved = (beat < 0.0) != (playheadBeat < 0.0) || std::abs (beat - playheadBeat) * timeline.pixelsPerBeat > 0.5;
        playheadBeat = beat;

        if (keyChanged)
            rebuild();
        else if (changed)
            repaint();
        else if (playheadMoved)
            repaint (lineArea.expanded (2));
    }

    juce::String getTooltip() override
    {
        const auto mouse = getMouseXYRelative();
        if (addArea.contains (mouse))
            return names::utf8 ("Agrega un acorde al final: de la escala o prestado de otro modo. Tambi\xc3\xa9n puedes encender "
                                "\xc2\xab" "Agregar con clic\xc2\xbb y elegirlos en la paleta.");
        if (const auto h = hitAt (mouse); h.step >= 0)
            return h.edge ? names::utf8 ("Arrastra el borde para alargar o acortar el acorde (de a un tiempo); los de despu\xc3\xa9s se corren.")
                          : names::utf8 ("Mant\xc3\xa9n pulsado para escucharlo; arr\xc3\xa1stralo para moverlo. Clic derecho: cambiar el acorde, "
                                         "tipo, duraci\xc3\xb3n, duplicar o quitar. Rueda: m\xc3\xa1s o menos tiempos.");
        if (rhythmArea.contains (mouse))
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
        templateBox.setBounds (row.removeFromLeft (simple ? 240 : 200));
        row.removeFromLeft (8);
        ideaButton.setBounds (row.removeFromLeft (100));
        row.removeFromLeft (8);
        clearButton.setBounds (row.removeFromRight (70));
        row.removeFromRight (8);
        addButton.setBounds (row.removeFromRight (130));
        row.removeFromRight (8);
        textEditor.setBounds (row);

        area.removeFromTop (8);
        infoArea = area.removeFromBottom (28); // dos líneas: el nombre de la idea y por qué funciona
        area.removeFromBottom (4);
        rhythmArea = area.removeFromBottom (20);
        area.removeFromBottom (8);
        // La línea: la regla de compases arriba, los pasos debajo y, a la derecha, el "+".
        addArea = area.removeFromRight (addWidth).withTrimmedTop (rulerHeight);
        area.removeFromRight (6);
        lineArea = area;
        rulerArea = area.removeFromTop (rulerHeight);
        stepsArea = area;
        layoutTimeline();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (names::utf8 ("PROGRESI\xc3\x93N"), getLocalBounds().removeFromTop (26).removeFromLeft (100), juce::Justification::centredLeft);

        drawTimeline (g);
        drawRhythm (g);

        g.setFont (juce::FontOptions (11.5f));
        g.setColour (error ? theme::warning : theme::dimText);
        g.drawFittedText (info, infoArea, juce::Justification::centredLeft, 2, 1.0f);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto h = hitAt (e.getPosition());
        setMouseCursor (h.edge ? juce::MouseCursor::LeftRightResizeCursor
                               : (h.step >= 0 || (canAdd() && addArea.contains (e.getPosition())) ? juce::MouseCursor::PointingHandCursor
                                                                                                 : juce::MouseCursor::NormalCursor));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (addArea.contains (e.getPosition()))
        {
            if (canAdd())
                showAddMenu();
            return;
        }
        const auto h = hitAt (e.getPosition());
        if (h.step < 0)
            return;
        if (e.mods.isPopupMenu())
        {
            showMenu (h.step);
            return;
        }
        drag = h.edge ? Drag::resize : Drag::pending;
        dragStep = h.step;
        pressX = dragX = e.position.x;
        grabOffset = pressX - static_cast<float> (timeline.x (Timeline::startBeat (progression, h.step)));
        if (! h.edge)
        {
            callbacks.preview (progression.steps[static_cast<std::size_t> (h.step)].numeral);
            previewing = true;
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag == Drag::none)
            return;
        dragX = e.position.x;
        if (drag == Drag::pending && std::abs (dragX - pressX) > 4.0f)
        {
            drag = Drag::move; // ya no es escuchar: es mover
            stopPreview();
        }
        if (drag == Drag::resize)
        {
            const int beats = timeline.resizedBeats (progression, dragStep, pressX, dragX);
            info = tiles[static_cast<std::size_t> (dragStep)].roman + ": " + beatsText (beats)
                   + names::utf8 (" \xc2\xb7 suelta para aplicarlo");
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        const auto finished = drag;
        const auto edited = draggedProgression();
        drag = Drag::none;
        stopPreview();
        if ((finished == Drag::move || finished == Drag::resize) && ! (edited == progression))
        {
            commit (edited);
            info = {};
        }
        else if (finished == Drag::resize)
            info = {};
        layoutTimeline();
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const int index = hitAt (e.getPosition()).step;
        if (index < 0 || wheel.deltaY == 0.0f || drag != Drag::none)
            return;
        auto updated = progression;
        auto& beats = updated.steps[static_cast<std::size_t> (index)].beats;
        beats = deepwater::theory::Progression::clampBeats (beats + (wheel.deltaY > 0.0f ? 1 : -1));
        commit (updated);
    }

private:
    struct Tile
    {
        juce::String roman, symbol, origin; // origin: " · de frigio" si es prestado
        deepwater::theory::HarmonicFunction function = deepwater::theory::HarmonicFunction::tonic;
        bool borrowed = false;
    };

    // Lo que se arrastra: nada, un clic que todavía puede ser escuchar o mover, mover un paso, o su borde derecho.
    enum class Drag
    {
        none,
        pending,
        move,
        resize
    };

    static constexpr int rulerHeight = 13;
    static constexpr int addWidth = 58;

    [[nodiscard]] bool canAdd() const { return static_cast<int> (progression.steps.size()) < deepwater::theory::maxProgressionSteps; }

    [[nodiscard]] static juce::String beatsText (int beats) { return juce::String (beats) + (beats == 1 ? " tiempo" : " tiempos"); }

    // La escala de la línea: se ajusta al soltar y al cambiar la progresión, nunca mientras se arrastra (si no, el borde
    // se escaparía del ratón al cambiar el largo total).
    void layoutTimeline()
    {
        if (drag == Drag::none)
            timeline = Timeline::fit (progression, stepsArea.getX(), stepsArea.getWidth());
    }

    [[nodiscard]] Timeline::Hit hitAt (juce::Point<int> point) const
    {
        if (! lineArea.contains (point))
            return {};
        return timeline.hit (progression, point.x);
    }

    // Arrastrando un paso: dónde caería.
    [[nodiscard]] int dropTarget() const
    {
        const double width = timeline.x (Timeline::endBeat (progression, dragStep)) - timeline.x (Timeline::startBeat (progression, dragStep));
        return timeline.dropIndex (progression, dragStep, dragX - grabOffset + width * 0.5);
    }

    // La progresión como quedaría al soltar lo que se arrastra (la que se dibuja mientras tanto).
    [[nodiscard]] deepwater::theory::Progression draggedProgression() const
    {
        if (drag == Drag::move)
            return moveStep (progression, dragStep, dropTarget());
        if (drag == Drag::resize)
            return resizeStep (progression, dragStep, timeline.resizedBeats (progression, dragStep, pressX, dragX));
        return progression;
    }

    // Qué paso original se dibuja en cada posición (al mover, el arrastrado ocupa su sitio nuevo).
    [[nodiscard]] std::vector<int> drawOrder() const
    {
        std::vector<int> order;
        for (int i = 0; i < static_cast<int> (progression.steps.size()); ++i)
            if (drag != Drag::move || i != dragStep)
                order.push_back (i);
        if (drag == Drag::move)
            order.insert (order.begin() + std::min (dropTarget(), static_cast<int> (order.size())), dragStep);
        return order;
    }

    void stopPreview()
    {
        if (! previewing)
            return;
        previewing = false;
        callbacks.stopPreview();
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
            if (! th::isDiatonic (chord, settings.key))
            {
                tile.borrowed = true;
                const auto modes = th::modesContaining (chord, settings.key.tonic);
                tile.origin << names::utf8 (" \xc2\xb7 ") << (modes.empty() ? names::utf8 ("crom\xc3\xa1tico") : names::borrowedFrom (modes[0]));
            }
            tiles.push_back (tile);
        }
        layoutTimeline();
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
        info = truncated ? names::utf8 ("Solo caben ") + juce::String (deepwater::theory::maxProgressionSteps)
                               + names::utf8 (" pasos: se qued\xc3\xb3 con los primeros.")
                         : juce::String();
        refreshText();
    }

    void applyTemplate (int index)
    {
        if (index < 0 || index >= static_cast<int> (progressionTemplates.size()))
            return;
        lastTemplate = index;
        const auto& t = progressionTemplates[static_cast<std::size_t> (index)];
        deepwater::theory::Progression updated;
        if (const auto parsed = deepwater::theory::parseProgression (t.numerals))
            for (const auto& numeral : *parsed)
                updated.steps.push_back ({ numeral, 4 });
        callbacks.setMode (t.mode);
        commit (updated);
        info = names::utf8 (t.name) + names::utf8 (": ") + names::utf8 (t.description);
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

        juce::PopupMenu chordMenu, insertMenu;
        addChordChoices (chordMenu, [this, index] (const th::RomanNumeral& numeral) { setNumeral (index, numeral); });
        addChordChoices (insertMenu, [this, index] (const th::RomanNumeral& numeral) { commit (insertStep (progression, index, { numeral, 4 })); });

        menu.addSectionHeader (tiles[static_cast<std::size_t> (index)].roman + "  " + tiles[static_cast<std::size_t> (index)].symbol);
        menu.addSubMenu ("Cambiar el acorde", chordMenu);
        menu.addSubMenu ("Tipo de acorde", typeMenu);
        menu.addSubMenu (names::utf8 ("Duraci\xc3\xb3n"), beatsMenu);
        menu.addSeparator();
        menu.addSubMenu ("Insertar antes", insertMenu, canAdd());
        menu.addItem ("Duplicar", canAdd(), false, [this, index, current] { commit (insertStep (progression, index + 1, current)); });
        menu.addItem (names::utf8 ("Mover a la izquierda"), index > 0, false, [this, index] { commit (moveStep (progression, index, index - 1)); });
        menu.addItem (names::utf8 ("Mover a la derecha"), index + 1 < static_cast<int> (progression.steps.size()), false,
                      [this, index] { commit (moveStep (progression, index, index + 1)); });
        menu.addItem ("Quitar", [this, index] { commit (removeStep (progression, index)); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (stepBounds (index).toNearestInt())));
    }

    // El "+": un acorde de la escala o prestado, al final.
    void showAddMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Agregar al final");
        addChordChoices (menu, [this] (const deepwater::theory::RomanNumeral& numeral) { append (numeral); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (addArea)));
    }

    // Los 7 acordes de la escala (como la paleta: tríadas o séptimas) y, en un submenú por modo, los prestados.
    void addChordChoices (juce::PopupMenu& menu, std::function<void (const deepwater::theory::RomanNumeral&)> pick) const
    {
        namespace th = deepwater::theory;
        const th::Notation notation { true, 5 };
        const auto label = [&] (const th::RomanNumeral& numeral, const th::Chord& chord) {
            return juce::String::fromUTF8 ((th::romanNumeralText (numeral, notation) + "   " + th::chordSymbol (chord, notation)).c_str());
        };
        for (int d = 0; d < 7; ++d)
            if (const auto chord = th::diatonicChord (settings.key, d, settings.size))
            {
                const auto numeral = th::romanNumeralFor (*chord, settings.key);
                menu.addItem (label (numeral, *chord) + "   (" + names::functionName (th::harmonicFunction (numeral)) + ")",
                              [pick, numeral] { pick (numeral); });
            }

        const auto borrowedList = th::borrowedChords (settings.key, settings.size);
        if (borrowedList.empty())
            return;
        menu.addSeparator();
        juce::PopupMenu borrowedMenu;
        for (const auto source : th::parallelModeOrder (settings.key))
        {
            juce::PopupMenu modeMenu;
            for (const auto& b : borrowedList)
                if (b.source == source)
                    modeMenu.addItem (label (b.numeral, b.chord), [pick, numeral = b.numeral] { pick (numeral); });
            if (modeMenu.getNumItems() > 0)
                borrowedMenu.addSubMenu (names::borrowedFrom (source), modeMenu);
        }
        menu.addSubMenu ("Prestados de otros modos", borrowedMenu);
    }

    // Dónde se dibuja el paso en la posición 'position' de 'shown' (la progresión tal como se ve).
    [[nodiscard]] juce::Rectangle<float> stepBounds (int position, const deepwater::theory::Progression& shown) const
    {
        const auto x0 = static_cast<float> (timeline.x (Timeline::startBeat (shown, position)));
        const auto x1 = static_cast<float> (timeline.x (Timeline::endBeat (shown, position)));
        return { x0 + 1.0f, static_cast<float> (stepsArea.getY()), std::max (1.0f, x1 - x0 - 2.0f), static_cast<float> (stepsArea.getHeight()) };
    }
    [[nodiscard]] juce::Rectangle<float> stepBounds (int index) const { return stepBounds (index, progression); }

    // Un paso: fondo (encendido si suena), la franja de su función y, según el ancho, grado, acorde y duración.
    void drawStep (juce::Graphics& g, juce::Rectangle<float> bounds, int index, int beats, bool here, bool held, float alpha)
    {
        const auto& tile = tiles[static_cast<std::size_t> (index)];
        const bool active = settings.enabled;
        const auto colour = names::functionColour (tile.function);
        const bool lit = here && sounding;
        g.setColour ((lit ? colour.withAlpha (0.28f) : (held ? theme::control : theme::display)).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour ((here ? (settings.playMode == midi::PlayMode::progression ? colour : theme::dimText) : theme::panelOutline).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, here ? 1.8f : 1.0f);
        g.setColour (colour.withAlpha (active ? 0.9f : 0.35f));
        g.fillRoundedRectangle (bounds.withHeight (3.0f).reduced (std::min (5.0f, bounds.getWidth() * 0.2f), 0.0f).translated (0.0f, 3.0f), 1.5f);

        const float width = bounds.getWidth();
        if (width < 12.0f)
            return;
        auto inner = bounds.reduced (3.0f, 0.0f).withTrimmedTop (8.0f);
        g.setColour (active ? theme::text : theme::dimText);
        g.setFont (juce::FontOptions (width >= 40.0f ? 16.0f : 12.0f, juce::Font::bold));
        g.drawFittedText (tile.roman, inner.removeFromTop (19.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        if (width < 26.0f)
            return;
        g.setColour (active ? colour : theme::dimText);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawFittedText (tile.symbol, inner.removeFromTop (15.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        g.setColour (tile.borrowed ? theme::freeRunning : theme::dimText);
        g.setFont (juce::FontOptions (10.0f));
        const auto detail = width >= 150.0f ? beatsText (beats) + tile.origin : (width >= 62.0f ? beatsText (beats) : juce::String (beats));
        g.drawFittedText (detail, inner.removeFromTop (12.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

    // La regla (compases numerados, los tiempos como marcas), los pasos, lo que se arrastra, el "+" y la línea de FL.
    void drawTimeline (juce::Graphics& g)
    {
        namespace th = deepwater::theory;
        const auto shown = draggedProgression();
        const int visibleBeats = static_cast<int> (std::floor (timeline.beatAt (stepsArea.getRight()) + 1.0e-6));
        const double barWidth = 4.0 * timeline.pixelsPerBeat;
        const int barLabelEvery = barWidth >= 18.0 ? 1 : (barWidth >= 9.0 ? 2 : 4);
        g.setFont (juce::FontOptions (9.5f));
        for (int beat = 0; beat <= visibleBeats; ++beat)
        {
            const auto x = static_cast<float> (timeline.x (beat));
            const bool bar = beat % 4 == 0;
            g.setColour (theme::dimText.withAlpha (bar ? 0.55f : 0.25f));
            g.fillRect (x, static_cast<float> (rulerArea.getBottom() - (bar ? 6 : 3)), 1.0f, bar ? 6.0f : 3.0f);
            if (bar && (beat / 4) % barLabelEvery == 0 && beat < visibleBeats)
                g.drawText (juce::String (beat / 4 + 1), juce::Rectangle<float> (x + 3.0f, static_cast<float> (rulerArea.getY()), 24.0f, 9.0f),
                            juce::Justification::centredLeft);
        }
        // Lo que está más allá de la vuelta: la progresión vuelve a empezar.
        const auto loopEnd = static_cast<float> (timeline.x (shown.totalBeats()));
        if (loopEnd < static_cast<float> (stepsArea.getRight()))
        {
            g.setColour (theme::panelOutline.withAlpha (0.35f));
            g.fillRoundedRectangle (juce::Rectangle<float> (loopEnd + 2.0f, static_cast<float> (stepsArea.getY()),
                                                            static_cast<float> (stepsArea.getRight()) - loopEnd - 2.0f, static_cast<float> (stepsArea.getHeight())),
                                    4.0f);
            if (static_cast<float> (stepsArea.getRight()) - loopEnd > 90.0f)
            {
                g.setColour (theme::dimText.withAlpha (0.6f));
                g.setFont (juce::FontOptions (10.5f));
                g.drawText (tiles.empty() ? names::utf8 ("Vac\xc3\xad" "a: pulsa + o elige una plantilla") : names::utf8 ("y vuelve a empezar"),
                            juce::Rectangle<float> (loopEnd + 8.0f, static_cast<float> (stepsArea.getY()), static_cast<float> (stepsArea.getRight()) - loopEnd - 12.0f,
                                                    static_cast<float> (stepsArea.getHeight())),
                            juce::Justification::centred);
            }
        }

        const auto order = drawOrder();
        for (int position = 0; position < static_cast<int> (order.size()); ++position)
        {
            const int index = order[static_cast<std::size_t> (position)];
            const auto bounds = stepBounds (position, shown);
            if (drag == Drag::move && index == dragStep)
            {
                // El hueco donde caería.
                g.setColour (theme::accent.withAlpha (0.7f));
                g.drawRoundedRectangle (bounds.reduced (1.0f), 4.0f, 1.5f);
                continue;
            }
            drawStep (g, bounds, index, shown.steps[static_cast<std::size_t> (position)].beats, index == step,
                      previewing && index == dragStep, 1.0f);
        }
        if (drag == Drag::move)
        {
            const auto width = stepBounds (dragStep).getWidth();
            const auto floating = juce::Rectangle<float> (dragX - grabOffset + 1.0f, static_cast<float> (stepsArea.getY()) - 3.0f, width,
                                                          static_cast<float> (stepsArea.getHeight()));
            drawStep (g, floating, dragStep, progression.steps[static_cast<std::size_t> (dragStep)].beats, false, true, 0.9f);
        }
        if (drag == Drag::resize)
        {
            g.setColour (theme::accent);
            const auto edge = static_cast<float> (timeline.x (Timeline::endBeat (shown, dragStep)));
            g.fillRect (edge - 1.0f, static_cast<float> (stepsArea.getY()), 2.0f, static_cast<float> (stepsArea.getHeight()));
        }

        // El "+".
        const auto add = addArea.toFloat();
        const bool canAddMore = canAdd();
        g.setColour (theme::panelOutline.withAlpha (canAddMore ? 1.0f : 0.4f));
        g.drawRoundedRectangle (add.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (canAddMore ? theme::dimText : theme::dimText.withAlpha (0.35f));
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText ("+", add.withTrimmedBottom (18.0f), juce::Justification::centred);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (canAddMore ? "acorde" : juce::String (th::maxProgressionSteps) + names::utf8 (" m\xc3\xa1x."), add.withTrimmedTop (30.0f),
                    juce::Justification::centredTop);

        // FL: una línea en la posición de la canción dentro de la vuelta.
        if (playheadBeat >= 0.0 && drag == Drag::none)
        {
            const auto x = static_cast<float> (timeline.x (playheadBeat));
            g.setColour (theme::text.withAlpha (0.85f));
            g.fillRect (x - 0.5f, static_cast<float> (rulerArea.getY()), 1.5f, static_cast<float> (stepsArea.getBottom() - rulerArea.getY()));
        }
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
    juce::TextButton addButton, clearButton, ideaButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> playModeAttachment, rhythmAttachment, voicingAttachment;

    deepwater::theory::Progression progression;
    midi::ChordSettings settings;
    std::vector<Tile> tiles;
    juce::Rectangle<int> lineArea, rulerArea, stepsArea, addArea, rhythmArea, infoArea;
    Timeline timeline;
    double playheadBeat = -1.0; // FL dentro de la vuelta, en tiempos (−1 = sin host o vacía)
    Drag drag = Drag::none;
    int dragStep = -1;
    float pressX = 0.0f, dragX = 0.0f, grabOffset = 0.0f;
    bool previewing = false;
    juce::String info;
    bool error = false, simple = false;
    int lastTemplate = -1;
    int step = -1, rhythmStep = -1;
    bool sounding = false;
};

} // namespace tidechart::gui
