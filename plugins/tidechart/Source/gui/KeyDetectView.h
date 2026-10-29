#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "deepwater/theory/KeyDetection.h"
#include "deepwater/theory/Text.h"
#include "gui/Theme.h"
#include "gui/TheoryNames.h"
#include "TransportSnapshot.h"

namespace tidechart::gui
{

// Detección de tonalidad (Fase 5). "Escuchar" hace que Tidechart deje pasar el MIDI tal cual y acumule lo que entra
// (del teclado o del Piano Roll con FL reproduciendo); aquí se ordenan las 120 tonalidades con deepwater::theory::rankKeys
// y se muestra la mejor, qué tan segura es, por qué (qué notas sonaron más y qué la separa de la segunda) y las
// alternativas. "Usar" (o clic en una alternativa) la pone en la paleta. Debajo, el histograma de las 12 notas: la
// altura es cuánto sonó cada una; en color las de la escala detectada (la tónica en turquesa) y un punto naranja en
// las que se llevaron votos de "casa" (el bajo al empezar cada frase).
// Todo corre en la GUI con lo que publica el audio; el audio solo suma.
// Vista simple: una sola fila (Escuchar, el resultado con su confianza, Usar), sin título, histograma ni alternativas;
// el porqué queda en el tooltip del resultado.
class KeyDetectView final : public juce::Component, public juce::TooltipClient
{
public:
    struct Callbacks
    {
        std::function<void (bool)> listen;                         // empezar (de cero) o dejar de escuchar
        std::function<void (const deepwater::theory::Key&)> apply; // poner esta tonalidad en la paleta
    };

    // Menos que esto (segundos con algo sonando) todavía es "escuchando...".
    static constexpr double minHeardSeconds = 1.0;
    static constexpr int alternativeCount = 3;

    KeyDetectView (Callbacks callbacksToUse, bool listeningNow) : callbacks (std::move (callbacksToUse))
    {
        listenButton.setButtonText ("Escuchar");
        listenButton.setClickingTogglesState (true);
        listenButton.setToggleState (listeningNow, juce::dontSendNotification);
        listenButton.setTooltip (names::utf8 ("Escucha lo que entra (el teclado, o el Piano Roll de Tidechart con FL reproduciendo) y "
                                              "detecta la tonalidad. Mientras escucha, el MIDI pasa tal cual (como en bypass): se oye "
                                              "lo que tocas y la paleta no suena. Cada vez que se activa empieza de cero."));
        listenButton.onClick = [this] { callbacks.listen (listenButton.getToggleState()); };

        useButton.setButtonText ("Usar");
        useButton.setTooltip (names::utf8 ("Pone la tonalidad detectada en la paleta (t\xc3\xb3nica y modo)."));
        useButton.onClick = [this] {
            if (ready)
                callbacks.apply (ranking[0].key);
        };

        for (auto* button : { &listenButton, &useButton })
        {
            button->setColour (juce::TextButton::buttonColourId, theme::control);
            button->setColour (juce::TextButton::buttonOnColourId, theme::input.withAlpha (0.55f));
            button->setColour (juce::TextButton::textColourOffId, theme::text);
            addAndMakeVisible (*button);
        }
        for (std::size_t i = 0; i < alternatives.size(); ++i)
        {
            auto& button = alternatives[i];
            button.setColour (juce::TextButton::buttonColourId, theme::display);
            button.setColour (juce::TextButton::textColourOffId, theme::dimText);
            button.setTooltip (names::utf8 ("Otra lectura posible: clic para ponerla en la paleta."));
            button.onClick = [this, i] {
                if (ready)
                    callbacks.apply (ranking[i + 1].key);
            };
            addChildComponent (button);
        }
        refresh();
    }

    // Desde el temporizador del editor. Solo recalcula y repinta si cambió lo escuchado.
    void update (const TransportSnapshot& snapshot)
    {
        if (snapshot.keyEvidence == evidence && snapshot.listening == listening && snapshot.heardSeconds == heardSeconds)
            return;
        evidence = snapshot.keyEvidence;
        listening = snapshot.listening;
        heardSeconds = snapshot.heardSeconds;
        refresh();
    }

    // Vista simple (una fila) o completa.
    void setSimple (bool shouldBeSimple)
    {
        simple = shouldBeSimple;
        refresh();
        resized();
    }

    juce::String getTooltip() override { return simple ? explanation : juce::String(); }

    void resized() override
    {
        if (simple)
        {
            auto row = getLocalBounds();
            listenButton.setBounds (row.removeFromLeft (90));
            row.removeFromLeft (10);
            useButton.setBounds (row.removeFromRight (64));
            row.removeFromRight (8);
            resultArea = row;
            return;
        }

        // Arriba: título, Escuchar, el resultado y Usar. Abajo: el histograma a la izquierda; a la derecha el porqué y
        // las alternativas.
        auto area = getLocalBounds();
        auto top = area.removeFromTop (26);
        top.removeFromLeft (94); // el título
        listenButton.setBounds (top.removeFromLeft (96));
        top.removeFromLeft (12);
        useButton.setBounds (top.removeFromRight (80));
        top.removeFromRight (12);
        resultArea = top;

        area.removeFromTop (8);
        histogramArea = area.removeFromLeft (360);
        area.removeFromLeft (16);
        auto row = area.removeFromBottom (24);
        area.removeFromBottom (4);
        explanationArea = area;
        alternativesTitle = row.removeFromLeft (70);
        row.removeFromLeft (6);
        constexpr int gap = 6;
        const int width = (row.getWidth() - (alternativeCount - 1) * gap) / alternativeCount;
        for (auto& button : alternatives)
        {
            button.setBounds (row.removeFromLeft (width));
            row.removeFromLeft (gap);
        }
    }

    void paint (juce::Graphics& g) override
    {
        if (simple)
        {
            paintSimple (g);
            return;
        }

        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("TONALIDAD", getLocalBounds().removeFromTop (26).removeFromLeft (94), juce::Justification::centredLeft);

        // El resultado y su confianza.
        auto result = resultArea;
        if (ready)
        {
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            const auto name = keyText (ranking[0].key);
            const int nameWidth = juce::jmin (result.getWidth() - 160, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 12);
            g.drawText (name, result.removeFromLeft (nameWidth), juce::Justification::centredLeft);
            const auto [label, colour] = confidenceLabel (confidence);
            g.setColour (colour);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (label, result, juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (listening ? theme::input : theme::dimText);
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText (listening ? names::utf8 ("escuchando\xe2\x80\xa6") : names::utf8 ("\xe2\x80\x94"), result, juce::Justification::centredLeft);
        }
        if (ready)
        {
            g.setColour (theme::dimText);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (names::utf8 ("o tambi\xc3\xa9n:"), alternativesTitle, juce::Justification::centredLeft);
        }

        paintHistogram (g);

        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (explanation, explanationArea, juce::Justification::topLeft, 3, 1.0f);
    }

private:
    using Key = deepwater::theory::Key;

    static juce::String keyText (const Key& key)
    {
        const deepwater::theory::Notation notation { true, 5 };
        return juce::String::fromUTF8 (deepwater::theory::noteName (key.tonic, notation).c_str()) + " " + names::modeName (key.mode);
    }

    static juce::String noteText (int pc, const Key& key)
    {
        const deepwater::theory::Notation notation { true, 5 };
        return juce::String::fromUTF8 (deepwater::theory::noteName (deepwater::theory::spellInKey (pc, key), notation).c_str());
    }

    static std::pair<juce::String, juce::Colour> confidenceLabel (deepwater::theory::KeyConfidence c)
    {
        switch (c)
        {
            case deepwater::theory::KeyConfidence::clear: return { "clara", theme::input };
            case deepwater::theory::KeyConfidence::likely: return { "probable", theme::accent };
            case deepwater::theory::KeyConfidence::doubtful: return { names::utf8 ("dudosa: escucha m\xc3\xa1s"), theme::freeRunning };
            case deepwater::theory::KeyConfidence::none: break;
        }
        return { {}, theme::dimText };
    }

    // El papel de una nota en la tonalidad, como grado respecto de la escala mayor de la tónica: "tónica", "♭2", "5", y
    // "fuera" si no es de la escala.
    static juce::String roleText (int pc, const Key& key)
    {
        if (pc == key.tonic.pitchClass())
            return names::utf8 ("t\xc3\xb3nica");
        if (! key.contains (pc))
            return names::utf8 ("fuera");
        const deepwater::theory::Notation notation { true, 5 };
        const auto interval = deepwater::theory::intervalBetween (key.tonic, deepwater::theory::spellInKey (pc, key));
        return juce::String::fromUTF8 (deepwater::theory::degreeLabel (interval, notation).c_str());
    }

    void refresh()
    {
        namespace th = deepwater::theory;
        ranking = th::rankKeys (evidence);
        confidence = th::keyConfidence (evidence, ranking);
        ready = confidence != th::KeyConfidence::none && heardSeconds >= minHeardSeconds;

        useButton.setEnabled (ready);
        for (std::size_t i = 0; i < alternatives.size(); ++i)
        {
            alternatives[i].setVisible (ready && ! simple);
            if (ready)
                alternatives[i].setButtonText (keyText (ranking[i + 1].key));
        }
        explanation = ready ? explain() : waitingText();
        repaint();
    }

    juce::String waitingText() const
    {
        if (listening)
            return names::utf8 ("Toca en el teclado o dale play en FL a lo que quieras analizar (unos compases bastan; hacen falta al menos "
                                "3 notas distintas). Ahora el MIDI pasa tal cual: se oye lo que tocas.");
        return names::utf8 ("Pulsa Escuchar y toca, o dale play en FL a una melod\xc3\xad" "a o unos acordes en el Piano Roll de Tidechart. "
                            "Se compara cu\xc3\xa1nto suena cada nota con lo que se espera de cada tonalidad, y el bajo al empezar "
                            "cada frase dice cu\xc3\xa1l es la casa.");
    }

    // Por qué esta tonalidad: las notas que más sonaron con su papel, y qué la separa de la segunda.
    juce::String explain() const
    {
        namespace th = deepwater::theory;
        const auto& best = ranking[0].key;
        const auto& second = ranking[1].key;

        std::array<int, 12> order {};
        for (int i = 0; i < 12; ++i)
            order[static_cast<std::size_t> (i)] = i;
        std::stable_sort (order.begin(), order.end(), [this] (int a, int b) {
            return evidence.pitchWeight[static_cast<std::size_t> (a)] > evidence.pitchWeight[static_cast<std::size_t> (b)];
        });
        juce::String text = names::utf8 ("Lo que m\xc3\xa1s suena: ");
        for (int i = 0; i < 3; ++i)
        {
            const int pc = order[static_cast<std::size_t> (i)];
            text << (i > 0 ? ", " : "") << noteText (pc, best) << " (" << roleText (pc, best) << ")";
        }
        text << ". ";

        const auto tonicName = noteText (best.tonic.pitchClass(), best);
        if (th::sameNotes (best, second))
        {
            text << keyText (second) << names::utf8 (" tiene las mismas notas (es su relativa): ");
            if (ranking[0].cue > ranking[1].cue)
                text << names::utf8 ("decide el bajo al empezar cada frase, que se apoya en ") << tonicName << ".";
            else
                text << names::utf8 ("decide a o\xc3\xad" "do cu\xc3\xa1l se siente como casa (\xc2\xbf" "en qu\xc3\xa9 acorde descansa?).");
        }
        else if (best.tonic.pitchClass() == second.tonic.pitchClass())
        {
            // Misma tónica, otro modo: las notas que cambian.
            juce::String ours, theirs;
            for (int pc = 0; pc < 12; ++pc)
            {
                if (best.contains (pc) && ! second.contains (pc))
                    ours << (ours.isEmpty() ? "" : ", ") << noteText (pc, best);
                if (second.contains (pc) && ! best.contains (pc))
                    theirs << (theirs.isEmpty() ? "" : ", ") << noteText (pc, second);
            }
            text << names::utf8 ("Misma t\xc3\xb3nica que ") << keyText (second) << names::utf8 (": lo que las separa es ") << ours
                 << names::utf8 (" (aqu\xc3\xad) contra ") << theirs << ".";
        }
        else
        {
            text << names::utf8 ("La segunda opci\xc3\xb3n es ") << keyText (second) << ".";
        }
        return text;
    }

    // Una fila: el resultado y su confianza, o qué hacer para tenerlo.
    void paintSimple (juce::Graphics& g) const
    {
        auto result = resultArea;
        if (ready)
        {
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
            const auto name = keyText (ranking[0].key);
            const int nameWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 10;
            g.drawText (name, result.removeFromLeft (nameWidth), juce::Justification::centredLeft);
            const auto [label, colour] = confidenceLabel (confidence);
            g.setColour (colour);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (label, result, juce::Justification::centredLeft);
            return;
        }
        g.setColour (listening ? theme::input : theme::dimText);
        g.setFont (juce::FontOptions (11.5f, listening ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText (listening ? names::utf8 ("Escuchando\xe2\x80\xa6 toca o dale play a tu canci\xc3\xb3n")
                                    : names::utf8 ("\xc2\xbfNo sabes la tonalidad? Pulsa Escuchar y toca algo"),
                          result, juce::Justification::centredLeft, 1, 0.9f);
    }

    void paintHistogram (juce::Graphics& g) const
    {
        const auto area = histogramArea.toFloat();
        const float columnWidth = area.getWidth() / 12.0f;
        const float labelHeight = 13.0f;
        const auto bars = area.withTrimmedBottom (labelHeight + 2.0f);

        double maxWeight = 0.0;
        for (double w : evidence.pitchWeight)
            maxWeight = std::max (maxWeight, w);
        const double cues = evidence.totalCues();
        const Key key = ready ? ranking[0].key : Key {};

        g.setColour (theme::display);
        g.fillRoundedRectangle (bars, 3.0f);
        for (int pc = 0; pc < 12; ++pc)
        {
            const auto column = juce::Rectangle<float> (area.getX() + pc * columnWidth, bars.getY(), columnWidth, bars.getHeight()).reduced (3.0f, 3.0f);
            const double share = maxWeight > 0.0 ? evidence.pitchWeight[static_cast<std::size_t> (pc)] / maxWeight : 0.0;
            const bool inScale = ready && key.contains (pc);
            const bool tonic = ready && key.tonic.pitchClass() == pc;
            const auto colour = tonic ? theme::input : (inScale ? theme::accent : theme::dimText.withAlpha (0.6f));
            const float height = static_cast<float> (share) * column.getHeight();
            g.setColour (colour.withAlpha (ready ? 0.85f : 0.5f));
            g.fillRoundedRectangle (column.withTop (column.getBottom() - height), 2.0f);

            // Los votos de "casa".
            if (cues > 0.0 && evidence.tonicCue[static_cast<std::size_t> (pc)] > 0.0)
            {
                const float radius = 2.5f + 2.0f * static_cast<float> (evidence.tonicCue[static_cast<std::size_t> (pc)] / cues);
                g.setColour (theme::freeRunning);
                g.fillEllipse (column.getCentreX() - radius, column.getY() + 1.0f, radius * 2.0f, radius * 2.0f);
            }

            g.setColour (tonic ? theme::input : (inScale ? theme::text : theme::dimText));
            g.setFont (juce::FontOptions (10.5f, tonic ? juce::Font::bold : juce::Font::plain));
            const auto label = ready ? noteText (pc, key) : noteText (pc, Key {});
            g.drawText (label, juce::Rectangle<float> (area.getX() + pc * columnWidth, area.getBottom() - labelHeight, columnWidth, labelHeight),
                        juce::Justification::centred);
        }
    }

    Callbacks callbacks;
    juce::TextButton listenButton, useButton;
    std::array<juce::TextButton, alternativeCount> alternatives;

    deepwater::theory::KeyEvidence evidence;
    deepwater::theory::KeyRanking ranking {};
    deepwater::theory::KeyConfidence confidence = deepwater::theory::KeyConfidence::none;
    bool ready = false;
    bool listening = false;
    bool simple = false;
    double heardSeconds = 0.0;
    juce::String explanation;

    juce::Rectangle<int> resultArea, alternativesTitle, histogramArea, explanationArea;
};

} // namespace tidechart::gui
