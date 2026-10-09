#pragma once

#include <optional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Warp.h"
#include "dsp/Wavetable.h"
#include "gui/Theme.h"

namespace undertow::gui
{

// Visor del wavetable con el warp aplicado. Dos vistas (clic en "3D / 2D"):
//  - 3D: todos los frames apilados en profundidad (el primero delante) y, resaltado, el frame que suena.
//    Se ve de un vistazo cómo cambia la onda a lo largo de la tabla y por dónde la recorre Position.
//  - 2D: solo el frame actual, grande; en gris la onda original y en naranja la deformada por el warp.
// Lo que se dibuja usa las mismas funciones que el oscilador (mezcla de frames y dsp::warpedValue): es lo que suena.
// Si suena una nota con Position o Warp modulados, se dibuja el valor modulado (se ve moverse con el LFO).
// Arrastrar sobre el visor (arriba/derecha = más adelante en la tabla) cambia Position.
class WavetableView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class View { stack, single };

    struct State
    {
        const dsp::Wavetable* table = nullptr;
        float position = 0.0f;   // el que se dibuja (ya con la modulación en vivo, si la hay)
        dsp::WarpMode warpMode = dsp::WarpMode::none;
        float warpAmount = 0.0f; // ídem
        bool enabled = true;     // oscilador apagado = dibujo tenue
        bool modulated = false;  // Position o Warp se están moviendo por la matriz

        bool operator== (const State& other) const
        {
            return table == other.table && juce::approximatelyEqual (position, other.position) && warpMode == other.warpMode
                   && juce::approximatelyEqual (warpAmount, other.warpAmount) && enabled == other.enabled
                   && modulated == other.modulated;
        }
    };

    explicit WavetableView (View initialView = View::stack) : view (initialView)
    {
        setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);
        setTooltip (juce::String::fromUTF8 ("Arrastra para recorrer la tabla (Position). Clic en 3D / 2D para cambiar la vista."));
    }

    void setPositionParameter (juce::RangedAudioParameter* parameter) { positionParameter = parameter; }

    void setState (const State& newState)
    {
        if (newState == state)
            return;
        state = newState;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::display);
        g.fillRoundedRectangle (bounds, 6.0f);

        if (state.table != nullptr && state.table->getNumFrames() > 0)
        {
            const float alpha = state.enabled ? 1.0f : 0.35f;
            if (view == View::stack && state.table->getNumFrames() > 1)
                paintStack (g, bounds.reduced (10.0f, 12.0f).withTrimmedTop (10.0f), alpha);
            else
                paintSingle (g, bounds.reduced (8.0f, 12.0f).withTrimmedTop (8.0f), alpha);
            paintCaption (g, bounds);
        }

        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (viewToggleArea().contains (e.position))
        {
            view = view == View::stack ? View::single : View::stack;
            repaint();
            return;
        }
        if (positionParameter == nullptr)
            return;
        dragging = true;
        dragStartPosition = positionParameter->convertFrom0to1 (positionParameter->getValue());
        positionParameter->beginChangeGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;
        // Recorrer todo el visor (en diagonal) = recorrer toda la tabla. Con Shift, 5 veces más fino.
        const float pixels = static_cast<float> (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY());
        const float span = static_cast<float> (getWidth() + getHeight()) * 0.6f * (e.mods.isShiftDown() ? 5.0f : 1.0f);
        const float position = juce::jlimit (0.0f, 1.0f, dragStartPosition + pixels / span);
        positionParameter->setValueNotifyingHost (positionParameter->convertTo0to1 (position));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging)
            return;
        dragging = false;
        positionParameter->endChangeGesture();
    }

private:
    // Un frame leído en el nivel 0 (todos sus armónicos) con interpolación lineal. Al dibujar sobra precisión.
    struct FrameReader
    {
        const float* a;
        const float* b;
        float blend;

        float operator() (double phase) const
        {
            const double exact = phase * dsp::Wavetable::levelSize (0);
            const int index = static_cast<int> (exact);
            const auto fraction = static_cast<float> (exact - index);
            const float fromA = a[index] + (a[index + 1] - a[index]) * fraction;
            const float fromB = b[index] + (b[index + 1] - b[index]) * fraction;
            return fromA + (fromB - fromA) * blend;
        }
    };

    // La misma mezcla entre frames vecinos que hace el oscilador.
    [[nodiscard]] FrameReader readerAt (float position) const
    {
        const int numFrames = state.table->getNumFrames();
        const float framePosition = juce::jlimit (0.0f, 1.0f, position) * static_cast<float> (numFrames - 1);
        const int first = juce::jmin (static_cast<int> (framePosition), juce::jmax (0, numFrames - 2));
        const int second = juce::jmin (first + 1, numFrames - 1);
        return { state.table->getFrame (0, first), state.table->getFrame (0, second), framePosition - static_cast<float> (first) };
    }

    // Traza un ciclo deformado: x de 'left' a 'left + width', y = baseline − valor · amplitud.
    template <typename Read>
    static juce::Path tracePath (const dsp::Warp& warp, const Read& read, float left, float width, float baseline,
                                 float amplitude, int numPoints, bool applyWarp = true)
    {
        juce::Path path;
        for (int i = 0; i < numPoints; ++i)
        {
            const double phase = static_cast<double> (i) / static_cast<double> (numPoints);
            const float value = applyWarp ? dsp::warpedValue (warp, phase, read, read) : read (phase);
            const float x = left + width * static_cast<float> (i) / static_cast<float> (numPoints - 1);
            const float y = baseline - value * amplitude;
            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
        return path;
    }

    void paintStack (juce::Graphics& g, juce::Rectangle<float> area, float alpha) const
    {
        // Perspectiva sencilla: cada frame se desplaza hacia arriba a la derecha según su profundidad (0 = delante).
        const float depthX = area.getWidth() * 0.28f;
        const float depthY = area.getHeight() * 0.42f;
        const float frameWidth = area.getWidth() - depthX;
        const float frameHeight = area.getHeight() - depthY;
        const float amplitude = frameHeight * 0.42f;
        const auto warp = dsp::Warp::make (state.warpMode, state.warpAmount);
        const int numPoints = juce::jlimit (32, 256, static_cast<int> (frameWidth * 0.6f));

        const auto originFor = [&] (float depth) {
            return juce::Point<float> (area.getX() + depth * depthX, area.getBottom() - frameHeight * 0.5f - depth * depthY);
        };

        // Como mucho 32 frames (repartidos por la tabla): más líneas solo serían ruido visual.
        const int numFrames = state.table->getNumFrames();
        const int numDrawn = juce::jmin (numFrames, 32);
        for (int i = numDrawn - 1; i >= 0; --i)
        {
            const float depth = static_cast<float> (i) / static_cast<float> (numDrawn - 1);
            const int frame = juce::roundToInt (depth * static_cast<float> (numFrames - 1));
            const float* samples = state.table->getFrame (0, frame);
            const FrameReader read { samples, samples, 0.0f };
            const auto origin = originFor (depth);
            const auto path = tracePath (warp, read, origin.x, frameWidth, origin.y, amplitude, numPoints);
            g.setColour (theme::accent.withAlpha (alpha * (0.10f + 0.22f * (1.0f - depth))));
            g.strokePath (path, juce::PathStrokeType (1.0f));
        }

        // El frame que suena (mezclado como en el oscilador), relleno hasta su línea base.
        const auto origin = originFor (state.position);
        const auto read = readerAt (state.position);
        const auto path = tracePath (warp, read, origin.x, frameWidth, origin.y, amplitude, numPoints * 2);
        auto filled = path;
        filled.lineTo (origin.x + frameWidth, origin.y);
        filled.lineTo (origin.x, origin.y);
        filled.closeSubPath();
        const auto colour = state.warpMode == dsp::WarpMode::none ? theme::accent : theme::warm;
        g.setColour (colour.withAlpha (alpha * 0.14f));
        g.fillPath (filled);
        g.setColour (colour.withAlpha (alpha));
        g.strokePath (path, juce::PathStrokeType (2.0f));

        // Eje de profundidad con la posición marcada.
        const auto front = originFor (0.0f).translated (frameWidth + 6.0f, 0.0f);
        const auto back = originFor (1.0f).translated (frameWidth + 6.0f, 0.0f);
        g.setColour (theme::dimText.withAlpha (0.4f * alpha));
        g.drawLine ({ front, back }, 1.0f);
        const auto marker = front + (back - front) * state.position;
        g.setColour (colour.withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (marker));
    }

    void paintSingle (juce::Graphics& g, juce::Rectangle<float> area, float alpha) const
    {
        const auto warp = dsp::Warp::make (state.warpMode, state.warpAmount);
        const auto read = readerAt (state.position);
        const int numPoints = juce::jmax (2, static_cast<int> (area.getWidth() * 2.0f));
        const float baseline = area.getCentreY();
        const float amplitude = area.getHeight() * 0.48f;

        g.setColour (theme::text.withAlpha (0.08f));
        g.drawHorizontalLine (juce::roundToInt (baseline), area.getX(), area.getRight());

        const bool warped = state.warpMode != dsp::WarpMode::none;
        if (warped)
        {
            g.setColour (theme::text.withAlpha (0.22f * alpha));
            g.strokePath (tracePath (warp, read, area.getX(), area.getWidth(), baseline, amplitude, numPoints, false),
                          juce::PathStrokeType (1.5f));
        }
        g.setColour ((warped ? theme::warm : theme::accent).withAlpha (alpha));
        g.strokePath (tracePath (warp, read, area.getX(), area.getWidth(), baseline, amplitude, numPoints),
                      juce::PathStrokeType (2.0f));
    }

    void paintCaption (juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        const auto toggle = viewToggleArea();
        g.setColour (theme::control);
        g.fillRoundedRectangle (toggle, 4.0f);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        auto half = toggle;
        const auto left = half.removeFromLeft (toggle.getWidth() * 0.5f);
        g.setColour (view == View::stack ? theme::accent : theme::dimText);
        g.drawText ("3D", left, juce::Justification::centred);
        g.setColour (view == View::single ? theme::accent : theme::dimText);
        g.drawText ("2D", half, juce::Justification::centred);

        const int numFrames = state.table->getNumFrames();
        const int frame = juce::roundToInt (state.position * static_cast<float> (numFrames - 1)) + 1;
        juce::String caption = juce::String (frame) + " / " + juce::String (numFrames);
        if (state.warpMode != dsp::WarpMode::none)
            caption << "   " << dsp::warpModeNames[static_cast<size_t> (state.warpMode)] << " "
                    << juce::roundToInt (state.warpAmount * 100.0f) << " %";
        g.setColour (state.modulated ? theme::text : theme::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (caption, bounds.reduced (8.0f, 4.0f).withHeight (16.0f), juce::Justification::centredRight);
    }

    [[nodiscard]] juce::Rectangle<float> viewToggleArea() const { return { 6.0f, 5.0f, 44.0f, 16.0f }; }

    View view;
    State state;
    juce::RangedAudioParameter* positionParameter = nullptr;
    bool dragging = false;
    float dragStartPosition = 0.0f;
};

} // namespace undertow::gui
