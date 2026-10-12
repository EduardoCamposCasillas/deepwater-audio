#pragma once

#include <cstdint>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/TransientDetector.h"
#include "gui/Theme.h"

namespace ebb::gui
{

// Fase 5: el nivel del sidechain (la envolvente que mira el detector) contra el umbral, para ajustarlo a ojo: la barra
// tiene que pasar la línea del umbral en cada kick y bajar de la línea punteada (umbral − 6 dB) entre uno y otro. La
// línea del umbral se puede arrastrar. A la izquierda, una luz: acento = el golpe disparó la curva; ámbar = se detectó
// pero no disparó (no estás en modo Audio). Si la barra no se mueve, el sidechain no llega (revisa el ruteo en FL).
class SidechainMeterView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr float minDb = -60.0f;

    // Arrastrar la línea del umbral: el editor lo convierte en un gesto del parámetro (para la automatización de FL).
    std::function<void()> onDragStart, onDragEnd;
    std::function<void (float)> onThresholdDrag;

    // Una vez por cuadro de la GUI.
    void update (float peakEnvelope, std::uint32_t detected, std::uint32_t triggers, float newThresholdDb)
    {
        // Sube al instante y baja a 30 dB/s (a 30 cuadros/s), como el medidor de reducción.
        const float db = deepwater::dsp::gainToDb (peakEnvelope, minDb);
        const float next = std::max (db, shownDb - 1.0f);
        bool changed = std::abs (next - shownDb) > 0.01f || newThresholdDb != thresholdDb;
        const float previousGlow = glow;
        shownDb = std::max (next, minDb);
        thresholdDb = newThresholdDb;

        if (! primed)
            primed = true; // abrir la GUI no es un golpe
        else if (triggers != shownTriggers)
            flash (theme::accent);
        else if (detected != shownDetected)
            flash (theme::freeRunning);
        else if (glow > 0.0f)
            glow = std::max (0.0f, glow - 0.2f);
        changed = changed || glow != previousGlow;
        shownTriggers = triggers;
        shownDetected = detected;
        if (changed)
            repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        auto lightArea = area.removeFromLeft (18).withSizeKeepingCentre (14, 14).toFloat().withY (static_cast<float> (barArea().getCentreY()) - 7.0f);
        g.setColour (theme::track);
        g.fillEllipse (lightArea);
        if (glow > 0.0f)
        {
            g.setColour (glowColour.withAlpha (glow));
            g.fillEllipse (lightArea);
        }

        const auto bar = barArea().toFloat();
        g.setColour (theme::display);
        g.fillRoundedRectangle (bar, 3.0f);

        // Nivel: verde agua si está por encima del umbral (golpe), gris por debajo.
        const float levelX = xForDb (shownDb);
        if (levelX > bar.getX())
        {
            g.setColour (shownDb >= thresholdDb ? theme::accent.withAlpha (0.85f) : theme::dimText.withAlpha (0.55f));
            g.fillRoundedRectangle (bar.withRight (levelX), 3.0f);
        }

        // Escala debajo de la barra.
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (theme::dimText);
        for (const int db : { -60, -48, -36, -24, -12, 0 })
        {
            const float x = xForDb (static_cast<float> (db));
            g.setColour (theme::track);
            g.drawVerticalLine (juce::roundToInt (x), bar.getBottom(), bar.getBottom() + 3.0f);
            g.setColour (theme::dimText);
            const auto text = db == 0 ? juce::String ("0") : juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String (-db);
            g.drawText (text, juce::Rectangle<float> (x - 15.0f, bar.getBottom() + 3.0f, 30.0f, 12.0f), juce::Justification::centred);
        }

        // Rearme (umbral − histéresis), punteado, y el umbral con su asa.
        const float alpha = isEnabled() ? 1.0f : 0.4f;
        const float rearmX = xForDb (thresholdDb - deepwater::dsp::TransientDetector::hysteresisDb);
        g.setColour (theme::text.withAlpha (0.45f * alpha));
        for (float y = bar.getY(); y < bar.getBottom(); y += 4.0f)
            g.drawVerticalLine (juce::roundToInt (rearmX), y, std::min (y + 2.0f, bar.getBottom()));
        const float x = xForDb (thresholdDb);
        g.setColour (theme::freeRunning.withAlpha (alpha));
        g.fillRect (x - 1.0f, bar.getY() - 3.0f, 2.0f, bar.getHeight() + 3.0f);
        juce::Path handle;
        handle.addTriangle (x - 5.0f, bar.getY() - 7.0f, x + 5.0f, bar.getY() - 7.0f, x, bar.getY() - 1.0f);
        g.fillPath (handle);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (onDragStart)
            onDragStart();
        mouseDrag (event);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (onThresholdDrag)
            onThresholdDrag (dbForX (static_cast<float> (event.x)));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (onDragEnd)
            onDragEnd();
    }

private:
    [[nodiscard]] juce::Rectangle<int> barArea() const
    {
        // La luz a la izquierda; arriba, sitio para el asa; abajo, para la escala. Márgenes para que "0" y "−60" quepan.
        return getLocalBounds().withTrimmedLeft (18 + 14).withTrimmedRight (12).withTrimmedTop (9).withTrimmedBottom (16);
    }

    [[nodiscard]] float xForDb (float db) const
    {
        const auto bar = barArea().toFloat();
        return bar.getX() + bar.getWidth() * (juce::jlimit (minDb, 0.0f, db) - minDb) / -minDb;
    }

    [[nodiscard]] float dbForX (float x) const
    {
        const auto bar = barArea().toFloat();
        return juce::jlimit (minDb, 0.0f, minDb + (x - bar.getX()) / bar.getWidth() * -minDb);
    }

    void flash (juce::Colour colour)
    {
        glow = 1.0f;
        glowColour = colour;
    }

    float shownDb = minDb;
    float thresholdDb = -24.0f;
    std::uint32_t shownDetected = 0, shownTriggers = 0;
    bool primed = false;
    float glow = 0.0f;
    juce::Colour glowColour = theme::accent;
};

} // namespace ebb::gui
