#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "gui/ModulationRouter.h"
#include "gui/ModulationTargets.h"
#include "gui/Theme.h"
#include "gui/UndertowLookAndFeel.h"

namespace undertow::gui
{

// Perilla de Undertow. Si su parámetro es un destino de la matriz, además:
//  - acepta fuentes de modulación arrastradas desde la barra inferior (crea la ruta en la matriz);
//  - dibuja por fuera un anillo de color por cada ruta: el tramo que recorre la modulación;
//  - arrastrar sobre ese anillo cambia el amount de la ruta seleccionada (clic derecho: elegir o quitar rutas);
//  - un punto blanco muestra en vivo dónde está el destino modulado en la nota más reciente.
class ModKnob final : public juce::Slider, public juce::DragAndDropTarget
{
public:
    ModKnob() : juce::Slider (RotaryHorizontalVerticalDrag, TextBoxBelow)
    {
        setRotaryParameters (UndertowLookAndFeel::rotaryStart, UndertowLookAndFeel::rotaryEnd, true);
    }

    // --- Arrastrar y soltar: la descripción del arrastre lleva el índice de la fuente ---
    static juce::var dragDescription (synth::ModSource source)
    {
        return juce::String (dragPrefix) + juce::String (static_cast<int> (source));
    }

    static std::optional<synth::ModSource> sourceFromDescription (const juce::var& description)
    {
        const auto text = description.toString();
        if (! text.startsWith (dragPrefix))
            return std::nullopt;
        const int index = text.fromFirstOccurrenceOf (dragPrefix, false, false).getIntValue();
        if (index <= 0 || index >= synth::numModSources)
            return std::nullopt;
        return static_cast<synth::ModSource> (index);
    }

    // El parámetro sirve para el menú contextual del host (automatización de FL).
    void setParameter (juce::RangedAudioParameter* newParameter) { parameter = newParameter; }

    void setModulationTarget (const ModulationTarget& newTarget, ModulationRouter& newRouter,
                              std::function<void (const juce::String&)> messageCallback)
    {
        target = newTarget;
        router = &newRouter;
        onMessage = std::move (messageCallback);
    }

    [[nodiscard]] bool isModulationTarget() const noexcept { return router != nullptr; }
    [[nodiscard]] synth::ModDestination getModulationDestination() const noexcept { return target.destination; }

    // Lo llama el timer del editor. 'live' = modulación total del destino en la nota más reciente (si suena alguna).
    void updateModulation (const ModSlots& slots, std::optional<float> live)
    {
        if (router == nullptr)
            return;

        std::vector<Route> newRoutes;
        for (const int s : routesTo (slots, target.destination))
            newRoutes.push_back ({ s, slots[static_cast<size_t> (s)].source, slots[static_cast<size_t> (s)].amount });

        // Si la ruta seleccionada ya no llega aquí (se quitó o se cambió en la matriz), se selecciona la última.
        const bool focusValid = std::any_of (newRoutes.begin(), newRoutes.end(), [this] (const Route& r) { return r.slot == focusedSlot; });
        if (! focusValid && ! amountDrag)
            focusedSlot = newRoutes.empty() ? -1 : newRoutes.back().slot;

        // Mientras se arrastra el anillo manda el valor local (el del parámetro llega un instante después).
        if (amountDrag)
            for (auto& route : newRoutes)
                if (route.slot == focusedSlot)
                    route.amount = dragAmount;

        if (newRoutes != routes || live != liveModulation)
        {
            routes = std::move (newRoutes);
            liveModulation = live;
            updateTooltip();
            repaint();
        }
    }

    // Mientras se arrastra una fuente por la ventana, las perillas que la aceptan se resaltan.
    void setDragInProgress (std::optional<synth::ModSource> source)
    {
        const auto shown = router != nullptr ? source : std::nullopt;
        if (shown != draggedSource)
        {
            draggedSource = shown;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        juce::Slider::paint (g);
        if (router == nullptr)
            return;

        const auto [centre, radius] = rotaryGeometry();
        const float ring = radius * UndertowLookAndFeel::modRingRadius;
        const float thickness = juce::jmax (2.5f, radius * 0.1f);
        const auto angleFor = [] (float proportion) {
            return UndertowLookAndFeel::rotaryStart
                   + proportion * (UndertowLookAndFeel::rotaryEnd - UndertowLookAndFeel::rotaryStart);
        };
        const float knobValue = static_cast<float> (valueToProportionOfLength (getValue()));

        // Arrastre en curso: guía tenue del anillo; si la fuente está encima, en su color.
        if (draggedSource.has_value())
        {
            juce::Path guide;
            guide.addCentredArc (centre.x, centre.y, ring, ring, 0.0f, UndertowLookAndFeel::rotaryStart,
                                 UndertowLookAndFeel::rotaryEnd, true);
            const auto colour = dropHover ? theme::sourceColour (*draggedSource) : theme::text;
            g.setColour (colour.withAlpha (dropHover ? 0.9f : 0.25f));
            g.strokePath (guide, juce::PathStrokeType (dropHover ? thickness : 1.5f));
        }

        // Primero las rutas no seleccionadas (más tenues) y la seleccionada encima.
        for (const bool drawFocused : { false, true })
            for (const auto& route : routes)
            {
                if ((route.slot == focusedSlot) != drawFocused)
                    continue;

                const auto range = modulationRange (knobValue, route.amount, target.scale, isBipolar (route.source));
                const auto colour = theme::sourceColour (route.source).withAlpha (drawFocused ? 1.0f : 0.5f);
                g.setColour (colour);
                if (range.to - range.from < 0.004f)
                {
                    // Amount 0: un punto para que se vea que la ruta existe.
                    const auto point = centre.getPointOnCircumference (ring, angleFor (knobValue));
                    g.fillEllipse (juce::Rectangle<float> (thickness, thickness).withCentre (point));
                    continue;
                }
                juce::Path arc;
                arc.addCentredArc (centre.x, centre.y, ring, ring, 0.0f, angleFor (range.from), angleFor (range.to), true);
                g.strokePath (arc, juce::PathStrokeType (drawFocused ? thickness : thickness * 0.7f,
                                                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

        if (liveModulation.has_value() && ! routes.empty())
        {
            const float position = juce::jlimit (0.0f, 1.0f, knobValue + *liveModulation * target.scale);
            const auto point = centre.getPointOnCircumference (ring, angleFor (position));
            g.setColour (theme::background);
            g.fillEllipse (juce::Rectangle<float> (thickness + 3.0f, thickness + 3.0f).withCentre (point));
            g.setColour (theme::text);
            g.fillEllipse (juce::Rectangle<float> (thickness + 1.0f, thickness + 1.0f).withCentre (point));
        }

        // Mientras se ajusta un amount, su valor tapa el cuadro de texto de la perilla.
        if (amountDrag)
            if (const auto* route = focusedRoute())
            {
                const auto box = getLocalBounds().removeFromBottom (20).toFloat().reduced (2.0f, 1.0f);
                g.setColour (theme::background.withAlpha (0.95f));
                g.fillRoundedRectangle (box, 4.0f);
                g.setColour (theme::sourceColour (route->source));
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawText (amountText (route->amount), box, juce::Justification::centred);
            }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            showMenu();
            return;
        }

        if (isInModulationRing (e.position) && focusedRoute() != nullptr)
        {
            amountDrag = true;
            dragAmount = dragStartAmount = focusedRoute()->amount;
            router->beginAmountGesture (focusedSlot);
            repaint();
            return;
        }
        juce::Slider::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! amountDrag)
        {
            if (! e.mods.isPopupMenu())
                juce::Slider::mouseDrag (e);
            return;
        }

        // 200 px de arrastre = 100 % (con Shift, 1000 px: ajuste fino). Arriba o a la derecha suma.
        const float pixels = static_cast<float> (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY());
        const float perPixel = e.mods.isShiftDown() ? 0.001f : 0.005f;
        dragAmount = juce::jlimit (-1.0f, 1.0f, dragStartAmount + pixels * perPixel);
        router->setAmount (focusedSlot, dragAmount);
        for (auto& route : routes)
            if (route.slot == focusedSlot)
                route.amount = dragAmount;
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (amountDrag)
        {
            amountDrag = false;
            router->endAmountGesture (focusedSlot);
            updateTooltip();
            repaint();
            return;
        }
        if (! e.mods.isPopupMenu())
            juce::Slider::mouseUp (e);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (isInModulationRing (e.position) && focusedRoute() != nullptr)
            return; // un doble clic en el anillo no debe devolver la perilla a su valor por defecto
        juce::Slider::mouseDoubleClick (e);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        juce::Slider::mouseMove (e);
        const bool overRing = isInModulationRing (e.position) && focusedRoute() != nullptr;
        setMouseCursor (overRing ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    }

    // --- DragAndDropTarget ---
    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        return router != nullptr && sourceFromDescription (details.description).has_value();
    }

    void itemDragEnter (const SourceDetails&) override
    {
        dropHover = true;
        repaint();
    }

    void itemDragExit (const SourceDetails&) override
    {
        dropHover = false;
        repaint();
    }

    void itemDropped (const SourceDetails& details) override
    {
        dropHover = false;
        const auto source = sourceFromDescription (details.description);
        if (! source.has_value())
            return;

        const int slot = router->connect (*source, target.destination);
        const juce::String name (synth::modSourceNames[static_cast<size_t> (*source)]);
        if (slot < 0)
        {
            notify (juce::String::fromUTF8 ("Las 8 rutas de la matriz est\xc3\xa1n en uso: quita una (clic derecho en una perilla) "
                                            "o lib\xc3\xa9rala en la pesta\xc3\xb1" "a Modulaci\xc3\xb3n."));
        }
        else
        {
            focusedSlot = slot;
            notify (name + juce::String::fromUTF8 (" \xe2\x86\x92 ") + getName() + " (ruta " + juce::String (slot + 1)
                    + "). Arrastra el anillo de color para ajustarla.");
        }
        repaint();
    }

private:
    static constexpr const char* dragPrefix = "undertow-mod-source:";

    struct Route
    {
        int slot = -1;
        synth::ModSource source = synth::ModSource::none;
        float amount = 0.0f;
        bool operator== (const Route&) const = default;
    };

    struct Geometry
    {
        juce::Point<float> centre;
        float radius;
    };

    // La misma geometría con la que el LookAndFeel dibuja la perilla.
    [[nodiscard]] Geometry rotaryGeometry()
    {
        const auto area = getLookAndFeel().getSliderLayout (*this).sliderBounds.toFloat();
        return { area.getCentre(), juce::jmin (area.getWidth(), area.getHeight()) * 0.5f };
    }

    [[nodiscard]] bool isInModulationRing (juce::Point<float> position)
    {
        if (router == nullptr)
            return false;
        const auto [centre, radius] = rotaryGeometry();
        const float distance = position.getDistanceFrom (centre);
        return distance >= radius * 0.84f && distance <= radius * 1.08f;
    }

    [[nodiscard]] const Route* focusedRoute() const
    {
        for (const auto& route : routes)
            if (route.slot == focusedSlot)
                return &route;
        return nullptr;
    }

    static juce::String amountText (float amount)
    {
        const int percent = juce::roundToInt (amount * 100.0f);
        return (percent > 0 ? "+" : "") + juce::String (percent) + " %";
    }

    void notify (const juce::String& message)
    {
        if (onMessage)
            onMessage (message);
    }

    void updateTooltip()
    {
        if (routes.empty())
        {
            setTooltip (juce::String::fromUTF8 ("Suelta aqu\xc3\xad una fuente de la barra inferior para modular ") + getName()
                        + ".");
            return;
        }
        juce::String text = getName() + juce::String::fromUTF8 (" est\xc3\xa1 modulado por:");
        for (const auto& route : routes)
            text << "\n  " << (route.slot == focusedSlot ? juce::String::fromUTF8 ("\xe2\x96\xb8 ") : "   ")
                 << synth::modSourceNames[static_cast<size_t> (route.source)] << "  " << amountText (route.amount);
        text << juce::String::fromUTF8 ("\nArrastra el anillo exterior para ajustar la ruta marcada. Clic derecho: elegir o quitar.");
        setTooltip (text);
    }

    void showMenu()
    {
        juce::PopupMenu menu;
        constexpr int selectBase = 1, removeBase = 100, removeAll = 200;

        if (! routes.empty())
        {
            menu.addSectionHeader (juce::String::fromUTF8 ("Modulaci\xc3\xb3n de ") + getName());
            for (const auto& route : routes)
                menu.addItem (selectBase + route.slot,
                              juce::String::fromUTF8 ("Ajustar ") + synth::modSourceNames[static_cast<size_t> (route.source)]
                                  + "  (" + amountText (route.amount) + ")",
                              true, route.slot == focusedSlot);
            menu.addSeparator();
            for (const auto& route : routes)
                menu.addItem (removeBase + route.slot,
                              juce::String ("Quitar ") + synth::modSourceNames[static_cast<size_t> (route.source)]);
            if (routes.size() > 1)
                menu.addItem (removeAll, "Quitar todas");
        }

        // Opciones del host para este parámetro (en FL, p. ej., crear un clip de automatización).
        if (parameter != nullptr)
            if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
                if (auto* context = editor->getHostContext())
                    if (auto hostMenu = context->getContextMenuForParameter (parameter))
                    {
                        if (menu.getNumItems() > 0)
                            menu.addSeparator();
                        menu.addSubMenu ("Host", hostMenu->getEquivalentPopupMenu());
                    }

        if (menu.getNumItems() == 0)
            return;

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safeThis = juce::Component::SafePointer<ModKnob> (this)] (int result) {
                                if (safeThis == nullptr || result == 0)
                                    return;
                                auto& self = *safeThis;
                                if (result >= selectBase && result < removeBase)
                                    self.focusedSlot = result - selectBase;
                                else if (result >= removeBase && result < removeAll)
                                    self.router->remove (result - removeBase);
                                else if (result == removeAll)
                                    for (const auto& route : self.routes)
                                        self.router->remove (route.slot);
                                self.updateTooltip();
                                self.repaint();
                            });
    }

    juce::RangedAudioParameter* parameter = nullptr;
    ModulationTarget target { synth::ModDestination::none, "", 1.0f };
    ModulationRouter* router = nullptr;
    std::function<void (const juce::String&)> onMessage;

    std::vector<Route> routes;
    std::optional<float> liveModulation;
    std::optional<synth::ModSource> draggedSource;
    int focusedSlot = -1;
    bool dropHover = false;
    bool amountDrag = false;
    float dragAmount = 0.0f, dragStartAmount = 0.0f;
};

} // namespace undertow::gui
