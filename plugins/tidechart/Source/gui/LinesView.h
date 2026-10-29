#pragma once

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "deepwater/theory/Melody.h"
#include "deepwater/theory/Text.h"
#include "gui/Theme.h"
#include "gui/TheoryNames.h"
#include "Lines.h"
#include "midi/ChordTrigger.h"
#include "midi/EventLog.h"
#include "Parameters.h"
#include "TransportSnapshot.h"

namespace tidechart::gui
{

// Melodía y bajo (Fase 6). Arriba, qué capas suenan en el modo progresión (acordes, bajo, melodía: parámetros, se pueden
// automatizar). Para cada línea: estilo, densidad, rango y "Otra" (otra semilla: otra línea con los mismos ajustes). La
// melodía además se puede CAPTURAR (tocarla o escribirla en el Piano Roll de Tidechart con FL reproduciendo), VARIAR
// (cuánto y otra variación) y FIJAR (la melodía de ahora pasa a ser la base de las variaciones).
// Debajo, un piano roll de una vuelta con los acordes arriba y cada nota coloreada por su papel sobre el acorde que suena:
// nota del acorde (apoyo), de la escala (de paso, adorno o tensión) o cromática. Pasar el ratón por una nota la explica.
class LinesView final : public juce::Component
{
public:
    struct Callbacks
    {
        std::function<void (const BassSettings&)> setBass;
        std::function<void (const MelodySettings&)> setMelody;
        std::function<void (bool)> setCapturing;
        std::function<void (const deepwater::theory::Line&)> captured;
        std::function<void()> fix;
    };

    LinesView (juce::AudioProcessorValueTreeState& state, Callbacks callbacksToUse) : callbacks (std::move (callbacksToUse))
    {
        namespace id = tidechart::params;
        namespace th = deepwater::theory;

        chordsButton.setButtonText ("Acordes");
        bassButton.setButtonText ("Bajo");
        melodyButton.setButtonText (names::utf8 ("Melod\xc3\xad" "a"));
        chordsButton.setTooltip (names::utf8 ("Los acordes de la progresi\xc3\xb3n (con su ritmo y voicing). Ap\xc3\xa1galos para escuchar solo el bajo y la melod\xc3\xad" "a."));
        bassButton.setTooltip (names::utf8 ("La l\xc3\xadnea de bajo. Suena en el modo Progresi\xc3\xb3n, con FL reproduciendo, mientras una tecla est\xc3\xa9 abajo."));
        melodyButton.setTooltip (names::utf8 ("La melod\xc3\xad" "a. Suena en el modo Progresi\xc3\xb3n, con FL reproduciendo, mientras una tecla est\xc3\xa9 abajo."));
        for (auto* button : { &chordsButton, &bassButton, &melodyButton })
        {
            button->setClickingTogglesState (true);
            styleButton (*button);
            addAndMakeVisible (*button);
        }
        chordsAttachment = std::make_unique<ButtonAttachment> (state, id::playChords, chordsButton);
        bassAttachment = std::make_unique<ButtonAttachment> (state, id::playBass, bassButton);
        melodyAttachment = std::make_unique<ButtonAttachment> (state, id::playMelody, melodyButton);

        for (int i = 0; i < th::bassStyleCount; ++i)
            bassStyle.addItem (bassStyleName (i), i + 1);
        for (int i = 0; i < th::melodyStyleCount; ++i)
            melodyStyle.addItem (melodyStyleName (i), i + 1);
        bassStyle.onChange = [this] { applyBass (false); };
        melodyStyle.onChange = [this] { applyMelody (false); };
        for (auto* box : { &bassStyle, &melodyStyle })
        {
            box->setColour (juce::ComboBox::backgroundColourId, theme::control);
            box->setColour (juce::ComboBox::outlineColourId, theme::panelOutline);
            box->setColour (juce::ComboBox::textColourId, theme::text);
            addAndMakeVisible (*box);
        }

        for (auto* slider : { &bassDensity, &melodyDensity, &variation })
        {
            slider->setSliderStyle (juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
            slider->setRange (0.0, 100.0, 1.0);
            slider->setTextValueSuffix (" %");
            styleSlider (*slider);
            addAndMakeVisible (*slider);
        }
        bassDensity.onValueChange = [this] { applyBass (false); };
        melodyDensity.onValueChange = [this] { applyMelody (false); };
        variation.onValueChange = [this] { applyVariation (false); };
        bassDensity.setTooltip (names::utf8 ("Cu\xc3\xa1ntos golpes por comp\xc3\xa1s: pocos (notas largas) o muchos (m\xc3\xa1s movimiento)."));
        melodyDensity.setTooltip (names::utf8 ("Cu\xc3\xa1ntas notas por comp\xc3\xa1s. Una melod\xc3\xad" "a con espacio deja respirar a la voz y al bajo."));
        variation.setTooltip (names::utf8 ("Cu\xc3\xa1nto se aparta la melod\xc3\xad" "a de su base: algunas notas cambian a la vecina, se parten, se unen o "
                                           "se adelantan una semicorchea. El primer comp\xc3\xa1s de cada frase cambia poco (es el que la hace reconocible) "
                                           "y los apoyos vuelven siempre a notas del acorde."));

        for (auto* slider : { &bassRange, &melodyRange })
        {
            slider->setSliderStyle (juce::Slider::TwoValueHorizontal);
            slider->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            styleSlider (*slider);
            addAndMakeVisible (*slider);
        }
        bassRange.setRange (12.0, 72.0, 1.0);
        melodyRange.setRange (36.0, 108.0, 1.0);
        bassRange.onValueChange = [this] { keepOctave (bassRange); applyBass (false); };
        melodyRange.onValueChange = [this] { keepOctave (melodyRange); applyMelody (false); };
        bassRange.setTooltip (names::utf8 ("De qu\xc3\xa9 nota a qu\xc3\xa9 nota puede ir el bajo (al menos una octava). Cada fundamental va a la octava "
                                           "m\xc3\xa1s cercana a la anterior: el bajo se mueve poco."));
        melodyRange.setTooltip (names::utf8 ("De qu\xc3\xa9 nota a qu\xc3\xa9 nota puede ir la melod\xc3\xad" "a (al menos una octava). Por encima de los acordes "
                                             "se distingue mejor; una voz c\xc3\xb3moda abarca una octava y media."));

        bassOther.setButtonText ("Otra");
        melodyOther.setButtonText ("Otra");
        variationOther.setButtonText (names::utf8 ("Otra variaci\xc3\xb3n"));
        captureButton.setButtonText ("Capturar");
        fixButton.setButtonText ("Fijar");
        bassOther.setTooltip (names::utf8 ("Otra l\xc3\xadnea con los mismos ajustes (otra semilla). La misma semilla siempre da la misma l\xc3\xadnea."));
        melodyOther.setTooltip (names::utf8 ("Otra melod\xc3\xad" "a con los mismos ajustes (otra semilla). Si la melod\xc3\xad" "a era capturada, vuelve a la generada."));
        variationOther.setTooltip (names::utf8 ("Otra variaci\xc3\xb3n de la misma base, con la misma cantidad."));
        captureButton.setTooltip (names::utf8 ("Captura una melod\xc3\xad" "a: enci\xc3\xa9ndelo, dale play en FL y toca (o deja que suene lo que escribiste en el "
                                               "Piano Roll de Tidechart). Mientras captura, el MIDI pasa tal cual. Al apagarlo, lo tocado se "
                                               "cuantiza a semicorcheas, se dobla sobre la vuelta y pasa a ser la base de las variaciones."));
        fixButton.setTooltip (names::utf8 ("La melod\xc3\xad" "a de ahora (con su variaci\xc3\xb3n) pasa a ser la base: las variaciones siguientes salen de ella."));
        bassOther.onClick = [this] { applyBass (true); };
        melodyOther.onClick = [this] { applyMelody (true); };
        variationOther.onClick = [this] { applyVariation (true); };
        captureButton.onClick = [this] { toggleCapture(); };
        fixButton.onClick = [this] {
            captureMessage = {};
            if (callbacks.fix)
                callbacks.fix();
        };
        for (auto* button : { &bassOther, &melodyOther, &variationOther, &captureButton, &fixButton })
        {
            styleButton (*button);
            addAndMakeVisible (*button);
        }
        captureButton.setColour (juce::TextButton::buttonOnColourId, theme::warning.withAlpha (0.7f));
    }

    ~LinesView() override
    {
        if (capturing && callbacks.setCapturing)
            callbacks.setCapturing (false);
    }

    // Desde el editor: las líneas del procesador (al abrir, al cargar un proyecto, tras cada cambio).
    void setLines (const Lines& newLines)
    {
        lines = newLines;
        updating = true;
        const auto& b = lines.bassSettings.generator;
        const auto& m = lines.melodySettings;
        bassStyle.setSelectedId (static_cast<int> (b.style) + 1, juce::dontSendNotification);
        melodyStyle.setSelectedId (static_cast<int> (m.generator.style) + 1, juce::dontSendNotification);
        bassDensity.setValue (std::round (b.density * 100.0), juce::dontSendNotification);
        melodyDensity.setValue (std::round (m.generator.density * 100.0), juce::dontSendNotification);
        variation.setValue (std::round (m.variation * 100.0), juce::dontSendNotification);
        bassRange.setMinAndMaxValues (b.low, b.high, juce::dontSendNotification);
        melodyRange.setMinAndMaxValues (m.generator.low, m.generator.high, juce::dontSendNotification);
        bassStyle.setTooltip (bassStyleDescription (static_cast<int> (b.style)));
        melodyStyle.setTooltip (melodyStyleDescription (static_cast<int> (m.generator.style)));
        updating = false;
        rebuildRoll();
    }

    // Desde el editor: la progresión y los ajustes (la tonalidad para los papeles de las notas; qué capas suenan).
    void setContext (const deepwater::theory::Progression& newProgression, const midi::ChordSettings& newSettings)
    {
        const bool changed = ! (newProgression == progression) || ! (newSettings.key == settings.key);
        const bool redraw = changed || newSettings.playBass != settings.playBass || newSettings.playMelody != settings.playMelody
                            || newSettings.playMode != settings.playMode || newSettings.enabled != settings.enabled;
        progression = newProgression;
        settings = newSettings;
        if (changed)
            rebuildRoll();
        else if (redraw)
            repaint();
    }

    // Desde el temporizador del editor: dónde está FL (la línea de reproducción en el piano roll).
    void update (const TransportSnapshot& transport)
    {
        const int length = std::max (lines.melody.length, lines.bass.length);
        float position = -1.0f;
        if (transport.source == deepwater::dsp::ClockSource::host && length > 0)
        {
            const double sixteenth = std::fmod (transport.ppq * deepwater::theory::sixteenthsPerQuarter, static_cast<double> (length));
            position = static_cast<float> ((sixteenth < 0.0 ? sixteenth + length : sixteenth) / length);
        }
        if (std::abs (position - playhead) > 0.0005f || transport.linesSounding != linesSounding)
        {
            playhead = position;
            linesSounding = transport.linesSounding;
            repaint (rollArea.expanded (2));
        }
    }

    // Desde el editor: cada evento que ENTRA (para capturar la melodía).
    void observe (const midi::LoggedEvent& event)
    {
        if (! capturing || event.output)
            return;
        const auto& m = event.message;
        if (m.isNoteOn())
            capture.noteOn (m.channel(), m.data1, m.data2, event.ppq, event.playing);
        else if (m.isNoteOff())
            capture.noteOff (m.channel(), m.data1, event.ppq);
        else
            return;
        if (! event.playing)
            warnStopped = true;
        refreshInfo();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop (22); // título
        auto row = area.removeFromTop (26);
        row.removeFromLeft (64);
        const int third = (row.getWidth() - 12) / 3;
        chordsButton.setBounds (row.removeFromLeft (third));
        row.removeFromLeft (6);
        bassButton.setBounds (row.removeFromLeft (third));
        row.removeFromLeft (6);
        melodyButton.setBounds (row);

        const auto layoutLine = [&area] (juce::Rectangle<int>& header, juce::ComboBox& style, juce::Button& other, juce::Slider& density,
                                         juce::Slider& range, juce::Rectangle<int>& rangeText) {
            area.removeFromTop (12);
            header = area.removeFromTop (18);
            auto r = area.removeFromTop (26);
            other.setBounds (r.removeFromRight (70));
            r.removeFromRight (6);
            style.setBounds (r);
            area.removeFromTop (4);
            r = area.removeFromTop (24);
            r.removeFromLeft (72);
            density.setBounds (r);
            area.removeFromTop (2);
            r = area.removeFromTop (24);
            r.removeFromLeft (72);
            rangeText = r.removeFromRight (150);
            range.setBounds (r);
        };
        layoutLine (bassHeader, bassStyle, bassOther, bassDensity, bassRange, bassRangeText);
        layoutLine (melodyHeader, melodyStyle, melodyOther, melodyDensity, melodyRange, melodyRangeText);
        area.removeFromTop (4);
        row = area.removeFromTop (26);
        const int half = (row.getWidth() - 6) / 2;
        captureButton.setBounds (row.removeFromLeft (half));
        row.removeFromLeft (6);
        fixButton.setBounds (row);
        area.removeFromTop (4);
        row = area.removeFromTop (24);
        row.removeFromLeft (72);
        variationOther.setBounds (row.removeFromRight (110));
        row.removeFromRight (6);
        variation.setBounds (row);

        area.removeFromTop (12);
        infoArea = area.removeFromBottom (64);
        area.removeFromBottom (4);
        legendArea = area.removeFromBottom (16);
        area.removeFromBottom (4);
        rollArea = area;
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (names::utf8 ("MELOD\xc3\x8d" "A Y BAJO"), getLocalBounds().removeFromTop (22), juce::Justification::centredLeft);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Suenan:", chordsButton.getBounds().withX (0).withWidth (60), juce::Justification::centredLeft);

        const auto label = [&g] (juce::Rectangle<int> row, const char* text) {
            g.drawText (names::utf8 (text), row.withX (0).withWidth (70), juce::Justification::centredLeft);
        };
        for (const auto& [header, text] : { std::pair { bassHeader, "BAJO" }, std::pair { melodyHeader, "MELOD\xc3\x8d" "A" } })
        {
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (names::utf8 (text), header, juce::Justification::centredLeft);
            g.setColour (theme::panelOutline);
            g.fillRect (header.withTrimmedLeft (70).withHeight (1).withY (header.getCentreY()));
        }
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.0f));
        label (bassDensity.getBounds(), "Densidad");
        label (melodyDensity.getBounds(), "Densidad");
        label (bassRange.getBounds(), "Rango");
        label (melodyRange.getBounds(), "Rango");
        label (variation.getBounds(), "Variaci\xc3\xb3n");
        g.setColour (theme::text);
        g.drawText (rangeText (lines.bassSettings.generator.low, lines.bassSettings.generator.high, true), bassRangeText,
                    juce::Justification::centredRight);
        g.drawText (rangeText (lines.melodySettings.generator.low, lines.melodySettings.generator.high, false), melodyRangeText,
                    juce::Justification::centredRight);

        drawRoll (g);
        drawLegend (g);

        g.setFont (juce::FontOptions (11.5f));
        auto info = infoArea;
        for (int i = 0; i < static_cast<int> (infoLines.size()); ++i)
        {
            g.setColour (i == 0 && capturing ? theme::warning : (i == 0 ? theme::text : theme::dimText));
            g.drawFittedText (infoLines[static_cast<std::size_t> (i)], info.removeFromTop (i == 1 ? 30 : 16), juce::Justification::topLeft,
                              i == 1 ? 2 : 1);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int index = noteAt (e.getPosition());
        if (index != hovered)
        {
            hovered = index;
            refreshInfo();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hovered >= 0)
        {
            hovered = -1;
            refreshInfo();
        }
    }

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // Una nota del piano roll ya calculada: dónde va y qué papel tiene sobre su acorde.
    struct RollNote
    {
        bool bass = false;
        deepwater::theory::LineNote note;
        deepwater::theory::NoteRole role = deepwater::theory::NoteRole::chordTone;
        juce::String description;
    };

    static void styleButton (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, theme::control);
        button.setColour (juce::TextButton::buttonOnColourId, theme::accent.withAlpha (0.55f));
        button.setColour (juce::TextButton::textColourOffId, theme::text);
        button.setColour (juce::TextButton::textColourOnId, theme::text);
    }

    static void styleSlider (juce::Slider& slider)
    {
        slider.setColour (juce::Slider::backgroundColourId, theme::track);
        slider.setColour (juce::Slider::trackColourId, theme::accent.withAlpha (0.7f));
        slider.setColour (juce::Slider::thumbColourId, theme::text);
        slider.setColour (juce::Slider::textBoxTextColourId, theme::text);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    static juce::String bassStyleName (int index)
    {
        static constexpr std::array<const char*, deepwater::theory::bassStyleCount> text {
            "Fundamental", "Dembow (3+3+2)", "Octavas", "Caminante (walking)", "Pedal de t\xc3\xb3nica",
        };
        return names::utf8 (text[static_cast<std::size_t> (index)]);
    }

    static juce::String bassStyleDescription (int index)
    {
        static constexpr std::array<const char*, deepwater::theory::bassStyleCount> text {
            "La nota que da nombre a cada acorde, larga (m\xc3\xa1s densidad = blancas o negras): la base m\xc3\xa1s s\xc3\xb3lida.",
            "Fundamental en el 3+3 y, en el 2, la 5.\xc2\xaa, la octava o una nota que lleva al acorde siguiente: el bajo del reggaet\xc3\xb3n.",
            "Fundamental y su octava alternadas: el bajo del disco y del house, mucho movimiento sin cambiar de nota.",
            "Un paso por tiempo por las notas del acorde y una aproximaci\xc3\xb3n (un semitono o un grado) justo antes de cada cambio.",
            "El bajo se queda en la t\xc3\xb3nica y los acordes cambian encima: tensi\xc3\xb3n que no se resuelve, el sonido del perreo oscuro.",
        };
        return names::utf8 (text[static_cast<std::size_t> (index)]);
    }

    static juce::String melodyStyleName (int index)
    {
        static constexpr std::array<const char*, deepwater::theory::melodyStyleCount> text {
            "Gancho (reggaet\xc3\xb3n)", "Cantada", "Arpegio", "R&B",
        };
        return names::utf8 (text[static_cast<std::size_t> (index)]);
    }

    static juce::String melodyStyleDescription (int index)
    {
        static constexpr std::array<const char*, deepwater::theory::melodyStyleCount> text {
            "Gancho: notas cortas y repetidas sobre el 3+3+2 del dembow, pocas notas distintas y saltos chicos, como un coro de reggaet\xc3\xb3n.",
            "Cantada: notas largas en los tiempos, por grados conjuntos (notas vecinas de la escala): una voz lenta o un lead mel\xc3\xb3" "dico.",
            "Arpegio: las notas del acorde una tras otra en una cuadr\xc3\xad" "cula fija (negras, corcheas o semicorcheas seg\xc3\xban la densidad).",
            "R&B: s\xc3\xadncopas y anticipaciones; en los apoyos admite tensiones (la 9.\xc2\xaa, la 11.\xc2\xaa) que le dan el color del neo-soul.",
        };
        return names::utf8 (text[static_cast<std::size_t> (index)]);
    }

    // "E2 (41 Hz) – D4 (147 Hz)" para el bajo (en el grave, los hercios dicen más que el nombre), "E5 – A6" para la melodía.
    static juce::String rangeText (int low, int high, bool withHz)
    {
        const auto name = [withHz] (int note) {
            auto text = juce::String::fromUTF8 (deepwater::theory::midiNoteName (note, deepwater::theory::Notation { true, 5 }, false).c_str());
            if (withHz)
                text << " (" << juce::roundToInt (440.0 * std::pow (2.0, (note - 69) / 12.0)) << " Hz)";
            return text;
        };
        return name (std::min (low, high)) + names::utf8 (" \xe2\x80\x93 ") + name (std::max (low, high));
    }

    // Un rango de al menos una octava: si se acerca más, el otro extremo se aparta.
    void keepOctave (juce::Slider& range)
    {
        if (updating)
            return;
        auto low = static_cast<int> (range.getMinValue()), high = static_cast<int> (range.getMaxValue());
        if (high - low >= 12)
            return;
        const int minimum = static_cast<int> (range.getMinimum()), maximum = static_cast<int> (range.getMaximum());
        if (range.getThumbBeingDragged() == 1) // se arrastra el de abajo: el de arriba sube
        {
            high = std::min (maximum, low + 12);
            low = high - 12;
        }
        else
        {
            low = std::max (minimum, high - 12);
            high = low + 12;
        }
        range.setMinAndMaxValues (low, high, juce::dontSendNotification);
    }

    void applyBass (bool newSeed)
    {
        if (updating || ! callbacks.setBass)
            return;
        auto s = lines.bassSettings;
        s.generator.style = static_cast<deepwater::theory::BassStyle> (std::max (0, bassStyle.getSelectedId() - 1));
        s.generator.density = bassDensity.getValue() / 100.0;
        s.generator.low = static_cast<int> (bassRange.getMinValue());
        s.generator.high = static_cast<int> (bassRange.getMaxValue());
        if (newSeed)
            ++s.generator.seed;
        captureMessage = {};
        callbacks.setBass (s);
    }

    // Tocar el generador = volver a la melodía generada (si era capturada o fijada).
    void applyMelody (bool newSeed)
    {
        if (updating || ! callbacks.setMelody)
            return;
        auto s = lines.melodySettings;
        s.generator.style = static_cast<deepwater::theory::MelodyStyle> (std::max (0, melodyStyle.getSelectedId() - 1));
        s.generator.density = melodyDensity.getValue() / 100.0;
        s.generator.low = static_cast<int> (melodyRange.getMinValue());
        s.generator.high = static_cast<int> (melodyRange.getMaxValue());
        if (newSeed)
            ++s.generator.seed;
        s.hasSource = false;
        captureMessage = {};
        callbacks.setMelody (s);
    }

    // Otra variación con cantidad 0 no haría nada: pone un 30 %.
    void applyVariation (bool newSeed)
    {
        if (updating || ! callbacks.setMelody)
            return;
        auto s = lines.melodySettings;
        s.variation = variation.getValue() / 100.0;
        if (newSeed)
        {
            ++s.variationSeed;
            if (s.variation <= 0.0)
                s.variation = 0.3;
        }
        captureMessage = {};
        callbacks.setMelody (s);
    }

    void toggleCapture()
    {
        capturing = ! capturing;
        captureButton.setToggleState (capturing, juce::dontSendNotification);
        captureButton.setButtonText (capturing ? names::utf8 ("\xe2\x97\x8f Capturando\xe2\x80\xa6 (clic = terminar)") : names::utf8 ("Capturar"));
        if (capturing)
        {
            capture.clear();
            warnStopped = false;
            captureMessage = {};
        }
        if (callbacks.setCapturing)
            callbacks.setCapturing (capturing);
        if (! capturing)
        {
            const auto line = capture.toLine (deepwater::theory::lineLength (progression));
            if (line.notes.empty())
                captureMessage = names::utf8 ("No lleg\xc3\xb3 ninguna nota con FL reproduciendo: la melod\xc3\xad" "a no cambi\xc3\xb3.");
            else
            {
                captureMessage = names::utf8 ("Capturadas ") + juce::String (static_cast<int> (line.notes.size()))
                                 + names::utf8 (" notas: son la base de las variaciones.");
                if (callbacks.captured)
                    callbacks.captured (line);
            }
        }
        refreshInfo();
    }

    // Recalcula las notas del piano roll (posición y papel) con la tonalidad y la progresión de ahora.
    void rebuildRoll()
    {
        namespace th = deepwater::theory;
        rollNotes.clear();
        for (const bool bass : { true, false })
        {
            const auto& line = bass ? lines.bass : lines.melody;
            for (const auto& n : line.notes)
            {
                RollNote r { bass, n, th::NoteRole::chordTone, {} };
                const auto chord = th::chordAt (progression, settings.key, n.start);
                if (chord)
                {
                    r.role = th::noteRole (n.note, *chord, settings.key);
                    r.description = describe (n, *chord, r.role, bass);
                }
                rollNotes.push_back (r);
            }
        }
        hovered = -1;
        refreshInfo();
    }

    // "Melodía · E6 · 5 de Am · nota del acorde, en un apoyo (compás 2, tiempo 1)".
    juce::String describe (const deepwater::theory::LineNote& n, const deepwater::theory::Chord& chord, deepwater::theory::NoteRole role,
                           bool bass) const
    {
        namespace th = deepwater::theory;
        const th::Notation notation { true, 5 };
        const auto u = [] (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); };
        auto interval = th::intervalBetween (chord.root, th::spellInKey (n.note, settings.key));
        if (role != th::NoteRole::chordTone && (interval.number == 2 || interval.number == 4 || interval.number == 6))
            interval.number += 7; // fuera del acorde, la 2.ª, la 4.ª y la 6.ª se nombran como tensiones: 9, 11, 13
        const bool strong = n.start % 8 == 0 || th::chordChangesAt (progression, n.start);
        juce::String text;
        text << (bass ? "Bajo" : names::utf8 ("Melod\xc3\xad" "a")) << names::utf8 (" \xc2\xb7 ") << u (th::midiNoteName (n.note, settings.key, notation))
             << names::utf8 (" \xc2\xb7 ") << u (th::degreeLabel (interval, notation)) << " de " << u (th::chordSymbol (chord, notation))
             << names::utf8 (" \xc2\xb7 ");
        switch (role)
        {
            case th::NoteRole::chordTone: text << (strong ? names::utf8 ("nota del acorde en un apoyo: estable") : names::utf8 ("nota del acorde")); break;
            case th::NoteRole::scaleTone:
                text << (strong ? names::utf8 ("tensi\xc3\xb3n en un apoyo: color (pide resolver a una vecina)")
                                : names::utf8 ("de paso o de adorno: une las notas del acorde"));
                break;
            case th::NoteRole::chromatic: text << names::utf8 ("crom\xc3\xa1tica: una aproximaci\xc3\xb3n desde fuera de la escala"); break;
        }
        text << names::utf8 ("  (comp\xc3\xa1s ") << (n.start / 16 + 1) << ", tiempo " << (n.start % 16) / 4 + 1;
        if (n.start % 4 != 0)
            text << (n.start % 4 == 2 ? " y" : names::utf8 (" semicorchea ") + juce::String (n.start % 4 + 1));
        text << ")";
        return text;
    }

    void refreshInfo()
    {
        namespace th = deepwater::theory;
        infoLines.clear();
        const auto& m = lines.melodySettings;
        juce::String state;
        if (capturing)
        {
            state << names::utf8 ("Capturando: ") << capture.count() << " notas. "
                  << (warnStopped ? names::utf8 ("\xc2\xa1" "Dale play en FL! Con FL parado no se sabe d\xc3\xb3nde va cada nota.")
                                  : names::utf8 ("Dale play en FL y toca (o deja sonar el Piano Roll de Tidechart)."));
        }
        else if (captureMessage.isNotEmpty())
            state = captureMessage;
        else
        {
            state << names::utf8 ("Melod\xc3\xad" "a ") << (m.hasSource ? names::utf8 ("fija (capturada o fijada)")
                                                              : names::utf8 ("generada: ") + melodyStyleName (static_cast<int> (m.generator.style))
                                                                    + names::utf8 (", n.\xc2\xba ") + juce::String (m.generator.seed));
            if (m.variation > 0.0)
                state << names::utf8 (" + variaci\xc3\xb3n ") << juce::roundToInt (m.variation * 100.0) << names::utf8 (" % (n.\xc2\xba ") << m.variationSeed << ")";
            state << names::utf8 (" \xc2\xb7 ") << static_cast<int> (lines.melody.notes.size()) << " notas en " << lines.melody.length / 16
                  << (lines.melody.length / 16 == 1 ? names::utf8 (" comp\xc3\xa1s") : names::utf8 (" compases"));
        }
        infoLines.add (state);
        if (settings.playMode != midi::PlayMode::progression)
            infoLines.add (names::utf8 ("El bajo y la melod\xc3\xad" "a suenan en el modo Progresi\xc3\xb3n (panel PROGRESI\xc3\x93N), con FL reproduciendo y una tecla abajo."));
        else
            infoLines.add (melodyStyleDescription (static_cast<int> (m.generator.style)));
        infoLines.add (hovered >= 0 && hovered < static_cast<int> (rollNotes.size()) ? rollNotes[static_cast<std::size_t> (hovered)].description
                                                                                   : names::utf8 ("Pasa el rat\xc3\xb3n por una nota para ver su papel sobre el acorde."));
        repaint();
    }

    // El área de la melodía (arriba) y la del bajo (abajo) dentro del piano roll, debajo de la fila de los acordes.
    [[nodiscard]] juce::Rectangle<float> laneArea (bool bass) const
    {
        auto area = rollArea.toFloat().withTrimmedTop (18.0f);
        const float melodyHeight = area.getHeight() * 0.6f;
        return bass ? area.withTrimmedTop (melodyHeight + 4.0f) : area.withHeight (melodyHeight);
    }

    // La franja de notas que cubre cada carril: el rango elegido, ampliado si alguna nota se sale (una melodía capturada).
    [[nodiscard]] std::pair<int, int> laneRange (bool bass) const
    {
        const auto& line = bass ? lines.bass : lines.melody;
        int low = bass ? lines.bassSettings.generator.low : lines.melodySettings.generator.low;
        int high = bass ? lines.bassSettings.generator.high : lines.melodySettings.generator.high;
        if (low > high)
            std::swap (low, high);
        for (const auto& n : line.notes)
        {
            low = std::min (low, n.note);
            high = std::max (high, n.note);
        }
        return { low - 1, high + 1 };
    }

    [[nodiscard]] juce::Rectangle<float> noteBounds (const RollNote& r) const
    {
        const auto& line = r.bass ? lines.bass : lines.melody;
        const auto lane = laneArea (r.bass);
        const auto [low, high] = laneRange (r.bass);
        const float rowHeight = lane.getHeight() / static_cast<float> (high - low + 1);
        const float x = lane.getX() + lane.getWidth() * static_cast<float> (r.note.start) / static_cast<float> (std::max (1, line.length));
        const float w = lane.getWidth() * static_cast<float> (r.note.length) / static_cast<float> (std::max (1, line.length));
        const float y = lane.getBottom() - static_cast<float> (r.note.note - low + 1) * rowHeight;
        return { x, y, std::max (2.0f, w - 1.0f), std::max (3.0f, rowHeight) };
    }

    [[nodiscard]] int noteAt (juce::Point<int> point) const
    {
        for (int i = static_cast<int> (rollNotes.size()) - 1; i >= 0; --i)
            if (noteBounds (rollNotes[static_cast<std::size_t> (i)]).expanded (0.0f, 1.5f).contains (point.toFloat()))
                return i;
        return -1;
    }

    static juce::Colour roleColour (deepwater::theory::NoteRole role)
    {
        switch (role)
        {
            case deepwater::theory::NoteRole::chordTone: return theme::accent;
            case deepwater::theory::NoteRole::scaleTone: return theme::input;
            case deepwater::theory::NoteRole::chromatic: return theme::freeRunning;
        }
        return theme::text;
    }

    void drawRoll (juce::Graphics& g)
    {
        namespace th = deepwater::theory;
        const auto area = rollArea.toFloat();
        g.setColour (theme::display);
        g.fillRoundedRectangle (area, 4.0f);
        const int length = std::max (lines.melody.length, lines.bass.length);
        if (length <= 0 || progression.steps.empty())
        {
            g.setColour (theme::dimText);
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (names::utf8 ("Sin progresi\xc3\xb3n no hay l\xc3\xadneas: arma una en el panel PROGRESI\xc3\x93N."), rollArea, juce::Justification::centred);
            return;
        }
        const auto xOf = [&] (int sixteenth) { return area.getX() + area.getWidth() * static_cast<float> (sixteenth) / static_cast<float> (length); };

        // Los acordes: una franja por paso (en todas las vueltas de la progresión que caben en la de la línea).
        const int loop = progression.totalBeats() * th::sixteenthsPerQuarter;
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        for (int base = 0; base < length && loop > 0; base += loop)
            for (int i = 0; i < static_cast<int> (progression.steps.size()); ++i)
            {
                const int start = base + progression.stepStart (i);
                if (start >= length)
                    break;
                const int end = std::min (length, base + progression.stepStart (i + 1));
                const auto& numeral = progression.steps[static_cast<std::size_t> (i)].numeral;
                const auto colour = names::functionColour (th::harmonicFunction (numeral));
                const auto strip = juce::Rectangle<float> (xOf (start), area.getY(), xOf (end) - xOf (start), area.getHeight());
                g.setColour (colour.withAlpha (i % 2 == 0 ? 0.05f : 0.09f));
                g.fillRect (strip);
                g.setColour (colour.withAlpha (0.8f));
                g.drawText (juce::String::fromUTF8 (th::romanNumeralText (numeral, { true, 5 }).c_str()), strip.withHeight (16.0f).reduced (3.0f, 0.0f),
                            juce::Justification::centredLeft, true);
                g.setColour (theme::panelOutline);
                g.fillRect (strip.getX(), area.getY(), 1.0f, area.getHeight());
            }
        // Compases y tiempos.
        for (int s = 0; s < length; s += 4)
        {
            g.setColour (s % 16 == 0 ? theme::dimText.withAlpha (0.35f) : theme::panelOutline.withAlpha (0.5f));
            g.fillRect (xOf (s), area.getY() + 18.0f, 1.0f, area.getHeight() - 18.0f);
        }
        // Separación entre melodía y bajo, con su nombre.
        for (const bool bass : { false, true })
        {
            const auto lane = laneArea (bass);
            const bool on = bass ? settings.playBass : settings.playMelody;
            g.setColour (theme::dimText.withAlpha (on ? 0.8f : 0.4f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (bass ? "BAJO" : names::utf8 ("MELOD\xc3\x8d" "A"), lane.reduced (4.0f, 2.0f).toNearestInt(), juce::Justification::topRight);
            if (bass)
            {
                g.setColour (theme::panelOutline);
                g.fillRect (area.getX(), lane.getY() - 2.0f, area.getWidth(), 1.0f);
            }
        }
        // Las notas.
        for (int i = 0; i < static_cast<int> (rollNotes.size()); ++i)
        {
            const auto& r = rollNotes[static_cast<std::size_t> (i)];
            const bool on = r.bass ? settings.playBass : settings.playMelody;
            const auto colour = roleColour (r.role);
            const auto bounds = noteBounds (r);
            g.setColour (colour.withAlpha (on ? 0.85f : 0.35f));
            g.fillRoundedRectangle (bounds, 1.5f);
            if (i == hovered)
            {
                g.setColour (theme::text);
                g.drawRoundedRectangle (bounds.expanded (1.0f), 2.0f, 1.2f);
            }
        }
        // Dónde está FL.
        if (playhead >= 0.0f)
        {
            g.setColour (linesSounding != 0 ? theme::text : theme::dimText);
            g.fillRect (area.getX() + area.getWidth() * playhead, area.getY(), 1.5f, area.getHeight());
        }
    }

    void drawLegend (juce::Graphics& g)
    {
        auto area = legendArea;
        const juce::Font font (juce::FontOptions (10.5f));
        g.setFont (font);
        for (const auto& [role, text] : { std::pair { deepwater::theory::NoteRole::chordTone, "nota del acorde (apoyo)" },
                                          std::pair { deepwater::theory::NoteRole::scaleTone, "de la escala (paso, adorno, tensi\xc3\xb3n)" },
                                          std::pair { deepwater::theory::NoteRole::chromatic, "crom\xc3\xa1tica" } })
        {
            g.setColour (roleColour (role));
            g.fillRoundedRectangle (area.removeFromLeft (10).withSizeKeepingCentre (10, 8).toFloat(), 1.5f);
            area.removeFromLeft (4);
            g.setColour (theme::dimText);
            const auto label = names::utf8 (text);
            const int width = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, label)) + 14;
            g.drawText (label, area.removeFromLeft (width), juce::Justification::centredLeft);
        }
    }

    Callbacks callbacks;
    juce::TextButton chordsButton, bassButton, melodyButton;
    std::unique_ptr<ButtonAttachment> chordsAttachment, bassAttachment, melodyAttachment;
    juce::ComboBox bassStyle, melodyStyle;
    juce::Slider bassDensity, melodyDensity, bassRange, melodyRange, variation;
    juce::TextButton bassOther, melodyOther, variationOther, captureButton, fixButton;

    Lines lines;
    deepwater::theory::Progression progression;
    midi::ChordSettings settings;
    std::vector<RollNote> rollNotes;
    MelodyCapture capture;
    bool capturing = false, warnStopped = false, updating = false;
    juce::String captureMessage;
    juce::StringArray infoLines;
    int hovered = -1;
    float playhead = -1.0f;
    std::uint8_t linesSounding = 0;
    juce::Rectangle<int> bassHeader, melodyHeader, bassRangeText, melodyRangeText, rollArea, legendArea, infoArea;
};

} // namespace tidechart::gui
