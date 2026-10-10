#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gui/Theme.h"
#include "gui/UndertowLookAndFeel.h"

namespace undertow::gui
{

// Fase 10: barra de presets de la cabecera.  [◀][▶] [ BASS  Bajo Moog *  ▾ ] [Guardar] [···]
// Solo muestra y avisa: qué hacer al pulsar cada cosa lo decide el editor (callbacks).
class PresetBar final : public juce::Component
{
public:
    std::function<void (int delta)> onStep;   // ◀ / ▶
    std::function<void()> onToggleBrowser;    // clic en el nombre
    std::function<void()> onSave;
    std::function<void (juce::Component& anchor)> onMenu;

    PresetBar()
    {
        previous.setTooltip ("Preset anterior");
        next.setTooltip ("Preset siguiente");
        previous.onClick = [this] { if (onStep) onStep (-1); };
        next.onClick = [this] { if (onStep) onStep (1); };

        nameButton.setTooltip (juce::String::fromUTF8 ("Abrir el navegador de presets: categor\xc3\xad" "as, b\xc3\xbasqueda y las "
                                                       "notas de c\xc3\xb3mo se construy\xc3\xb3 cada sonido."));
        nameButton.onClick = [this] { if (onToggleBrowser) onToggleBrowser(); };

        saveButton.setTooltip (juce::String::fromUTF8 ("Guardar el sonido actual como preset tuyo (con nombre, categor\xc3\xad" "a "
                                                       "y notas)."));
        saveButton.onClick = [this] { if (onSave) onSave(); };

        menuButton.setTooltip (juce::String::fromUTF8 ("Init, guardar como, carpeta de presets\xe2\x80\xa6"));
        menuButton.onClick = [this] { if (onMenu) onMenu (menuButton); };

        for (auto* button : { static_cast<juce::Button*> (&previous), static_cast<juce::Button*> (&next),
                              static_cast<juce::Button*> (&nameButton), static_cast<juce::Button*> (&saveButton),
                              static_cast<juce::Button*> (&menuButton) })
            addAndMakeVisible (button);
    }

    void setPreset (const juce::String& category, const juce::String& name, bool modified)
    {
        if (category == nameButton.category && name == nameButton.name && modified == nameButton.modified)
            return;
        nameButton.category = category;
        nameButton.name = name;
        nameButton.modified = modified;
        nameButton.repaint();
    }

    void setBrowserOpen (bool open)
    {
        nameButton.setToggleState (open, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        previous.setBounds (area.removeFromLeft (arrowWidth));
        area.removeFromLeft (2);
        next.setBounds (area.removeFromLeft (arrowWidth));
        area.removeFromLeft (6);
        menuButton.setBounds (area.removeFromRight (arrowWidth + 4));
        area.removeFromRight (6);
        saveButton.setBounds (area.removeFromRight (saveWidth));
        area.removeFromRight (6);
        nameButton.setBounds (area);
    }

private:
    static constexpr int arrowWidth = 26;
    static constexpr int saveWidth = 76;

    // Flecha dibujada con un triángulo (vectorial: nítida a cualquier zoom).
    class ArrowButton final : public juce::Button
    {
    public:
        explicit ArrowButton (bool pointsLeft) : juce::Button ({}), left (pointsLeft) {}

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            getLookAndFeel().drawButtonBackground (g, *this, {}, highlighted, down);
            const auto c = getLocalBounds().toFloat().getCentre();
            juce::Path arrow;
            if (left)
                arrow.addTriangle (c.x + 3.0f, c.y - 5.0f, c.x + 3.0f, c.y + 5.0f, c.x - 4.0f, c.y);
            else
                arrow.addTriangle (c.x - 3.0f, c.y - 5.0f, c.x - 3.0f, c.y + 5.0f, c.x + 4.0f, c.y);
            g.setColour (highlighted ? theme::text : theme::dimText);
            g.fillPath (arrow);
        }

    private:
        const bool left;
    };

    // "Pantalla" con la categoría (en color de acento), el nombre y un * si se modificó desde que se cargó.
    class NameButton final : public juce::Button
    {
    public:
        NameButton() : juce::Button ({}) {}

        juce::String category, name;
        bool modified = false;

        void paintButton (juce::Graphics& g, bool highlighted, bool) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (theme::display.brighter (highlighted ? 0.06f : 0.0f));
            g.fillRoundedRectangle (bounds, 6.0f);
            g.setColour (getToggleState() ? theme::accent.withAlpha (0.8f) : theme::panelOutline);
            g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

            auto area = getLocalBounds().reduced (10, 0);

            // Triángulo a la derecha: hacia abajo = abrir, hacia arriba = cerrar.
            const auto arrowArea = area.removeFromRight (12).toFloat();
            const float cx = arrowArea.getCentreX();
            const float cy = arrowArea.getCentreY();
            juce::Path arrow;
            if (getToggleState())
                arrow.addTriangle (cx - 4.0f, cy + 2.0f, cx + 4.0f, cy + 2.0f, cx, cy - 3.0f);
            else
                arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
            g.setColour (theme::dimText);
            g.fillPath (arrow);

            const juce::Font categoryFont (juce::FontOptions (10.5f, juce::Font::bold));
            const auto categoryText = UndertowLookAndFeel::upperCase (category);
            const int categoryWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (categoryFont, categoryText)) + 10;
            g.setFont (categoryFont);
            g.setColour (theme::accent);
            g.drawText (categoryText, area.removeFromLeft (categoryWidth), juce::Justification::centredLeft);

            g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
            g.setColour (theme::text);
            g.drawFittedText (name + (modified ? " *" : ""), area, juce::Justification::centredLeft, 1, 0.8f);
        }
    };

    ArrowButton previous { true }, next { false };
    NameButton nameButton;
    juce::TextButton saveButton { "Guardar" };
    juce::TextButton menuButton { juce::String::fromUTF8 ("\xc2\xb7\xc2\xb7\xc2\xb7") }; // ···
};

} // namespace undertow::gui
