#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "deepwater/dsp/SpectrumAnalyzer.h"
#include "gui/Format.h"
#include "gui/Theme.h"

namespace fathom::gui
{

// El visor del EQ: el "lienzo" sobre el que las fases siguientes dibujan la curva y sus nodos.
//
//  - Eje horizontal logarítmico (20 Hz - 20 kHz): cada octava ocupa lo mismo, como la oye el oído. Abajo, las zonas del
//    mapa de frecuencias (SUB, GRAVES, BARRO...) para aprender a ubicar lo que se oye.
//  - DOS escalas verticales sobre las mismas líneas: a la derecha la del EQ (cuánto sube o baja la curva, ±12 dB por
//    defecto; el botón la cambia) y a la izquierda la del analizador (0 a −96 dBFS). Son independientes: una banda de
//    +3 dB tiene que verse bien aunque el espectro ocupe 90 dB.
//  - La curva (blanca) es la respuesta TOTAL del EQ: las 24 bandas en serie (sus dB se suman) más la ganancia de
//    salida. Se calcula con los mismos coeficientes que usa el audio (EqCascade::response), así que es lo que suena, no
//    un dibujo aproximado. Lo que cada banda encendida sube o baja va relleno con su color, y su nodo numerado marca su
//    frecuencia; un clic en un nodo elige esa banda en el panel (Fase 4: se podrán arrastrar).
//  - Analizador: ENTRADA (línea gris) y SALIDA (relleno azul), medidos igual (un seno de 0 dBFS marca 0 dB). Cada punto
//    sube casi al instante y baja a 40 dB/s, para que la música se pueda leer sin que tiemble.
//  - Al pasar el mouse, arriba se lee la frecuencia, la nota (nombres de FL) y los dos niveles en ese punto.
//
// El análisis (FFT) corre aquí, en el hilo de mensajes; el audio solo copia muestras en las tomas del procesador.
class EqDisplay final : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr float minHz = 20.0f;
    static constexpr float maxHz = 20000.0f;
    static constexpr float analyzerTopDb = 0.0f;
    static constexpr float analyzerBottomDb = -96.0f;
    static constexpr int gridIntervals = 8; // las dos escalas comparten estas líneas
    static constexpr std::array<float, 4> eqRanges { 6.0f, 12.0f, 24.0f, 36.0f };
    static constexpr float fallDbPerSecond = 40.0f;
    static constexpr double staleSeconds = 0.25; // sin audio nuevo durante este tiempo, el espectro cae a silencio

    explicit EqDisplay (FathomAudioProcessor& p) : processor (p)
    {
        outputGain = processor.getParameters().getRawParameterValue (params::outputGain);

        setUpToggle (inButton, theme::spectrumIn,
                     juce::String::fromUTF8 ("Ver el espectro de lo que ENTRA al plugin (l\xc3\xadnea gris)."));
        setUpToggle (outButton, theme::accent,
                     juce::String::fromUTF8 ("Ver el espectro de lo que SALE del plugin (relleno azul). Lo que cambie "
                                             "entre ENTRADA y SALIDA lo hizo el EQ."));
        rangeButton.setTooltip (juce::String::fromUTF8 ("Escala del EQ (n\xc3\xbameros de la derecha): cu\xc3\xa1nto sube o "
                                                        "baja la curva de arriba a abajo. \xc2\xb1" "6 dB para ajustes finos "
                                                        "(mezcla, master); \xc2\xb1" "24 o \xc2\xb1" "36 para cortes profundos. "
                                                        "No cambia el sonido, solo c\xc3\xb3mo se ve."));
        rangeButton.onClick = [this] {
            rangeIndex = (rangeIndex + 1) % eqRanges.size();
            updateRangeText();
            repaint();
        };
        updateRangeText();
        addAndMakeVisible (rangeButton);
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
    }

    // Una vez por cuadro (lo llama el editor).
    void update (double elapsedSeconds)
    {
        const double sampleRate = processor.getAnalyzerSampleRate();
        const int order = deepwater::dsp::SpectrumAnalyzer::orderForSampleRate (sampleRate);
        if (order != input.analyzer.getOrder())
        {
            // Fuera del audio: aquí sí se puede reservar memoria.
            for (auto* trace : { &input, &output })
                trace->analyzer.prepare (order);
            scratch.assign (static_cast<size_t> (input.analyzer.getSize()), 0.0f);
        }
        // Solo se repinta si algo cambió de forma visible: sin audio, las curvas terminan de caer al piso y el visor
        // queda quieto (no gasta CPU).
        const float gainDb = outputGain->load();
        bool changed = gainDb != paintedGainDb || sampleRate != curveSampleRate;
        const bool allBands = sampleRate != curveSampleRate;
        paintedGainDb = gainDb;
        curveSampleRate = sampleRate;
        for (int b = 0; b < dsp::Engine::numBands; ++b)
        {
            const auto settings = processor.getBandSettings (b);
            if (allBands || ! sameSettings (settings, paintedBands[static_cast<size_t> (b)]))
            {
                paintedBands[static_cast<size_t> (b)] = settings;
                updateBandCurve (b);
                changed = true;
            }
        }
        if (changed)
            updateTotalCurve();
        changed = updateTrace (input, processor.getInputTap(), sampleRate, elapsedSeconds) || changed;
        changed = updateTrace (output, processor.getOutputTap(), sampleRate, elapsedSeconds) || changed;
        if (changed)
            repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds().toFloat().reduced (10.0f, 8.0f);
        auto title = area.removeFromTop (22.0f);
        rangeButton.setBounds (title.removeFromRight (64.0f).reduced (0.0f, 2.0f).toNearestInt());
        title.removeFromRight (12.0f);
        outButton.setBounds (title.removeFromRight (70.0f).reduced (0.0f, 2.0f).toNearestInt());
        title.removeFromRight (6.0f);
        inButton.setBounds (title.removeFromRight (74.0f).reduced (0.0f, 2.0f).toNearestInt());
        titleArea = title;
        area.removeFromTop (4.0f);
        zoneArea = area.removeFromBottom (16.0f);
        area.removeFromBottom (14.0f); // etiquetas de frecuencia
        plot = area.withTrimmedLeft (30.0f).withTrimmedRight (30.0f); // números de las dos escalas a los lados
        zoneArea = zoneArea.withX (plot.getX()).withWidth (plot.getWidth());

        // Un punto cada 2 píxeles.
        const auto points = static_cast<size_t> (plot.getWidth() / 2.0f) + 1;
        for (auto* trace : { &input, &output })
            trace->shown.assign (points, analyzerBottomDb - 10.0f);
        curveDb.assign (points, 0.0f);
        for (int b = 0; b < dsp::Engine::numBands; ++b)
        {
            bandCurveDb[static_cast<size_t> (b)].assign (points, 0.0f);
            updateBandCurve (b);
        }
        updateTotalCurve();
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
        g.drawText ("EQ", titleArea, juce::Justification::centredLeft);
        paintReadout (g);

        g.setColour (theme::display);
        g.fillRoundedRectangle (plot, 4.0f);
        paintGrid (g);
        paintZones (g);
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot.toNearestInt());
            if (inButton.getToggleState())
            {
                g.setColour (theme::spectrumIn.withAlpha (0.7f));
                g.strokePath (tracePath (input.shown, false), juce::PathStrokeType (1.2f));
            }
            if (outButton.getToggleState())
            {
                g.setGradientFill (juce::ColourGradient (theme::accent.withAlpha (0.30f), 0.0f, plot.getY(),
                                                         theme::accent.withAlpha (0.03f), 0.0f, plot.getBottom(), false));
                g.fillPath (tracePath (output.shown, true));
                g.setColour (theme::accent.withAlpha (0.85f));
                g.strokePath (tracePath (output.shown, false), juce::PathStrokeType (1.3f));
            }
            paintCurve (g);
        }
    }

    juce::String getTooltip() override
    {
        const auto position = getMouseXYRelative().toFloat();
        if (titleArea.contains (position))
            return juce::String::fromUTF8 ("Graves a la izquierda, agudos a la derecha; cada octava ocupa lo mismo. "
                                           "La l\xc3\xadnea blanca es la curva del EQ (escala de la derecha). Detr\xc3\xa1s, "
                                           "el analizador: cu\xc3\xa1nto nivel hay en cada frecuencia (escala de la "
                                           "izquierda). Pasa el mouse para leer la frecuencia y la nota.");
        if (zoneArea.contains (position))
            return zoneTooltip (xToHz (position.x));
        return {};
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        mouse = event.position;
        setMouseCursor (nodeAt (mouse) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor);
        repaintReadoutAndZones();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        mouse = { -1.0f, -1.0f };
        repaintReadoutAndZones();
    }

    // Un clic sobre un nodo elige esa banda (el más cercano, si hay varios encima).
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (const int b = nodeAt (event.position); b >= 0 && onBandClicked)
            onBandClicked (b);
    }

    // La banda que muestra el panel: su nodo y su relleno se resaltan (y se ve aunque esté apagada).
    void setSelectedBand (int band)
    {
        selectedBand = band;
        repaint();
    }

    std::function<void (int)> onBandClicked;

private:
    struct Trace
    {
        deepwater::dsp::SpectrumAnalyzer analyzer;
        std::vector<float> shown; // lo que se dibuja, en dB, un valor por punto
        std::uint64_t lastWritten = 0;
        double secondsWithoutAudio = 0.0;
    };

    // Las zonas del mapa de frecuencias (aproximadas: cada instrumento las cruza, pero sirven para ubicarse).
    struct Zone
    {
        float lowHz, highHz;
        const char* name;
        const char* description;
    };
    static constexpr std::array<Zone, 7> zones { {
        { 20.0f, 60.0f, "SUB", "Sub (20-60 Hz): se siente m\xc3\xa1s de lo que se oye. La fundamental del 808 y el "
                               "golpe grave del kick. Casi no suena en bocinas chicas ni en el celular." },
        { 60.0f, 250.0f, "GRAVES", "Graves (60-250 Hz): el cuerpo del bajo y del kick, la calidez de la voz "
                                   "masculina. Demasiado = mezcla pesada y \"embarrada\"." },
        { 250.0f, 500.0f, "BARRO", "Medios-graves (250-500 Hz): la zona del \"barro\". Casi todo tiene energ\xc3\xad" "a "
                                   "aqu\xc3\xad y se acumula; un corte suave suele aclarar la mezcla." },
        { 500.0f, 2000.0f, "MEDIOS", "Medios (500 Hz-2 kHz): el sonido de \"caja\" o \"nasal\" (de m\xc3\xa1s) y lo que "
                                     "hace que algo se oiga en el celular (de menos se pierde)." },
        { 2000.0f, 5000.0f, "PRESENCIA", "Presencia (2-5 kHz): donde el o\xc3\xad" "do es m\xc3\xa1s sensible. Claridad de "
                                         "la voz y el ataque de la percusi\xc3\xb3n; de m\xc3\xa1s suena agresivo y cansa." },
        { 5000.0f, 10000.0f, "BRILLO", "Brillo (5-10 kHz): las eses de la voz (sibilancia), los platillos y hi-hats." },
        { 10000.0f, 20000.0f, "AIRE", "Aire (10-20 kHz): sensaci\xc3\xb3n de espacio y \"caro\"; una subida suave en "
                                      "voces y en el master. Con la edad se oye menos." },
    } };

    void repaintReadoutAndZones()
    {
        repaint (titleArea.toNearestInt());
        repaint (zoneArea.toNearestInt());
    }

    void setUpToggle (juce::TextButton& button, juce::Colour colour, const juce::String& tooltip)
    {
        button.setClickingTogglesState (true);
        button.setToggleState (true, juce::dontSendNotification);
        button.setColour (juce::TextButton::buttonOnColourId, colour.withAlpha (0.35f));
        button.setTooltip (tooltip);
        button.onClick = [this] { repaint(); };
        addAndMakeVisible (button);
    }

    void updateRangeText()
    {
        rangeButton.setButtonText (juce::String::fromUTF8 ("\xc2\xb1") + juce::String (juce::roundToInt (eqRange())) + " dB");
    }

    float eqRange() const noexcept { return eqRanges[rangeIndex]; }

    // Frecuencia ↔ posición: logarítmica, cada octava ocupa lo mismo.
    float hzToX (float hz) const noexcept
    {
        return plot.getX() + plot.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz);
    }
    float xToHz (float x) const noexcept
    {
        return minHz * std::pow (maxHz / minHz, (x - plot.getX()) / plot.getWidth());
    }

    // Las dos escalas verticales: la del EQ (0 dB al centro) y la del analizador (0 dBFS arriba).
    float eqDbToY (float db) const noexcept { return plot.getCentreY() - db / eqRange() * plot.getHeight() * 0.5f; }
    float yToEqDb (float y) const noexcept { return (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * eqRange(); }
    float analyzerDbToY (float db) const noexcept
    {
        return plot.getY() + plot.getHeight() * (analyzerTopDb - db) / (analyzerTopDb - analyzerBottomDb);
    }
    float yToAnalyzerDb (float y) const noexcept
    {
        return analyzerTopDb - (y - plot.getY()) / plot.getHeight() * (analyzerTopDb - analyzerBottomDb);
    }

    // Devuelve true si algún punto se movió de forma visible (0.05 dB: menos de un píxel).
    bool updateTrace (Trace& trace, const FathomAudioProcessor::AnalyzerTap& tap, double sampleRate, double elapsed)
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
            const float y = juce::jmin (floorY, analyzerDbToY (levels[i]));
            if (i == 0)
            {
                path.startNewSubPath (x, closed ? floorY : y);
                if (closed)
                    path.lineTo (x, y);
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
        for (const float hz : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const auto text = hz < 1000.0f ? juce::String (juce::roundToInt (hz)) : juce::String (juce::roundToInt (hz / 1000.0f)) + "k";
            g.drawText (text, juce::Rectangle<float> (hzToX (hz) - 20.0f, plot.getBottom() + 1.0f, 40.0f, 13.0f), juce::Justification::centred);
        }

        // Niveles: las mismas líneas para las dos escalas. Derecha = EQ (0 dB al centro, más clara); izquierda = analizador.
        for (int i = 0; i <= gridIntervals; ++i)
        {
            const float y = plot.getY() + plot.getHeight() * static_cast<float> (i) / static_cast<float> (gridIntervals);
            const float eqDb = yToEqDb (y);
            const bool centre = i == gridIntervals / 2;
            g.setColour (centre ? theme::panelOutline.brighter (0.3f) : theme::grid);
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());

            const auto eqText = (eqDb > 0.05f ? "+" : "") + juce::String (eqDb, std::abs (eqDb - std::round (eqDb)) > 0.05f ? 1 : 0);
            g.setColour (theme::text.withAlpha (0.75f));
            g.drawText (eqText, juce::Rectangle<float> (plot.getRight() + 3.0f, y - 6.0f, 27.0f, 12.0f), juce::Justification::centredLeft);
            g.setColour (theme::dimText.withAlpha (0.7f));
            g.drawText (juce::String (juce::roundToInt (yToAnalyzerDb (y))),
                        juce::Rectangle<float> (plot.getX() - 30.0f, y - 6.0f, 27.0f, 12.0f), juce::Justification::centredRight);
        }
    }

    // Debajo de las frecuencias: las zonas del mapa, alternando el tono para distinguirlas.
    void paintZones (juce::Graphics& g) const
    {
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        const float hoveredHz = zoneArea.contains (mouse) ? xToHz (mouse.x) : -1.0f;
        for (size_t i = 0; i < zones.size(); ++i)
        {
            const auto& zone = zones[i];
            const float left = hzToX (zone.lowHz);
            const auto box = juce::Rectangle<float> (left, zoneArea.getY(), hzToX (zone.highHz) - left, zoneArea.getHeight()).reduced (0.5f, 1.0f);
            const bool hovered = hoveredHz >= zone.lowHz && hoveredHz < zone.highHz;
            g.setColour (theme::control.brighter (hovered ? 0.35f : (i % 2 == 0 ? 0.05f : 0.15f)));
            g.fillRect (box);
            g.setColour (hovered ? theme::text : theme::dimText);
            g.drawText (zone.name, box, juce::Justification::centred, true);
        }
    }

    juce::String zoneTooltip (float hz) const
    {
        for (const auto& zone : zones)
            if (hz >= zone.lowHz && hz < zone.highHz)
                return juce::String::fromUTF8 (zone.description);
        return {};
    }

    static bool sameSettings (const dsp::BandSettings& a, const dsp::BandSettings& b) noexcept
    {
        return a.enabled == b.enabled && a.frequency == b.frequency && a.gainDb == b.gainDb && a.q == b.q && a.shape == b.shape
               && a.slopeIndex == b.slopeIndex;
    }

    // La respuesta total en dB en una frecuencia: las bandas (con sus coeficientes reales) más la ganancia de salida.
    float totalDb (float hz) const noexcept
    {
        double total = static_cast<double> (paintedGainDb);
        for (int b = 0; b < dsp::Engine::numBands; ++b)
            if (paintedBands[static_cast<size_t> (b)].enabled)
                total += bandCascades[static_cast<size_t> (b)].magnitudeDb (hz, curveSampleRate);
        return static_cast<float> (total);
    }

    // Frecuencia de cada punto de las curvas (uno cada 2 píxeles, como el analizador).
    double pointHz (size_t i, size_t points) const noexcept
    {
        return minHz * std::pow (static_cast<double> (maxHz / minHz), static_cast<double> (i) / static_cast<double> (points - 1));
    }

    // La curva propia de una banda (en dB, sin la salida). Solo cuando cambian sus ajustes o el tamaño.
    void updateBandCurve (int b)
    {
        if (curveSampleRate <= 0.0) // antes del primer update() no se sabe la frecuencia de muestreo
            return;
        const auto index = static_cast<size_t> (b);
        const auto& settings = paintedBands[index];
        bandCascades[index] = dsp::Band::coefficientsFor (settings, curveSampleRate);
        // El nodo: sobre la curva de la banda en su frecuencia si la forma tiene ganancia (la campana en su pico, el
        // shelf en su punto medio, los tilts en su eje); los cortes, el notch, el pasa banda y el all-pass, en 0 dB.
        nodeDb[index] = settings.enabled && dsp::usesGain (settings.shape)
                            ? static_cast<float> (bandCascades[index].magnitudeDb (settings.frequency, curveSampleRate))
                            : 0.0f;
        auto& curve = bandCurveDb[index];
        const auto points = curve.size();
        if (points < 2)
            return;
        for (size_t i = 0; i < points; ++i)
            curve[i] = settings.enabled ? static_cast<float> (bandCascades[index].magnitudeDb (pointHz (i, points), curveSampleRate)) : 0.0f;
    }

    void updateTotalCurve()
    {
        const auto points = curveDb.size();
        for (size_t i = 0; i < points; ++i)
        {
            float total = paintedGainDb;
            for (const auto& curve : bandCurveDb)
                total += curve[i];
            curveDb[i] = total;
        }
    }

    juce::Path curvePath (const std::vector<float>& db, float offsetDb) const
    {
        juce::Path path;
        const auto points = db.size();
        const float step = plot.getWidth() / static_cast<float> (points - 1);
        const float top = plot.getY() + 1.0f, bottom = plot.getBottom() - 1.0f;
        for (size_t i = 0; i < points; ++i)
        {
            const float x = plot.getX() + step * static_cast<float> (i);
            const float y = juce::jlimit (top, bottom, eqDbToY (db[i] + offsetDb));
            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
        return path;
    }

    juce::Point<float> nodePosition (int b) const noexcept
    {
        const auto& settings = paintedBands[static_cast<size_t> (b)];
        const float hz = juce::jlimit (minHz, maxHz, settings.frequency);
        const float y = eqDbToY (paintedGainDb + nodeDb[static_cast<size_t> (b)]);
        return { hzToX (hz), juce::jlimit (plot.getY() + 7.0f, plot.getBottom() - 7.0f, y) };
    }

    bool nodeVisible (int b) const noexcept { return paintedBands[static_cast<size_t> (b)].enabled || b == selectedBand; }

    // La banda cuyo nodo está bajo 'position' (o −1).
    int nodeAt (juce::Point<float> position) const noexcept
    {
        int best = -1;
        float bestDistance = nodeRadius + 3.0f;
        for (int b = 0; b < dsp::Engine::numBands; ++b)
            if (nodeVisible (b))
                if (const float distance = nodePosition (b).getDistanceFrom (position); distance < bestDistance)
                {
                    best = b;
                    bestDistance = distance;
                }
        return best;
    }

    // Las curvas del EQ: lo que cada banda encendida sube o baja (relleno con su color, respecto de la línea de la
    // salida; la elegida, más marcada), la respuesta total (blanca; pegada al borde si se sale de la escala) y encima los
    // nodos de las bandas encendidas y de la elegida (hueco si está apagada).
    void paintCurve (juce::Graphics& g) const
    {
        if (curveDb.size() < 2)
            return;
        const float baseY = juce::jlimit (plot.getY() + 1.0f, plot.getBottom() - 1.0f, eqDbToY (paintedGainDb));
        for (int b = 0; b < dsp::Engine::numBands; ++b)
        {
            if (! paintedBands[static_cast<size_t> (b)].enabled)
                continue;
            const bool selected = b == selectedBand;
            auto path = curvePath (bandCurveDb[static_cast<size_t> (b)], paintedGainDb);
            const auto colour = theme::bandColour (b);
            if (selected)
            {
                g.setColour (colour.withAlpha (0.6f));
                g.strokePath (path, juce::PathStrokeType (1.0f));
            }
            path.lineTo (plot.getRight(), baseY);
            path.lineTo (plot.getX(), baseY);
            path.closeSubPath();
            g.setColour (colour.withAlpha (selected ? 0.22f : 0.10f));
            g.fillPath (path);
        }
        g.setColour (theme::text.withAlpha (0.9f));
        g.strokePath (curvePath (curveDb, 0.0f), juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        // La elegida al final: queda encima si se superpone con otra.
        for (int pass = 0; pass < 2; ++pass)
            for (int b = 0; b < dsp::Engine::numBands; ++b)
            {
                if (! nodeVisible (b) || (b == selectedBand) != (pass == 1))
                    continue;
                const bool enabled = paintedBands[static_cast<size_t> (b)].enabled;
                const auto colour = theme::bandColour (b);
                const auto node = juce::Rectangle<float> (2.0f * nodeRadius, 2.0f * nodeRadius).withCentre (nodePosition (b));
                if (enabled)
                {
                    g.setColour (colour);
                    g.fillEllipse (node);
                }
                g.setColour (b == selectedBand ? theme::text : enabled ? colour.darker (0.6f) : theme::dimText);
                g.drawEllipse (node, b == selectedBand ? 2.0f : 1.2f);
                g.setColour (enabled ? juce::Colours::black.withAlpha (0.8f) : theme::dimText);
                g.drawText (juce::String (b + 1), node, juce::Justification::centred);
            }
    }

    // Arriba, al pasar el mouse por el visor: frecuencia · nota · escala del EQ · escala del analizador.
    void paintReadout (juce::Graphics& g) const
    {
        if (! plot.contains (mouse))
            return;
        const float hz = xToHz (mouse.x);
        const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (format::hz (hz) + dot + format::noteName (hz) + dot + "curva " + format::db (totalDb (hz)) + dot
                        + "EQ " + format::db (yToEqDb (mouse.y)) + dot
                        + juce::String::fromUTF8 ("analizador ") + juce::String (juce::roundToInt (yToAnalyzerDb (mouse.y))) + " dB",
                    titleArea.withTrimmedLeft (30.0f), juce::Justification::centred);
    }

    FathomAudioProcessor& processor;
    std::atomic<float>* outputGain = nullptr;
    juce::TextButton inButton { "ENTRADA" };
    juce::TextButton outButton { "SALIDA" };
    juce::TextButton rangeButton;
    size_t rangeIndex = 1; // ±12 dB
    Trace input, output;
    std::vector<float> scratch;
    float paintedGainDb = 0.0f;
    static constexpr float nodeRadius = 8.0f;
    std::array<dsp::BandSettings, dsp::Engine::numBands> paintedBands {};
    std::array<deepwater::dsp::EqCascade, dsp::Engine::numBands> bandCascades {};
    std::array<std::vector<float>, dsp::Engine::numBands> bandCurveDb; // la curva propia de cada banda, en dB
    std::array<float, dsp::Engine::numBands> nodeDb {};
    int selectedBand = 0;
    double curveSampleRate = 0.0; // 0 = todavía sin calcular
    std::vector<float> curveDb; // la respuesta total en dB, un valor por punto
    juce::Rectangle<float> plot, titleArea, zoneArea;
    juce::Point<float> mouse { -1.0f, -1.0f };
};

} // namespace fathom::gui
