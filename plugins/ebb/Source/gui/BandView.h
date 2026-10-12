#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "deepwater/dsp/Decibels.h"
#include "dsp/Bands.h"
#include "gui/Theme.h"

namespace ebb::gui
{

// Fase 6: qué parte del espectro baja con el golpe. Eje x = frecuencia (20 Hz – 20 kHz, en octavas), eje y = nivel
// (0 a −24 dB). La línea gris de arriba es la señal sin ducking (plana: las bandas LR4 suman 1); la de acento es la
// respuesta en el punto más bajo del golpe (profundidad × mix), y lo relleno entre las dos es lo que el ducking quita.
// La línea blanca fina sigue la ganancia en vivo. En "Todo" baja el espectro entero; en "Graves", solo bajo el cruce.
class BandView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr float minHz = 20.0f;
    static constexpr float maxHz = 20000.0f;
    static constexpr float minDb = -24.0f;

    // Una vez por cuadro de la GUI. 'deepestGain' = 1 − profundidad · mix; 'liveGain' = la ganancia más baja del cuadro.
    void update (dsp::DuckBand newBand, float newCrossoverHz, float newDeepestGain, float newLiveGain)
    {
        // La ganancia en vivo sube a 30 dB/s (a 30 cuadros/s), como el medidor de reducción: así se ve cada golpe.
        const float liveDb = deepwater::dsp::gainToDb (newLiveGain, minDb);
        const float shownDb = std::max (liveDb, deepwater::dsp::gainToDb (liveGain, minDb) - 1.0f);
        const float nextLive = deepwater::dsp::dbToGain (std::min (shownDb, 0.0f));
        if (newBand == band && newCrossoverHz == crossoverHz && newDeepestGain == deepestGain && std::abs (nextLive - liveGain) < 1.0e-4f)
            return;
        band = newBand;
        crossoverHz = newCrossoverHz;
        deepestGain = newDeepestGain;
        liveGain = nextLive;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        g.setColour (theme::display);
        g.fillRoundedRectangle (area, 6.0f);
        const auto plot = area.reduced (8.0f, 6.0f).withTrimmedBottom (12.0f);

        // Rejilla: 100 Hz, 1 kHz, 10 kHz y −6 / −12 / −18 dB.
        g.setFont (juce::FontOptions (10.0f));
        for (const float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = xFor (hz, plot);
            g.setColour (theme::track.withAlpha (0.6f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (theme::dimText.withAlpha (0.7f));
            g.drawText (hz < 1000.0f ? "100" : hz < 10000.0f ? "1k" : "10k", juce::Rectangle<float> (x - 15.0f, plot.getBottom(), 30.0f, 12.0f),
                        juce::Justification::centred);
        }
        for (const float db : { -6.0f, -12.0f, -18.0f })
        {
            g.setColour (theme::track.withAlpha (0.35f));
            g.drawHorizontalLine (juce::roundToInt (yFor (deepwater::dsp::dbToGain (db), plot)), plot.getX(), plot.getRight());
        }

        // Respuesta con una ganancia dada en la banda elegida: graves·g + agudos (o al revés, o todo·g).
        const auto responsePath = [&] (float gain) {
            juce::Path path;
            constexpr int steps = 160;
            for (int i = 0; i <= steps; ++i)
            {
                const float hz = minHz * std::pow (maxHz / minHz, static_cast<float> (i) / steps);
                const float y = yFor (magnitude (hz, gain), plot);
                const float x = plot.getX() + plot.getWidth() * static_cast<float> (i) / steps;
                if (i == 0)
                    path.startNewSubPath (x, y);
                else
                    path.lineTo (x, y);
            }
            return path;
        };

        const float top = yFor (1.0f, plot);
        const auto deepest = responsePath (deepestGain);
        juce::Path removed (deepest);
        removed.lineTo (plot.getRight(), top);
        removed.lineTo (plot.getX(), top);
        removed.closeSubPath();
        g.setColour (theme::accent.withAlpha (0.18f));
        g.fillPath (removed);

        g.setColour (theme::dimText.withAlpha (0.6f));
        g.drawHorizontalLine (juce::roundToInt (top), plot.getX(), plot.getRight());
        g.setColour (theme::accent);
        g.strokePath (deepest, juce::PathStrokeType (1.6f));
        if (liveGain < 0.999f)
        {
            g.setColour (theme::text.withAlpha (0.85f));
            g.strokePath (responsePath (liveGain), juce::PathStrokeType (1.0f));
        }

        // El cruce (solo cuando importa).
        if (band != dsp::DuckBand::full)
        {
            const float x = xFor (crossoverHz, plot);
            g.setColour (theme::freeRunning.withAlpha (0.8f));
            const float dashes[] { 3.0f, 3.0f };
            g.drawDashedLine (juce::Line<float> (x, plot.getY(), x, plot.getBottom()), dashes, 2, 1.0f);
        }
        g.setColour (theme::dimText);
        g.drawText (juce::String (juce::roundToInt (deepwater::dsp::gainToDb (deepestGain, -99.0f))) + " dB",
                    plot.withTrimmedRight (4.0f).withHeight (14.0f), juce::Justification::centredRight);
    }

private:
    [[nodiscard]] float magnitude (float hz, float gain) const noexcept
    {
        const float low = dsp::lowBandMagnitude (hz, crossoverHz);
        switch (band)
        {
            case dsp::DuckBand::low:  return low * gain + (1.0f - low);
            case dsp::DuckBand::high: return low + (1.0f - low) * gain;
            case dsp::DuckBand::full: break;
        }
        return gain;
    }

    [[nodiscard]] static float xFor (float hz, juce::Rectangle<float> plot) noexcept
    {
        return plot.getX() + plot.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz);
    }

    [[nodiscard]] static float yFor (float gain, juce::Rectangle<float> plot) noexcept
    {
        const float db = deepwater::dsp::gainToDb (gain, minDb);
        return plot.getY() + plot.getHeight() * (std::max (db, minDb) / minDb);
    }

    dsp::DuckBand band = dsp::DuckBand::full;
    float crossoverHz = dsp::defaultCrossoverHz;
    float deepestGain = 1.0f;
    float liveGain = 1.0f;
};

} // namespace ebb::gui
