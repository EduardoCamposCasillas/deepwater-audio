#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "deepwater/theory/ChordFinder.h"
#include "deepwater/theory/Text.h"
#include "deepwater/theory/Harmony.h"
#include "gui/Theme.h"
#include "gui/TheoryNames.h"

namespace tidechart::gui
{

// El acorde que forman las notas que suenan, con el motor de libs/theory (Fase 2): su nombre (C6 o Am7 según el bajo),
// la inversión, cada nota con su función en el acorde (E♭5 = ♭3) y otras lecturas posibles de las mismas notas. Con dos
// notas, el intervalo. Desde la Fase 3, con la tonalidad de la paleta: las notas se escriben como en ella, y el acorde
// dice su grado en números romanos, su función y, si se sale de la escala, de qué modos con la misma tónica viene
// (préstamo modal). Corre en el hilo de la GUI y solo recalcula cuando cambian las notas o la tonalidad.
class ChordView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChordView()
    {
        setTooltip (juce::String::fromUTF8 ("El acorde que forman las notas que suenan. Las mismas notas pueden tener varios nombres: "
                                            "decide la nota del bajo (C E G A con Do abajo es C6; con La abajo, Am7)."));
    }

    // Un bit por canal para cada nota (0 = no suena), como lo publica el audio.
    void setSounding (const std::uint16_t (&channels)[128])
    {
        std::vector<int> notes;
        for (int n = 0; n < 128; ++n)
            if (channels[n] != 0)
                notes.push_back (n);
        if (notes == current)
            return;
        current = std::move (notes);
        describe();
        repaint();
    }

    void setKey (const deepwater::theory::Key& newKey)
    {
        if (hasKey && newKey == key)
            return;
        key = newKey;
        hasKey = true;
        describe();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("ACORDE", area.removeFromLeft (70), juce::Justification::centredLeft);

        g.setColour (name.isEmpty() ? theme::dimText : theme::text);
        const juce::Font nameFont { juce::FontOptions (20.0f, juce::Font::bold) };
        g.setFont (nameFont);
        const auto nameText = name.isEmpty() ? juce::String::fromUTF8 ("\xe2\x80\x94") : name;
        const int nameWidth = juce::jmax (60, juce::roundToInt (juce::GlyphArrangement::getStringWidth (nameFont, nameText)) + 18);
        g.drawText (nameText, area.removeFromLeft (nameWidth), juce::Justification::centredLeft);

        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (detail, area, juce::Justification::centredLeft, 2);
    }

private:
    static juce::String utf8 (const std::string& text) { return juce::String::fromUTF8 (text.c_str()); }

    // Los intervalos en palabras (dentro de una octava; los de más de una octava se nombran por su resto).
    static const char* intervalWords (int semitones)
    {
        static constexpr std::array<const char*, 12> words {
            "un\xc3\xadsono / octava", "2.\xc2\xaa menor", "2.\xc2\xaa mayor", "3.\xc2\xaa menor", "3.\xc2\xaa mayor", "4.\xc2\xaa justa",
            "tritono", "5.\xc2\xaa justa", "6.\xc2\xaa menor", "6.\xc2\xaa mayor", "7.\xc2\xaa menor", "7.\xc2\xaa mayor",
        };
        return words[static_cast<std::size_t> (((semitones % 12) + 12) % 12)];
    }

    void describe()
    {
        namespace th = deepwater::theory;
        const th::Notation notation { true, 5 };
        name.clear();
        detail.clear();

        if (current.empty())
        {
            detail = utf8 ("toca 3 notas o m\xc3\xa1s (o un power chord: fundamental + 5.\xc2\xaa) y aqu\xc3\xad aparece su nombre");
            return;
        }
        if (current.size() == 1)
        {
            name = utf8 (th::midiNoteName (current.front(), notation));
            detail = utf8 ("una nota sola: a\xc3\xb1" "ade la 3.\xc2\xaa (mayor o menor) y la 5.\xc2\xaa para formar un acorde");
            return;
        }

        const auto readings = hasKey ? th::findChords (current, key) : th::findChords (current);
        if (readings.empty())
        {
            std::string notes;
            for (const int n : current)
                notes += (notes.empty() ? "" : "  ") + th::midiNoteName (n, notation);
            detail = utf8 (notes);
            if (current.size() == 2)
                detail = utf8 ("intervalo de " + std::string (intervalWords (current[1] - current[0])) + "   \xc2\xb7   ") + detail;
            else
                detail = utf8 ("no es un acorde del cat\xc3\xa1logo   \xc2\xb7   ") + detail;
            return;
        }

        const auto& best = readings.front();
        const auto& chord = best.chord;
        name = utf8 (th::chordSymbol (chord, notation));

        // El grado en la tonalidad y su función; fuera de la escala, de dónde se tomó prestado.
        std::string text;
        if (hasKey)
        {
            const auto numeral = th::romanNumeralFor (chord, key);
            text = th::romanNumeralText (numeral, notation) + " ("
                 + names::functionName (th::harmonicFunction (numeral)).toStdString() + ")";
            if (! th::isDiatonic (chord, key))
            {
                const auto modes = th::modesContaining (chord, key.tonic);
                if (modes.empty())
                    text += ", fuera de la escala";
                else
                {
                    text += ", prestado de";
                    for (std::size_t i = 0; i < modes.size() && i < 3; ++i)
                        text += (i == 0 ? " " : ", ") + names::modeName (modes[i]).toStdString();
                }
            }
            text += "   \xc2\xb7   ";
        }

        // La inversión: qué tono del acorde está en el bajo.
        const int inversion = chord.inversion();
        if (inversion == 0)
            text += "estado fundamental";
        else
            text += std::to_string (inversion) + ".\xc2\xaa inversi\xc3\xb3n (bajo: "
                 + th::degreeLabel (chord.info().tones[static_cast<std::size_t> (inversion)], notation) + ")";
        if (best.omitsFifth)
            text += ", sin 5.\xc2\xaa";

        // Cada nota que suena, escrita como en el acorde y con su función (1, ♭3, 5, ♭7...).
        text += "   \xc2\xb7   ";
        const auto tones = chord.tones();
        for (std::size_t i = 0; i < current.size(); ++i)
        {
            const int note = current[i];
            for (std::size_t t = 0; t < tones.size(); ++t)
                if (tones[t].pitchClass() == note % 12)
                {
                    text += (i == 0 ? "" : "  ") + th::noteName (note, tones[t], notation) + " ("
                          + th::degreeLabel (chord.info().tones[t], notation) + ")";
                    break;
                }
        }

        // Otras lecturas de las mismas notas (sin repetir y sin las que solo omiten la 5.ª).
        std::vector<std::string> others;
        for (std::size_t i = 1; i < readings.size() && others.size() < 2; ++i)
            if (! readings[i].omitsFifth)
            {
                auto symbol = th::chordSymbol (readings[i].chord, notation);
                if (symbol != th::chordSymbol (chord, notation) && std::find (others.begin(), others.end(), symbol) == others.end())
                    others.push_back (std::move (symbol));
            }
        if (! others.empty())
        {
            text += "   \xc2\xb7   tambi\xc3\xa9n: ";
            for (std::size_t i = 0; i < others.size(); ++i)
                text += (i == 0 ? "" : ", ") + others[i];
        }
        detail = utf8 (text);
    }

    std::vector<int> current;
    deepwater::theory::Key key;
    bool hasKey = false;
    juce::String name;
    juce::String detail { juce::String::fromUTF8 ("toca 3 notas o m\xc3\xa1s (o un power chord: fundamental + 5.\xc2\xaa) y aqu\xc3\xad aparece su nombre") };
};

} // namespace tidechart::gui
