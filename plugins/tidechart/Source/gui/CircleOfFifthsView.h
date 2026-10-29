#pragma once

#include <array>
#include <cmath>
#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "deepwater/theory/Harmony.h"
#include "deepwater/theory/Text.h"
#include "gui/Theme.h"
#include "gui/TheoryNames.h"

namespace tidechart::gui
{

// Fase 8.3: el círculo de quintas, en una ventana que se abre al hacer clic en la tónica (reemplaza al menú). Afuera,
// las 12 tónicas mayores una quinta justa más arriba en cada paso (C, G, D... en el sentido del reloj; F, B♭, E♭... en
// el contrario); adentro, la relativa menor de cada una (Am debajo de C: mismas notas, otra casa). Vecinos en el
// círculo = tonalidades que comparten 6 de sus 7 notas.
// Encima se dibuja la tonalidad actual: los acordes mayores y menores de la escala con su grado y el color de su función
// (en una mayor o una menor caen en 3 casillas vecinas de cada anillo: la "zona" de la tonalidad), los prestados que
// ofrece la paleta con un borde naranja, y la casilla de la tónica con un aro. El disminuido no tiene casilla (lo dice
// el texto). Un clic en cualquier casilla pone su fundamental como tónica, con el mismo modo. Al pasar el ratón, el
// texto de abajo dice qué es ese acorde en la tonalidad.
class CircleOfFifthsView final : public juce::Component
{
public:
    explicit CircleOfFifthsView (std::function<void (int)> pickTonic) : onPick (std::move (pickTonic))
    {
        setSize (340, 392);
        rebuild(); // las casillas existen desde el principio (con Do mayor hasta que llegue la tonalidad)
    }

    void setKey (const deepwater::theory::Key& newKey)
    {
        if (newKey == key && hasKey)
            return;
        key = newKey;
        hasKey = true;
        rebuild();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        namespace th = deepwater::theory;
        g.fillAll (theme::panel);
        const auto centre = circleCentre();
        const float radius = circleRadius();

        for (int ring = 0; ring < 2; ++ring)
            for (int i = 0; i < 12; ++i)
            {
                const auto& cell = cells[static_cast<std::size_t> (ring * 12 + i)];
                const float outer = ring == 0 ? radius : radius * innerSplit;
                const float inner = ring == 0 ? radius * innerSplit : radius * innerHole;
                juce::Path segment;
                segment.addPieSegment (centre.x - outer, centre.y - outer, outer * 2.0f, outer * 2.0f, angleOf (i) - halfStep, angleOf (i) + halfStep,
                                       inner / outer);
                const bool hover = hovered == ring * 12 + i;
                const auto colour = names::functionColour (cell.function);
                g.setColour (cell.diatonic ? colour.withAlpha (hover ? 0.55f : 0.32f) : (hover ? theme::control : theme::display));
                g.fillPath (segment);
                g.setColour (theme::panelOutline);
                g.strokePath (segment, juce::PathStrokeType (1.0f));
                if (cell.borrowed)
                {
                    g.setColour (theme::freeRunning.withAlpha (0.85f));
                    g.strokePath (segment, juce::PathStrokeType (1.6f));
                }

                const float labelRadius = (outer + inner) * 0.5f;
                const auto at = centre.getPointOnCircumference (labelRadius, angleOf (i));
                auto area = juce::Rectangle<float> (outer - inner + 18.0f, outer - inner).withCentre (at);
                g.setColour (cell.diatonic || cell.borrowed ? theme::text : theme::dimText);
                g.setFont (juce::FontOptions (ring == 0 ? 14.0f : 12.0f, juce::Font::bold));
                if (cell.roman.isNotEmpty())
                {
                    g.drawText (cell.name, area.removeFromTop (area.getHeight() * 0.55f), juce::Justification::centredBottom);
                    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                    g.setColour (cell.diatonic ? colour : theme::freeRunning);
                    g.drawText (cell.roman, area, juce::Justification::centredTop);
                }
                else
                    g.drawText (cell.name, area, juce::Justification::centred);
            }

        // La tónica: un aro sobre su casilla.
        if (tonicCell >= 0)
        {
            const int ring = tonicCell / 12;
            const float outer = ring == 0 ? radius : radius * innerSplit;
            const float inner = ring == 0 ? radius * innerSplit : radius * innerHole;
            juce::Path segment;
            segment.addPieSegment (centre.x - outer, centre.y - outer, outer * 2.0f, outer * 2.0f, angleOf (tonicCell % 12) - halfStep,
                                   angleOf (tonicCell % 12) + halfStep, inner / outer);
            g.setColour (theme::text);
            g.strokePath (segment, juce::PathStrokeType (2.4f));
        }

        // En el centro, la tonalidad y su armadura.
        auto middle = juce::Rectangle<float> (radius * innerHole * 1.85f, radius * innerHole * 1.3f).withCentre (centre);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        g.drawFittedText (tonicName, middle.removeFromTop (middle.getHeight() * 0.6f).toNearestInt(), juce::Justification::centredBottom, 2, 0.85f);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (10.5f));
        g.drawFittedText (signature, middle.toNearestInt(), juce::Justification::centredTop, 2);

        auto text = getLocalBounds().removeFromBottom (textHeight).reduced (10, 4);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (hovered >= 0 ? cells[static_cast<std::size_t> (hovered)].explanation : summary, text, juce::Justification::topLeft, 4, 1.0f);
    }

    void mouseMove (const juce::MouseEvent& e) override { setHovered (cellAt (e.position)); }
    void mouseExit (const juce::MouseEvent&) override { setHovered (-1); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const int pc = tonicAt (e.position);
        if (pc >= 0 && onPick && e.mouseWasClicked())
            onPick (pc);
    }

    // La tónica (clase de altura) que elige un clic en 'point', o −1 fuera de los anillos. Pública para los tests.
    [[nodiscard]] int tonicAt (juce::Point<float> point) const
    {
        const int cell = cellAt (point);
        return cell < 0 ? -1 : cells[static_cast<std::size_t> (cell)].rootPc;
    }

    // La casilla con el aro (0..11 afuera, 12..23 adentro), o −1. Pública para los tests.
    [[nodiscard]] int tonicCellIndex() const { return tonicCell; }

private:
    struct Cell
    {
        juce::String name, roman, explanation;
        int rootPc = 0;
        bool diatonic = false, borrowed = false;
        deepwater::theory::HarmonicFunction function = deepwater::theory::HarmonicFunction::tonic;
    };

    static constexpr int maxBorrowed = 8; // los que muestra la paleta
    static constexpr float innerSplit = 0.66f; // dónde termina el anillo de las menores, por fracción del radio
    static constexpr float innerHole = 0.36f;
    static constexpr int textHeight = 62;
    static constexpr float halfStep = juce::MathConstants<float>::pi / 12.0f;

    [[nodiscard]] juce::Point<float> circleCentre() const
    {
        return { static_cast<float> (getWidth()) * 0.5f, static_cast<float> (getHeight() - textHeight) * 0.5f + 2.0f };
    }
    [[nodiscard]] float circleRadius() const
    {
        return std::min (static_cast<float> (getWidth()), static_cast<float> (getHeight() - textHeight)) * 0.5f - 8.0f;
    }

    // C arriba (las 12 en punto), una quinta más por casilla en el sentido del reloj (JUCE mide desde arriba, en ese sentido).
    [[nodiscard]] static float angleOf (int index) { return static_cast<float> (index) * 2.0f * halfStep; }

    [[nodiscard]] int cellAt (juce::Point<float> point) const
    {
        const auto centre = circleCentre();
        const float radius = circleRadius();
        const float distance = centre.getDistanceFrom (point);
        if (distance > radius || distance < radius * innerHole)
            return -1;
        float angle = std::atan2 (point.x - centre.x, centre.y - point.y); // 0 arriba, sentido del reloj
        if (angle < 0.0f)
            angle += 2.0f * juce::MathConstants<float>::pi;
        const int index = static_cast<int> (std::floor ((angle + halfStep) / (2.0f * halfStep))) % 12;
        return (distance >= radius * innerSplit ? 0 : 12) + index;
    }

    void setHovered (int cell)
    {
        if (cell == hovered)
            return;
        hovered = cell;
        setMouseCursor (cell >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    // Las 24 casillas con lo que son en la tonalidad actual.
    void rebuild()
    {
        namespace th = deepwater::theory;
        const th::Notation notation { true, 5 };
        const auto u = [] (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); };
        const auto keyName = u (th::noteName (key.tonic, notation)) + " " + names::modeName (key.mode);
        const auto borrowedList = th::borrowedChords (key, th::ChordSize::triad);
        const bool majorKey = th::modeInfo (key.mode).semitones[2] == 4;

        tonicCell = -1;
        for (int ring = 0; ring < 2; ++ring)
            for (int i = 0; i < 12; ++i)
            {
                auto& cell = cells[static_cast<std::size_t> (ring * 12 + i)];
                cell = {};
                const int majorPc = (i * 7) % 12;
                cell.rootPc = ring == 0 ? majorPc : (majorPc + 9) % 12;
                // El nombre "de libro" de esa tonalidad (F♯ / G♭ y D♯m / E♭m, abajo, son la misma).
                const auto standard = th::standardKey (cell.rootPc, ring == 0 ? th::Mode::ionian : th::Mode::aeolian).tonic;
                cell.name = u (th::noteName (standard, notation)) + (ring == 0 ? "" : "m");
                if (i == 6)
                    cell.name = ring == 0 ? names::utf8 ("F\xe2\x99\xaf/G\xe2\x99\xad") : names::utf8 ("E\xe2\x99\xad" "m"); // = D♯m

                // Ese acorde (mayor afuera, menor adentro) escrito en la tonalidad: grado, función, de la escala o prestado.
                const th::Chord chord { th::spellInKey (cell.rootPc, key), th::chordType (ring == 0 ? "" : "m") };
                const auto numeral = th::romanNumeralFor (chord, key);
                const auto symbol = u (th::chordSymbol (chord, notation));
                const auto roman = u (th::romanNumeralText (numeral, notation));
                cell.function = th::harmonicFunction (numeral);
                cell.diatonic = th::isDiatonic (chord, key);
                std::optional<th::Mode> source;
                // Solo los que ofrece la paleta (los primeros): con todos los de los modos paralelos, medio círculo sería naranja.
                for (std::size_t k = 0; k < borrowedList.size() && k < static_cast<std::size_t> (maxBorrowed); ++k)
                    if (const auto& b = borrowedList[k]; b.chord.root.pitchClass() == cell.rootPc && b.chord.type == chord.type)
                    {
                        source = b.source;
                        break;
                    }
                cell.borrowed = ! cell.diatonic && source.has_value();
                if (cell.diatonic || cell.borrowed)
                    cell.roman = roman;

                const auto click = names::utf8 ("Clic: t\xc3\xb3nica ") + u (th::noteName (standard, notation)) + " " + names::modeName (key.mode)
                                   + names::utf8 (" (el modo se queda).");
                if (cell.diatonic)
                    cell.explanation = symbol + " = " + roman + " (" + names::functionName (cell.function) + names::utf8 (") en ") + keyName
                                       + names::utf8 (": est\xc3\xa1 en la escala. ") + click;
                else if (cell.borrowed)
                    cell.explanation = symbol + " = " + roman + ", prestado " + names::borrowedFrom (*source)
                                       + names::utf8 (": trae notas de fuera de la escala (color). ") + click;
                else
                    cell.explanation = symbol + names::utf8 (": lejos de ") + keyName + names::utf8 (" (no est\xc3\xa1 ni en la paleta ni en los prestados). ")
                                       + click;

                if (cell.rootPc == key.tonic.pitchClass() && (ring == 0) == majorKey)
                    tonicCell = ring * 12 + i;
            }

        // La armadura: las alteraciones de la escala escrita.
        int sharps = 0, flats = 0;
        for (int d = 0; d < 7; ++d)
        {
            const int a = key.degree (d).accidental;
            (a > 0 ? sharps : flats) += a > 0 ? a : -a;
        }
        tonicName = keyName;
        signature = sharps + flats == 0 ? names::utf8 ("sin alteraciones")
                    : sharps > 0 && flats > 0 ? juce::String (sharps) + names::utf8 (" \xe2\x99\xaf y ") + juce::String (flats) + names::utf8 (" \xe2\x99\xad")
                    : sharps > 0 ? juce::String (sharps) + names::utf8 (sharps == 1 ? " sostenido" : " sostenidos")
                                 : juce::String (flats) + names::utf8 (flats == 1 ? " bemol" : " bemoles");

        // El disminuido de la escala (si hay) no tiene casilla.
        juce::String diminished;
        for (int d = 0; d < 7; ++d)
            if (const auto chord = th::diatonicChord (key, d, th::ChordSize::triad); chord && chord->info().toneCount == 3
                                                                                    && th::chordTypeInfo (chord->type).tones[2].semitones == 6)
                diminished << (diminished.isEmpty() ? "" : ", ") << u (th::chordSymbol (*chord, notation)) << " ("
                           << u (th::romanNumeralText (th::romanNumeralFor (*chord, key), notation)) << ")";
        summary = names::utf8 ("Coloreados: los acordes de ") + keyName + names::utf8 (", vecinos en el c\xc3\xadrculo. Borde naranja: los prestados. ")
                  + (diminished.isEmpty() ? juce::String() : names::utf8 ("El disminuido, ") + diminished + names::utf8 (", no tiene casilla. "))
                  + names::utf8 ("Clic en una casilla: esa t\xc3\xb3nica, con el mismo modo.");
    }

    std::function<void (int)> onPick;
    deepwater::theory::Key key;
    bool hasKey = false;
    std::array<Cell, 24> cells;
    int tonicCell = -1, hovered = -1;
    juce::String tonicName, signature, summary;
};

} // namespace tidechart::gui
