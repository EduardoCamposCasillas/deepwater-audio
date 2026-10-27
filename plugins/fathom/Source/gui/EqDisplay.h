#pragma once

#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "analysis/SpectrumView.h"
#include "deepwater/dsp/SpectrumAnalyzer.h"
#include "dsp/PhaseDesign.h"
#include "dsp/Solo.h"
#include "edit/CurveTools.h"
#include "edit/NodeEditing.h"
#include "gui/BandEdits.h"
#include "gui/Format.h"
#include "gui/Keys.h"
#include "gui/Theme.h"

namespace fathom::gui
{

// El visor del EQ: el "lienzo" donde se ve y se edita la curva.
//
//  - Eje horizontal logarítmico (20 Hz - 20 kHz): cada octava ocupa lo mismo, como la oye el oído. Debajo, un teclado
//    de piano con las notas de FL en su frecuencia y las zonas del mapa de frecuencias (SUB, GRAVES, BARRO...).
//  - DOS escalas verticales sobre las mismas líneas: a la derecha la del EQ (cuánto sube o baja la curva, ±12 dB por
//    defecto; el botón la cambia) y a la izquierda la del analizador (0 a −96 dBFS).
//  - La curva (blanca) es la respuesta TOTAL del EQ: las 24 bandas en serie (sus dB se suman) más la ganancia de
//    salida, calculada con los mismos coeficientes que usa el audio. Lo que cada banda sube o baja va relleno con su
//    color y su nodo numerado marca su frecuencia.
//  - Analizador (Fase 5): ENTRADA (línea gris), SALIDA (relleno azul) y SIDECHAIN (línea ámbar: otra pista), con
//    inclinación, rango, resolución, velocidad y retención de picos (línea fina) a elección (AnalyzerPanel) y CONGELAR.
//    Fase 8: del mid, del side o los dos (el side de la salida en una línea violeta).
//  - Estéreo por banda (Fase 8): con bandas en un solo canal, en vez de la curva blanca hay una por canal (L, R, M, S:
//    las bandas estéreo más las de ese canal) y cada nodo lleva su letra. Con solo L/R o solo M/S es la respuesta exacta
//    de cada canal; si se mezclan las dos clases, cada curva es una guía (una banda izquierda también mueve el mid).
//  - Modos de fase (Fase 10): con fase lineal o natural la curva total es la del FIR que suena de verdad (la GUI diseña
//    el mismo filtro): con poca calidad se ve cómo una campana angosta en los graves no llega a su ganancia.
//  - Dinámica espectral (Fase 11): la curva de una banda espectral muestra lo que mueve AHORA cada frecuencia (los
//    "dientes" donde corta), con la curva en reposo punteada; llega del audio en una grilla fija (SpectralStage).
//  - Agarrar un pico: con el mouse cerca de un pico del espectro aparece su marca (Hz y nota); clic y arrastrar = una
//    campana nueva justo en ese pico (arrastrar hacia abajo = cortarlo). Ctrl mientras se arrastra un nodo = imán a los
//    picos.
//  - Igualar y dibujar (Fase 12), con los botones de arriba: IGUALAR aprende el espectro promedio de una referencia y el
//    de la pista y muestra la curva que lleva la pista hacia la referencia (naranja); DIBUJAR deja trazar esa curva a
//    mano (arrastrar = dibujar, clic derecho o Alt = borrar). En las dos, la línea verde es lo que harían las bandas que
//    se crearían (edit/CurveFit.h); el panel de abajo (CurvePanel) las crea.
//
// Edición (Fase 4), pensada para no tener que ir al panel:
//  - Doble clic en el vacío = crear una banda ahí (campana; en los extremos, un corte). Doble clic en un nodo = apagarla
//    o encenderla. Supr = borrar las elegidas (vuelven a estar libres).
//  - Arrastrar un nodo = frecuencia (horizontal) y ganancia (vertical). Shift = ajuste fino (10 veces más lento),
//    Alt = un solo eje (el que más se movió). Rueda sobre un nodo = Q (Shift = fino).
//  - Selección múltiple: Ctrl+clic agrega o quita un nodo, arrastrar en el vacío = rectángulo, Ctrl+A = todas, Esc =
//    solo la del panel. Arrastrar, la rueda, el menú y Supr actúan sobre todas las elegidas.
//  - Clic derecho = menú (forma, pendiente, encender, afinar a la nota, borrar; en el vacío, crear con otra forma).
//  - Teclado del eje: clic en una tecla = llevar la banda del panel justo a esa nota.
// Cada edición es un paso de deshacer (BandEdits abre y cierra los gestos de los parámetros).
//
// El análisis (FFT) corre aquí, en el hilo de mensajes; el audio solo copia muestras en las tomas del procesador.
class EqDisplay final : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr float minHz = 20.0f;
    static constexpr float maxHz = 20000.0f;
    static constexpr float analyzerTopDb = 0.0f;
    static constexpr int gridIntervals = 8; // las dos escalas comparten estas líneas
    static constexpr std::array<float, 4> eqRanges { 6.0f, 12.0f, 24.0f, 36.0f };
    static constexpr double staleSeconds = 0.25; // sin audio nuevo durante este tiempo, el espectro cae a silencio
    static constexpr float peakProminenceDb = 6.0f; // cuánto debe despegarse un pico de sus valles para poder agarrarlo
    static constexpr float peakReachOctaves = 1.0f / 6.0f; // el vecindario donde un pico debe ser el más alto
    static constexpr float peakGrabPixels = 14.0f;  // distancia del mouse a la que se ofrece un pico
    static constexpr float peakSnapPixels = 24.0f;  // Ctrl + arrastrar: distancia del imán
    static constexpr float grabbedQ = 4.0f;         // la campana nace angosta (un tercio de octava): una resonancia
    static constexpr double wheelGestureSeconds = 0.5; // la rueda quieta este tiempo = fin del paso de deshacer
    static constexpr float fineFactor = 0.1f;          // Shift: diez veces más lento

    explicit EqDisplay (FathomAudioProcessor& p) : processor (p), edits (p)
    {
        outputGain = processor.getParameters().getRawParameterValue (params::outputGain);
        gainScale = processor.getParameters().getRawParameterValue (params::gainScale);

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

        // Fase 12: las dos herramientas (una a la vez; la misma otra vez = salir).
        matchButton.setTooltip (juce::String::fromUTF8 (
            "Igualar a una referencia: Fathom aprende el espectro promedio de otra pista (por el sidechain) y el de la tuya, "
            "y crea las bandas que llevan tu pista hacia el color de la referencia. Abajo aparecen sus controles."));
        drawButton.setTooltip (juce::String::fromUTF8 (
            "Dibujar la curva a mano: arrastra sobre el visor la forma que quieres y Fathom la convierte en bandas "
            "(editables despuÃ©s como cualquier otra)."));
        for (auto* button : { &matchButton, &drawButton })
        {
            button->setClickingTogglesState (false);
            button->setColour (juce::TextButton::buttonOnColourId, theme::curveWanted.withAlpha (0.5f));
            addAndMakeVisible (*button);
        }
        matchButton.onClick = [this] { setTool (tool == Tool::match ? Tool::none : Tool::match); };
        drawButton.onClick = [this] { setTool (tool == Tool::draw ? Tool::none : Tool::draw); };
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        setWantsKeyboardFocus (true);
        selection.set (static_cast<size_t> (focusedBand));
    }

    // Una vez por cuadro (lo llama el editor).
    void update (double elapsedSeconds)
    {
        const double sampleRate = processor.getAnalyzerSampleRate();
        const int order = analysis::fftOrder (view.resolutionIndex, sampleRate);
        if (order != input.analyzer.getOrder())
        {
            // Fuera del audio: aquí sí se puede reservar memoria.
            for (auto* trace : traces())
                trace->analyzer.prepare (order);
            scratch.assign (static_cast<size_t> (input.analyzer.getSize()), 0.0f);
        }
        // La rueda no tiene "soltar": su paso de deshacer se cierra cuando deja de moverse un momento.
        if (wheelEditing && nowSeconds() - lastWheelSeconds > wheelGestureSeconds)
        {
            wheelEditing = false;
            if (drag != Drag::nodes)
                edits.end();
        }
        // Solo se repinta si algo cambió de forma visible: sin audio, las curvas terminan de caer al piso y el visor
        // queda quieto (no gasta CPU).
        bool changed = refreshBands (sampleRate);
        changed = refreshSpectral() || changed;
        changed = refreshPhaseCurve (sampleRate) || changed;
        changed = updateCurveTool (sampleRate) || changed; // Fase 12
        const bool side = view.channels() == analysis::Channels::side;
        changed = updateTrace (input, side ? processor.getInputSideTap() : processor.getInputTap(), sampleRate, elapsedSeconds) || changed;
        changed = updateTrace (output, side ? processor.getOutputSideTap() : processor.getOutputTap(), sampleRate, elapsedSeconds) || changed;
        changed = updateTrace (sidechain, side ? processor.getSidechainSideTap() : processor.getSidechainTap(), sampleRate, elapsedSeconds)
                  || changed;
        if (view.channels() == analysis::Channels::midAndSide) // el side de la salida, aparte (una FFT más solo si se ve)
            changed = updateTrace (outputSide, processor.getOutputSideTap(), sampleRate, elapsedSeconds) || changed;
        changed = updatePeaks() || changed;
        const int solo = processor.getSoloBand();
        changed = changed || solo != paintedSolo;
        paintedSolo = solo;
        const bool connected = processor.isSidechainConnected();
        changed = changed || connected != sidechainConnected;
        sidechainConnected = connected;
        if (changed)
            repaint();
    }

    // Los ajustes del analizador (del AnalyzerPanel o del proyecto). Solo cambian lo que se ve.
    void setView (const analysis::ViewSettings& newView)
    {
        if (newView == view)
            return;
        const bool newChannels = newView.channelsIndex != view.channelsIndex;
        const bool restartHold = newView.holdIndex != view.holdIndex || newView.tiltIndex != view.tiltIndex || newChannels;
        // Fase 12: los espectros aprendidos se dibujan con la inclinación.
        wantedDirty = wantedDirty || newView.tiltIndex != view.tiltIndex;
        view = newView;
        if (newChannels)
            for (auto* trace : traces())
            {
                // Otra toma: se analiza de nuevo en el siguiente cuadro y lo dibujado arranca desde el piso.
                trace->lastWritten = 0;
                std::fill (trace->shown.begin(), trace->shown.end(), deepwater::dsp::SpectrumAnalyzer::floorDb);
            }
        if (restartHold)
            clearPeakHold();
        hoveredPeak = -1;
        repaint();
    }

    // CONGELAR: el espectro se queda como está (el audio sigue).
    void setFrozen (bool shouldFreeze)
    {
        frozen = shouldFreeze;
        repaint();
    }

    // Borra la línea de picos: empieza desde lo que se ve ahora.
    void clearPeakHold()
    {
        for (auto* trace : traces())
            for (size_t i = 0; i < trace->held.size(); ++i)
                trace->held[i] = { trace->shown[i], 0.0 };
        repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds().toFloat().reduced (10.0f, 8.0f);
        auto title = area.removeFromTop (22.0f);
        rangeButton.setBounds (title.removeFromRight (64.0f).reduced (0.0f, 2.0f).toNearestInt());
        title.removeFromRight (12.0f);
        drawButton.setBounds (title.removeFromRight (72.0f).reduced (0.0f, 2.0f).toNearestInt());
        title.removeFromRight (4.0f);
        matchButton.setBounds (title.removeFromRight (76.0f).reduced (0.0f, 2.0f).toNearestInt());
        title.removeFromRight (12.0f);
        titleArea = title;
        area.removeFromTop (4.0f);
        zoneArea = area.removeFromBottom (16.0f);
        area.removeFromBottom (14.0f); // etiquetas de frecuencia
        pianoArea = area.removeFromBottom (16.0f);
        area.removeFromBottom (1.0f);
        plot = area.withTrimmedLeft (30.0f).withTrimmedRight (30.0f); // números de las dos escalas a los lados
        zoneArea = zoneArea.withX (plot.getX()).withWidth (plot.getWidth());
        pianoArea = pianoArea.withX (plot.getX()).withWidth (plot.getWidth());

        // Un punto cada 2 píxeles.
        const auto points = static_cast<size_t> (plot.getWidth() / 2.0f) + 1;
        for (auto* trace : traces())
        {
            trace->shown.assign (points, deepwater::dsp::SpectrumAnalyzer::floorDb);
            trace->held.assign (points, { deepwater::dsp::SpectrumAnalyzer::floorDb, 0.0 });
        }
        peaks.clear();
        hoveredPeak = -1;
        curveDb.assign (points, 0.0f);
        for (int b = 0; b < dsp::Engine::numBands; ++b)
        {
            bandCurveDb[static_cast<size_t> (b)].assign (points, 0.0f);
            updateBandCurve (b);
        }
        for (auto& curve : spectralCurveDb)
            curve.clear(); // ídem: llega con el próximo tramo
        phaseDb.clear(); // otra cantidad de puntos: se recalcula en el próximo cuadro
        phaseDirty = true;
        referenceShown.clear(); // Fase 12: ídem
        currentShown.clear();
        previewDb.clear();
        wantedDirty = previewDirty = true;
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
        paintPiano (g);
        paintZones (g);
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot.toNearestInt());
            const bool holding = view.holdMode() != analysis::Hold::off;
            if (view.showSidechain)
            {
                g.setColour (theme::spectrumSidechain.withAlpha (0.75f));
                g.strokePath (tracePath (sidechain.shown, false), juce::PathStrokeType (1.2f));
                if (holding)
                    paintHeld (g, sidechain, theme::spectrumSidechain);
            }
            if (view.showInput)
            {
                g.setColour (theme::spectrumIn.withAlpha (0.7f));
                g.strokePath (tracePath (input.shown, false), juce::PathStrokeType (1.2f));
                if (holding)
                    paintHeld (g, input, theme::spectrumIn);
            }
            if (view.showOutput)
            {
                g.setGradientFill (juce::ColourGradient (theme::accent.withAlpha (0.30f), 0.0f, plot.getY(),
                                                         theme::accent.withAlpha (0.03f), 0.0f, plot.getBottom(), false));
                g.fillPath (tracePath (output.shown, true));
                g.setColour (theme::accent.withAlpha (0.85f));
                g.strokePath (tracePath (output.shown, false), juce::PathStrokeType (1.3f));
                if (holding)
                    paintHeld (g, output, theme::accent.brighter (0.4f));
                if (view.channels() == analysis::Channels::midAndSide)
                {
                    g.setColour (theme::spectrumSide.withAlpha (0.85f));
                    g.strokePath (tracePath (outputSide.shown, false), juce::PathStrokeType (1.3f));
                    if (holding)
                        paintHeld (g, outputSide, theme::spectrumSide);
                }
            }
            paintAnalyzerStatus (g);
            paintCurve (g);
            paintCurveTool (g);
            paintPeakMarker (g);
            paintLasso (g);
            paintNodeLabel (g);
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
        if (pianoArea.contains (position))
            return juce::String::fromUTF8 ("Las notas en su frecuencia (nombres de FL: C5 = Do central, A5 = 440 Hz). "
                                           "Clic en una tecla = llevar la banda del panel justo a esa nota: una campana "
                                           "afinada a la nota del 808, un notch en el tono que zumba.");
        if (tool == Tool::draw && plot.contains (position))
            return juce::String::fromUTF8 ("Arrastrar = dibujar la curva que quieres (en la escala del EQ, la de la "
                                           "derecha). Clic derecho o Alt + arrastrar = borrar. Lo que no dibujes se queda "
                                           "como est\xc3\xa1. La l\xc3\xadnea verde es lo que har\xc3\xad" "an las bandas; "
                                           "CREAR BANDAS (abajo) las crea. Supr = borrar todo el dibujo, Esc = salir.");
        if (nodeAt (position) >= 0)
            return juce::String::fromUTF8 ("Arrastrar = frecuencia y ganancia (Shift = fino, Alt = un solo eje, Ctrl = "
                                           "im\xc3\xa1n a los picos del espectro). Rueda = Q. Doble clic = apagar/encender. "
                                           "Ctrl+clic = elegir varias. Supr = borrar. Clic derecho = forma, pendiente y "
                                           "m\xc3\xa1s.");
        if (hoveredPeak >= 0)
            return juce::String::fromUTF8 ("Un pico del espectro: algo suena aqu\xc3\xad m\xc3\xa1s fuerte que alrededor. "
                                           "Clic y arrastrar = una campana justo en el pico (abajo = cortarlo, arriba = "
                                           "realzarlo). Mientras arrastras, el espectro se congela.");
        if (plot.contains (position))
            return juce::String::fromUTF8 ("Doble clic = crear una banda aqu\xc3\xad (en los extremos, un corte). Arrastrar "
                                           "= elegir varios nodos con un rect\xc3\xa1ngulo. Clic derecho = crear otra forma. "
                                           "Ctrl+Z / Ctrl+Y = deshacer / rehacer.");
        return {};
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        mouse = event.position;
        if (tool == Tool::draw) // dibujando, los nodos y los picos no se agarran
        {
            if (hoveredNode >= 0 || hoveredPeak >= 0)
                repaint (plot.toNearestInt());
            hoveredNode = hoveredPeak = -1;
            setMouseCursor (pianoArea.contains (mouse) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor);
            repaintReadoutAndAxis();
            return;
        }
        const int node = nodeAt (mouse);
        const int peak = pickPeak();
        if (node != hoveredNode || peak != hoveredPeak)
        {
            hoveredNode = node;
            hoveredPeak = peak;
            repaint (plot.toNearestInt());
        }
        setMouseCursor (node >= 0 || pianoArea.contains (mouse) ? juce::MouseCursor::PointingHandCursor
                        : peak >= 0                             ? juce::MouseCursor::DraggingHandCursor
                                                                : juce::MouseCursor::CrosshairCursor);
        repaintReadoutAndAxis();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        mouse = { -1.0f, -1.0f };
        if (hoveredNode >= 0 || hoveredPeak >= 0)
        {
            hoveredNode = -1;
            hoveredPeak = -1;
            repaint (plot.toNearestInt());
        }
        repaintReadoutAndAxis();
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        grabKeyboardFocus(); // para Supr, Ctrl+A, Esc y Ctrl+Z
        mouse = event.position;
        drag = Drag::none;
        if (event.getNumberOfClicks() == 1)
            lastClickGrabbed = false;

        if (pianoArea.contains (event.position) && ! event.mods.isPopupMenu())
        {
            tuneToNote (juce::roundToInt (edit::hzToNote (xToHz (event.position.x))));
            return;
        }
        if (! plot.contains (event.position))
            return;

        if (tool == Tool::draw) // Fase 12: un trazo (o borrar, con el botón derecho o Alt)
        {
            drag = Drag::draw;
            erasing = event.mods.isPopupMenu() || event.mods.isAltDown();
            drawLast = event.position;
            drawTo (event.position);
            return;
        }

        const int node = nodeAt (event.position);
        if (node >= 0)
        {
            if (event.mods.isCommandDown() && ! event.mods.isPopupMenu())
            {
                toggleInSelection (node);
                return;
            }
            if (! selection.test (static_cast<size_t> (node)))
                selectOnly (node);
            else
                focus (node);
            if (event.mods.isPopupMenu())
            {
                showNodeMenu();
                return;
            }
            startNodeDrag (event.position);
            return;
        }

        if (event.mods.isPopupMenu())
        {
            showEmptyMenu (event.position);
            return;
        }
        // El segundo clic de un doble clic no agarra otra vez (el primero ya creó la banda).
        if (hoveredPeak >= 0 && ! event.mods.isCommandDown() && event.getNumberOfClicks() == 1)
        {
            grabPeak (event.position);
            return;
        }
        // En el vacío: un rectángulo de selección (empieza de verdad al moverse unos píxeles).
        drag = Drag::lasso;
        lassoAdds = event.mods.isCommandDown();
        dragOrigin = event.position;
        lassoMoved = false;
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        mouse = event.position;
        if (drag == Drag::draw)
        {
            drawTo (event.position);
            repaintReadoutAndAxis();
            return;
        }
        if (drag == Drag::lasso)
        {
            lassoMoved = lassoMoved || event.position.getDistanceFrom (dragOrigin) > 3.0f;
            repaint (plot.toNearestInt());
            return;
        }
        if (drag != Drag::nodes)
            return;

        // Por diferencias desde el evento anterior, no desde el clic: así Shift se puede apretar o soltar a mitad del
        // arrastre sin que el nodo salte.
        const auto delta = event.position - dragLast;
        dragLast = event.position;
        dragTotal += delta;
        if (! dragMoved && dragTotal.getDistanceFromOrigin() < 2.0f)
            return; // un clic con un temblor de 1 px no es una edición
        if (! dragMoved)
        {
            dragMoved = true;
            edits.begin();
        }
        const float fine = event.mods.isShiftDown() ? fineFactor : 1.0f;
        dragOctaves += static_cast<double> (delta.x * fine * octavesPerPixel());
        dragDb -= static_cast<double> (delta.y * fine * eqDbPerPixel());

        // Alt: solo el eje en que más se movió el mouse desde el clic.
        double octaves = dragOctaves, db = dragDb;
        if (event.mods.isAltDown())
        {
            if (std::abs (dragTotal.x) >= std::abs (dragTotal.y))
                db = 0.0;
            else
                octaves = 0.0;
        }
        // Ctrl con un solo nodo: imán a los picos del espectro (la frecuencia salta al pico más cercano; al alejarse,
        // vuelve a seguir al mouse porque lo acumulado no se toca).
        const bool snap = event.mods.isCommandDown() && selection.count() == 1;
        for (int b = 0; b < dsp::Engine::numBands; ++b)
            if (selection.test (static_cast<size_t> (b)))
            {
                const auto& start = dragStart[static_cast<size_t> (b)];
                const auto moved = edit::dragged (start, octaves, db / nodeScale(), minHz, maxHz);
                edits.set (b, params::frequency, snap ? snappedHz (moved.frequency) : moved.frequency);
                if (dsp::usesGain (start.shape))
                    edits.set (b, params::gain, moved.gainDb);
            }
        refreshNow();
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (drag == Drag::lasso && lassoMoved)
            finishLasso (lassoRectangle (event.position));
        if (drag == Drag::nodes && dragMoved && ! wheelEditing)
            edits.end();
        if (drag == Drag::draw) // la vista previa se calcula al soltar (ajustar mientras se dibuja trabaría el trazo)
        {
            wantedDirty = previewDirty = true;
            lastFitSeconds = 0.0;
        }
        drag = Drag::none;
        grabbing = false;
        repaint (plot.toNearestInt());
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        if (! plot.contains (event.position) || event.mods.isPopupMenu() || lastClickGrabbed || tool == Tool::draw)
            return;
        if (const int node = nodeAt (event.position); node >= 0)
        {
            const bool on = processor.getBandSettings (node).enabled;
            edits.begin();
            edits.set (node, params::enabled, on ? 0.0f : 1.0f);
            edits.end();
            refreshNow();
            return;
        }
        createBand (event.position, edit::shapeForNewBand (xToHz (event.position.x)));
    }

    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        const int node = tool == Tool::draw ? -1 : nodeAt (event.position);
        if (node < 0)
            return;
        if (! selection.test (static_cast<size_t> (node)))
            selectOnly (node);
        // Rueda hacia arriba = Q más alto (más angosta). Cada "muesca" (~0.23 en Windows) multiplica el Q por ~1.18: de
        // Q 1 a Q 10 en unas 14 muescas.
        const float amount = (wheel.isReversed ? -wheel.deltaY : wheel.deltaY) * (event.mods.isShiftDown() ? fineFactor : 1.0f);
        if (amount == 0.0f)
            return;
        const double factor = std::exp2 (static_cast<double> (amount));
        if (! edits.isActive())
            edits.begin();
        wheelEditing = true;
        lastWheelSeconds = nowSeconds();
        for (int b = 0; b < dsp::Engine::numBands; ++b)
            if (selection.test (static_cast<size_t> (b)))
            {
                const auto settings = processor.getBandSettings (b); // el valor actual (puede haber varias muescas por cuadro)
                if (dsp::usesQ (settings.shape, settings.slopeIndex))
                    edits.set (b, params::q, edit::scaledQ (settings.q, factor));
            }
        refreshNow();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        // Fase 12: Esc sale de la herramienta; dibujando, Supr borra el dibujo (no las bandas).
        if (key == juce::KeyPress::escapeKey && tool != Tool::none)
        {
            setTool (Tool::none);
            return true;
        }
        if (tool == Tool::draw && (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey))
        {
            clearDrawing();
            return true;
        }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            deleteSelection();
            return true;
        }
        if (commandLetter (key) == 'a')
        {
            selectAllInUse();
            return true;
        }
        if (key == juce::KeyPress::escapeKey)
        {
            selectOnly (focusedBand);
            return true;
        }
        return false; // Ctrl+Z y demás siguen hacia el editor
    }

    // La banda que muestra el panel: su nodo y su relleno se resaltan (y se ve aunque esté libre). Si no estaba entre
    // las elegidas, pasa a ser la única.
    void setSelectedBand (int band)
    {
        focusedBand = band;
        if (! selection.test (static_cast<size_t> (band)))
        {
            selection.reset();
            selection.set (static_cast<size_t> (band));
        }
        repaint();
    }

    // Avisa al editor de qué banda mostrar en el panel (un clic en un nodo, una banda creada...).
    std::function<void (int)> onBandFocused;

    // ---- Fase 12: igualar y dibujar ------------------------------------------------------------------------------------

    enum class Tool
    {
        none,  // edición normal
        match, // IGUALAR: la curva sale de los espectros aprendidos (los nodos se siguen editando)
        draw,  // DIBUJAR: arrastrar sobre el visor traza la curva (los nodos quedan quietos)
    };

    Tool getTool() const noexcept { return tool; }

    void setTool (Tool newTool)
    {
        if (newTool == tool)
            return;
        if (drag == Drag::nodes && dragMoved && ! wheelEditing) // Esc a mitad de un arrastre: el paso de deshacer se cierra
            edits.end();
        drag = Drag::none;
        grabbing = false;
        tool = newTool;
        auto& tools = processor.getCurveTools();
        if (tool != Tool::match) // sin la herramienta a la vista no se aprende (nadie ve cuánto lleva)
            tools.learningReference = tools.learningCurrent = false;
        matchButton.setToggleState (tool == Tool::match, juce::dontSendNotification);
        drawButton.setToggleState (tool == Tool::draw, juce::dontSendNotification);
        hoveredPeak = -1;
        wantedDirty = previewDirty = true;
        preview = {};
        previewDb.clear();
        if (onToolChanged)
            onToolChanged (tool);
        repaint();
    }

    std::function<void (Tool)> onToolChanged;

    // Lo que el panel de la herramienta muestra.
    struct CurveStatus
    {
        bool hasWanted = false;   // hay una curva que pedir (aprendida o dibujada)
        bool previewReady = false; // la vista previa corresponde a lo de ahora
        int bands = 0;            // las bandas que se crearían
        int freeBands = 0;        // las que hay disponibles
        float startErrorDb = 0.0f, maxErrorDb = 0.0f;
    };

    CurveStatus getCurveStatus() const
    {
        CurveStatus status;
        status.hasWanted = std::any_of (wanted.weight.begin(), wanted.weight.end(), [] (float w) { return w > 0.0f; });
        status.previewReady = ! previewDirty;
        status.bands = static_cast<int> (preview.bands.size());
        status.freeBands = freeBandCount();
        status.startErrorDb = preview.startErrorDb;
        status.maxErrorDb = preview.maxErrorDb;
        return status;
    }

    // El panel cambió algo de CurveTools (aprender, cantidad, suavizado, máximo, reemplazar, borrar).
    void curveSettingsChanged()
    {
        wantedDirty = previewDirty = true;
        repaint();
    }

    void clearDrawing()
    {
        processor.getCurveTools().clearDrawing();
        curveSettingsChanged();
    }

    // CREAR BANDAS: las de la vista previa (calculada de nuevo, por si algo cambió), en bandas libres, como un solo paso de
    // deshacer. Quedan elegidas (para moverlas juntas) y se vuelve a la edición normal. Devuelve cuántas se crearon.
    int createBandsFromCurve()
    {
        refreshBands (processor.getAnalyzerSampleRate());
        if (tool == Tool::match)
            updateWanted();
        computePreview();
        if (preview.bands.empty())
            return 0;
        auto& tools = processor.getCurveTools();
        const float scale = curveScale();
        edits.begin();
        if (tools.replace)
            for (int b = 0; b < dsp::Engine::numBands; ++b)
                if (inUse (b))
                    edits.reset (b);
        std::bitset<dsp::Engine::numBands> created;
        size_t next = 0;
        for (int b = 0; b < dsp::Engine::numBands && next < preview.bands.size(); ++b)
        {
            if (! tools.replace && inUse (b))
                continue;
            auto settings = preview.bands[next++];
            // Las bandas se ajustaron en dB "como suenan": con la escala de ganancia, el parámetro es eso dividido por ella.
            settings.gainDb = juce::jlimit (dsp::BandSettings::minGainDb, dsp::BandSettings::maxGainDb, settings.gainDb / scale);
            edits.setSettings (b, settings);
            created.set (static_cast<size_t> (b));
        }
        edits.end();
        if (tool == Tool::draw)
            tools.clearDrawing();
        const int count = static_cast<int> (created.count());
        setTool (Tool::none);
        for (int b = 0; b < dsp::Engine::numBands; ++b)
            if (created.test (static_cast<size_t> (b)))
            {
                selectOnly (b);
                break;
            }
        selection |= created;
        refreshNow();
        repaint();
        return count;
    }

private:
    struct Trace
    {
        deepwater::dsp::SpectrumAnalyzer analyzer;
        std::vector<float> shown;                 // lo que se dibuja, en dB (con la inclinación), un valor por punto
        std::vector<analysis::HeldPoint> held;    // la retención de picos de cada punto
        std::uint64_t lastWritten = 0;
        double secondsWithoutAudio = 0.0;
    };

    std::array<Trace*, 4> traces() noexcept { return { &input, &output, &sidechain, &outputSide }; }

    enum class Drag
    {
        none,
        nodes, // moviendo los nodos elegidos
        lasso, // rectángulo de selección
        draw,  // Fase 12: trazando (o borrando) la curva a mano
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

    static double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    void repaintReadoutAndAxis()
    {
        repaint (titleArea.toNearestInt());
        repaint (pianoArea.toNearestInt());
        repaint (zoneArea.toNearestInt());
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
    float octavesPerPixel() const noexcept { return std::log2 (maxHz / minHz) / plot.getWidth(); }
    float eqDbPerPixel() const noexcept { return 2.0f * eqRange() / plot.getHeight(); }

    // Las dos escalas verticales: la del EQ (0 dB al centro) y la del analizador (0 dBFS arriba).
    float eqDbToY (float db) const noexcept { return plot.getCentreY() - db / eqRange() * plot.getHeight() * 0.5f; }
    float yToEqDb (float y) const noexcept { return (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * eqRange(); }
    float analyzerBottomDb() const noexcept { return analyzerTopDb - view.range(); }
    float analyzerDbToY (float db) const noexcept
    {
        return plot.getY() + plot.getHeight() * (analyzerTopDb - db) / view.range();
    }
    float yToAnalyzerDb (float y) const noexcept
    {
        return analyzerTopDb - (y - plot.getY()) / plot.getHeight() * view.range();
    }

    // ---- Selección y edición ------------------------------------------------------------------------------------------

    void focus (int band)
    {
        focusedBand = band;
        if (onBandFocused)
            onBandFocused (band); // el editor muestra la banda en el panel (y llama a setSelectedBand)
        repaint();
    }

    void selectOnly (int band)
    {
        selection.reset();
        selection.set (static_cast<size_t> (band));
        focus (band);
    }

    // Ctrl+clic: agrega o quita. La del panel siempre está elegida: si se quita, el panel pasa a otra de las elegidas.
    void toggleInSelection (int band)
    {
        const auto index = static_cast<size_t> (band);
        if (! selection.test (index))
        {
            selection.set (index);
            focus (band);
            return;
        }
        if (selection.count() == 1)
            return;
        selection.reset (index);
        if (band == focusedBand)
            for (int b = 0; b < dsp::Engine::numBands; ++b)
                if (selection.test (static_cast<size_t> (b)))
                {
                    focus (b);
                    break;
                }
        repaint();
    }

    void selectAllInUse()
    {
        for (int b = 0; b < dsp::Engine::numBands; ++b)
            if (inUse (b))
                selection.set (static_cast<size_t> (b));
        repaint();
    }

    juce::Rectangle<float> lassoRectangle (juce::Point<float> end) const
    {
        return juce::Rectangle<float> (dragOrigin, end).getIntersection (plot);
    }

    void finishLasso (juce::Rectangle<float> area)
    {
        std::bitset<dsp::Engine::numBands> caught;
        for (int b = 0; b < dsp::Engine::numBands; ++b)
            if (nodeVisible (b) && area.contains (nodePosition (b)))
                caught.set (static_cast<size_t> (b));
        if (caught.none())
            return;
        if (! lassoAdds)
            selection.reset();
        selection |= caught;
        if (! selection.test (static_cast<size_t> (focusedBand)))
            for (int b = 0; b < dsp::Engine::numBands; ++b)
                if (caught.test (static_cast<size_t> (b)))
                {
                    focus (b);
                    break;
                }
        repaint();
    }

    void startNodeDrag (juce::Point<float> position)
    {
        drag = Drag::nodes;
        dragLast = position;
        dragTotal = {};
        dragOctaves = dragDb = 0.0;
        dragMoved = false;
        for (int b = 0; b < dsp::Engine::numBands; ++b)
            dragStart[static_cast<size_t> (b)] = processor.getBandSettings (b);
    }

    // Crear una banda donde se hizo clic: la primera libre, encendida, con la forma pedida y (si tiene) la ganancia de
    // la altura del clic. Sin bandas libres no pasa nada.
    void createBand (juce::Point<float> position, dsp::Shape shape)
    {
        std::array<bool, dsp::Engine::numBands> used {};
        for (int b = 0; b < dsp::Engine::numBands; ++b)
            used[static_cast<size_t> (b)] = inUse (b);
        const int band = edit::freeBand (used);
        if (band < 0)
            return;
        auto settings = processor.getDefaultBandSettings (band);
        settings.enabled = true;
        settings.shape = shape;
        settings.frequency = juce::jlimit (minHz, maxHz, xToHz (position.x));
        if (dsp::usesGain (shape))
            settings.gainDb = juce::jlimit (dsp::BandSettings::minGainDb, dsp::BandSettings::maxGainDb,
                                            (yToEqDb (position.y) - paintedGainDb) * edit::gainPerNodeDb (shape) / nodeScale());
        if (shape == dsp::Shape::lowShelf || shape == dsp::Shape::highShelf || shape == dsp::Shape::lowCut
            || shape == dsp::Shape::highCut)
            settings.q = 0.71f; // sin rebote (Butterworth en los cortes)
        edits.begin();
        edits.setSettings (band, settings);
        edits.end();
        selectOnly (band);
        refreshNow();
    }

    void deleteSelection()
    {
        edits.begin();
        for (int b = 0; b < dsp::Engine::numBands; ++b)
            if (selection.test (static_cast<size_t> (b)))
                edits.reset (b);
        edits.end();
        selectOnly (focusedBand);
        refreshNow();
    }

    // Aplica 'change' a cada banda elegida, como un solo paso de deshacer.
    template <typename Change>
    void editSelection (Change&& change)
    {
        edits.begin();
        for (int b = 0; b < dsp::Engine::numBands; ++b)
            if (selection.test (static_cast<size_t> (b)))
                change (b, processor.getBandSettings (b));
        edits.end();
        refreshNow();
    }

    // El teclado del eje: la banda del panel, justo en la nota (y encendida: es una orden explícita sobre ella).
    void tuneToNote (int note)
    {
        const float hz = static_cast<float> (edit::noteToHz (note));
        if (hz < minHz * 0.97f || hz > maxHz * 1.03f)
            return;
        edits.begin();
        edits.set (focusedBand, params::frequency, hz);
        edits.set (focusedBand, params::enabled, 1.0f);
        edits.end();
        refreshNow();
    }

    void showNodeMenu()
    {
        const auto settings = processor.getBandSettings (focusedBand);
        const bool several = selection.count() > 1;
        juce::PopupMenu menu;
        menu.addSectionHeader (several ? juce::String (static_cast<int> (selection.count())) + " bandas elegidas"
                                       : "Banda " + juce::String (focusedBand + 1));
        menu.addItem (settings.enabled ? "Apagar" : "Encender", guarded ([this, on = ! settings.enabled] {
            editSelection ([this, on] (int b, const dsp::BandSettings&) { edits.set (b, params::enabled, on ? 1.0f : 0.0f); });
        }));
        juce::PopupMenu shapes;
        for (int s = 0; s < dsp::numShapes; ++s)
            shapes.addItem (juce::String::fromUTF8 (format::shapeNames[static_cast<size_t> (s)]), true,
                            static_cast<int> (settings.shape) == s, guarded ([this, s] {
                                editSelection ([this, s] (int b, const dsp::BandSettings&) {
                                    edits.set (b, params::shape, static_cast<float> (s));
                                });
                            }));
        menu.addSubMenu ("Forma", shapes);
        juce::PopupMenu slopes;
        for (size_t s = 0; s < dsp::slopes.size(); ++s)
            slopes.addItem (juce::String (dsp::slopes[s]) + " dB/oct", true, settings.slopeIndex == static_cast<int> (s), guarded ([this, s] {
                editSelection ([this, s] (int b, const dsp::BandSettings&) {
                    edits.set (b, params::slope, static_cast<float> (s));
                });
            }));
        menu.addSubMenu ("Pendiente", slopes, dsp::usesSlope (settings.shape));
        juce::PopupMenu placements;
        for (int p = 0; p < dsp::numPlacements; ++p)
            placements.addItem (juce::String::fromUTF8 (format::placementNames[static_cast<size_t> (p)]), true,
                                static_cast<int> (settings.placement) == p, guarded ([this, p] {
                                    editSelection ([this, p] (int b, const dsp::BandSettings&) {
                                        edits.set (b, params::stereo, static_cast<float> (p));
                                    });
                                }));
        menu.addSubMenu ("Canal", placements);
        // Fase 9: encender o apagar la dinámica de las elegidas (las que no tienen ganancia no cambian).
        menu.addItem (juce::String::fromUTF8 ("EQ din\xc3\xa1mico"), dsp::canBeDynamic (settings.shape), settings.dynamics.enabled,
                      guarded ([this, on = ! settings.dynamics.enabled] {
                          editSelection ([this, on] (int b, const dsp::BandSettings& s) {
                              if (dsp::canBeDynamic (s.shape))
                                  edits.set (b, params::dynamic, on ? 1.0f : 0.0f);
                          });
                      }));
        // Fase 11: la dinámica espectral (enciende también la dinámica; solo campanas y shelves).
        menu.addItem (juce::String::fromUTF8 ("Din\xc3\xa1mica espectral"), dsp::canBeSpectral (settings.shape),
                      settings.dynamics.enabled && settings.dynamics.spectral, guarded ([this, on = ! (settings.dynamics.enabled && settings.dynamics.spectral)] {
                          editSelection ([this, on] (int b, const dsp::BandSettings& s) {
                              if (! dsp::canBeSpectral (s.shape))
                                  return;
                              edits.set (b, params::spectral, on ? 1.0f : 0.0f);
                              if (on)
                                  edits.set (b, params::dynamic, 1.0f);
                          });
                      }));
        menu.addItem (juce::String::fromUTF8 ("Afinar a la nota m\xc3\xa1s cercana (") + format::noteName (edit::nearestNoteHz (settings.frequency))
                          + ")",
                      guarded ([this] {
                          editSelection ([this] (int b, const dsp::BandSettings& s) {
                              edits.set (b, params::frequency, edit::nearestNoteHz (s.frequency));
                          });
                      }));
        const bool soloing = processor.getSoloBand() >= 0;
        menu.addItem (juce::String::fromUTF8 ("Escuchar su zona (SOLO)"), dsp::canSolo (settings.shape), soloing,
                      guarded ([this, soloing] { processor.setSoloBand (soloing ? -1 : focusedBand); }));
        menu.addSeparator();
        menu.addItem (several ? "Borrar las elegidas (Supr)" : "Borrar (Supr)", guarded ([this] { deleteSelection(); }));
        showMenu (menu);
    }

    void showEmptyMenu (juce::Point<float> position)
    {
        juce::PopupMenu create;
        for (int s = 0; s < dsp::numShapes; ++s)
            create.addItem (juce::String::fromUTF8 (format::shapeNames[static_cast<size_t> (s)]),
                            guarded ([this, s, position] { createBand (position, static_cast<dsp::Shape> (s)); }));
        juce::PopupMenu menu;
        menu.addSubMenu (juce::String::fromUTF8 ("Crear aqu\xc3\xad (") + format::hz (xToHz (position.x)) + ")", create);
        menu.addItem ("Elegir todas (Ctrl+A)", guarded ([this] { selectAllInUse(); }));
        showMenu (menu);
    }

    // Las acciones del menú llegan después (el menú es asíncrono): si el visor ya no existe, no hacen nada.
    template <typename Action>
    std::function<void()> guarded (Action&& action)
    {
        return [safe = juce::Component::SafePointer<EqDisplay> (this), action = std::forward<Action> (action)] {
            if (safe != nullptr)
                action();
        };
    }

    void showMenu (juce::PopupMenu& menu)
    {
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    // ---- Bandas y curvas -----------------------------------------------------------------------------------------------

    // Trae los ajustes de las bandas y recalcula las curvas que cambiaron. Devuelve true si algo se movió.
    bool refreshBands (double sampleRate)
    {
        const float gainDb = outputGain->load();
        const float scale = gainScale->load() * 0.01f;
        const bool allBands = sampleRate != curveSampleRate || scale != paintedScale;
        bool changed = gainDb != paintedGainDb || allBands;
        paintedGainDb = gainDb;
        paintedScale = scale;
        curveSampleRate = sampleRate;
        for (int b = 0; b < dsp::Engine::numBands; ++b)
        {
            const auto index = static_cast<size_t> (b);
            const auto settings = processor.getBandSettings (b);
            // Fase 9: lo que la dinámica le suma ahora (0 si no es dinámica). Cambios de menos de 0.05 dB no se ven.
            // Las espectrales (Fase 11) no mueven la banda entera: su curva en vivo es la de refreshSpectral.
            const bool dynamic = settings.enabled && settings.dynamics.enabled && dsp::canBeDynamic (settings.shape) && ! dsp::isSpectral (settings);
            const float live = dynamic ? processor.getDynamicState (b).gainDb : 0.0f;
            const bool moved = std::abs (live - liveDb[index]) >= 0.05f || (live == 0.0f) != (liveDb[index] == 0.0f);
            const bool edited = ! sameSettings (settings, paintedBands[index]);
            // Fase 12: lo que piden las herramientas depende de las bandas de ahora (no de lo que mueve la dinámica).
            previewDirty = previewDirty || edited || allBands;
            if (allBands || moved || edited)
            {
                paintedBands[index] = settings;
                liveDb[index] = live;
                updateBandCurve (b);
                changed = true;
            }
        }
        // Fase 10: el modo de fase cambia la curva total (la del FIR).
        const auto mode = processor.getPhaseMode();
        const int quality = processor.getLinearQuality();
        if (mode != paintedMode || quality != paintedQuality)
        {
            paintedMode = mode;
            paintedQuality = quality;
            changed = true;
        }
        if (changed)
        {
            phaseDirty = true;
            updateTotalCurve();
        }
        return changed;
    }

    // Fase 11: lo que mueve ahora cada banda espectral (de la grilla del audio a los puntos de la curva, interpolando en
    // log). Una banda que no mueve nada queda sin curva espectral. Devuelve true si algo cambió.
    bool refreshSpectral()
    {
        if (! processor.readSpectralSnapshot (spectralSnapshot))
            return false;
        const auto points = curveDb.size();
        if (points < 2)
            return false;
        bool changed = false;
        constexpr int last = dsp::SpectralStage::gridPoints - 1;
        const double span = std::log (dsp::SpectralStage::gridMaxHz / dsp::SpectralStage::gridMinHz);
        for (size_t b = 0; b < spectralCurveDb.size(); ++b)
        {
            const auto& grid = spectralSnapshot.db[b];
            auto& curve = spectralCurveDb[b];
            const float now = isSpectralBand (static_cast<int> (b)) ? processor.getDynamicState (static_cast<int> (b)).gainDb : 0.0f;
            changed = changed || now != spectralNowDb[b];
            spectralNowDb[b] = now;
            const bool moving = isSpectralBand (static_cast<int> (b)) && std::any_of (grid.begin(), grid.end(), [] (float db) { return db != 0.0f; });
            if (! moving)
            {
                changed = changed || ! curve.empty();
                curve.clear();
                continue;
            }
            curve.resize (points);
            for (size_t i = 0; i < points; ++i)
            {
                const double position = std::clamp (std::log (pointHz (i, points) / dsp::SpectralStage::gridMinHz) / span * last, 0.0,
                                                    static_cast<double> (last));
                const auto below = static_cast<size_t> (position);
                const auto above = std::min (below + 1, static_cast<size_t> (last));
                const auto t = static_cast<float> (position - static_cast<double> (below));
                curve[i] = grid[below] + (grid[above] - grid[below]) * t;
            }
            changed = true;
        }
        if (changed)
            updateTotalCurve();
        return changed;
    }

    // Lo que mueve ahora una banda espectral en una frecuencia (para la lectura de arriba).
    float spectralDbAt (size_t b, float hz) const noexcept
    {
        const auto& curve = spectralCurveDb[b];
        if (curve.size() != curveDb.size() || curve.size() < 2)
            return 0.0f;
        const double position = std::clamp (std::log (static_cast<double> (hz / minHz)) / std::log (static_cast<double> (maxHz / minHz))
                                                * static_cast<double> (curve.size() - 1),
                                            0.0, static_cast<double> (curve.size() - 1));
        const auto below = static_cast<size_t> (position);
        const auto above = std::min (below + 1, curve.size() - 1);
        return curve[below] + (curve[above] - curve[below]) * static_cast<float> (position - static_cast<double> (below));
    }

    // Fase 10: con fase lineal o natural, la diferencia entre la curva de las bandas y la del FIR que suena (en dB por
    // punto). Diseñar el FIR cuesta (hasta decenas de ms con calidad máxima): como mucho cada 0.12 s, y solo si algo
    // cambió. Devuelve true si la curva cambió.
    bool refreshPhaseCurve (double sampleRate)
    {
        if (paintedMode == dsp::PhaseMode::zeroLatency)
        {
            if (phaseDb.empty())
                return false;
            phaseDb.clear();
            updateTotalCurve();
            return true;
        }
        if (! phaseDirty || nowSeconds() - lastPhaseSeconds < phaseRefreshSeconds || curveDb.size() < 2 || sampleRate <= 0.0)
            return false;
        phaseDirty = false;
        lastPhaseSeconds = nowSeconds();
        if (phaseDesigner == nullptr || phaseDesigner->getSampleRate() != sampleRate)
        {
            phaseDesigner = std::make_unique<dsp::PhaseDesigner>(); // fuera del audio: aquí se puede reservar
            phaseDesigner->prepare (sampleRate);
        }
        // El mismo pedido que hace el motor: las bandas con la escala de ganancia y la dinámica de ahora.
        dsp::DesignRequest request;
        request.mode = paintedMode;
        request.quality = paintedQuality;
        for (size_t b = 0; b < paintedBands.size(); ++b)
        {
            request.bands[b] = paintedBands[b];
            request.bands[b].gainDb *= paintedScale;
            request.dynamicDb[b] = liveDb[b];
        }
        const auto result = phaseDesigner->designImpulses (request);
        phaseDesigner->analyse (0, result.length);
        const auto points = curveDb.size();
        phaseDb.assign (points, 0.0f);
        for (size_t i = 0; i < points; ++i)
        {
            float bands = 0.0f; // sin la parte espectral (Fase 11): esa se suma aparte, después del FIR
            for (const auto& curve : bandCurveDb)
                bands += curve[i];
            phaseDb[i] = phaseCorrectionDb (pointHz (i, points), bands);
        }
        updateTotalCurve();
        return true;
    }

    // Lo que el FIR le cambia a la curva de las bandas ('bandsDb') en una frecuencia: con fase lineal el FIR ES toda la
    // respuesta (se reemplaza); con natural, el FIR es la corrección que se suma.
    float phaseCorrectionDb (double hz, float bandsDb) const noexcept
    {
        const float fir = phaseDesigner->analysedDb (hz);
        return paintedMode == dsp::PhaseMode::linear ? fir - bandsDb : fir;
    }

    // ¿La curva total lleva la corrección del FIR? (Con curvas por canal, no: esas se quedan con las bandas.)
    bool phaseCurveShown() const noexcept { return ! phaseDb.empty() && phaseDb.size() == curveDb.size() && ! splitCurves(); }

    // Después de editar: la curva y los nodos al instante, sin esperar al siguiente cuadro.
    void refreshNow()
    {
        const bool changed = refreshBands (processor.getAnalyzerSampleRate());
        if (drag == Drag::none)
            hoveredNode = nodeAt (mouse); // la banda pudo moverse (o borrarse) bajo el mouse quieto
        if (changed)
            repaint();
    }

    // Devuelve true si algún punto se movió de forma visible (0.05 dB: menos de un píxel). Congelado (o mientras se
    // arrastra un pico agarrado) no se toca nada: ni el análisis ni lo dibujado.
    bool updateTrace (Trace& trace, const FathomAudioProcessor::AnalyzerTap& tap, double sampleRate, double elapsed)
    {
        if (frozen || grabbing)
            return false;
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
        const double ratio = pointRatio();
        const double halfStep = std::sqrt (ratio);
        const auto& speed = view.speed();
        const auto holdMode = view.holdMode();
        bool changed = false;
        for (size_t i = 0; i < points; ++i)
        {
            const double centre = minHz * std::pow (ratio, static_cast<double> (i));
            const float level = silent ? deepwater::dsp::SpectrumAnalyzer::floorDb
                                       : trace.analyzer.levelDb (centre / halfStep, centre * halfStep, sampleRate)
                                             + analysis::tiltDb (centre, view.tilt());
            float& shown = trace.shown[i];
            const float previous = shown;
            const float previousHeld = trace.held[i].db;
            shown = analysis::follow (shown, level, elapsed, speed);
            analysis::hold (trace.held[i], shown, elapsed, holdMode);
            changed = changed || std::abs (shown - previous) >= 0.05f || std::abs (trace.held[i].db - previousHeld) >= 0.05f;
        }
        return changed;
    }

    // Razón de frecuencia entre dos puntos vecinos de las curvas (uno cada 2 píxeles).
    double pointRatio() const noexcept
    {
        const auto points = input.shown.size();
        return points < 2 ? 1.0 : std::pow (static_cast<double> (maxHz / minHz), 1.0 / static_cast<double> (points - 1));
    }

    // ---- Picos del espectro --------------------------------------------------------------------------------------------

    // De qué espectro se agarran los picos: la salida si se ve (lo que se oye), si no la entrada, si no el sidechain.
    const Trace* grabTrace() const noexcept
    {
        return view.showOutput ? &output : view.showInput ? &input : view.showSidechain ? &sidechain : nullptr;
    }

    // Busca los picos del espectro solo mientras el mouse está en el visor (si no, nadie los va a agarrar).
    bool updatePeaks()
    {
        const int previous = hoveredPeak;
        peaks.clear();
        const auto* trace = grabTrace();
        if (trace != nullptr && plot.contains (mouse) && drag != Drag::lasso)
        {
            const double pointsPerOctave = static_cast<double> (trace->shown.size() - 1) / std::log2 (static_cast<double> (maxHz / minHz));
            const auto reach = static_cast<size_t> (std::max (2.0, std::round (pointsPerOctave * peakReachOctaves)));
            peaks = analysis::findPeaks (trace->shown, reach, peakProminenceDb, analyzerBottomDb() + peakProminenceDb);
        }
        if (! grabbing)
            hoveredPeak = drag == Drag::none ? pickPeak() : -1;
        return hoveredPeak != previous || (hoveredPeak >= 0 && ! frozen && ! grabbing);
    }

    // El pico bajo el mouse (índice en 'peaks') o −1: cerca en horizontal y a menos de 40 px de la línea del espectro.
    // Un nodo bajo el mouse tiene prioridad.
    int pickPeak() const noexcept
    {
        const auto* trace = grabTrace();
        if (tool == Tool::draw || trace == nullptr || peaks.empty() || ! plot.contains (mouse) || nodeAt (mouse) >= 0 || trace->shown.size() < 2)
            return -1;
        const float step = plot.getWidth() / static_cast<float> (trace->shown.size() - 1);
        const int peak = analysis::nearestPeak (peaks, static_cast<double> ((mouse.x - plot.getX()) / step), static_cast<double> (peakGrabPixels / step));
        if (peak < 0 || std::abs (analyzerDbToY (trace->shown[peaks[static_cast<size_t> (peak)]]) - mouse.y) > 40.0f)
            return -1;
        return peak;
    }

    // La frecuencia exacta de un pico: la del punto se afina con los bins de la FFT (parábola por el bin más fuerte).
    float peakHz (size_t point) const
    {
        const auto* trace = grabTrace();
        const double ratio = pointRatio();
        const double centre = minHz * std::pow (ratio, static_cast<double> (point));
        if (trace == nullptr)
            return static_cast<float> (centre);
        const auto found = trace->analyzer.peak (centre / (ratio * ratio), centre * ratio * ratio, processor.getAnalyzerSampleRate());
        const bool plausible = found.hz >= centre / (ratio * ratio) && found.hz <= centre * ratio * ratio;
        return juce::jlimit (minHz, maxHz, static_cast<float> (plausible ? found.hz : centre));
    }

    // Agarrar un pico: una campana nueva justo encima (encendida, 0 dB, angosta) que sigue al mouse como un nodo
    // arrastrado; crear y arrastrar son un solo paso de deshacer. Mientras tanto el espectro se queda quieto.
    void grabPeak (juce::Point<float> position)
    {
        std::array<bool, dsp::Engine::numBands> used {};
        for (int b = 0; b < dsp::Engine::numBands; ++b)
            used[static_cast<size_t> (b)] = inUse (b);
        const int band = edit::freeBand (used);
        if (band < 0)
            return;
        auto settings = processor.getDefaultBandSettings (band);
        settings.enabled = true;
        settings.shape = dsp::Shape::bell;
        settings.frequency = peakHz (peaks[static_cast<size_t> (hoveredPeak)]);
        settings.gainDb = 0.0f;
        settings.q = grabbedQ;
        edits.begin();
        edits.setSettings (band, settings);
        selectOnly (band);
        refreshNow();
        startNodeDrag (position);
        dragMoved = true; // el paso de deshacer ya está abierto: lo cierra mouseUp
        grabbing = true;
        lastClickGrabbed = true;
        hoveredPeak = -1;
    }

    // Ctrl + arrastrar un nodo: la frecuencia del pico más cercano si está a menos de peakSnapPixels; si no, 'hz'.
    float snappedHz (float hz) const
    {
        const auto* trace = grabTrace();
        if (trace == nullptr || peaks.empty() || trace->shown.size() < 2)
            return hz;
        const float step = plot.getWidth() / static_cast<float> (trace->shown.size() - 1);
        const int peak = analysis::nearestPeak (peaks, static_cast<double> ((hzToX (hz) - plot.getX()) / step), static_cast<double> (peakSnapPixels / step));
        return peak < 0 ? hz : peakHz (peaks[static_cast<size_t> (peak)]);
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
        const float labelsY = pianoArea.getBottom() + 1.0f;
        for (const float hz : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const auto text = hz < 1000.0f ? juce::String (juce::roundToInt (hz)) : juce::String (juce::roundToInt (hz / 1000.0f)) + "k";
            g.drawText (text, juce::Rectangle<float> (hzToX (hz) - 20.0f, labelsY, 40.0f, 13.0f), juce::Justification::centred);
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

    // El teclado del eje: cada semitono es una columna del mismo ancho (el eje es logarítmico, como las notas). Las
    // teclas negras cubren la parte de arriba; abajo, las blancas se separan a la mitad de cada negra, como en un piano
    // de verdad. Cada Do lleva su nombre de FL; la tecla de la banda del panel se pinta de su color y la del mouse, del
    // acento.
    void paintPiano (juce::Graphics& g) const
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (pianoArea.toNearestInt());
        g.setColour (juce::Colour (0xffb9c2cc));
        g.fillRect (pianoArea);

        const auto keyLeft = [this] (int note) { return hzToX (static_cast<float> (edit::noteToHz (note - 0.5))); };
        const int firstNote = static_cast<int> (std::floor (edit::hzToNote (minHz)));
        const int lastNote = static_cast<int> (std::ceil (edit::hzToNote (maxHz)));
        const int focusedNote = juce::roundToInt (edit::hzToNote (paintedBands[static_cast<size_t> (focusedBand)].frequency));
        const int hoveredNote = pianoArea.contains (mouse) ? juce::roundToInt (edit::hzToNote (xToHz (mouse.x))) : -1000;
        const float blackBottom = pianoArea.getY() + pianoArea.getHeight() * 0.55f;

        for (int note = firstNote; note <= lastNote; ++note)
        {
            const float left = keyLeft (note), right = keyLeft (note + 1);
            const bool black = edit::isBlackKey (note);
            const auto key = juce::Rectangle<float> (left, pianoArea.getY(), right - left,
                                                     (black ? blackBottom : pianoArea.getBottom()) - pianoArea.getY());
            if (black)
            {
                g.setColour (juce::Colour (0xff1b2027));
                g.fillRect (key);
                // Debajo de la negra, la separación entre las dos blancas vecinas.
                g.setColour (juce::Colour (0xff6d7782));
                g.drawVerticalLine (juce::roundToInt ((left + right) * 0.5f), blackBottom, pianoArea.getBottom());
            }
            else if (! edit::isBlackKey (note - 1)) // E-F y B-C: dos blancas juntas
            {
                g.setColour (juce::Colour (0xff6d7782));
                g.drawVerticalLine (juce::roundToInt (left), pianoArea.getY(), pianoArea.getBottom());
            }
            const auto highlight = note == hoveredNote ? theme::accent.withAlpha (0.8f)
                                   : note == focusedNote && nodeVisible (focusedBand) ? theme::bandColour (focusedBand)
                                                                                        : juce::Colours::transparentBlack;
            if (! highlight.isTransparent())
            {
                g.setColour (highlight);
                g.fillRect (key);
            }
        }
        // Los nombres al final (encima de las teclas): cada Do, con el nombre de FL (C5 = Do central, nota MIDI 60).
        g.setColour (juce::Colour (0xff1b2027));
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        for (int note = firstNote; note <= lastNote; ++note)
            if (((note % 12) + 12) % 12 == 0)
                g.drawText ("C" + juce::String (note / 12),
                            juce::Rectangle<float> (keyLeft (note) + 1.0f, blackBottom - 1.5f, 24.0f, pianoArea.getBottom() - blackBottom + 1.5f),
                            juce::Justification::centredLeft, false);
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
               && a.slopeIndex == b.slopeIndex && a.analog == b.analog && a.placement == b.placement
               && a.dynamics.enabled == b.dynamics.enabled && a.dynamics.rangeDb == b.dynamics.rangeDb
               && a.dynamics.external == b.dynamics.external && a.dynamics.spectral == b.dynamics.spectral;
    }

    // Fase 9: una banda que se mueve sola (encendida, dinámica y con una forma que tiene ganancia).
    bool isDynamic (int b) const noexcept
    {
        const auto& s = paintedBands[static_cast<size_t> (b)];
        return s.enabled && s.dynamics.enabled && dsp::canBeDynamic (s.shape);
    }

    // Fase 11: una banda dinámica espectral.
    bool isSpectralBand (int b) const noexcept { return dsp::isSpectral (paintedBands[static_cast<size_t> (b)]); }

    // La curva propia de una banda más lo que mueve ahora su parte espectral (si mueve algo).
    std::vector<float> liveBandCurve (size_t b) const
    {
        auto curve = bandCurveDb[b];
        if (const auto& spectral = spectralCurveDb[b]; spectral.size() == curve.size())
            for (size_t i = 0; i < curve.size(); ++i)
                curve[i] += spectral[i];
        return curve;
    }

    // Una banda está "en uso" si está encendida o si se tocó algo (apagada con ajustes propios: su nodo se ve hueco).
    // Una libre (apagada y como nace) no se dibuja, salvo que la muestre el panel.
    bool inUse (int b) const noexcept
    {
        const auto& s = paintedBands[static_cast<size_t> (b)];
        const auto& d = processor.getDefaultBandSettings (b);
        const auto near = [] (float x, float y) { return std::abs (x - y) <= 1.0e-4f * std::max (1.0f, std::abs (y)); };
        return s.enabled || ! (near (s.frequency, d.frequency) && near (s.gainDb, d.gainDb) && near (s.q, d.q) && s.shape == d.shape
                               && s.slopeIndex == d.slopeIndex && s.placement == d.placement);
    }

    // La respuesta total en dB en una frecuencia: las bandas (con sus coeficientes reales) más la ganancia de salida.
    float totalDb (float hz) const noexcept
    {
        double bands = 0.0;
        for (int b = 0; b < dsp::Engine::numBands; ++b)
            if (paintedBands[static_cast<size_t> (b)].enabled)
                bands += bandCascades[static_cast<size_t> (b)].magnitudeDb (hz, curveSampleRate);
        if (phaseCurveShown()) // Fase 10: la del FIR
            bands += static_cast<double> (phaseCorrectionDb (hz, static_cast<float> (bands)));
        for (size_t b = 0; b < spectralCurveDb.size(); ++b) // Fase 11: lo que mueven ahora las espectrales
            bands += static_cast<double> (spectralDbAt (b, hz));
        return static_cast<float> (bands) + paintedGainDb;
    }

    // Las curvas por canal (Fase 8): L, R, M y S, cada una con las bandas estéreo y las de ese canal.
    static constexpr std::array<dsp::Placement, 4> curveChannels { dsp::Placement::left, dsp::Placement::right, dsp::Placement::mid,
                                                                   dsp::Placement::side };

    static bool actsOn (dsp::Placement band, size_t channel) noexcept
    {
        return band == dsp::Placement::stereo || band == curveChannels[channel];
    }

    bool splitCurves() const noexcept { return channelShown[0] || channelShown[2]; }

    // La respuesta de un canal en dB en una frecuencia (como totalDb).
    float channelDb (float hz, size_t channel) const noexcept
    {
        double total = static_cast<double> (paintedGainDb);
        for (size_t b = 0; b < paintedBands.size(); ++b)
            if (paintedBands[b].enabled && actsOn (paintedBands[b].placement, channel))
                total += bandCascades[b].magnitudeDb (hz, curveSampleRate) + static_cast<double> (spectralDbAt (b, hz));
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
        // Con la escala de ganancia: la curva es la que suena, no la de los números de la banda.
        auto settings = paintedBands[index];
        settings.gainDb *= paintedScale;
        // Fase 9: la curva (y la lectura de arriba) es la que suena AHORA, con la ganancia dinámica; el nodo se queda en
        // la ganancia propia (donde descansa la banda) y una línea marca hasta dónde la lleva el rango.
        bandCascades[index] = dsp::Band::coefficientsFor (settings, curveSampleRate, liveDb[index]);
        const auto restingCascade = liveDb[index] == 0.0f ? bandCascades[index] : dsp::Band::coefficientsFor (settings, curveSampleRate);
        // El nodo: sobre la curva de la banda en su frecuencia si la forma tiene ganancia (la campana en su pico, el
        // shelf en su punto medio, los tilts en su eje); los cortes, el notch, el pasa banda y el all-pass, en 0 dB.
        // Apagada, donde estaría encendida (hueco): se puede seguir editando.
        auto active = settings;
        active.enabled = true;
        nodeDb[index] = dsp::usesGain (settings.shape)
                            ? static_cast<float> ((settings.enabled ? restingCascade : dsp::Band::coefficientsFor (active, curveSampleRate))
                                                      .magnitudeDb (settings.frequency, curveSampleRate))
                            : 0.0f;
        rangeDb[index] = isDynamic (b) ? static_cast<float> (dsp::Band::coefficientsFor (settings, curveSampleRate,
                                                                                          settings.dynamics.rangeDb * paintedScale)
                                                                 .magnitudeDb (settings.frequency, curveSampleRate))
                                       : nodeDb[index];
        auto& curve = bandCurveDb[index];
        auto& resting = restingCurveDb[index];
        const auto points = curve.size();
        if (points < 2)
            return;
        resting.assign (liveDb[index] != 0.0f ? points : 0, 0.0f);
        for (size_t i = 0; i < points; ++i)
        {
            const double hz = pointHz (i, points);
            curve[i] = settings.enabled ? static_cast<float> (bandCascades[index].magnitudeDb (hz, curveSampleRate)) : 0.0f;
            if (! resting.empty())
                resting[i] = static_cast<float> (restingCascade.magnitudeDb (hz, curveSampleRate));
        }
    }

    void updateTotalCurve()
    {
        // Con bandas en un solo canal: las curvas de L y R (si hay bandas L o R) y las de M y S (si hay bandas M o S).
        bool leftRight = false, midSide = false;
        for (const auto& band : paintedBands)
        {
            leftRight = leftRight || (band.enabled && (band.placement == dsp::Placement::left || band.placement == dsp::Placement::right));
            midSide = midSide || (band.enabled && (band.placement == dsp::Placement::mid || band.placement == dsp::Placement::side));
        }
        channelShown = { leftRight, leftRight, midSide, midSide };

        const auto points = curveDb.size();
        const bool phase = phaseCurveShown();
        for (size_t i = 0; i < points; ++i)
        {
            float total = paintedGainDb;
            for (const auto& curve : bandCurveDb)
                total += curve[i];
            for (const auto& curve : spectralCurveDb) // Fase 11
                if (curve.size() == points)
                    total += curve[i];
            curveDb[i] = phase ? total + phaseDb[i] : total;
        }

        for (size_t c = 0; c < curveChannels.size(); ++c)
        {
            auto& curve = channelCurveDb[c];
            curve.assign (points, paintedGainDb);
            if (! channelShown[c])
                continue;
            for (size_t b = 0; b < paintedBands.size(); ++b)
                if (paintedBands[b].enabled && actsOn (paintedBands[b].placement, c))
                {
                    const bool moving = spectralCurveDb[b].size() == points;
                    for (size_t i = 0; i < points; ++i)
                        curve[i] += bandCurveDb[b][i] + (moving ? spectralCurveDb[b][i] : 0.0f);
                }
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

    bool nodeVisible (int b) const noexcept { return inUse (b) || b == focusedBand; }

    // La banda cuyo nodo está bajo 'position' (o −1). Si hay varios encima, el más cercano.
    int nodeAt (juce::Point<float> position) const noexcept
    {
        if (! plot.expanded (nodeRadius).contains (position))
            return -1;
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
    // salida; la del panel, más marcada), la respuesta total (blanca; pegada al borde si se sale de la escala) y encima
    // los nodos: llenos si la banda suena, huecos si está apagada; con anillo blanco los elegidos (más grueso el del
    // panel).
    void paintCurve (juce::Graphics& g) const
    {
        if (curveDb.size() < 2)
            return;
        const float baseY = juce::jlimit (plot.getY() + 1.0f, plot.getBottom() - 1.0f, eqDbToY (paintedGainDb));
        for (int b = 0; b < dsp::Engine::numBands; ++b)
        {
            if (! paintedBands[static_cast<size_t> (b)].enabled)
                continue;
            const bool focused = b == focusedBand;
            const bool selected = selection.test (static_cast<size_t> (b));
            const bool spectralMoving = spectralCurveDb[static_cast<size_t> (b)].size() == curveDb.size(); // Fase 11
            auto path = curvePath (spectralMoving ? liveBandCurve (static_cast<size_t> (b)) : bandCurveDb[static_cast<size_t> (b)], paintedGainDb);
            const auto colour = theme::bandColour (b);
            if (selected)
            {
                g.setColour (colour.withAlpha (focused ? 0.6f : 0.4f));
                g.strokePath (path, juce::PathStrokeType (1.0f));
            }
            path.lineTo (plot.getRight(), baseY);
            path.lineTo (plot.getX(), baseY);
            path.closeSubPath();
            g.setColour (colour.withAlpha (focused ? 0.22f : selected ? 0.16f : 0.10f));
            g.fillPath (path);
            // Fase 9: mientras la dinámica la mueve, la curva donde descansa queda punteada (cuánto se movió = la
            // distancia entre las dos).
            // Fase 11: con la espectral, la curva en reposo es la de la banda (sin lo que mueve cada frecuencia).
            if (const auto& resting = spectralMoving ? bandCurveDb[static_cast<size_t> (b)] : restingCurveDb[static_cast<size_t> (b)];
                resting.size() == curveDb.size())
            {
                juce::Path dashed;
                const float dashes[] { 3.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, curvePath (resting, paintedGainDb), dashes, 2);
                g.setColour (colour.withAlpha (0.55f));
                g.fillPath (dashed);
            }
        }
        const juce::PathStrokeType curveStroke (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        if (! splitCurves())
        {
            g.setColour (theme::text.withAlpha (0.9f));
            g.strokePath (curvePath (curveDb, 0.0f), curveStroke);
        }
        else
        {
            // Una curva por canal, con su letra en el borde derecho (corrida si dos quedan a la misma altura).
            static constexpr std::array<const char*, 4> letters { "L", "R", "M", "S" };
            const std::array<juce::Colour, 4> colours { theme::curveLeft, theme::curveRight, theme::text, theme::curveSide };
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            float lastLabelY = -100.0f;
            for (size_t c = 0; c < curveChannels.size(); ++c)
            {
                if (! channelShown[c])
                    continue;
                g.setColour (colours[c].withAlpha (0.9f));
                g.strokePath (curvePath (channelCurveDb[c], 0.0f), curveStroke);
                float y = juce::jlimit (plot.getY() + 14.0f, plot.getBottom() - 6.0f, eqDbToY (channelCurveDb[c].back()) - 8.0f);
                if (std::abs (y - lastLabelY) < 11.0f)
                    y = lastLabelY + 11.0f;
                lastLabelY = y;
                g.drawText (letters[c], juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ plot.getRight() - 9.0f, y }),
                            juce::Justification::centred);
            }
        }

        const auto lasso = drag == Drag::lasso && lassoMoved ? lassoRectangle (mouse) : juce::Rectangle<float>();
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        // Los elegidos al final y el del panel último de todos: quedan encima si se superponen con otros.
        for (int pass = 0; pass < 3; ++pass)
            for (int b = 0; b < dsp::Engine::numBands; ++b)
            {
                const bool focused = b == focusedBand;
                const bool selected = selection.test (static_cast<size_t> (b)) || (! lasso.isEmpty() && lasso.contains (nodePosition (b)));
                if (! nodeVisible (b) || pass != (focused ? 2 : selected ? 1 : 0))
                    continue;
                const bool enabled = paintedBands[static_cast<size_t> (b)].enabled;
                const auto colour = theme::bandColour (b);
                const float radius = nodeRadius + (b == hoveredNode ? 1.5f : 0.0f);
                const auto node = juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (nodePosition (b));
                // Fase 9: la banda dinámica lleva una línea desde el nodo hasta donde la lleva el rango, con una marca
                // al final y un punto donde está ahora.
                if (isDynamic (b))
                {
                    const auto centre = node.getCentre();
                    const float rangeY = juce::jlimit (plot.getY() + 2.0f, plot.getBottom() - 2.0f, eqDbToY (paintedGainDb + rangeDb[static_cast<size_t> (b)]));
                    g.setColour (colour.withAlpha (0.85f));
                    g.drawLine (centre.x, centre.y, centre.x, rangeY, 2.0f);
                    g.drawLine (centre.x - 5.0f, rangeY, centre.x + 5.0f, rangeY, 2.0f);
                    if (isSpectralBand (b))
                    {
                        // Fase 11: en la espectral, el punto marca lo que más se mueve ahora alguna frecuencia de la zona.
                        if (const float now = spectralNowDb[static_cast<size_t> (b)]; now != 0.0f)
                        {
                            const float nowY = eqDbToY (paintedGainDb + nodeDb[static_cast<size_t> (b)] + now);
                            g.setColour (theme::text);
                            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ centre.x, juce::jlimit (plot.getY() + 2.0f, plot.getBottom() - 2.0f, nowY) }));
                        }
                    }
                    else if (liveDb[static_cast<size_t> (b)] != 0.0f)
                    {
                        auto settings = paintedBands[static_cast<size_t> (b)];
                        settings.gainDb *= paintedScale;
                        const float nowY = eqDbToY (paintedGainDb + static_cast<float> (bandCascades[static_cast<size_t> (b)].magnitudeDb (
                                                                        settings.frequency, curveSampleRate)));
                        g.setColour (theme::text);
                        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ centre.x, juce::jlimit (plot.getY() + 2.0f, plot.getBottom() - 2.0f, nowY) }));
                    }
                }
                if (enabled)
                {
                    g.setColour (colour);
                    g.fillEllipse (node);
                }
                else
                {
                    g.setColour (theme::display.withAlpha (0.8f));
                    g.fillEllipse (node);
                }
                g.setColour (focused ? theme::text : selected ? theme::text.withAlpha (0.75f) : enabled ? colour.darker (0.6f) : colour.withAlpha (0.7f));
                g.drawEllipse (node, focused ? 2.2f : selected ? 1.6f : 1.2f);
                g.setColour (enabled ? juce::Colours::black.withAlpha (0.8f) : colour);
                g.drawText (juce::String (b + 1), node, juce::Justification::centred);
                // Fase 8: la letra del canal (L, R, M, S) arriba a la derecha del nodo; estéreo, sin letra.
                if (const auto placement = paintedBands[static_cast<size_t> (b)].placement; placement != dsp::Placement::stereo)
                {
                    g.setColour (colour);
                    g.drawText (format::placementLetters[static_cast<size_t> (placement)],
                                juce::Rectangle<float> (10.0f, 10.0f).withCentre (node.getTopRight().translated (1.0f, -1.0f)),
                                juce::Justification::centred);
                }
            }
    }

    // ---- Fase 12: igualar y dibujar ------------------------------------------------------------------------------------

    // La escala de ganancia con que se crean las bandas (con 0 % no se podría: lo poco que se deja es un mínimo).
    float curveScale() const noexcept { return std::max (paintedScale, 0.05f); }

    // Cuántas bandas se pueden crear: las libres, o todas si se reemplazan.
    int freeBandCount() const noexcept
    {
        if (processor.getCurveTools().replace)
            return dsp::Engine::numBands;
        int count = 0;
        for (int b = 0; b < dsp::Engine::numBands; ++b)
            count += inUse (b) ? 0 : 1;
        return count;
    }

    // Las bandas que cuentan como "lo que ya hay": las encendidas en estéreo, en reposo (sin lo que mueve la dinámica) y
    // con la escala de ganancia. Las de un solo canal no entran: las herramientas crean bandas estéreo.
    std::vector<deepwater::dsp::EqCascade> existingCascades() const
    {
        std::vector<deepwater::dsp::EqCascade> cascades;
        if (curveSampleRate <= 0.0)
            return cascades;
        for (auto settings : paintedBands)
            if (settings.enabled && settings.placement == dsp::Placement::stereo)
            {
                settings.gainDb *= paintedScale;
                cascades.push_back (dsp::Band::coefficientsFor (settings, curveSampleRate));
            }
        return cascades;
    }

    edit::CurveValues existingGrid() const
    {
        edit::CurveValues total {};
        for (const auto& cascade : existingCascades())
            for (int i = 0; i < edit::CurveGrid::points; ++i)
                total[static_cast<size_t> (i)] += static_cast<float> (cascade.magnitudeDb (edit::CurveGrid::hz (i), curveSampleRate));
        return total;
    }

    // Una vez por cuadro con una herramienta: aprender, recalcular lo que se pide y la vista previa. Devuelve true si
    // algo visible cambió.
    bool updateCurveTool (double sampleRate)
    {
        if (tool == Tool::none)
            return false;
        auto& tools = processor.getCurveTools();
        bool changed = false;
        const bool learning = tool == Tool::match && (tools.learningReference || tools.learningCurrent);
        if (tool == Tool::match)
        {
            tools.reference.prepare (sampleRate);
            tools.current.prepare (sampleRate);
            bool learned = false;
            if (tools.learningReference)
                learned = tools.reference.feed (tools.referenceSource == 0 ? processor.getSidechainTap() : processor.getInputTap());
            if (tools.learningCurrent)
                learned = tools.current.feed (processor.getInputTap()) || learned;
            wantedDirty = wantedDirty || learned;
            // Aprendiendo, la curva se recalcula 4 veces por segundo (cambia poco de un cuadro a otro).
            if (wantedDirty && (! learning || nowSeconds() - lastWantedSeconds >= wantedRefreshSeconds))
            {
                updateWanted();
                changed = true;
            }
        }
        else if (wantedDirty && drag != Drag::draw)
        {
            updateWanted();
            changed = true;
        }
        // La vista previa cuesta (de unos ms a ~0.2 s con 24 bandas): no mientras se aprende, se dibuja o se arrastra un
        // nodo, y como mucho 5 veces por segundo (girar una perilla del panel no la recalcula en cada paso).
        if (learning && ! previewDb.empty())
        {
            previewDb.clear();
            preview = {};
            changed = true;
        }
        if (previewDirty && ! learning && drag == Drag::none && ! wheelEditing && nowSeconds() - lastFitSeconds >= 0.2)
        {
            computePreview();
            changed = true;
        }
        return changed;
    }

    // Lo que se pide en total: la curva de igualar (y los espectros aprendidos para dibujarlos) o lo dibujado.
    void updateWanted()
    {
        wantedDirty = false;
        lastWantedSeconds = nowSeconds();
        previewDirty = true;
        const auto& tools = processor.getCurveTools();
        if (tool == Tool::draw)
        {
            wanted = edit::drawnTarget (tools, existingGrid());
            return;
        }
        if (tool != Tool::match)
        {
            wanted = {};
            return;
        }
        const double octaves = analysis::matchSmoothingOctaves[static_cast<size_t> (std::clamp (tools.smoothingIndex, 0, 3))];
        wanted = analysis::matchCurve (tools.reference, tools.current, octaves, tools.amount);
        const auto points = curveDb.size();
        const auto shown = [&] (const analysis::AverageSpectrum& spectrum, std::vector<float>& out) {
            out.clear();
            if (! spectrum.hasData() || points < 2)
                return;
            out.resize (points);
            for (size_t i = 0; i < points; ++i)
            {
                const double hz = pointHz (i, points);
                out[i] = spectrum.levelDb (hz, octaves) + analysis::tiltDb (hz, view.tilt());
            }
        };
        shown (tools.reference, referenceShown);
        shown (tools.current, currentShown);
    }

    // Ajusta las bandas a lo que se pide (edit/CurveFit.h) y la curva total que quedaría con ellas.
    void computePreview()
    {
        previewDirty = false;
        lastFitSeconds = nowSeconds();
        preview = {};
        previewDb.clear();
        if (curveSampleRate <= 0.0)
            return;
        const auto& tools = processor.getCurveTools();
        const auto existing = existingGrid();
        if (tool == Tool::draw) // lo no dibujado sigue a las bandas de ahora
            wanted = edit::drawnTarget (tools, existing);
        if (std::none_of (wanted.weight.begin(), wanted.weight.end(), [] (float w) { return w > 0.0f; }))
            return;
        edit::FitOptions options;
        options.sampleRate = curveSampleRate;
        options.analog = paintedBands[0].analog; // el ajuste global (todas las bandas lo tienen igual)
        options.maxBands = std::min (tools.maxBands, freeBandCount());
        options.maxGainDb = dsp::BandSettings::maxGainDb * curveScale();
        preview = edit::fitBands (edit::newBandsTarget (wanted, existing, tools.replace), options);

        const auto points = curveDb.size();
        if (points < 2)
            return;
        std::vector<deepwater::dsp::EqCascade> cascades;
        if (! tools.replace)
            cascades = existingCascades();
        for (const auto& band : preview.bands)
            cascades.push_back (dsp::Band::coefficientsFor (band, curveSampleRate));
        previewDb.assign (points, 0.0f);
        for (size_t i = 0; i < points; ++i)
            for (const auto& cascade : cascades)
                previewDb[i] += static_cast<float> (cascade.magnitudeDb (pointHz (i, points), curveSampleRate));
    }

    // Un tramo del trazo, desde el evento anterior del mouse: dibuja (lo que deben sumar las bandas en la altura del
    // mouse, sin la salida) o borra.
    void drawTo (juce::Point<float> position)
    {
        auto& tools = processor.getCurveTools();
        const auto hzAt = [this] (float x) { return static_cast<double> (xToHz (juce::jlimit (plot.getX(), plot.getRight(), x))); };
        const auto dbAt = [this] (float y) {
            return juce::jlimit (-eqRange(), eqRange(), yToEqDb (juce::jlimit (plot.getY(), plot.getBottom(), y))) - paintedGainDb;
        };
        if (erasing)
            tools.erase (hzAt (drawLast.x), hzAt (position.x));
        else
            tools.draw (hzAt (drawLast.x), dbAt (drawLast.y), hzAt (position.x), dbAt (position.y));
        drawLast = position;
        wantedDirty = previewDirty = true;
        repaint (plot.toNearestInt());
    }

    // Unos valores de la grilla (dB que suman las bandas) como líneas, cortadas donde 'keep' dice que no.
    template <typename Keep>
    juce::Path gridPath (const edit::CurveValues& db, Keep&& keep) const
    {
        juce::Path path;
        bool open = false;
        const float top = plot.getY() + 1.0f, bottom = plot.getBottom() - 1.0f;
        for (int i = 0; i < edit::CurveGrid::points; ++i)
        {
            const auto at = static_cast<size_t> (i);
            if (! keep (at))
            {
                open = false;
                continue;
            }
            const juce::Point<float> point { hzToX (static_cast<float> (edit::CurveGrid::hz (i))),
                                             juce::jlimit (top, bottom, eqDbToY (db[at] + paintedGainDb)) };
            if (open)
                path.lineTo (point);
            else
                path.startNewSubPath (point);
            open = true;
        }
        return path;
    }

    // IGUALAR: los espectros aprendidos (referencia ámbar, pista gris punteada, en la escala del analizador) y la curva
    // que se pide (naranja punteada). DIBUJAR: el trazo (naranja; un punto suelto, un círculo). Las dos: la vista previa
    // (verde: la curva total con las bandas que se crearían).
    void paintCurveTool (juce::Graphics& g) const
    {
        if (tool == Tool::none || curveDb.size() < 2)
            return;
        const auto& tools = processor.getCurveTools();
        const float dashes[] { 5.0f, 4.0f };
        if (tool == Tool::match)
        {
            if (referenceShown.size() == curveDb.size())
            {
                g.setColour (theme::spectrumSidechain.withAlpha (0.9f));
                g.strokePath (tracePath (referenceShown, false), juce::PathStrokeType (1.8f));
            }
            if (currentShown.size() == curveDb.size())
            {
                juce::Path dashed;
                juce::PathStrokeType (1.6f).createDashedStroke (dashed, tracePath (currentShown, false), dashes, 2);
                g.setColour (theme::spectrumIn.withAlpha (0.9f));
                g.fillPath (dashed);
            }
            juce::Path dashed;
            juce::PathStrokeType (2.4f).createDashedStroke (dashed, gridPath (wanted.db, [this] (size_t i) { return wanted.weight[i] > 0.0f; }),
                                                            dashes, 2);
            g.setColour (theme::curveWanted);
            g.fillPath (dashed);
        }
        else
        {
            g.setColour (theme::curveWanted);
            g.strokePath (gridPath (tools.drawnDb, [&tools] (size_t i) { return tools.drawn[i]; }),
                          juce::PathStrokeType (2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            for (int i = 0; i < edit::CurveGrid::points; ++i) // los puntos sueltos (un clic sin arrastrar)
            {
                const auto at = static_cast<size_t> (i);
                const bool alone = tools.drawn[at] && (i == 0 || ! tools.drawn[at - 1]) && (i == edit::CurveGrid::points - 1 || ! tools.drawn[at + 1]);
                if (alone)
                    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (
                        { hzToX (static_cast<float> (edit::CurveGrid::hz (i))), eqDbToY (tools.drawnDb[at] + paintedGainDb) }));
            }
        }
        if (! previewDirty && previewDb.size() == curveDb.size())
        {
            g.setColour (theme::curvePreview.withAlpha (0.95f));
            g.strokePath (curvePath (previewDb, paintedGainDb), juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // La retención de picos: una línea fina por encima del espectro.
    void paintHeld (juce::Graphics& g, const Trace& trace, juce::Colour colour) const
    {
        std::vector<float> held (trace.held.size());
        for (size_t i = 0; i < held.size(); ++i)
            held[i] = trace.held[i].db;
        g.setColour (colour.withAlpha (0.55f));
        g.strokePath (tracePath (held, false), juce::PathStrokeType (0.9f));
    }

    // Arriba a la izquierda del visor: lo que conviene saber del analizador (congelado, sidechain sin conectar).
    void paintAnalyzerStatus (juce::Graphics& g) const
    {
        const juce::Font font { juce::FontOptions (10.0f, juce::Font::bold) };
        g.setFont (font);
        auto line = plot.reduced (8.0f, 5.0f).removeFromTop (12.0f);
        const auto note = [&] (const juce::String& text, juce::Colour colour) {
            g.setColour (colour);
            g.drawText (text, line, juce::Justification::topLeft);
            line.removeFromLeft (juce::GlyphArrangement::getStringWidth (font, text) + 16.0f);
        };
        if (frozen)
            note ("CONGELADO", theme::accent);
        // Fase 12: la herramienta activa.
        if (tool == Tool::match)
        {
            const auto& tools = processor.getCurveTools();
            note (tools.learningReference || tools.learningCurrent ? "IGUALAR: APRENDIENDO" : "IGUALAR", theme::curveWanted);
            if (tools.referenceSource == 0 && ! sidechainConnected)
                note ("REFERENCIA: SIDECHAIN SIN CONECTAR", theme::spectrumSidechain);
        }
        else if (tool == Tool::draw)
        {
            note ("DIBUJAR: ARRASTRA PARA TRAZAR (CLIC DERECHO = BORRAR)", theme::curveWanted);
        }
        // Fase 10: con fase lineal o natural la curva es la del FIR (y con curvas por canal, la guía de las bandas).
        if (paintedMode != dsp::PhaseMode::zeroLatency)
            note (juce::String (paintedMode == dsp::PhaseMode::linear ? "FASE LINEAL" : "FASE NATURAL")
                      + (splitCurves() ? juce::String::fromUTF8 (" (CURVAS POR CANAL: LAS DE LAS BANDAS)") : juce::String()),
                  theme::text.withAlpha (0.7f));
        if (view.channels() == analysis::Channels::side)
            note ("ANALIZADOR: SIDE", theme::spectrumSide);
        else if (view.channels() == analysis::Channels::midAndSide)
            note ("ANALIZADOR: MID + SIDE (VIOLETA)", theme::spectrumSide);
        if (view.showSidechain && ! sidechainConnected)
            note ("SIDECHAIN SIN CONECTAR", theme::spectrumSidechain);
        else if (! sidechainConnected)
            for (int b = 0; b < dsp::Engine::numBands; ++b)
                if (isDynamic (b) && paintedBands[static_cast<size_t> (b)].dynamics.external)
                {
                    note (juce::String::fromUTF8 ("DIN\xc3\x81MICA CON SIDECHAIN: SIN CONECTAR"), theme::spectrumSidechain);
                    break;
                }
        if (paintedSolo >= 0)
            note (dsp::canSolo (paintedBands[static_cast<size_t> (paintedSolo)].shape)
                      ? "SOLO: BANDA " + juce::String (paintedSolo + 1)
                      : "SOLO: LA BANDA " + juce::String (paintedSolo + 1) + " (TILT) NO TIENE ZONA",
                  theme::solo);
    }

    // Arrastrar un nodo 1 dB lo mueve 1 dB en pantalla aunque la escala de ganancia no esté en 100 % (la ganancia de la
    // banda cambia 1 dB / escala). Con la escala muy baja no se multiplica más de 4 veces: sería imposible de controlar.
    float nodeScale() const noexcept { return std::max (paintedScale, 0.25f); }

    // El pico bajo el mouse: un círculo sobre el espectro con su frecuencia y su nota (afinadas con los bins).
    void paintPeakMarker (juce::Graphics& g) const
    {
        const auto* trace = grabTrace();
        if (hoveredPeak < 0 || drag != Drag::none || trace == nullptr || trace->shown.size() < 2)
            return;
        const auto point = peaks[static_cast<size_t> (hoveredPeak)];
        const float step = plot.getWidth() / static_cast<float> (trace->shown.size() - 1);
        const juce::Point<float> centre { plot.getX() + step * static_cast<float> (point), analyzerDbToY (trace->shown[point]) };
        g.setColour (theme::text);
        g.drawEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (centre), 1.6f);
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (centre));

        const float hz = peakHz (point);
        const auto note = format::noteName (hz);
        const auto text = format::hz (hz) + (note.isEmpty() ? juce::String() : juce::String::fromUTF8 (" \xc2\xb7 ") + note);
        const juce::Font font { juce::FontOptions (11.0f, juce::Font::bold) };
        const float width = 12.0f + juce::GlyphArrangement::getStringWidth (font, text);
        auto box = juce::Rectangle<float> (width, 18.0f).withCentre ({ centre.x, centre.y - 20.0f });
        box = box.constrainedWithin (plot.reduced (2.0f));
        g.setColour (theme::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (theme::text.withAlpha (0.6f));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        g.setFont (font);
        g.setColour (theme::text);
        g.drawText (text, box, juce::Justification::centred);
    }

    void paintLasso (juce::Graphics& g) const
    {
        if (drag != Drag::lasso || ! lassoMoved)
            return;
        const auto area = lassoRectangle (mouse);
        g.setColour (theme::accent.withAlpha (0.12f));
        g.fillRect (area);
        g.setColour (theme::accent.withAlpha (0.7f));
        g.drawRect (area, 1.0f);
    }

    // Junto al nodo que se arrastra (o bajo el mouse): sus valores, para no tener que mirar el panel.
    void paintNodeLabel (juce::Graphics& g) const
    {
        const int b = drag == Drag::nodes && dragMoved ? focusedBand : hoveredNode;
        if (b < 0 || drag == Drag::lasso)
            return;
        const auto& s = paintedBands[static_cast<size_t> (b)];
        const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        auto first = juce::String (b + 1) + dot + juce::String::fromUTF8 (format::shapeNames[static_cast<size_t> (s.shape)]);
        if (s.placement != dsp::Placement::stereo)
            first << dot << juce::String::fromUTF8 (format::placementNames[static_cast<size_t> (s.placement)]);
        if (! s.enabled)
            first << " (apagada)";
        const auto note = format::noteName (s.frequency);
        auto second = format::hz (s.frequency) + (note.isEmpty() ? juce::String() : dot + note);
        juce::String third;
        if (dsp::usesGain (s.shape))
        {
            third = s.shape == dsp::Shape::flatTilt
                        ? juce::String (s.gainDb * dsp::BandSettings::flatTiltDbPerOctavePerDb, 2) + " dB/oct"
                        : format::db (s.gainDb);
            if (paintedScale != 1.0f && s.shape != dsp::Shape::flatTilt) // la escala la cambia: lo que de verdad suena
                third << juce::String::fromUTF8 (" \xe2\x86\x92 ") << format::db (s.gainDb * paintedScale);
        }
        if (dsp::usesSlope (s.shape))
            third = juce::String (dsp::slopes[static_cast<size_t> (s.slopeIndex)]) + " dB/oct";
        if (dsp::usesQ (s.shape, s.slopeIndex))
            third << (third.isEmpty() ? "" : dot) << "Q " << juce::String (s.q, s.q < 10.0f ? 2 : 1);
        if (isDynamic (b)) // Fase 9: el rango y lo que se mueve ahora (Fase 11: espectral, lo que más se mueve una frecuencia)
            third << dot << (isSpectralBand (b) ? "espectral " : "din ") << format::db (s.dynamics.rangeDb * paintedScale)
                  << juce::String::fromUTF8 (" (ahora ")
                  << format::db (isSpectralBand (b) ? spectralNowDb[static_cast<size_t> (b)] : liveDb[static_cast<size_t> (b)]) << ")";

        const juce::Font font { juce::FontOptions (11.0f) };
        const float width = 12.0f + juce::jmax (juce::GlyphArrangement::getStringWidth (font, first),
                                                juce::GlyphArrangement::getStringWidth (font, second),
                                                juce::GlyphArrangement::getStringWidth (font, third));
        const float height = 46.0f;
        const auto centre = nodePosition (b);
        auto box = juce::Rectangle<float> (width, height).withPosition (centre.x + nodeRadius + 6.0f, centre.y - height - 6.0f);
        if (box.getRight() > plot.getRight() - 2.0f)
            box.setX (centre.x - nodeRadius - 6.0f - width);
        if (box.getY() < plot.getY() + 2.0f)
            box.setY (centre.y + nodeRadius + 6.0f);
        g.setColour (theme::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (theme::bandColour (b).withAlpha (0.8f));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        g.setFont (font);
        auto lines = box.reduced (6.0f, 3.0f);
        const float lineHeight = lines.getHeight() / 3.0f;
        g.setColour (theme::text);
        g.drawText (first, lines.removeFromTop (lineHeight), juce::Justification::centredLeft);
        g.setColour (theme::text.withAlpha (0.85f));
        g.drawText (second, lines.removeFromTop (lineHeight), juce::Justification::centredLeft);
        g.drawText (third, lines, juce::Justification::centredLeft);
    }

    // Arriba, al pasar el mouse por el visor: frecuencia · nota · escala del EQ · escala del analizador. Sobre el
    // teclado, la nota de la tecla y su frecuencia exacta.
    void paintReadout (juce::Graphics& g) const
    {
        const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
        const auto area = titleArea.withTrimmedLeft (30.0f);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (11.0f));
        if (pianoArea.contains (mouse))
        {
            const int note = juce::roundToInt (edit::hzToNote (xToHz (mouse.x)));
            const float hz = static_cast<float> (edit::noteToHz (note));
            g.drawText (format::noteName (hz) + dot + format::hz (hz) + dot + "clic = llevar la banda " + juce::String (focusedBand + 1) + " a esta nota",
                        area, juce::Justification::centred);
            return;
        }
        if (! plot.contains (mouse))
            return;
        const float hz = xToHz (mouse.x);
        juce::String curve = "curva " + format::db (totalDb (hz));
        if (splitCurves())
        {
            static constexpr std::array<const char*, 4> letters { "L ", "R ", "M ", "S " };
            curve = "curva";
            for (size_t c = 0; c < curveChannels.size(); ++c)
                if (channelShown[c])
                    curve << " " << letters[c] << format::db (channelDb (hz, c));
        }
        g.drawText (format::hz (hz) + dot + format::noteName (hz) + dot + curve + dot
                        + "EQ " + format::db (yToEqDb (mouse.y)) + dot
                        + juce::String::fromUTF8 ("analizador ") + juce::String (juce::roundToInt (yToAnalyzerDb (mouse.y))) + " dB",
                    area, juce::Justification::centred);
    }

    FathomAudioProcessor& processor;
    BandEdits edits;
    std::atomic<float>* outputGain = nullptr;
    std::atomic<float>* gainScale = nullptr;
    juce::TextButton rangeButton;
    size_t rangeIndex = 1; // ±12 dB
    Trace input, output, sidechain;
    Trace outputSide; // Fase 8: el side de la salida en la vista Mid + Side
    std::vector<float> scratch;
    analysis::ViewSettings view;
    bool frozen = false;
    bool sidechainConnected = false;

    // Picos del espectro (índices de punto) y el que está bajo el mouse (índice en 'peaks', o −1).
    std::vector<size_t> peaks;
    int hoveredPeak = -1;
    bool grabbing = false;         // arrastrando la banda recién creada en un pico (el espectro se queda quieto)
    bool lastClickGrabbed = false; // el primer clic de un posible doble clic agarró un pico
    float paintedGainDb = 0.0f;
    float paintedScale = 1.0f;  // la escala de ganancia (1 = 100 %)
    int paintedSolo = -1;       // la banda en solo, o −1
    static constexpr float nodeRadius = 8.0f;
    std::array<dsp::BandSettings, dsp::Engine::numBands> paintedBands {};
    std::array<deepwater::dsp::EqCascade, dsp::Engine::numBands> bandCascades {};
    std::array<std::vector<float>, dsp::Engine::numBands> bandCurveDb; // la curva propia de cada banda, en dB
    std::array<float, dsp::Engine::numBands> nodeDb {};
    // Fase 9: la ganancia dinámica que se está dibujando, la curva de cada banda en reposo (solo mientras se mueve) y
    // hasta dónde la lleva el rango (en dB, en su frecuencia).
    std::array<float, dsp::Engine::numBands> liveDb {}, rangeDb {};
    std::array<std::vector<float>, dsp::Engine::numBands> restingCurveDb;
    // Fase 11: lo que mueve ahora cada banda espectral, por punto (vacío si no mueve nada), lo que más se mueve alguna
    // de sus frecuencias y lo último que llegó del audio.
    std::array<std::vector<float>, dsp::Engine::numBands> spectralCurveDb;
    std::array<float, dsp::Engine::numBands> spectralNowDb {};
    dsp::SpectralStage::Snapshot spectralSnapshot;
    double curveSampleRate = 0.0; // 0 = todavía sin calcular
    // Fase 10: el modo de fase dibujado y la corrección del FIR por punto (vacía con cero latencia).
    dsp::PhaseMode paintedMode = dsp::PhaseMode::zeroLatency;
    int paintedQuality = -1;
    std::unique_ptr<dsp::PhaseDesigner> phaseDesigner;
    std::vector<float> phaseDb;
    bool phaseDirty = true;
    double lastPhaseSeconds = 0.0;
    static constexpr double phaseRefreshSeconds = 0.12;
    std::vector<float> curveDb; // la respuesta total en dB, un valor por punto
    std::array<std::vector<float>, 4> channelCurveDb; // Fase 8: la de L, R, M y S (si hay bandas en un solo canal)
    std::array<bool, 4> channelShown {};
    juce::Rectangle<float> plot, titleArea, pianoArea, zoneArea;
    juce::Point<float> mouse { -1.0f, -1.0f };

    // Selección: la banda del panel ('focusedBand') siempre está entre las elegidas.
    int focusedBand = 0;
    std::bitset<dsp::Engine::numBands> selection;
    int hoveredNode = -1;

    // Arrastre.
    Drag drag = Drag::none;
    std::array<dsp::BandSettings, dsp::Engine::numBands> dragStart {}; // cada banda al empezar a arrastrar
    juce::Point<float> dragOrigin, dragLast, dragTotal;
    double dragOctaves = 0.0, dragDb = 0.0; // lo acumulado (con el ajuste fino aplicado)
    bool dragMoved = false, lassoMoved = false, lassoAdds = false;

    // Rueda.
    bool wheelEditing = false;
    double lastWheelSeconds = 0.0;

    // Fase 12: la herramienta, lo que se pide en total (dB por punto de la grilla), las bandas de la vista previa y la
    // curva total con ellas (sin la salida, por punto del visor), los espectros aprendidos (por punto, con la
    // inclinación) y el trazo en curso.
    juce::TextButton matchButton { "IGUALAR" }, drawButton { "DIBUJAR" };
    Tool tool = Tool::none;
    edit::CurveTarget wanted;
    edit::FitResult preview;
    std::vector<float> previewDb, referenceShown, currentShown;
    bool wantedDirty = true, previewDirty = true, erasing = false;
    double lastWantedSeconds = 0.0, lastFitSeconds = 0.0;
    juce::Point<float> drawLast;
    static constexpr double wantedRefreshSeconds = 0.25;
};

} // namespace fathom::gui
