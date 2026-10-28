#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "deepwater/theory/Text.h"
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
class ChordPaletteView final : public juce::Component, public juce::TooltipClient
{
public:
    ChordPaletteView (juce::AudioProcessorValueTreeState& state, std::function<void (int)> previewFn)
        : preview (std::move (previewFn))
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
            preview (-1);
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
        repaint();
    }

    juce::String getTooltip() override
    {
        return juce::String::fromUTF8 ("Mant\xc3\xa9n pulsado un acorde para escucharlo (sale por el puerto MIDI, como las teclas); "
                                       "arrastra para pasar de uno a otro. En el Piano Roll de Tidechart, la tecla de arriba de "
                                       "cada acorde lo dispara.");
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto top = area.removeFromTop (26);
        top.removeFromLeft (74); // el título
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
        scaleArea = area.removeFromBottom (16);
        area.removeFromBottom (6);
        constexpr int gap = 8;
        const int padWidth = (area.getWidth() - 6 * gap) / 7;
        for (int d = 0; d < 7; ++d)
            padBounds[static_cast<std::size_t> (d)] = juce::Rectangle<int> (area.getX() + d * (padWidth + gap), area.getY(), padWidth, area.getHeight());
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("PALETA", getLocalBounds().removeFromTop (26).removeFromLeft (74), juce::Justification::centredLeft);

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

            auto inner = bounds.reduced (8.0f, 10.0f);
            g.setColour (active ? theme::dimText : theme::dimText.withAlpha (0.5f));
            g.setFont (juce::FontOptions (10.5f));
            g.drawText (pad.trigger, inner.removeFromTop (14.0f), juce::Justification::centredLeft);

            g.setColour (active ? theme::text : theme::dimText);
            g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
            g.drawText (pad.roman, inner.removeFromTop (28.0f), juce::Justification::centred);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.setColour (active ? colour : theme::dimText);
            g.drawText (pad.symbol, inner.removeFromTop (18.0f), juce::Justification::centred);
            g.setColour (theme::dimText);
            g.setFont (juce::FontOptions (10.5f));
            g.drawText (names::functionName (pad.function), inner.removeFromTop (14.0f), juce::Justification::centred);
        }

        g.setFont (juce::FontOptions (11.5f));
        if (! active)
        {
            g.setColour (theme::warning);
            g.drawText (juce::String::fromUTF8 ("Bypass: las teclas pasan tal cual y la paleta no suena. Quita el bypass para tocar acordes."),
                        scaleArea, juce::Justification::centredLeft);
            return;
        }
        g.setColour (theme::text);
        g.drawText (scaleLine, scaleArea, juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override { pressPad (padAt (e.getPosition())); }
    void mouseDrag (const juce::MouseEvent& e) override { pressPad (padAt (e.getPosition())); }
    void mouseUp (const juce::MouseEvent&) override { pressPad (-1); }

private:
    struct Pad
    {
        juce::String roman, symbol, trigger;
        deepwater::theory::HarmonicFunction function = deepwater::theory::HarmonicFunction::tonic;
    };

    [[nodiscard]] int padAt (juce::Point<int> point) const
    {
        for (int d = 0; d < 7; ++d)
            if (padBounds[static_cast<std::size_t> (d)].contains (point))
                return d;
        return -1;
    }

    void pressPad (int degree)
    {
        if (degree == pressedPad)
            return;
        pressedPad = degree;
        preview (degree);
    }

    // Recalcula los 7 acordes con libs/theory cuando cambia la tonalidad, el modo o el tamaño.
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
            const auto numeral = th::romanNumeralFor (*chord, settings.key);
            pad.roman = u (th::romanNumeralText (numeral, notation));
            pad.symbol = u (th::chordSymbol (*chord, notation));
            pad.function = th::harmonicFunction (numeral);
            if (settings.mapping == midi::KeyMapping::whiteKeys)
                pad.trigger = juce::String ("tecla ") + juce::String::charToString (static_cast<juce::juce_wchar> ("CDEFGAB"[d]));
            else
                pad.trigger = "tecla " + u (th::noteName (settings.key.degree (d), notation));
        }

        scaleLine = u (th::noteName (settings.key.tonic, notation)) + " " + names::modeName (settings.key.mode) + ":  ";
        for (int d = 0; d < 7; ++d)
            scaleLine << u (th::noteName (settings.key.degree (d), notation)) << "  ";
        scaleLine << juce::String::fromUTF8 (" \xc2\xb7   ") << names::modeCharacter (settings.key.mode);
        hasPads = true;
    }

    std::function<void (int)> preview;
    juce::ComboBox tonicBox, modeBox, sizeBox, mappingBox;
    juce::TextButton bassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tonicAttachment, modeAttachment, sizeAttachment, mappingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bassAttachment;

    midi::ChordSettings settings;
    std::uint8_t held = 0;
    bool hasPads = false;
    int pressedPad = -1;
    std::array<Pad, 7> pads;
    std::array<juce::Rectangle<int>, 7> padBounds;
    juce::Rectangle<int> scaleArea;
    juce::String scaleLine;
};

} // namespace tidechart::gui
