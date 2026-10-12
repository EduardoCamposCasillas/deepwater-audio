#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "deepwater/dsp/Decibels.h"
#include "dsp/Engine.h"
#include "dsp/TempoCurve.h"
#include "gui/Theme.h"

namespace ebb::gui
{

// Fase 2: dos ciclos de la curva de ganancia con los ajustes actuales (arriba = volumen completo, abajo = silencio),
// con el suavizado incluido, y a la derecha un medidor de cuánto está bajando el volumen ahora mismo. La curva en vivo
// con cursor de reproducción llega en la Fase 7.
class CurveView final : public juce::Component
{
public:
    void setCurve (const dsp::TempoSettings& newSettings, double newBpm, dsp::TimeSignature newSignature)
    {
        const bool changed = newSettings.division != settings.division || newSettings.depth != settings.depth
                             || newSettings.duration != settings.duration || newSettings.smoothingMs != settings.smoothingMs
                             || newBpm != bpm || newSignature.numerator != signature.numerator
                             || newSignature.denominator != signature.denominator;
        settings = newSettings;
        bpm = newBpm;
        signature = newSignature;
        if (changed)
            repaint();
    }

    // Ganancia más baja desde el cuadro anterior. El medidor cae al instante y vuelve a 30 dB/s (como un medidor de
    // reducción de ganancia de un compresor), para que un golpe corto se alcance a ver.
    void setMinGain (float gain)
    {
        const float reductionDb = -deepwater::dsp::gainToDb (gain, -meterRangeDb);
        const float released = std::max (0.0f, shownReductionDb - meterReleaseDbPerFrame);
        const float next = std::max (reductionDb, released);
        if (std::abs (next - shownReductionDb) > 0.01f)
        {
            shownReductionDb = next;
            repaint (meterArea);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        auto area = getLocalBounds().reduced (12, 10);
        paintCaption (g, area.removeFromTop (18));
        area.removeFromTop (6);
        paintMeter (g, area.removeFromRight (meterWidth + 34));
        area.removeFromRight (10);
        paintCurve (g, area);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 10);
        area.removeFromTop (24);
        meterArea = area.removeFromRight (meterWidth + 34);
    }

private:
    static constexpr int meterWidth = 14;
    static constexpr float meterRangeDb = 24.0f;
    static constexpr float meterReleaseDbPerFrame = 1.0f; // a 30 cuadros/s = 30 dB/s

    static juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

    [[nodiscard]] double periodBeats() const noexcept { return dsp::divisionBeats (settings.division, signature); }

    void paintCaption (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        const int index = juce::jlimit (0, static_cast<int> (dsp::duckDivisions.size()) - 1, settings.division);
        const double ms = periodBeats() * 60000.0 / bpm;
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (juce::String (dsp::duckDivisions[static_cast<size_t> (index)].name) + " = " + juce::String (ms, 0) + " ms a "
                        + juce::String (bpm, 1) + " BPM",
                    area, juce::Justification::centredLeft);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (utf8 ("vuelve en ") + juce::String (ms * settings.duration, 0) + " ms", area, juce::Justification::centredRight);
    }

    // Dos ciclos, calculados como lo hace el motor (curva × profundidad, y el filtro del suavizado a la escala de
    // tiempo real), arrancando del estado estable: se dibuja el tercer y cuarto ciclo de una simulación de cuatro.
    void paintCurve (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        const auto plot = area.toFloat();
        g.setColour (theme::display);
        g.fillRoundedRectangle (plot, 4.0f);

        // Líneas guía: −6 y −12 dB (la mitad y la cuarta parte de la amplitud).
        g.setFont (juce::FontOptions (10.0f));
        for (const float db : { -6.0f, -12.0f })
        {
            const float y = plot.getBottom() - plot.getHeight() * deepwater::dsp::dbToGain (db);
            g.setColour (theme::track.withAlpha (0.6f));
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (theme::dimText.withAlpha (0.7f));
            g.drawText (juce::String (juce::roundToInt (db)) + " dB", plot.withTop (y - 13.0f).withHeight (12.0f).reduced (4.0f, 0.0f),
                        juce::Justification::centredRight);
        }
        // Marca de cada golpe.
        g.setColour (theme::accent.withAlpha (0.35f));
        for (int cycle = 0; cycle < 2; ++cycle)
            g.drawVerticalLine (juce::roundToInt (plot.getX() + plot.getWidth() * static_cast<float> (cycle) * 0.5f), plot.getY(), plot.getBottom());

        const int points = juce::jmax (2, static_cast<int> (plot.getWidth()));
        const double periodSeconds = periodBeats() * 60.0 / bpm;
        const double secondsPerPoint = 2.0 * periodSeconds / static_cast<double> (points);
        const float coef = settings.smoothingMs <= 0.0f
                               ? 1.0f
                               : static_cast<float> (1.0 - std::exp (-3.0 * secondsPerPoint / (settings.smoothingMs * 0.001)));

        float gain = 1.0f;
        juce::Path curve;
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < points; ++i)
            {
                const double inCycle = std::fmod (static_cast<double> (i) / points * 2.0, 1.0) * periodBeats();
                const float target = 1.0f - settings.depth * dsp::duckAt (inCycle, periodBeats(), settings.duration);
                gain += (target - gain) * coef;
                if (pass == 1)
                {
                    const auto point = juce::Point<float> (plot.getX() + static_cast<float> (i) * plot.getWidth() / static_cast<float> (points - 1),
                                                           plot.getBottom() - gain * plot.getHeight());
                    if (i == 0)
                        curve.startNewSubPath (point);
                    else
                        curve.lineTo (point);
                }
            }

        juce::Path fill (curve);
        fill.lineTo (plot.getBottomRight());
        fill.lineTo (plot.getBottomLeft());
        fill.closeSubPath();
        g.setColour (theme::accent.withAlpha (0.18f));
        g.fillPath (fill);
        g.setColour (theme::accent);
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void paintMeter (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        auto bar = area.removeFromLeft (meterWidth).toFloat();
        g.setColour (theme::display);
        g.fillRoundedRectangle (bar, 3.0f);

        // De arriba hacia abajo: cuánto se bajó el volumen (0 a 24 dB).
        const float fraction = juce::jlimit (0.0f, 1.0f, shownReductionDb / meterRangeDb);
        if (fraction > 0.0f)
        {
            g.setColour (theme::accent);
            g.fillRoundedRectangle (bar.withHeight (bar.getHeight() * fraction), 3.0f);
        }

        g.setFont (juce::FontOptions (10.0f));
        g.setColour (theme::dimText);
        for (const int db : { 0, 6, 12, 18, 24 })
        {
            const float y = bar.getY() + bar.getHeight() * static_cast<float> (db) / meterRangeDb;
            g.drawText (db == 0 ? juce::String ("0") : utf8 ("\xe2\x88\x92") + juce::String (db),
                        juce::Rectangle<float> (bar.getRight() + 4.0f, y - 6.0f, 30.0f, 12.0f), juce::Justification::centredLeft);
        }
    }

    dsp::TempoSettings settings;
    double bpm = dsp::HostClock::defaultBpm;
    dsp::TimeSignature signature;
    float shownReductionDb = 0.0f;
    juce::Rectangle<int> meterArea;
};

} // namespace ebb::gui
