#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "TransportSnapshot.h"
#include "dsp/LiveScope.h"
#include "dsp/Triggers.h"
#include "gui/Theme.h"

namespace ebb::gui
{

// Fase 7: el visor en vivo. Una ventana de 1, 2 o 4 compases que un cursor barre al ritmo de la canción (LiveScope.h):
//   - gris: la señal que entra (antes del ducking); verde agua: la que sale. La altura es el pico de cada trozo,
//     escalada a lo más fuerte que se vio (así un bajo a −18 dBFS también llena el visor);
//   - línea blanca: la ganancia (arriba = volumen entero, abajo = silencio): la curva tal como sonó, con suavizado y mix;
//   - ámbar: el sidechain (el kick que entra por el bus auxiliar), con su propia escala;
//   - triángulos: dónde empezó una curva (cada golpe; en modo tempo, cada ciclo).
// Lo que está detrás del cursor es lo más nuevo; lo que está delante es la vuelta anterior y se va apagando. La luz
// GOLPE parpadea con cada curva que empieza.
class LiveView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (int)> onBarsChange;

    LiveView()
    {
        barsButton.setTooltip (juce::String::fromUTF8 ("Cu\xc3\xa1ntos compases muestra el visor. 1: el patr\xc3\xb3n del kick "
                                                       "de cerca. 2 o 4: para patrones que cambian de un comp\xc3\xa1s a otro "
                                                       "(el dembow con variaciones)."));
        barsButton.onClick = [this] {
            const auto& options = dsp::LiveScope::barOptions;
            const auto it = std::find (options.begin(), options.end(), bars);
            const int next = it == options.end() || it + 1 == options.end() ? options.front() : *(it + 1);
            setBars (next);
            if (onBarsChange)
                onBarsChange (next);
        };
        addAndMakeVisible (barsButton);
        setBars (1);
    }

    void setBars (int newBars)
    {
        bars = dsp::LiveScope::validBars (newBars);
        barsButton.setButtonText (juce::String (bars) + (bars == 1 ? juce::String::fromUTF8 (" comp\xc3\xa1s") : " compases"));
    }

    // Una vez por cuadro de la GUI.
    void update (const dsp::LiveScope& scope, const TransportSnapshot& newTransport, std::uint32_t hits)
    {
        float inputPeak = 0.0f, sidechainPeak = 0.0f;
        hasData = false;
        for (int c = 0; c < dsp::LiveScope::numColumns; ++c)
        {
            const auto column = scope.column (c);
            columns[static_cast<size_t> (c)] = column;
            inputPeak = std::max ({ inputPeak, column.input, column.output });
            sidechainPeak = std::max (sidechainPeak, column.sidechain);
            hasData = hasData || column.input > 0.0f || column.sidechain > 0.0f || column.hit;
        }
        cursor = scope.getCursor();
        transport = newTransport;
        holdPeaks();

        // Escala: sube al instante y baja despacio (≈ 2 s), para que el dibujo no "respire" con cada golpe.
        inputScale = follow (inputScale, std::max (inputPeak, minScale));
        sidechainScale = follow (sidechainScale, std::max (sidechainPeak, minScale));

        if (! primed)
            primed = true; // abrir la GUI no es un golpe
        else if (hits != shownHits)
            glow = 1.0f;
        else
            glow = std::max (0.0f, glow - 0.2f);
        shownHits = hits;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        paintCaption (g, captionArea());
        const auto plot = plotArea();
        g.setColour (theme::display);
        g.fillRoundedRectangle (plot.expanded (2.0f), 4.0f);
        paintGrid (g, plot, false);
        paintColumns (g, plot);
        paintGrid (g, plot, true); // los números de compás, encima de la señal
        if (! hasData)
        {
            g.setColour (theme::dimText.withAlpha (0.8f));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (juce::String::fromUTF8 ("Dale play en FL (o toca el kick): aqu\xc3\xad se ve la se\xc3\xb1" "al pasar por "
                                                "Ebb, comp\xc3\xa1s a comp\xc3\xa1s."),
                        plot, juce::Justification::centred);
        }
    }

    void resized() override
    {
        barsButton.setBounds (captionArea().removeFromRight (96).withSizeKeepingCentre (96, 20));
    }

private:
    static constexpr float minScale = 0.01f; // −40 dBFS: por debajo, el ruido no se agranda a pantalla completa

    // Cada columna dura unos ms (1 compás a 95 BPM = 5 ms): un trozo tan corto puede no contener el pico de un bajo de
    // 40 Hz (25 ms por ciclo) y el dibujo saldría a rayas. Se muestra el mayor pico de las columnas vecinas en ±12.5 ms
    // (un ciclo de 40 Hz en total). La ganancia y los golpes no se tocan.
    void holdPeaks() noexcept
    {
        const auto signature = transport.timeSignature.isValid() ? transport.timeSignature : dsp::TimeSignature {};
        const double columnSeconds = signature.quartersPerBar() * bars * 60.0 / std::max (transport.bpm, 1.0) / dsp::LiveScope::numColumns;
        const int reach = std::clamp (static_cast<int> (std::lround (0.0125 / columnSeconds)), 0, 8);
        constexpr int count = dsp::LiveScope::numColumns;
        for (int c = 0; c < count; ++c)
        {
            auto held = columns[static_cast<size_t> (c)];
            for (int d = -reach; d <= reach; ++d)
            {
                const auto& neighbour = columns[static_cast<size_t> ((c + d + count) % count)];
                held.input = std::max (held.input, neighbour.input);
                held.output = std::max (held.output, neighbour.output);
                held.sidechain = std::max (held.sidechain, neighbour.sidechain);
            }
            shown[static_cast<size_t> (c)] = held;
        }
    }

    static float follow (float current, float target) noexcept
    {
        return target > current ? target : current + (target - current) * 0.02f;
    }

    [[nodiscard]] juce::Rectangle<int> captionArea() const { return getLocalBounds().reduced (12, 0).withTrimmedTop (6).withHeight (22); }

    [[nodiscard]] juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().withTrimmedTop (34).reduced (14, 0).withTrimmedBottom (12).toFloat();
    }

    void paintCaption (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("EN VIVO", area.removeFromLeft (64), juce::Justification::centredLeft);

        // Luz GOLPE.
        const auto light = area.removeFromLeft (14).withSizeKeepingCentre (12, 12).toFloat();
        g.setColour (theme::track);
        g.fillEllipse (light);
        if (glow > 0.0f)
        {
            g.setColour (theme::accent.withAlpha (glow));
            g.fillEllipse (light);
        }
        area.removeFromLeft (6);
        g.setColour (glow > 0.0f ? theme::text : theme::dimText);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("GOLPE", area.removeFromLeft (48), juce::Justification::centredLeft);

        // Leyenda.
        area.removeFromRight (104);
        g.setFont (juce::FontOptions (11.0f));
        struct LegendItem
        {
            juce::Colour colour;
            const char* name;
            int width;
            bool line;
        };
        const LegendItem items[] {
            { theme::dimText.withAlpha (0.5f), "entrada", 66, false },
            { theme::accent, "salida", 60, false },
            { theme::text, "volumen", 70, true },
            { theme::freeRunning, "sidechain", 76, false },
        };
        for (auto it = std::rbegin (items); it != std::rend (items); ++it)
        {
            auto item = area.removeFromRight (it->width);
            const auto swatch = item.removeFromLeft (14).withSizeKeepingCentre (10, it->line ? 2 : 8).toFloat();
            g.setColour (it->colour);
            g.fillRect (swatch);
            g.setColour (theme::dimText);
            g.drawText (it->name, item.withTrimmedLeft (4), juce::Justification::centredLeft);
        }
    }

    // Líneas de compás (fuertes, con su número), de tiempo y, si caben, marcas de semicorchea (la cuadrícula del dembow).
    // Dos pasadas: las líneas (debajo de la señal) y los números (encima).
    void paintGrid (juce::Graphics& g, juce::Rectangle<float> plot, bool labels) const
    {
        const auto signature = transport.timeSignature.isValid() ? transport.timeSignature : dsp::TimeSignature {};
        const double windowBeats = signature.quartersPerBar() * bars;
        const auto xOf = [&] (double beats) { return plot.getX() + plot.getWidth() * static_cast<float> (beats / windowBeats); };

        const float sixteenth = plot.getWidth() * static_cast<float> (0.25 / windowBeats);
        if (sixteenth >= 5.0f && ! labels)
        {
            g.setColour (theme::track.withAlpha (0.45f));
            for (double b = 0.0; b < windowBeats - 1.0e-9; b += 0.25)
                g.drawVerticalLine (juce::roundToInt (xOf (b)), plot.getBottom() - 5.0f, plot.getBottom());
        }

        // El primer compás de la ventana: el actual redondeado a la ventana (compases 1–2, 3–4...).
        const auto barStart = transport.hasLastBarStart ? std::optional<double> (transport.lastBarStart) : std::nullopt;
        const int currentBar = dsp::musicalPosition (transport.ppq, signature, barStart).bar;
        const int firstBar = transport.source == dsp::ClockSource::stopped ? 1 : ((std::max (currentBar, 1) - 1) / bars) * bars + 1;

        g.setFont (juce::FontOptions (10.0f));
        for (int bar = 0; bar < bars; ++bar)
            for (int beat = 0; beat < signature.numerator; ++beat)
            {
                const float x = xOf (bar * signature.quartersPerBar() + beat * signature.quartersPerBeat());
                const bool barLine = beat == 0;
                if (! labels)
                {
                    g.setColour (theme::track.withAlpha (barLine ? 1.0f : 0.55f));
                    g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
                    if (barLine && bar > 0)
                        g.drawVerticalLine (juce::roundToInt (x) + 1, plot.getY(), plot.getBottom());
                    continue;
                }
                g.setColour (theme::text.withAlpha (barLine ? 0.85f : 0.5f));
                const auto label = barLine ? juce::String (firstBar + bar) : juce::String (firstBar + bar) + "." + juce::String (beat + 1);
                g.drawText (label, juce::Rectangle<float> (x + 3.0f, plot.getY() + 1.0f, 40.0f, 11.0f), juce::Justification::centredLeft);
            }
    }

    void paintColumns (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        constexpr int count = dsp::LiveScope::numColumns;
        const float width = plot.getWidth() / static_cast<float> (count);
        const float height = plot.getHeight();
        constexpr float gap = 0.02f; // hueco borrado delante del cursor

        // Qué tan nueva es una columna: justo detrás del cursor = recién escrita (≈ 1); justo delante = de la vuelta
        // anterior, a punto de reescribirse (≈ 0). Dentro del hueco no se dibuja (−1).
        const auto freshness = [this] (int c) {
            float ahead = static_cast<float> (c) / count - cursor;
            ahead -= std::floor (ahead);
            return ahead < gap ? -1.0f : ahead;
        };
        const auto xOf = [&] (int c) { return plot.getX() + static_cast<float> (c) * width; };

        // Señal: entrada (gris) y salida (verde agua), de abajo arriba. Cada una es un solo polígono escalonado por tramo
        // (entre el hueco del cursor y el borde): con un rectángulo por columna, los bordes semitransparentes se solapan
        // y el relleno sale a rayas.
        const auto envelope = [&] (auto level) {
            juce::Path path;
            bool open = false;
            float lastX = 0.0f;
            const auto close = [&] {
                if (! open)
                    return;
                path.lineTo (lastX, plot.getBottom());
                path.closeSubPath();
                open = false;
            };
            for (int c = 0; c < count; ++c)
            {
                if (freshness (c) < 0.0f)
                {
                    close();
                    continue;
                }
                const float x = xOf (c);
                const float y = plot.getBottom() - std::min (1.0f, level (shown[static_cast<size_t> (c)]) / inputScale) * height;
                if (! open)
                {
                    path.startNewSubPath (x, plot.getBottom());
                    open = true;
                }
                path.lineTo (x, y);
                path.lineTo (x + width, y);
                lastX = x + width;
            }
            close();
            return path;
        };
        // El borrado delante del cursor ya marca qué es viejo; el relleno va parejo (el desvanecido, en las líneas).
        g.setColour (theme::dimText.withAlpha (0.3f));
        g.fillPath (envelope ([] (const dsp::LiveScope::Column& column) { return column.input; }));
        g.setColour (theme::accent.withAlpha (0.5f));
        g.fillPath (envelope ([] (const dsp::LiveScope::Column& column) { return column.output; }));

        // Líneas: el sidechain (ámbar, solo donde llega algo) y la ganancia (blanca). Se cortan en el hueco del cursor.
        juce::Path sidechainLine, gainLine;
        bool sidechainOpen = false, gainOpen = false;
        const auto addPoint = [] (juce::Path& path, bool& open, juce::Point<float> point) {
            if (open)
                path.lineTo (point);
            else
                path.startNewSubPath (point);
            open = true;
        };
        for (int c = 0; c < count; ++c)
        {
            if (freshness (c) < 0.0f)
            {
                sidechainOpen = gainOpen = false;
                continue;
            }
            const auto& column = shown[static_cast<size_t> (c)];
            const float x = xOf (c) + 0.5f * width;
            if (column.sidechain > 0.0f)
                addPoint (sidechainLine, sidechainOpen, { x, plot.getBottom() - std::min (1.0f, column.sidechain / sidechainScale) * height });
            else
                sidechainOpen = false;
            addPoint (gainLine, gainOpen, { x, plot.getY() + (1.0f - column.gain) * height });
        }
        g.setColour (theme::freeRunning.withAlpha (0.9f));
        g.strokePath (sidechainLine, juce::PathStrokeType (1.2f));
        g.setColour (theme::text.withAlpha (0.9f));
        g.strokePath (gainLine, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Golpes, encima de todo: una línea fina y un triángulo arriba.
        for (int c = 0; c < count; ++c)
        {
            const float fresh = freshness (c);
            if (fresh < 0.0f || ! shown[static_cast<size_t> (c)].hit)
                continue;
            const float fade = 0.35f + 0.65f * fresh;
            const float x = xOf (c);
            g.setColour (theme::accent.withAlpha (0.5f * fade));
            g.fillRect (x, plot.getY(), 1.0f, height);
            juce::Path mark;
            mark.addTriangle (x - 4.5f, plot.getY(), x + 5.5f, plot.getY(), x + 0.5f, plot.getY() + 7.0f);
            g.setColour (theme::accent.brighter (0.3f).withAlpha (fade));
            g.fillPath (mark);
        }

        const float cursorX = plot.getX() + cursor * plot.getWidth();
        g.setColour (theme::text);
        g.fillRect (cursorX - 0.75f, plot.getY(), 1.5f, height);
    }

    juce::TextButton barsButton;
    int bars = 1;
    std::array<dsp::LiveScope::Column, dsp::LiveScope::numColumns> columns {}; // como las escribió el audio
    std::array<dsp::LiveScope::Column, dsp::LiveScope::numColumns> shown {};   // con los picos retenidos (holdPeaks)
    float cursor = 0.0f;
    TransportSnapshot transport;
    float inputScale = minScale, sidechainScale = minScale;
    bool hasData = false;
    bool primed = false;
    std::uint32_t shownHits = 0;
    float glow = 0.0f;
};

} // namespace ebb::gui
