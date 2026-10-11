#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "deepwater/dsp/Decibels.h"
#include "gui/DisplayState.h"
#include "gui/Format.h"
#include "gui/Theme.h"

namespace riptide::gui
{

// Curva de transferencia en vivo: qué valor sale (eje vertical) para cada valor que entra (eje horizontal).
//
// El eje horizontal es la señal tal cual llega al plugin, de −1 a +1 (los dos bordes son 0 dBFS); la curva dibujada es
// y = f(drive · x), con el modo y el drive que suenan ahora (el Input, el drive de la banda y lo que mueva la
// modulación). Subir el drive no mueve la señal: comprime la curva hacia el centro, así que la rodilla llega antes.
// La línea punteada es y = x (salir igual que entró): todo lo que la curva se separa de ella es saturación.
//
// El tramo brillante es el que la señal recorre de verdad: de −pico a +pico de la entrada (con una caída lenta para
// poder leerlo). Si queda en la parte recta del centro, esa señal casi no se satura, por mucho que la curva se doble más
// afuera. Con varias bandas se dibuja una curva por banda, en su color (sin el tramo del pico: cada banda recibe solo una
// parte de la señal y su pico es otro).
class TransferCurveView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr float yRange = 1.5f; // el techo negativo de Tube llega a −1.41

    TransferCurveView()
    {
        setTooltip (juce::String::fromUTF8 (
            "Curva de transferencia: cu\xc3\xa1nto sale (vertical) para cada valor que entra (horizontal, de -1 a +1 = "
            "0 dBFS). Punteada: sin saturar (sale igual que entra). Subir Input comprime la curva hacia el centro: la "
            "rodilla llega antes. El tramo brillante es el que tu se\xc3\xb1" "al recorre ahora (hasta su pico): si se "
            "queda en la parte recta, casi no se satura."));
    }

    void setState (const DisplayState& newState, float inputPeak, double elapsedSeconds)
    {
        // El pico sube al instante y cae con τ = 300 ms: se puede leer sin que tiemble con cada golpe. Por debajo de
        // −100 dBFS ya se lee "-inf": se deja en 0 (si no, la caída exponencial nunca termina).
        shownPeak = juce::jmax (inputPeak, shownPeak * static_cast<float> (std::exp (-elapsedSeconds / 0.3)));
        if (shownPeak < 1.0e-5f)
            shownPeak = 0.0f;

        // Fase 10: repintar cuesta CPU del hilo de mensajes (el de la interfaz de FL). Solo si cambió algo visible: la
        // curva o el pico (su lectura tiene un decimal: 0.05 dB). Con la canción parada, el visor queda quieto.
        if (newState == state && std::abs (deepwater::dsp::gainToDb (shownPeak) - deepwater::dsp::gainToDb (paintedPeak)) < 0.05f)
            return;
        state = newState;
        paintedPeak = shownPeak;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);
        area.reduce (10.0f, 8.0f);

        // Título y lo que se dibuja.
        auto title = area.removeFromTop (18.0f);
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("CURVA", title, juce::Justification::centredLeft);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.0f));
        const auto& first = state.bands[0];
        g.drawText (state.numBands == 1 ? juce::String (dsp::modeNames[static_cast<size_t> (first.mode)]) + "  drive " + format::db (first.driveDb)
                                        : juce::String (state.numBands) + " bandas (sus colores)",
                    title, juce::Justification::centredRight);

        auto footer = area.removeFromBottom (16.0f);
        area.removeFromBottom (2.0f);
        area.removeFromTop (4.0f);
        const float side = juce::jmin (area.getWidth(), area.getHeight());
        const auto plot = area.withSizeKeepingCentre (side, side);
        paintPlot (g, plot);

        // Pie: el pico de la entrada.
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (theme::dimText);
        const float peakDb = deepwater::dsp::gainToDb (shownPeak);
        g.drawText ("pico de la entrada", footer, juce::Justification::centredLeft);
        g.setColour (shownPeak > 1.0f ? theme::meterClip : theme::text);
        g.drawText (peakDb <= -99.0f ? juce::String ("-inf") : juce::String (peakDb, 1) + " dBFS", footer, juce::Justification::centredRight);
    }

private:
    void paintPlot (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        g.setColour (theme::display);
        g.fillRoundedRectangle (plot, 4.0f);

        const auto toPoint = [plot] (float x, float y)
        {
            return juce::Point<float> (plot.getX() + (x + 1.0f) * 0.5f * plot.getWidth(),
                                       plot.getCentreY() - y / yRange * 0.5f * plot.getHeight());
        };

        // Rejilla: ±0.5 y ±1 en los dos ejes, los ejes un poco más claros.
        for (const float v : { -1.0f, -0.5f, 0.5f, 1.0f })
        {
            g.setColour (theme::grid);
            g.drawLine ({ toPoint (v, -yRange), toPoint (v, yRange) }, 1.0f);
            g.drawLine ({ toPoint (-1.0f, v), toPoint (1.0f, v) }, 1.0f);
        }
        g.setColour (theme::panelOutline);
        g.drawLine ({ toPoint (0.0f, -yRange), toPoint (0.0f, yRange) }, 1.0f);
        g.drawLine ({ toPoint (-1.0f, 0.0f), toPoint (1.0f, 0.0f) }, 1.0f);
        g.setColour (theme::dimText.withAlpha (0.6f));
        g.setFont (juce::FontOptions (9.0f));
        g.drawText ("+1", juce::Rectangle<float> (toPoint (0.0f, 1.0f).x + 3.0f, toPoint (0.0f, 1.0f).y - 6.0f, 20.0f, 12.0f), juce::Justification::centredLeft);
        g.drawText ("-1", juce::Rectangle<float> (toPoint (0.0f, -1.0f).x + 3.0f, toPoint (0.0f, -1.0f).y - 6.0f, 20.0f, 12.0f), juce::Justification::centredLeft);

        // y = x, punteada: "sin saturar".
        {
            juce::Path identity;
            identity.startNewSubPath (toPoint (-1.0f, -1.0f));
            identity.lineTo (toPoint (1.0f, 1.0f));
            juce::Path dashed;
            const float dashes[] = { 4.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, identity, dashes, 2);
            g.setColour (theme::dimText.withAlpha (0.45f));
            g.fillPath (dashed);
        }

        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (plot.toNearestInt());

        const int count = juce::jlimit (1, dsp::maxBands, state.numBands);
        // Primero las que no se oyen (detrás) y la banda 1 al final (encima), para que con todas iguales se vea su color.
        for (int pass = 0; pass < 2; ++pass)
            for (int b = count - 1; b >= 0; --b)
            {
                const auto& band = state.bands[static_cast<size_t> (b)];
                if (band.audible != (pass == 1))
                    continue;
                const auto colour = count == 1 ? theme::accent : theme::bands[static_cast<size_t> (b)];
                paintCurve (g, band, colour.withMultipliedAlpha (band.audible ? 1.0f : 0.35f), count == 1, toPoint);
            }
    }

    template <typename ToPoint>
    void paintCurve (juce::Graphics& g, const BandDisplay& band, juce::Colour colour, bool showSignal, ToPoint toPoint) const
    {
        const float drive = deepwater::dsp::dbToGain (band.driveDb);
        const auto curve = [&] (float x) { return dsp::curves::forMode (band.mode, drive * x); };
        const auto path = [&] (float from, float to)
        {
            juce::Path p;
            constexpr int steps = 240;
            for (int i = 0; i <= steps; ++i)
            {
                const float x = from + (to - from) * static_cast<float> (i) / static_cast<float> (steps);
                const auto point = toPoint (x, curve (x));
                if (i == 0)
                    p.startNewSubPath (point);
                else
                    p.lineTo (point);
            }
            return p;
        };

        const auto thin = juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        if (! showSignal)
        {
            g.setColour (colour);
            g.strokePath (path (-1.0f, 1.0f), thin);
            return;
        }

        // Toda la curva tenue y, encima, el tramo que recorre la señal (hasta su pico) brillante y grueso.
        g.setColour (colour.withMultipliedAlpha (0.45f));
        g.strokePath (path (-1.0f, 1.0f), thin);
        const float reach = juce::jmin (1.0f, shownPeak);
        if (reach < 0.002f)
            return;
        g.setColour (colour);
        g.strokePath (path (-reach, reach), juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (const float x : { -reach, reach })
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (toPoint (x, curve (x))));
    }

    DisplayState state;
    float shownPeak = 0.0f;
    float paintedPeak = -1.0f; // el pico del último repintado (−1: todavía ninguno)
};

} // namespace riptide::gui
