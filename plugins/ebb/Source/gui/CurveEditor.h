#pragma once

#include <cmath>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "deepwater/dsp/BreakpointCurve.h"
#include "gui/Theme.h"

namespace ebb::gui
{

// Fase 3: editor de la forma del ducking. Dibuja la curva por puntos tal cual (x = del golpe al final de la duración,
// y = volumen: arriba entero, abajo la profundidad completa) y deja editarla con el ratón:
//   - arrastrar un punto lo mueve (los de los extremos, solo en vertical); con Mayús se ajusta a la cuadrícula;
//   - doble clic en un hueco añade un punto; doble clic (o clic derecho) en un punto lo quita;
//   - el círculo hueco a mitad de cada tramo es su tensión: arrastrarlo arriba/abajo curva el tramo hacia ese lado;
//     doble clic lo vuelve recto.
// Cada cambio se avisa por onChange (el procesador lo guarda y lo manda al audio, que lo aplica con glide).
// Fase 7: un cursor blanco marca por dónde va la curva que está sonando.
class CurveEditor final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (const deepwater::dsp::BreakpointCurve&)> onChange;

    void setCurve (const deepwater::dsp::BreakpointCurve& newCurve)
    {
        if (newCurve == curve)
            return;
        curve = newCurve;
        // Un cambio de fuera (otra forma, un proyecto cargado) puede dejar sin sentido lo que se estaba arrastrando.
        if (dragging.index >= curve.size())
            dragging = {};
        repaint();
    }

    // Fase 7: por dónde va la curva que suena (Engine::curvePosition): 0 = el golpe, 1 = el final de la duración (ahí se
    // queda hasta el golpe siguiente, más apagado); −1 = no suena ninguna (sin cursor). Una vez por cuadro de la GUI.
    void setPlayhead (float position)
    {
        if (position == playhead)
            return;
        // Fase 9: solo la franja del cursor viejo y la del nuevo (el resto del editor no cambió).
        repaint (playheadBounds());
        playhead = position;
        repaint (playheadBounds());
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = plotArea();
        g.setColour (theme::display);
        g.fillRoundedRectangle (area.expanded (static_cast<float> (margin) - 2.0f), 4.0f);

        // Cuadrícula: octavos de la duración en x, cuartos de volumen en y (la que usa Mayús al arrastrar).
        for (int i = 0; i <= gridX; ++i)
        {
            g.setColour (theme::track.withAlpha (i == 0 || i == gridX ? 0.9f : (i % 2 == 0 ? 0.55f : 0.3f)));
            g.drawVerticalLine (juce::roundToInt (area.getX() + area.getWidth() * static_cast<float> (i) / gridX), area.getY(), area.getBottom());
        }
        for (int i = 0; i <= gridY; ++i)
        {
            g.setColour (theme::track.withAlpha (i == 0 || i == gridY ? 0.9f : 0.4f));
            g.drawHorizontalLine (juce::roundToInt (area.getY() + area.getHeight() * static_cast<float> (i) / gridY), area.getX(), area.getRight());
        }
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (theme::dimText.withAlpha (0.8f));
        g.drawText ("golpe", juce::Rectangle<float> (area.getX() + 4.0f, area.getBottom() - 14.0f, 60.0f, 12.0f), juce::Justification::centredLeft);
        g.drawText (juce::String::fromUTF8 ("fin de la duraci\xc3\xb3n"), juce::Rectangle<float> (area.getRight() - 124.0f, area.getBottom() - 14.0f, 120.0f, 12.0f),
                    juce::Justification::centredRight);
        g.drawText ("100 %", juce::Rectangle<float> (area.getX() + 4.0f, area.getY() + 2.0f, 60.0f, 12.0f), juce::Justification::centredLeft);

        // La curva, muestreada a un punto por píxel (así se ven exactas las tensiones y los escalones).
        juce::Path path;
        const int pixels = juce::jmax (2, juce::roundToInt (area.getWidth()));
        for (int i = 0; i <= pixels; ++i)
        {
            const float x = static_cast<float> (i) / static_cast<float> (pixels);
            const auto point = toScreen (x, curve.valueAt (x));
            if (i == 0)
                path.startNewSubPath (point);
            else
                path.lineTo (point);
        }
        juce::Path fill (path);
        fill.lineTo (area.getBottomRight());
        fill.lineTo (area.getBottomLeft());
        fill.closeSubPath();
        g.setColour (theme::accent.withAlpha (0.14f));
        g.fillPath (fill);
        g.setColour (theme::accent);
        g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Cursor de reproducción: una línea vertical y un punto blanco sobre la curva, en el volumen que está sonando
        // (sin profundidad ni suavizado: el punto de la forma).
        if (playhead >= 0.0f)
        {
            const bool waiting = playhead >= 1.0f;
            const auto head = toScreen (playhead, curve.valueAt (playhead));
            g.setColour (theme::text.withAlpha (waiting ? 0.2f : 0.45f));
            g.fillRect (head.x - 0.75f, area.getY(), 1.5f, area.getHeight());
            g.setColour (theme::text.withAlpha (waiting ? 0.4f : 1.0f));
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (head));
        }

        // Asas de tensión (huecas) y puntos (llenos). Resaltado el que está bajo el ratón o se arrastra.
        const auto active = dragging.kind != Target::none ? dragging : hovered;
        for (int i = 0; i + 1 < curve.size(); ++i)
        {
            const bool lit = active.kind == Target::tension && active.index == i;
            const auto centre = handlePosition (i);
            g.setColour (theme::display);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (centre));
            g.setColour (lit ? theme::text : theme::accent.withAlpha (0.7f));
            g.drawEllipse (juce::Rectangle<float> (lit ? 11.0f : 9.0f, lit ? 11.0f : 9.0f).withCentre (centre), 1.5f);
        }
        for (int i = 0; i < curve.size(); ++i)
        {
            const bool lit = active.kind == Target::point && active.index == i;
            const auto centre = toScreen (curve[i].x, curve[i].y);
            g.setColour (lit ? theme::text : theme::accent);
            g.fillEllipse (juce::Rectangle<float> (lit ? 13.0f : 10.0f, lit ? 13.0f : 10.0f).withCentre (centre));
            g.setColour (theme::display);
            g.drawEllipse (juce::Rectangle<float> (lit ? 13.0f : 10.0f, lit ? 13.0f : 10.0f).withCentre (centre), 1.5f);
        }

        // Lectura del punto que se arrastra: dónde cae y cuánto volumen deja.
        if (dragging.kind == Target::point)
        {
            const auto& p = curve[dragging.index];
            const auto text = juce::String (juce::roundToInt (p.x * 100.0f)) + juce::String::fromUTF8 (" % de la duraci\xc3\xb3n  \xc2\xb7  volumen ")
                              + juce::String (juce::roundToInt (p.y * 100.0f)) + " %";
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (text, area.reduced (8.0f, 4.0f).removeFromTop (14.0f), juce::Justification::centredRight);
        }
        else if (dragging.kind == Target::tension)
        {
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (juce::String::fromUTF8 ("tensi\xc3\xb3n ") + juce::String (curve[dragging.index].tension, 2),
                        area.reduced (8.0f, 4.0f).removeFromTop (14.0f), juce::Justification::centredRight);
        }
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        const auto target = hitTest (event.position);
        if (target != hovered)
        {
            hovered = target;
            setMouseCursor (target.kind == Target::none ? juce::MouseCursor::NormalCursor
                            : target.kind == Target::tension ? juce::MouseCursor::UpDownResizeCursor
                                                             : juce::MouseCursor::DraggingHandCursor);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hovered = {};
        repaint();
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        dragging = hitTest (event.position);
        if (event.mods.isPopupMenu())
        {
            if (dragging.kind == Target::point && curve.remove (dragging.index))
                changed();
            dragging = {};
            return;
        }
        if (dragging.kind == Target::tension)
            dragStartTension = curve[dragging.index].tension;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (dragging.kind == Target::point)
        {
            auto value = fromScreen (event.position);
            if (event.mods.isShiftDown())
                value = { std::round (value.x * gridX) / gridX, std::round (value.y * gridY) / gridY };
            curve.move (dragging.index, value.x, value.y);
            changed();
        }
        else if (dragging.kind == Target::tension)
        {
            // Arrastrar hacia arriba sube el centro del tramo: en un tramo que sube eso es tensión negativa (arranca
            // rápido), en uno que baja, positiva (se queda arriba). Toda la altura del editor = de −1 a 1.
            const auto& a = curve[dragging.index];
            const auto& b = curve[dragging.index + 1];
            const float direction = b.y >= a.y ? 1.0f : -1.0f;
            const float drag = static_cast<float> (event.getDistanceFromDragStartY()) / plotArea().getHeight();
            curve.setTension (dragging.index, dragStartTension + 2.0f * direction * drag);
            changed();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = {};
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        const auto target = hitTest (event.position);
        if (target.kind == Target::point)
        {
            if (curve.remove (target.index))
                changed();
        }
        else if (target.kind == Target::tension)
        {
            curve.setTension (target.index, 0.0f);
            changed();
        }
        else if (plotArea().expanded (4.0f).contains (event.position))
        {
            const auto value = fromScreen (event.position);
            if (curve.insert (value.x, value.y) >= 0)
                changed();
        }
        dragging = {};
        hovered = hitTest (event.position);
        repaint();
    }

private:
    static constexpr int margin = 8;
    static constexpr int gridX = 8;
    static constexpr int gridY = 4;
    static constexpr float hitRadius = 9.0f;

    struct Target
    {
        enum Kind
        {
            none,
            point,
            tension
        };
        Kind kind = none;
        int index = -1;
        friend bool operator== (const Target&, const Target&) = default;
    };

    [[nodiscard]] juce::Rectangle<float> plotArea() const { return getLocalBounds().reduced (margin).toFloat(); }

    // Lo que ocupa el cursor de reproducción (la línea y el punto), con margen para el borde suavizado. Vacío sin cursor.
    [[nodiscard]] juce::Rectangle<int> playheadBounds() const
    {
        if (playhead < 0.0f)
            return {};
        const auto area = plotArea();
        const float x = toScreen (playhead, 0.0f).x;
        return juce::Rectangle<float> (x - 6.0f, area.getY() - 6.0f, 12.0f, area.getHeight() + 12.0f).getSmallestIntegerContainer();
    }

    [[nodiscard]] juce::Point<float> toScreen (float x, float y) const
    {
        const auto area = plotArea();
        return { area.getX() + x * area.getWidth(), area.getBottom() - y * area.getHeight() };
    }

    [[nodiscard]] juce::Point<float> fromScreen (juce::Point<float> position) const
    {
        const auto area = plotArea();
        return { juce::jlimit (0.0f, 1.0f, (position.x - area.getX()) / area.getWidth()),
                 juce::jlimit (0.0f, 1.0f, (area.getBottom() - position.y) / area.getHeight()) };
    }

    // El asa de tensión de un tramo: en su mitad horizontal, sobre la curva.
    [[nodiscard]] juce::Point<float> handlePosition (int segment) const
    {
        const float x = 0.5f * (curve[segment].x + curve[segment + 1].x);
        const float t = deepwater::dsp::BreakpointCurve::bend (0.5f, curve[segment].tension);
        return toScreen (x, curve[segment].y + (curve[segment + 1].y - curve[segment].y) * t);
    }

    // Lo que hay bajo el ratón. Los puntos ganan a las asas (en un tramo muy corto quedan casi encima).
    [[nodiscard]] Target hitTest (juce::Point<float> position) const
    {
        Target best;
        float bestDistance = hitRadius;
        for (int i = 0; i < curve.size(); ++i)
            if (const float d = position.getDistanceFrom (toScreen (curve[i].x, curve[i].y)); d < bestDistance)
            {
                best = { Target::point, i };
                bestDistance = d;
            }
        if (best.kind != Target::none)
            return best;
        for (int i = 0; i + 1 < curve.size(); ++i)
        {
            // Un tramo sin anchura no tiene tensión que tocar (es un escalón vertical).
            if (curve[i + 1].x - curve[i].x <= 0.0f)
                continue;
            if (const float d = position.getDistanceFrom (handlePosition (i)); d < bestDistance)
            {
                best = { Target::tension, i };
                bestDistance = d;
            }
        }
        return best;
    }

    void changed()
    {
        repaint();
        if (onChange)
            onChange (curve);
    }

    deepwater::dsp::BreakpointCurve curve;
    Target hovered, dragging;
    float dragStartTension = 0.0f;
    float playhead = -1.0f;
};

} // namespace ebb::gui
