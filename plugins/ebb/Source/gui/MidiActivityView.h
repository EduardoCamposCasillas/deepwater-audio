#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "dsp/Triggers.h"
#include "gui/Theme.h"

namespace ebb::gui
{

// Fase 4: una luz que parpadea con cada nota MIDI que llega, y cuál fue la última. Sirve para comprobar el ruteo en FL
// sin adivinar: acento = la nota disparó la curva; ámbar = llegó pero no disparó (modo tempo u otra nota del filtro);
// apagada = no llega MIDI (revisa el puerto).
class MidiActivityView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    // Una vez por cuadro de la GUI, con los contadores del procesador.
    void update (std::uint32_t notes, std::uint32_t triggers, int lastNote)
    {
        bool changed = ! primed; // la primera vez solo se copian los contadores: abrir la GUI no es una nota
        if (! primed)
            primed = true;
        else if (triggers != shownTriggers)
        {
            glow = 1.0f;
            glowColour = theme::accent;
            changed = true;
        }
        else if (notes != shownNotes)
        {
            glow = 1.0f;
            glowColour = theme::freeRunning;
            changed = true;
        }
        else if (glow > 0.0f)
        {
            glow = std::max (0.0f, glow - 0.2f); // se apaga en ~5 cuadros (≈170 ms)
            changed = true;
        }
        if (lastNote != shownNote)
            changed = true;

        shownNotes = notes;
        shownTriggers = triggers;
        shownNote = lastNote;
        if (changed)
            repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        auto lightArea = area.removeFromLeft (18).withSizeKeepingCentre (14, 14).toFloat();
        g.setColour (theme::track);
        g.fillEllipse (lightArea);
        if (glow > 0.0f)
        {
            g.setColour (glowColour.withAlpha (glow));
            g.fillEllipse (lightArea);
        }

        area.removeFromLeft (6);
        g.setColour (shownNote >= 0 ? theme::text : theme::dimText);
        g.setFont (juce::FontOptions (12.0f));
        const auto text = shownNote >= 0 ? juce::String::fromUTF8 ("\xc3\xbaltima: ") + juce::String (dsp::flNoteName (shownNote))
                                         : juce::String ("sin notas");
        g.drawText (text, area, juce::Justification::centredLeft);
    }

private:
    std::uint32_t shownNotes = 0, shownTriggers = 0;
    int shownNote = -1;
    bool primed = false;
    float glow = 0.0f;
    juce::Colour glowColour = theme::accent;
};

} // namespace ebb::gui
