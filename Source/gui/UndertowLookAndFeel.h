#pragma once

#include <array>
#include <cmath>
#include <utility>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gui/Theme.h"

namespace undertow::gui
{

// Aspecto propio de todos los controles de JUCE (perillas, selectores, botones, grupos).
// Todo se dibuja con vectores, sin imágenes: se ve nítido a cualquier zoom y con cualquier escalado de Windows.
class UndertowLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    // Geometría común de las perillas: ModKnob la usa para dibujar sus anillos de modulación justo por fuera.
    static constexpr float rotaryStart = juce::MathConstants<float>::pi * 1.2f;
    static constexpr float rotaryEnd = juce::MathConstants<float>::pi * 2.8f;
    static constexpr float valueRingRadius = 0.78f; // fracción del radio total
    static constexpr float bodyRadius = 0.6f;
    static constexpr float modRingRadius = 0.94f;    // el anillo de modulación va por fuera del de valor

    UndertowLookAndFeel()
    {
        using namespace theme;
        setColourScheme ({ background, panel, control, panelOutline, text, accent, juce::Colours::black, control, text });

        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::Label::textColourId, text);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.4f));
        setColour (juce::Slider::trackColourId, accent);
        setColour (juce::Slider::backgroundColourId, track);
        setColour (juce::Slider::thumbColourId, text);
        setColour (juce::Slider::rotarySliderFillColourId, accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, track);
        setColour (juce::ComboBox::backgroundColourId, control);
        setColour (juce::ComboBox::outlineColourId, panelOutline);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, dimText);
        setColour (juce::ComboBox::focusedOutlineColourId, accent);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedTextColourId, text);
        setColour (juce::PopupMenu::headerTextColourId, dimText);
        setColour (juce::TextButton::buttonColourId, control);
        setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.3f));
        setColour (juce::TextButton::textColourOffId, dimText);
        setColour (juce::TextButton::textColourOnId, text);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::GroupComponent::textColourId, dimText);
        setColour (juce::GroupComponent::outlineColourId, panelOutline);
        setColour (juce::TooltipWindow::backgroundColourId, panel.brighter (0.15f));
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, panelOutline);
    }

    // Perilla: recorrido tenue + arco de valor (desde el centro si el parámetro es bipolar, como Pan o Fine) +
    // cuerpo con una línea indicadora. El hueco exterior queda libre para los anillos de modulación.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float startAngle,
                           float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float ringRadius = radius * valueRingRadius;
        const float lineWidth = juce::jmax (2.0f, radius * 0.09f);
        const bool enabled = slider.isEnabled();

        juce::Path trackArc;
        trackArc.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (theme::track);
        g.strokePath (trackArc, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float origin = bipolar ? static_cast<float> (slider.valueToProportionOfLength (0.0)) : 0.0f;
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        const float originAngle = startAngle + origin * (endAngle - startAngle);
        if (std::abs (angle - originAngle) > 0.001f)
        {
            juce::Path valueArc;
            valueArc.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, juce::jmin (angle, originAngle),
                                    juce::jmax (angle, originAngle), true);
            g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId).withAlpha (enabled ? 1.0f : 0.4f));
            g.strokePath (valueArc,
                          juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const float body = radius * bodyRadius;
        g.setGradientFill (juce::ColourGradient (theme::control.brighter (0.25f), centre.x, centre.y - body,
                                                 theme::control.darker (0.3f), centre.x, centre.y + body, false));
        g.fillEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (centre));
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (centre), 1.0f);

        const auto tip = centre.getPointOnCircumference (body * 0.85f, angle);
        const auto inner = centre.getPointOnCircumference (body * 0.35f, angle);
        g.setColour (theme::text.withAlpha (enabled ? 1.0f : 0.4f));
        g.drawLine ({ inner, tip }, juce::jmax (1.5f, radius * 0.06f));
    }

    // Deslizador horizontal (amount de la matriz): barra fina con el relleno desde el centro si es bipolar.
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        const auto bar = bounds.withSizeKeepingCentre (bounds.getWidth(), 4.0f);
        g.setColour (theme::track);
        g.fillRoundedRectangle (bar, 2.0f);

        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float originX = bipolar ? bounds.getX() + bounds.getWidth() * static_cast<float> (slider.valueToProportionOfLength (0.0))
                                      : bounds.getX();
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (originX, sliderPos), bar.getY(),
                                                                          juce::jmax (originX, sliderPos), bar.getBottom()),
                                2.0f);

        g.setColour (theme::text);
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ sliderPos, bounds.getCentreY() }));
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (juce::FontOptions (12.0f));
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

    // Grupo = panel redondeado con el título en mayúsculas arriba a la izquierda.
    void drawGroupComponentOutline (juce::Graphics& g, int width, int height, const juce::String& text,
                                    const juce::Justification&, juce::GroupComponent&) override
    {
        const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

        g.setColour (theme::accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, 8.0f, 3.0f, 12.0f), 1.5f);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (upperCase (text), juce::Rectangle<float> (20.0f, 4.0f, bounds.getWidth() - 28.0f, 20.0f),
                    juce::Justification::centredLeft);
    }

    // String::toUpperCase depende de la configuración regional de C (en Windows deja "ó" en minúscula): las vocales con
    // tilde y la ñ se pasan a mano.
    static juce::String upperCase (const juce::String& text)
    {
        auto result = text.toUpperCase();
        constexpr std::array<std::pair<juce::juce_wchar, juce::juce_wchar>, 6> accents { {
            { 0xe1, 0xc1 }, { 0xe9, 0xc9 }, { 0xed, 0xcd }, { 0xf3, 0xd3 }, { 0xfa, 0xda }, { 0xf1, 0xd1 } } };
        for (const auto& [lower, upper] : accents)
            result = result.replaceCharacter (lower, upper);
        return result;
    }

    // "On" y opciones: un interruptor pequeño (pastilla) en vez de la casilla clásica.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool) override
    {
        const auto bounds = button.getLocalBounds().toFloat();
        const float switchHeight = juce::jmin (16.0f, bounds.getHeight() - 4.0f);
        const auto pill = juce::Rectangle<float> (bounds.getX() + 2.0f, bounds.getCentreY() - switchHeight * 0.5f,
                                                  switchHeight * 1.8f, switchHeight);
        const bool on = button.getToggleState();

        g.setColour (on ? theme::accent.withAlpha (0.85f) : theme::track.brighter (highlighted ? 0.15f : 0.0f));
        g.fillRoundedRectangle (pill, switchHeight * 0.5f);
        const float knob = switchHeight - 4.0f;
        g.setColour (on ? theme::background : theme::dimText);
        g.fillEllipse (on ? pill.getRight() - knob - 2.0f : pill.getX() + 2.0f, pill.getY() + 2.0f, knob, knob);

        g.setColour (button.findColour (juce::ToggleButton::textColourId).withAlpha (button.isEnabled() ? 1.0f : 0.5f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (button.getButtonText(), bounds.withTrimmedLeft (pill.getWidth() + 8.0f),
                    juce::Justification::centredLeft);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted,
                               bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = button.getToggleState();
        auto fill = on ? theme::accent.withAlpha (0.22f) : theme::control;
        if (highlighted || down)
            fill = fill.brighter (0.12f);
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (on ? theme::accent.withAlpha (0.8f) : theme::panelOutline);
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId).brighter (box.isMouseOver (true) ? 0.08f : 0.0f));
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (box.hasKeyboardFocus (false) ? theme::accent : theme::panelOutline);
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

        juce::Path arrow;
        const float cx = static_cast<float> (width) - 12.0f;
        const float cy = static_cast<float> (height) * 0.5f;
        arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (theme::dimText);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }
    juce::Font getPopupMenuFont() override { return juce::FontOptions (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (13.0f, juce::Font::bold); }
};

} // namespace undertow::gui
