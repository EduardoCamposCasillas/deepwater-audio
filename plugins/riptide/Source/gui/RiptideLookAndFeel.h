#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gui/Theme.h"

namespace riptide::gui
{

// Aspecto de los controles de JUCE. Fase 1: perillas y botones funcionales; la GUI profesional llega en la Fase 8.
// Todo con vectores: se ve nítido con cualquier escalado de Windows.
class RiptideLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    // Propiedad de un Slider con la posición modulada (0..1); sin ella no se dibuja el arco de modulación.
    static inline const juce::Identifier modulationProperty { "modulation" };

    RiptideLookAndFeel()
    {
        using namespace theme;
        setColourScheme ({ background, panel, control, panelOutline, text, accent, juce::Colours::black, control, text });

        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::Label::textColourId, text);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.4f));
        setColour (juce::Slider::rotarySliderFillColourId, accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, track);
        setColour (juce::TextButton::buttonColourId, control);
        setColour (juce::TextButton::buttonOnColourId, meterClip.withAlpha (0.55f));
        setColour (juce::TextButton::textColourOffId, dimText);
        setColour (juce::TextButton::textColourOnId, text);
        setColour (juce::ComboBox::backgroundColourId, control);
        setColour (juce::ComboBox::outlineColourId, panelOutline);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, dimText);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.35f));
        setColour (juce::PopupMenu::highlightedTextColourId, text);
        setColour (juce::TooltipWindow::backgroundColourId, panel.brighter (0.15f));
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, panelOutline);
    }

    // Perilla: recorrido tenue + arco de valor (desde el centro si el rango es bipolar: en las ganancias el centro es
    // 0 dB, así se ve de un vistazo si se sube o se baja) + cuerpo con una línea indicadora.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float startAngle,
                           float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float ringRadius = radius * 0.85f;
        const float lineWidth = juce::jmax (2.5f, radius * 0.1f);
        const auto stroke = juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path trackArc;
        trackArc.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (theme::track);
        g.strokePath (trackArc, stroke);

        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float origin = bipolar ? static_cast<float> (slider.valueToProportionOfLength (0.0)) : 0.0f;
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        const float originAngle = startAngle + origin * (endAngle - startAngle);
        if (std::abs (angle - originAngle) > 0.001f)
        {
            juce::Path valueArc;
            valueArc.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, juce::jmin (angle, originAngle),
                                    juce::jmax (angle, originAngle), true);
            g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId).withAlpha (slider.isEnabled() ? 1.0f : 0.4f));
            g.strokePath (valueArc, stroke);
        }

        // Fase 7: si la envolvente o el LFO mueven esta perilla, el editor guarda en la propiedad "modulation" la posición
        // (0..1 del recorrido) que suena de verdad. Un arco interior frío va de la perilla a esa posición, con un punto.
        if (const auto* modulated = slider.getProperties().getVarPointer (modulationProperty))
        {
            const float modAngle = startAngle + static_cast<float> (static_cast<double> (*modulated)) * (endAngle - startAngle);
            const float modRadius = ringRadius - lineWidth * 1.3f;
            if (std::abs (modAngle - angle) > 0.001f)
            {
                juce::Path modArc;
                modArc.addCentredArc (centre.x, centre.y, modRadius, modRadius, 0.0f, juce::jmin (angle, modAngle),
                                      juce::jmax (angle, modAngle), true);
                g.setColour (theme::modulation.withAlpha (slider.isEnabled() ? 0.9f : 0.35f));
                g.strokePath (modArc, juce::PathStrokeType (lineWidth * 0.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (theme::modulation);
            g.fillEllipse (juce::Rectangle<float> (lineWidth, lineWidth).withCentre (centre.getPointOnCircumference (modRadius, modAngle)));
        }

        const float body = radius * 0.65f;
        const auto bodyBounds = juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (centre);
        g.setGradientFill (juce::ColourGradient (theme::control.brighter (0.25f), centre.x, centre.y - body,
                                                 theme::control.darker (0.3f), centre.x, centre.y + body, false));
        g.fillEllipse (bodyBounds);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (bodyBounds, 1.0f);

        g.setColour (theme::text.withAlpha (slider.isEnabled() ? 1.0f : 0.4f));
        g.drawLine ({ centre.getPointOnCircumference (body * 0.35f, angle), centre.getPointOnCircumference (body * 0.85f, angle) },
                    juce::jmax (1.5f, radius * 0.06f));
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (juce::FontOptions (13.0f));
        label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        return label;
    }

    // Etiquetas sin recuadro (salvo mientras se escribe un valor en ellas).
    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        g.fillAll (label.findColour (juce::Label::backgroundColourId));
        if (label.isBeingEdited())
        {
            g.setColour (theme::accent);
            g.drawRect (label.getLocalBounds());
            return;
        }

        const juce::Font font (getLabelFont (label));
        const auto area = label.getBorderSize().subtractedFrom (label.getLocalBounds());
        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.5f));
        g.setFont (font);
        g.drawFittedText (label.getText(), area, label.getJustificationType(),
                          juce::jmax (1, static_cast<int> (static_cast<float> (area.getHeight()) / font.getHeight())),
                          label.getMinimumHorizontalScale());
    }
};

} // namespace riptide::gui
