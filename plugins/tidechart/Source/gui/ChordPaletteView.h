#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "deepwater/theory/Text.h"
#include "gui/ScaleKeyboard.h"
#include "gui/Theme.h"
#include "gui/TheoryNames.h"
#include "midi/ChordTrigger.h"
#include "Parameters.h"

namespace tidechart::gui
{

// La paleta (Fase 3): la tonalidad, cómo se tocan los acordes y los 7 acordes de la escala. Cada uno muestra su grado
// en números romanos, su nombre, su función (tónica / subdominante / dominante, con su color) y la tecla que lo
// dispara. Mantener pulsado un acorde lo hace sonar (sale por el mismo puerto MIDI que las teclas); arrastrar de uno a
// otro los encadena. Se enciende el que suena, se haya disparado desde aquí o desde el Piano Roll.
// Fase 4: debajo, los PRESTADOS: acordes de los modos paralelos (misma tónica) que no están en la escala, con el modo
// del que vienen. Se escuchan igual, y con "Agregar con clic" (en la progresión) cada clic agrega el acorde a la
// progresión.
// Fase 8.2: abajo, en vez de la escala en texto, un teclado de 2 octavas con la escala (gui/ScaleKeyboard.h) y, al
// lado, la escala y su carácter; al pasar el ratón por un acorde (o al tocarlo), sus notas en el teclado y en el texto,
// con las de fuera de la escala marcadas: así se ve qué trae cada prestado.
// Vista simple: arriba solo la tónica y el modo (con el título TONALIDAD; a la derecha queda sitio para la detección);
// tríadas/séptimas, qué teclas disparan y "+ Bajo" quedan en la vista completa (siguen sonando como estén).
class ChordPaletteView final : public juce::Component, public juce::TooltipClient
{
public:
    struct Callbacks
    {
        std::function<void (int)> previewDegree;                                      // 0..6, −1 = soltar
        std::function<void (const deepwater::theory::RomanNumeral&)> previewNumeral;  // un prestado
        std::function<void (const deepwater::theory::RomanNumeral&)> picked;          // clic en cualquier pad
    };

    ChordPaletteView (juce::AudioProcessorValueTreeState& state, Callbacks callbacksToUse) : callbacks (std::move (callbacksToUse))
    {
        namespace id = tidechart::params;
        for (int i = 0; i < 12; ++i)
            tonicBox.addItem (names::tonicChoice (i), i + 1);
        for (int m = 0; m < deepwater::theory::modeCount; ++m)
            modeBox.addItem (names::modeName (static_cast<deepwater::theory::Mode> (m)), m + 1);
        sizeBox.addItem (names::utf8 ("Tr\xc3\xad" "adas"), 1);
        sizeBox.addItem (names::utf8 ("S\xc3\xa9ptimas"), 2);
        mappingBox.addItem ("Teclas blancas = grados", 1);
        mappingBox.addItem ("Notas de la escala", 2);
        bassButton.setButtonText ("+ Bajo");
        bassButton.setClickingTogglesState (true);

        tonicBox.setTooltip (names::utf8 ("La t\xc3\xb3nica: la nota \"de casa\", donde la m\xc3\xbasica descansa."));
        modeBox.setTooltip (names::utf8 ("El modo: qu\xc3\xa9 notas forman la escala a partir de la t\xc3\xb3nica. Cambia el color de todos los acordes."));
        sizeBox.setTooltip (names::utf8 ("Tr\xc3\xad" "adas: 3 notas apiladas de 3.\xc2\xaa en 3.\xc2\xaa dentro de la escala. S\xc3\xa9ptimas: una 3.\xc2\xaa m\xc3\xa1s (m\xc3\xa1s rico, "
                            "el sonido del R&B)."));
        mappingBox.setTooltip ("Teclas blancas = grados: C dispara el I (o la i), D el ii, E el iii... en cualquier tonalidad; "
                               "las negras no suenan. Notas de la escala: cada nota de la escala dispara su acorde con la "
                               "fundamental en esa tecla; las de fuera no suenan.");
        bassButton.setTooltip (names::utf8 ("Agrega la fundamental una octava abajo: el acorde suena m\xc3\xa1s lleno y con el bajo claro."));

        for (auto* box : { &tonicBox, &modeBox, &sizeBox, &mappingBox })
        {
            box->setColour (juce::ComboBox::backgroundColourId, theme::control);
            box->setColour (juce::ComboBox::outlineColourId, theme::panelOutline);
            box->setColour (juce::ComboBox::textColourId, theme::text);
            addAndMakeVisible (*box);
        }
        bassButton.setColour (juce::TextButton::buttonColourId, theme::control);
        bassButton.setColour (juce::TextButton::buttonOnColourId, theme::accent.withAlpha (0.55f));
        bassButton.setColour (juce::TextButton::textColourOffId, theme::text);
        addAndMakeVisible (bassButton);
        addAndMakeVisible (keyboard);

        using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
        tonicAttachment = std::make_unique<ComboAttachment> (state, id::keyTonic, tonicBox);
        modeAttachment = std::make_unique<ComboAttachment> (state, id::keyMode, modeBox);
        sizeAttachment = std::make_unique<ComboAttachment> (state, id::chordSize, sizeBox);
        mappingAttachment = std::make_unique<ComboAttachment> (state, id::keyMapping, mappingBox);
        bassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::chordBass, bassButton);
    }

    ~ChordPaletteView() override
    {
        if (pressedPad >= 0)
            callbacks.previewDegree (-1);
    }

    // Desde el temporizador del editor: los ajustes que usa el audio y los grados que suenan. Solo repinta si cambió algo.
    void update (const midi::ChordSettings& newSettings, std::uint8_t newHeld)
    {
        if (newSettings == settings && newHeld == held && hasPads)
            return;
        if (! (newSettings == settings) || ! hasPads)
        {
            settings = newSettings;
            rebuild();
        }
        held = newHeld;
        refreshKeyboard();
        repaint();
    }

    // Vista simple o completa.
    void setSimple (bool shouldBeSimple)
    {
        simple = shouldBeSimple;
        for (auto* c : std::initializer_list<juce::Component*> { &sizeBox, &mappingBox, &bassButton })
            c->setVisible (! simple);
        resized();
        repaint();
    }

    // Vista simple: lo que queda libre en la fila de arriba, a la derecha del modo.
    [[nodiscard]] juce::Rectangle<int> freeTopArea() const
    {
        return getLocalBounds().removeFromTop (26).withLeft (modeBox.getRight() + 24);
    }

    juce::String getTooltip() override
    {
        const int pad = padAt (getMouseXYRelative());
        if (pad >= 7 && pad - 7 < borrowedCount)
            return borrowed[static_cast<std::size_t> (pad - 7)].tooltip;
        return juce::String::fromUTF8 ("Mant\xc3\xa9n pulsado un acorde para escucharlo (sale por el puerto MIDI, como las teclas); "
                                       "arrastra para pasar de uno a otro. En el Piano Roll de Tidechart, la tecla de arriba de "
                                       "cada acorde lo dispara. Con \xc2\xab" "Agregar con clic\xc2\xbb, cada clic lo suma a la progresi\xc3\xb3n.");
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto top = area.removeFromTop (26);
        top.removeFromLeft (simple ? 96 : 74); // el título
        tonicBox.setBounds (top.removeFromLeft (96));
        top.removeFromLeft (8);
        modeBox.setBounds (top.removeFromLeft (170));
        top.removeFromLeft (8);
        sizeBox.setBounds (top.removeFromLeft (110));
        top.removeFromLeft (8);
        mappingBox.setBounds (top.removeFromLeft (200));
        top.removeFromLeft (8);
        bassButton.setBounds (top.removeFromLeft (80));

        area.removeFromTop (8);
        scaleArea = area.removeFromBottom (26);
        keyboard.setBounds (scaleArea.removeFromLeft (14 * 21));
        scaleArea.removeFromLeft (12);
        area.removeFromBottom (6);
        auto borrowedRow = area.removeFromBottom (40);
        area.removeFromBottom (6);

        constexpr int gap = 8;
        const int padWidth = (area.getWidth() - 6 * gap) / 7;
        for (int d = 0; d < 7; ++d)
            padBounds[static_cast<std::size_t> (d)] = juce::Rectangle<int> (area.getX() + d * (padWidth + gap), area.getY(), padWidth, area.getHeight());

        borrowedTitle = borrowedRow.removeFromLeft (84);
        constexpr int smallGap = 6;
        const int smallWidth = (borrowedRow.getWidth() - (maxBorrowed - 1) * smallGap) / maxBorrowed;
        for (int i = 0; i < maxBorrowed; ++i)
            padBounds[static_cast<std::size_t> (7 + i)] = juce::Rectangle<int> (borrowedRow.getX() + i * (smallWidth + smallGap), borrowedRow.getY(),
                                                                                 smallWidth, borrowedRow.getHeight());
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (simple ? "TONALIDAD" : "PALETA", getLocalBounds().removeFromTop (26).removeFromLeft (simple ? 96 : 74),
                    juce::Justification::centredLeft);

        const bool active = settings.enabled;
        for (int d = 0; d < 7; ++d)
        {
            const auto& pad = pads[static_cast<std::size_t> (d)];
            const auto bounds = padBounds[static_cast<std::size_t> (d)].toFloat();
            const bool sounding = (held & (1u << d)) != 0;
            const auto colour = names::functionColour (pad.function);

            g.setColour (sounding ? colour.withAlpha (0.28f) : theme::display);
            g.fillRoundedRectangle (bounds, 5.0f);
            g.setColour (sounding ? colour : theme::panelOutline);
            g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, sounding ? 1.6f : 1.0f);
            // La función como una franja de color arriba.
            g.setColour (colour.withAlpha (active ? 0.9f : 0.35f));
            g.fillRoundedRectangle (bounds.withHeight (4.0f).reduced (6.0f, 0.0f).translated (0.0f, 4.0f), 2.0f);

            auto inner = bounds.reduced (8.0f, 5.0f);
            g.setColour (active ? theme::dimText : theme::dimText.withAlpha (0.5f));
            g.setFont (juce::FontOptions (10.5f));
            g.drawText (pad.trigger, inner.removeFromTop (14.0f), juce::Justification::centredLeft);

            g.setColour (active ? theme::text : theme::dimText);
            g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
            g.drawText (pad.roman, inner.removeFromTop (24.0f), juce::Justification::centred);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.setColour (active ? colour : theme::dimText);
            g.drawText (pad.symbol, inner.removeFromTop (18.0f), juce::Justification::centred);
            g.setColour (theme::dimText);
            g.setFont (juce::FontOptions (10.5f));
            g.drawText (names::functionName (pad.function), inner.removeFromTop (14.0f), juce::Justification::centred);
        }

        // Los prestados: grado y acorde en una línea, el modo de origen debajo.
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawFittedText (names::utf8 ("PRESTADOS\nde otros modos"), borrowedTitle, juce::Justification::centredLeft, 2);
        for (int i = 0; i < maxBorrowed; ++i)
        {
            const auto bounds = padBounds[static_cast<std::size_t> (7 + i)].toFloat();
            if (i >= borrowedCount)
                continue;
            const auto& pad = borrowed[static_cast<std::size_t> (i)];
            const bool pressed = pressedPad == 7 + i;
            const auto colour = names::functionColour (pad.function);
            g.setColour (pressed ? colour.withAlpha (0.28f) : theme::display);
            g.fillRoundedRectangle (bounds, 4.0f);
            g.setColour (pressed ? colour : colour.withAlpha (active ? 0.45f : 0.2f));
            g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, pressed ? 1.6f : 1.0f);
            auto inner = bounds.reduced (4.0f, 3.0f);
            g.setColour (active ? theme::text : theme::dimText);
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText (pad.roman + "  " + pad.symbol, inner.removeFromTop (18.0f), juce::Justification::centred);
            g.setColour (theme::dimText);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (pad.source, inner, juce::Justification::centred);
        }

        g.setFont (juce::FontOptions (11.5f));
        if (! active)
        {
            g.setColour (theme::warning);
            g.drawText (juce::String::fromUTF8 ("Bypass: las teclas pasan tal cual y la paleta no suena. Quita el bypass para tocar acordes."),
                        scaleArea, juce::Justification::centredLeft);
            return;
        }
        const int shown = shownPad();
        g.setColour (theme::text);
        if (shown < 0)
            g.drawFittedText (scaleLine, scaleArea, juce::Justification::centredLeft, 2, 0.9f);
        else
            g.drawFittedText (shown < 7 ? pads[static_cast<std::size_t> (shown)].notesLine : borrowed[static_cast<std::size_t> (shown - 7)].notesLine,
                              scaleArea, juce::Justification::centredLeft, 2, 0.9f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int pad = padAt (e.getPosition());
        pressPad (pad);
        if (callbacks.picked)
        {
            if (pad >= 0 && pad < 7 && pads[static_cast<std::size_t> (pad)].valid)
                callbacks.picked (pads[static_cast<std::size_t> (pad)].numeral);
            else if (pad >= 7 && pad - 7 < borrowedCount)
                callbacks.picked (borrowed[static_cast<std::size_t> (pad - 7)].numeral);
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override { pressPad (padAt (e.getPosition())); }
    void mouseUp (const juce::MouseEvent&) override { pressPad (-1); }
    void mouseMove (const juce::MouseEvent& e) override { setHovered (padAt (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override { setHovered (-1); }

private:
    static constexpr int maxBorrowed = 8;

    struct Pad
    {
        juce::String roman, symbol, trigger, notesLine;
        deepwater::theory::HarmonicFunction function = deepwater::theory::HarmonicFunction::tonic;
        deepwater::theory::RomanNumeral numeral;
        deepwater::theory::Chord chord;
        bool valid = false;
    };

    struct BorrowedPad
    {
        juce::String roman, symbol, source, tooltip, notesLine;
        deepwater::theory::HarmonicFunction function = deepwater::theory::HarmonicFunction::tonic;
        deepwater::theory::RomanNumeral numeral;
        deepwater::theory::Chord chord;
    };

    // El acorde que muestran el teclado y el texto: el pulsado, si no el que está bajo el ratón, si no el primero que
    // suena (desde el Piano Roll). −1 = ninguno (se ve solo la escala).
    [[nodiscard]] int shownPad() const
    {
        if (pressedPad >= 0)
            return pressedPad;
        if (hoveredPad >= 0)
            return hoveredPad;
        for (int d = 0; d < 7; ++d)
            if ((held & (1u << d)) != 0 && pads[static_cast<std::size_t> (d)].valid)
                return d;
        return -1;
    }

    void setHovered (int index)
    {
        if (index == hoveredPad)
            return;
        hoveredPad = index;
        refreshKeyboard();
        repaint (scaleArea);
    }

    void refreshKeyboard()
    {
        keyboard.setKey (settings.key);
        const int shown = shownPad();
        if (shown < 0 || (shown < 7 && ! pads[static_cast<std::size_t> (shown)].valid))
            keyboard.setChord (std::nullopt, theme::accent);
        else if (shown < 7)
            keyboard.setChord (pads[static_cast<std::size_t> (shown)].chord, names::functionColour (pads[static_cast<std::size_t> (shown)].function));
        else
            keyboard.setChord (borrowed[static_cast<std::size_t> (shown - 7)].chord,
                               names::functionColour (borrowed[static_cast<std::size_t> (shown - 7)].function));
    }

    // "♭VI = F:  F  A  C  ·  todas sus notas son de la escala", o "· trae B♭, de fuera de la escala".
    [[nodiscard]] juce::String notesLineFor (const juce::String& roman, const juce::String& symbol, const deepwater::theory::Chord& chord) const
    {
        namespace th = deepwater::theory;
        const th::Notation notation { true, 5 };
        auto line = roman + " = " + symbol + ":  ";
        juce::String outside;
        for (const auto& tone : chord.tones())
        {
            const auto name = juce::String::fromUTF8 (th::noteName (tone, notation).c_str());
            line << name << "  ";
            if (! settings.key.contains (tone.pitchClass()))
                outside << (outside.isEmpty() ? "" : ", ") << name;
        }
        line << names::utf8 (" \xc2\xb7  ");
        if (outside.isEmpty())
            line << names::utf8 ("todas sus notas son de la escala");
        else
            line << "trae " << outside << names::utf8 (", de fuera de la escala: de ah\xc3\xad su color");
        return line;
    }

    [[nodiscard]] int padAt (juce::Point<int> point) const
    {
        for (int i = 0; i < 7 + borrowedCount; ++i)
            if (padBounds[static_cast<std::size_t> (i)].contains (point))
                return i;
        return -1;
    }

    void pressPad (int index)
    {
        if (index == pressedPad)
            return;
        pressedPad = index;
        refreshKeyboard();
        if (index >= 7)
            callbacks.previewNumeral (borrowed[static_cast<std::size_t> (index - 7)].numeral);
        else
            callbacks.previewDegree (index);
        repaint();
    }

    // Recalcula los 7 acordes y los prestados con libs/theory cuando cambia la tonalidad, el modo o el tamaño.
    void rebuild()
    {
        namespace th = deepwater::theory;
        const th::Notation notation { true, 5 };
        const auto u = [] (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); };
        for (int d = 0; d < 7; ++d)
        {
            auto& pad = pads[static_cast<std::size_t> (d)];
            pad = {};
            const auto chord = th::diatonicChord (settings.key, d, settings.size);
            if (! chord)
                continue;
            pad.numeral = th::romanNumeralFor (*chord, settings.key);
            pad.valid = true;
            pad.chord = *chord;
            pad.roman = u (th::romanNumeralText (pad.numeral, notation));
            pad.symbol = u (th::chordSymbol (*chord, notation));
            pad.notesLine = notesLineFor (pad.roman, pad.symbol, *chord);
            pad.function = th::harmonicFunction (pad.numeral);
            if (settings.mapping == midi::KeyMapping::whiteKeys)
                pad.trigger = juce::String ("tecla ") + juce::String::charToString (static_cast<juce::juce_wchar> ("CDEFGAB"[d]));
            else
                pad.trigger = "tecla " + u (th::noteName (settings.key.degree (d), notation));
        }

        const auto list = th::borrowedChords (settings.key, settings.size);
        borrowedCount = std::min (maxBorrowed, static_cast<int> (list.size()));
        for (int i = 0; i < borrowedCount; ++i)
        {
            const auto& b = list[static_cast<std::size_t> (i)];
            auto& pad = borrowed[static_cast<std::size_t> (i)];
            pad.numeral = b.numeral;
            pad.roman = u (th::romanNumeralText (b.numeral, notation));
            pad.symbol = u (th::chordSymbol (b.chord, notation));
            pad.chord = b.chord;
            pad.notesLine = notesLineFor (pad.roman, pad.symbol, b.chord);
            pad.source = names::borrowedFrom (b.source);
            pad.function = th::harmonicFunction (b.numeral);
            pad.tooltip = pad.roman + " = " + pad.symbol + names::utf8 (", prestado ") + pad.source + ": no est\xc3\xa1 en "
                          + u (th::noteName (settings.key.tonic, notation)) + " " + names::modeName (settings.key.mode)
                          + names::utf8 (" pero s\xc3\xad en ") + u (th::noteName (settings.key.tonic, notation)) + " "
                          + names::modeName (b.source) + names::utf8 (", con la misma t\xc3\xb3nica. Trae notas de fuera de la escala: color.");
        }

        scaleLine = u (th::noteName (settings.key.tonic, notation)) + " " + names::modeName (settings.key.mode) + ":  ";
        for (int d = 0; d < 7; ++d)
            scaleLine << u (th::noteName (settings.key.degree (d), notation)) << "  ";
        scaleLine << juce::String::fromUTF8 (" \xc2\xb7   ") << names::modeCharacter (settings.key.mode);
        hasPads = true;
        refreshKeyboard();
    }

    Callbacks callbacks;
    ScaleKeyboard keyboard;
    juce::ComboBox tonicBox, modeBox, sizeBox, mappingBox;
    juce::TextButton bassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tonicAttachment, modeAttachment, sizeAttachment, mappingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bassAttachment;

    midi::ChordSettings settings;
    std::uint8_t held = 0;
    bool hasPads = false;
    bool simple = false;
    int pressedPad = -1, hoveredPad = -1;
    std::array<Pad, 7> pads;
    std::array<BorrowedPad, maxBorrowed> borrowed;
    int borrowedCount = 0;
    std::array<juce::Rectangle<int>, 7 + maxBorrowed> padBounds;
    juce::Rectangle<int> scaleArea, borrowedTitle;
    juce::String scaleLine;
};

} // namespace tidechart::gui
