#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "deepwater/dsp/SpectrumAnalyzer.h"
#include "gui/DisplayState.h"
#include "gui/Format.h"
#include "gui/Theme.h"

namespace riptide::gui
{

// Analizador de espectro antes/después, con las bandas encima.
//
//  - ANTES (gris) = lo que entra al plugin; DESPUÉS (acento, relleno) = lo que sale. Lo que aparece en DESPUÉS y no en
//    ANTES lo creó la saturación: armónicos sobre cada nota, "fizz" en agudos, aliasing (picos que no caen en
//    múltiplos de la nota). Los dos se miden igual: un seno de 0 dBFS marca 0 dB en su frecuencia.
//  - Eje horizontal logarítmico (20 Hz - 20 kHz): cada octava ocupa lo mismo, como la oye el oído.
//  - Balística: cada punto sube casi al instante y baja a 40 dB/s, para que la música se pueda leer sin que tiemble.
//  - Con 2 o más bandas: cada zona se tiñe del color de su banda (con su número, modo y drive arriba), las bandas que no
//    se oyen (mute, o fuera del solo) se oscurecen y cada cruce es una línea que se arrastra con el mouse (doble clic =
//    su valor por defecto). La línea está donde suena el cruce (el motor los separa al menos media octava).
//  - Al pasar el mouse, arriba se lee la frecuencia, la nota más cercana (nombres de FL) y el nivel en ese punto.
//
// El análisis (FFT) corre aquí, en el hilo de mensajes; el audio solo copia muestras en las tomas del procesador.
class SpectrumView final : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr float minHz = 20.0f;
    static constexpr float maxHz = 20000.0f;
    static constexpr float topDb = 6.0f;
    static constexpr float bottomDb = -90.0f;
    static constexpr float fallDbPerSecond = 40.0f;
    static constexpr double staleSeconds = 0.25; // sin audio nuevo durante este tiempo, el espectro cae a silencio

    explicit SpectrumView (RiptideAudioProcessor& p) : processor (p)
    {
        for (size_t i = 0; i < crossoverParameters.size(); ++i)
            crossoverParameters[i] = processor.getParameters().getParameter (params::crossover[i]);

        setUpToggle (beforeButton, theme::spectrumBefore,
                     juce::String::fromUTF8 ("Ver el espectro de lo que ENTRA al plugin (gris). Comp\xc3\xa1ralo con "
                                             "DESPU\xc3\x89S: lo que aparece solo en la salida lo cre\xc3\xb3 la saturaci\xc3\xb3n."));
        setUpToggle (afterButton, theme::accent,
                     juce::String::fromUTF8 ("Ver el espectro de lo que SALE del plugin (relleno). Con el volumen "
                                             "igualado (AUTO GAIN) la comparaci\xc3\xb3n es justa: si la salida es m\xc3\xa1s "
                                             "fuerte, todo se ve m\xc3\xa1s arriba."));
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
    }

    // Una vez por cuadro (lo llama el editor).
    void update (const DisplayState& newState, double elapsedSeconds)
    {
        state = newState;
        const double sampleRate = processor.getAnalyzerSampleRate();
        const int order = deepwater::dsp::SpectrumAnalyzer::orderForSampleRate (sampleRate);
        if (order != before.analyzer.getOrder())
        {
            // Fuera del audio: aquí sí se puede reservar memoria.
            for (auto* trace : { &before, &after })
                trace->analyzer.prepare (order);
            scratch.assign (static_cast<size_t> (before.analyzer.getSize()), 0.0f);
        }
        // Fase 10: solo se repinta si algo cambió de forma visible (las bandas o algún punto del espectro). Sin audio, las
        // curvas terminan de caer al piso y el visor queda quieto.
        bool changed = ! (newState == paintedState);
        changed = updateTrace (before, processor.getInputTap(), sampleRate, elapsedSeconds) || changed;
        changed = updateTrace (after, processor.getOutputTap(), sampleRate, elapsedSeconds) || changed;
        if (changed)
        {
            paintedState = newState;
            repaint();
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().toFloat().reduced (10.0f, 8.0f);
        auto title = area.removeFromTop (22.0f);
        afterButton.setBounds (title.removeFromRight (78.0f).reduced (0.0f, 2.0f).toNearestInt());
        title.removeFromRight (6.0f);
        beforeButton.setBounds (title.removeFromRight (66.0f).reduced (0.0f, 2.0f).toNearestInt());
        titleArea = title;
        area.removeFromTop (4.0f);
        area.removeFromBottom (14.0f); // etiquetas de frecuencia
        plot = area;

        // Un punto cada 2 píxeles.
        const auto points = static_cast<size_t> (plot.getWidth() / 2.0f) + 1;
        for (auto* trace : { &before, &after })
            trace->shown.assign (points, bottomDb - 10.0f);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("ESPECTRO", titleArea, juce::Justification::centredLeft);
        paintReadout (g);

        g.setColour (theme::display);
        g.fillRoundedRectangle (plot, 4.0f);
        paintGrid (g);
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot.toNearestInt());
            paintBands (g);
            if (beforeButton.getToggleState())
            {
                g.setColour (theme::spectrumBefore.withAlpha (0.75f));
                g.strokePath (tracePath (before.shown, false), juce::PathStrokeType (1.2f));
            }
            if (afterButton.getToggleState())
            {
                g.setGradientFill (juce::ColourGradient (theme::accent.withAlpha (0.32f), 0.0f, plot.getY(),
                                                         theme::accent.withAlpha (0.03f), 0.0f, plot.getBottom(), false));
                g.fillPath (tracePath (after.shown, true));
                g.setColour (theme::accent);
                g.strokePath (tracePath (after.shown, false), juce::PathStrokeType (1.5f));
            }
            paintCrossovers (g);
        }
    }

    juce::String getTooltip() override
    {
        const auto position = getMouseXYRelative().toFloat();
        if (crossoverAt (position.x, position.y) >= 0)
            return juce::String::fromUTF8 ("Cruce entre dos bandas: arr\xc3\xa1stralo para moverlo (doble clic = su valor "
                                           "por defecto). Es la misma perilla que la de abajo.");
        if (titleArea.contains (position))
            return juce::String::fromUTF8 ("Espectro: cu\xc3\xa1nto nivel hay en cada frecuencia (graves a la izquierda, "
                                           "agudos a la derecha; cada octava ocupa lo mismo). Gris = lo que entra; "
                                           "relleno = lo que sale. Lo que aparece solo en la salida lo cre\xc3\xb3 la "
                                           "saturaci\xc3\xb3n: los arm\xc3\xb3nicos. Pasa el mouse para leer la "
                                           "frecuencia y la nota.");
        return {};
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        mouse = event.position;
        const int crossover = crossoverAt (mouse.x, mouse.y);
        if (crossover != hovered)
        {
            hovered = crossover;
            setMouseCursor (hovered >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::CrosshairCursor);
        }
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        mouse = { -1.0f, -1.0f };
        hovered = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        dragging = crossoverAt (event.position.x, event.position.y);
        if (dragging >= 0)
            crossoverParameters[static_cast<size_t> (dragging)]->beginChangeGesture();
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        mouse = event.position;
        if (dragging < 0)
            return;
        auto* parameter = crossoverParameters[static_cast<size_t> (dragging)];
        const float hz = juce::jlimit (dsp::crossoverMinHz, dsp::crossoverMaxHz, xToHz (event.position.x));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (hz));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging >= 0)
            crossoverParameters[static_cast<size_t> (dragging)]->endChangeGesture();
        dragging = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        const int crossover = crossoverAt (event.position.x, event.position.y);
        if (crossover < 0)
            return;
        auto* parameter = crossoverParameters[static_cast<size_t> (crossover)];
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getDefaultValue());
        parameter->endChangeGesture();
    }

private:
    struct Trace
    {
        deepwater::dsp::SpectrumAnalyzer analyzer;
        std::vector<float> shown; // lo que se dibuja, en dB, un valor por punto
        std::uint64_t lastWritten = 0;
        double secondsWithoutAudio = 0.0;
    };

    void setUpToggle (juce::TextButton& button, juce::Colour colour, const juce::String& tooltip)
    {
        button.setClickingTogglesState (true);
        button.setToggleState (true, juce::dontSendNotification);
        button.setColour (juce::TextButton::buttonOnColourId, colour.withAlpha (0.35f));
        button.setTooltip (tooltip);
        button.onClick = [this] { repaint(); };
        addAndMakeVisible (button);
    }

    // Frecuencia ↔ posición: logarítmica, cada octava ocupa lo mismo.
    float hzToX (float hz) const noexcept
    {
        return plot.getX() + plot.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz);
    }
    float xToHz (float x) const noexcept
    {
        return minHz * std::pow (maxHz / minHz, (x - plot.getX()) / plot.getWidth());
    }
    float dbToY (float db) const noexcept
    {
        return plot.getY() + plot.getHeight() * (topDb - db) / (topDb - bottomDb);
    }

    // Devuelve true si algún punto se movió de forma visible (0.05 dB: menos de un píxel).
    bool updateTrace (Trace& trace, const RiptideAudioProcessor::AnalyzerTap& tap, double sampleRate, double elapsed)
    {
        const auto size = static_cast<size_t> (trace.analyzer.getSize());
        const auto written = tap.getWritten();
        if (written != trace.lastWritten && tap.copyLatest (scratch.data(), size))
        {
            trace.lastWritten = written;
            trace.secondsWithoutAudio = 0.0;
            trace.analyzer.analyze (scratch.data());
        }
        else
        {
            trace.secondsWithoutAudio += elapsed;
        }
        const bool silent = trace.secondsWithoutAudio > staleSeconds; // el host dejó de mandar audio

        // Cada punto cubre su tramo de frecuencias (entre la mitad de su píxel y el siguiente).
        const auto points = trace.shown.size();
        if (points < 2)
            return false;
        const float fall = static_cast<float> (fallDbPerSecond * elapsed);
        const double ratio = std::pow (static_cast<double> (maxHz / minHz), 1.0 / static_cast<double> (points - 1));
        const double halfStep = std::sqrt (ratio);
        bool changed = false;
        for (size_t i = 0; i < points; ++i)
        {
            const double centre = minHz * std::pow (ratio, static_cast<double> (i));
            const float level = silent ? deepwater::dsp::SpectrumAnalyzer::floorDb
                                       : trace.analyzer.levelDb (centre / halfStep, centre * halfStep, sampleRate);
            float& shown = trace.shown[i];
            const float previous = shown;
            shown = level > shown ? shown + (level - shown) * 0.7f : juce::jmax (level, shown - fall);
            changed = changed || std::abs (shown - previous) >= 0.05f;
        }
        return changed;
    }

    juce::Path tracePath (const std::vector<float>& levels, bool closed) const
    {
        juce::Path path;
        const auto points = levels.size();
        if (points < 2)
            return path;
        const float step = plot.getWidth() / static_cast<float> (points - 1);
        const float floorY = plot.getBottom() + 2.0f;
        for (size_t i = 0; i < points; ++i)
        {
            const float x = plot.getX() + step * static_cast<float> (i);
            const float y = juce::jmin (floorY, dbToY (levels[i]));
            if (i == 0)
            {
                if (closed)
                {
                    path.startNewSubPath (x, floorY);
                    path.lineTo (x, y);
                }
                else
                {
                    path.startNewSubPath (x, y);
                }
            }
            else
            {
                path.lineTo (x, y);
            }
        }
        if (closed)
        {
            path.lineTo (plot.getRight(), floorY);
            path.closeSubPath();
        }
        return path;
    }

    void paintGrid (juce::Graphics& g) const
    {
        // Frecuencias: líneas en 20, 30, ..., 90, 100, 200, ... (más claras en 100, 1k y 10k); etiquetas debajo.
        for (float decade = 10.0f; decade <= 10000.0f; decade *= 10.0f)
            for (int i = 1; i <= 9; ++i)
            {
                const float hz = decade * static_cast<float> (i);
                if (hz < minHz || hz > maxHz)
                    continue;
                g.setColour (i == 1 ? theme::panelOutline : theme::grid);
                g.drawVerticalLine (juce::roundToInt (hzToX (hz)), plot.getY(), plot.getBottom());
            }
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (theme::dimText);
        for (const float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const auto text = hz < 1000.0f ? juce::String (juce::roundToInt (hz)) : juce::String (juce::roundToInt (hz / 1000.0f)) + "k";
            g.drawText (text, juce::Rectangle<float> (hzToX (hz) - 20.0f, plot.getBottom() + 1.0f, 40.0f, 13.0f), juce::Justification::centred);
        }

        // Niveles: cada 12 dB, con el número dentro del visor a la izquierda.
        for (float db = 0.0f; db > bottomDb; db -= 12.0f)
        {
            const float y = dbToY (db);
            g.setColour (db == 0.0f ? theme::panelOutline : theme::grid);
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (theme::dimText.withAlpha (0.7f));
            g.drawText (juce::String (juce::roundToInt (db)), juce::Rectangle<float> (plot.getX() + 3.0f, y - 12.0f, 30.0f, 11.0f),
                        juce::Justification::centredLeft);
        }
    }

    // La posición (x) de cada cruce: donde suena, salvo el que se está arrastrando, que sigue a su perilla.
    float crossoverX (int index) const noexcept
    {
        const auto i = static_cast<size_t> (index);
        const float hz = index == dragging ? crossoverParameters[i]->convertFrom0to1 (crossoverParameters[i]->getValue())
                                           : state.crossoverHz[i];
        return hzToX (hz);
    }

    int crossoverAt (float x, float y) const noexcept
    {
        if (! plot.expanded (0.0f, 4.0f).contains (x, y))
            return -1;
        int nearest = -1;
        float distance = 7.0f; // píxeles de tolerancia
        for (int i = 0; i < state.numBands - 1; ++i)
            if (const float d = std::abs (x - crossoverX (i)); d < distance)
            {
                distance = d;
                nearest = i;
            }
        return nearest;
    }

    void paintBands (juce::Graphics& g) const
    {
        if (state.numBands < 2)
            return;
        for (int b = 0; b < state.numBands; ++b)
        {
            const auto index = static_cast<size_t> (b);
            const float left = b == 0 ? plot.getX() : crossoverX (b - 1);
            const float right = b == state.numBands - 1 ? plot.getRight() : crossoverX (b);
            const auto zone = juce::Rectangle<float> (left, plot.getY(), juce::jmax (0.0f, right - left), plot.getHeight());
            const auto& band = state.bands[index];
            const auto colour = theme::bands[index];

            g.setColour (colour.withAlpha (0.07f));
            g.fillRect (zone);
            if (! band.audible)
            {
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillRect (zone);
            }

            // Etiqueta arriba: "2  Tube  +6.0 dB" si cabe, si no solo el número.
            auto label = zone.reduced (4.0f, 0.0f).withHeight (16.0f).translated (0.0f, 3.0f);
            g.setColour (colour.withAlpha (band.audible ? 1.0f : 0.45f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            juce::String text (b + 1);
            if (label.getWidth() > 100.0f)
                text << "  " << dsp::modeNames[static_cast<size_t> (band.mode)] << "  " << format::db (band.bandDriveDb);
            else if (label.getWidth() > 56.0f)
                text << "  " << dsp::modeNames[static_cast<size_t> (band.mode)];
            g.drawText (text, label, juce::Justification::centred);
        }
    }

    void paintCrossovers (juce::Graphics& g) const
    {
        for (int i = 0; i < state.numBands - 1; ++i)
        {
            const float x = crossoverX (i);
            const bool active = i == dragging || i == hovered;
            g.setColour ((active ? theme::text : theme::dimText).withAlpha (active ? 0.9f : 0.55f));
            g.drawLine (x, plot.getY(), x, plot.getBottom(), active ? 2.0f : 1.2f);
            // Asa abajo, con el color de la banda de la izquierda a la derecha (de dónde a dónde separa).
            const auto handle = juce::Rectangle<float> (12.0f, 22.0f).withCentre ({ x, plot.getBottom() - 16.0f });
            g.setColour (theme::control.brighter (active ? 0.4f : 0.15f));
            g.fillRoundedRectangle (handle, 3.0f);
            g.setColour (theme::bands[static_cast<size_t> (i)]);
            g.fillRect (handle.withWidth (3.0f).withSizeKeepingCentre (3.0f, 12.0f).translated (2.0f, 0.0f));
            g.setColour (theme::bands[static_cast<size_t> (i + 1)]);
            g.fillRect (handle.withWidth (3.0f).withSizeKeepingCentre (3.0f, 12.0f).translated (7.0f, 0.0f));
            if (active)
            {
                g.setColour (theme::text);
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                const float hz = xToHz (x);
                g.drawText (format::hz (hz), juce::Rectangle<float> (x - 34.0f, plot.getBottom() - 44.0f, 68.0f, 12.0f), juce::Justification::centred);
            }
        }
    }

    // Arriba, al pasar el mouse por el visor: frecuencia · nota · nivel.
    void paintReadout (juce::Graphics& g) const
    {
        if (! plot.contains (mouse))
            return;
        const float hz = xToHz (mouse.x);
        const float db = topDb - (mouse.y - plot.getY()) / plot.getHeight() * (topDb - bottomDb);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (format::hz (hz) + juce::String::fromUTF8 ("  \xc2\xb7  ") + format::noteName (hz) + juce::String::fromUTF8 ("  \xc2\xb7  ")
                        + juce::String (juce::roundToInt (db)) + " dB",
                    titleArea.withTrimmedLeft (70.0f), juce::Justification::centred);
    }

    RiptideAudioProcessor& processor;
    std::array<juce::RangedAudioParameter*, dsp::maxCrossovers> crossoverParameters {};
    juce::TextButton beforeButton { "ANTES" };
    juce::TextButton afterButton { juce::String::fromUTF8 ("DESPU\xc3\x89S") };
    Trace before, after;
    std::vector<float> scratch;
    DisplayState state;
    DisplayState paintedState; // el del último repintado
    juce::Rectangle<float> plot, titleArea;
    juce::Point<float> mouse { -1.0f, -1.0f };
    int dragging = -1;
    int hovered = -1;
};

} // namespace riptide::gui
