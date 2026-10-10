#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "deepwater/dsp/LevelMeter.h"
#include "gui/Theme.h"

namespace riptide::gui
{

// Medidor vertical de un punto de la cadena (IN o OUT), una barra por canal:
//  - barra tenue = pico (sube al instante, cae 20 dB/s), barra sólida = RMS (300 ms), línea = pico más alto de los
//    últimos 1.5 s;
//  - LED rojo arriba = alguna muestra pasó de 0 dBFS (queda encendido hasta hacer clic en el medidor);
//  - abajo, en números: el pico sostenido y el RMS del canal más fuerte.
// El editor llama a update() en cada cuadro con el tiempo transcurrido.
class LevelMeterView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr float topDb = 6.0f;
    static constexpr float bottomDb = -60.0f;

    LevelMeterView (juce::String meterTitle, RiptideAudioProcessor::StereoMeter& meterSource)
        : title (std::move (meterTitle)), source (meterSource)
    {
        // Los literales con tildes van como UTF-8 escapado: String (const char*) solo acepta ASCII.
        setTooltip (juce::String::fromUTF8 ("Barra tenue: pico. Barra s\xc3\xb3lida: RMS (energ\xc3\xad" "a media, lo que se "
                                            "oye). L\xc3\xad" "nea: pico de los \xc3\xba" "ltimos 1.5 s. LED rojo: pas\xc3\xb3 "
                                            "de 0 dBFS (clic para apagarlo)."));
    }

    void update (int numChannels, double elapsedSeconds)
    {
        channels = juce::jlimit (1, static_cast<int> (source.size()), numChannels);
        for (size_t ch = 0; ch < source.size(); ++ch)
        {
            // exchange(0): recoge el pico máximo que publicó el audio desde el cuadro anterior y empieza de nuevo.
            peaks[ch].update (source[ch].peak.exchange (0.0f, std::memory_order_relaxed), elapsedSeconds);
            rmsDb[ch] = deepwater::dsp::gainToDb (source[ch].rms.load (std::memory_order_relaxed));
        }
        repaint();
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        for (auto& peak : peaks)
            peak.resetClip();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

        area.reduce (8.0f, 8.0f);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (title, area.removeFromTop (16.0f), juce::Justification::centred);

        auto readout = area.removeFromBottom (34.0f);
        auto leds = area.removeFromTop (12.0f);
        area.removeFromTop (4.0f);
        auto scale = area.removeFromLeft (24.0f);
        const auto bars = area;

        drawScale (g, scale, bars);

        const float gap = 4.0f;
        const float barWidth = (bars.getWidth() - gap * static_cast<float> (channels - 1)) / static_cast<float> (channels);
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto index = static_cast<size_t> (ch);
            const float x = bars.getX() + static_cast<float> (ch) * (barWidth + gap);
            drawBar (g, { x, bars.getY(), barWidth, bars.getHeight() }, peaks[index], rmsDb[index]);

            const auto led = juce::Rectangle<float> (x, leds.getY(), barWidth, leds.getHeight()).reduced (0.0f, 2.0f);
            g.setColour (peaks[index].clipped ? theme::meterClip : theme::control);
            g.fillRoundedRectangle (led, 2.0f);
        }

        drawReadout (g, readout);
    }

private:
    float dbToY (float db, juce::Rectangle<float> bars) const noexcept
    {
        const float proportion = (juce::jlimit (bottomDb, topDb, db) - bottomDb) / (topDb - bottomDb);
        return bars.getBottom() - proportion * bars.getHeight();
    }

    void drawScale (juce::Graphics& g, juce::Rectangle<float> scale, juce::Rectangle<float> bars) const
    {
        g.setFont (juce::FontOptions (10.0f));
        for (const float db : { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
        {
            const float y = dbToY (db, bars);
            g.setColour (db == 0.0f ? theme::text : theme::dimText);
            g.drawText (juce::String (juce::roundToInt (db)), scale.withY (y - 6.0f).withHeight (12.0f).withTrimmedRight (3.0f),
                        juce::Justification::centredRight);
            g.setColour (theme::panelOutline.withAlpha (db == 0.0f ? 1.0f : 0.6f));
            g.drawHorizontalLine (juce::roundToInt (y), bars.getX(), bars.getRight());
        }
    }

    void drawBar (juce::Graphics& g, juce::Rectangle<float> bar, const deepwater::dsp::PeakDisplay& peak, float rms) const
    {
        g.setColour (theme::display);
        g.fillRect (bar);

        // Un solo degradado para toda la altura: el color dice en qué zona está el nivel, no cuánto se llenó la barra.
        juce::ColourGradient colours (theme::meterClip, 0.0f, bar.getY(), theme::meterSafe, 0.0f, bar.getBottom(), false);
        const auto stop = [&] (float db) { return (dbToY (db, bar) - bar.getY()) / bar.getHeight(); };
        colours.addColour (stop (0.0f), theme::meterClip);
        colours.addColour (stop (0.01f) + 0.001f, theme::meterHot);
        colours.addColour (stop (-12.0f), theme::meterHot.interpolatedWith (theme::meterSafe, 0.5f));

        g.setGradientFill (colours);
        g.setOpacity (0.35f);
        g.fillRect (bar.withTop (dbToY (peak.levelDb, bar)));
        g.setOpacity (1.0f);
        g.fillRect (bar.withTop (dbToY (rms, bar)));

        if (peak.holdDb > bottomDb)
        {
            g.setColour (peak.holdDb > 0.0f ? theme::meterClip : theme::text);
            g.fillRect (bar.withTop (dbToY (peak.holdDb, bar) - 1.0f).withHeight (2.0f));
        }
    }

    void drawReadout (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        float peakDb = deepwater::dsp::silenceDb;
        float loudestRms = deepwater::dsp::silenceDb;
        for (size_t ch = 0; ch < static_cast<size_t> (channels); ++ch)
        {
            peakDb = juce::jmax (peakDb, peaks[ch].holdDb);
            loudestRms = juce::jmax (loudestRms, rmsDb[ch]);
        }

        const auto text = [] (float db) {
            return db <= bottomDb ? juce::String ("-inf") : juce::String (db, 1);
        };
        g.setFont (juce::FontOptions (11.0f));
        const auto row = [&] (juce::Rectangle<float> line, const juce::String& label, float db, juce::Colour colour) {
            g.setColour (theme::dimText);
            g.drawText (label, line, juce::Justification::centredLeft);
            g.setColour (colour);
            g.drawText (text (db), line, juce::Justification::centredRight);
        };
        row (area.removeFromTop (17.0f), "Pico", peakDb, peakDb > 0.0f ? theme::meterClip : theme::text);
        row (area, "RMS", loudestRms, theme::text);
    }

    juce::String title;
    RiptideAudioProcessor::StereoMeter& source;
    std::array<deepwater::dsp::PeakDisplay, 2> peaks;
    std::array<float, 2> rmsDb { deepwater::dsp::silenceDb, deepwater::dsp::silenceDb };
    int channels = 2;
};

} // namespace riptide::gui
