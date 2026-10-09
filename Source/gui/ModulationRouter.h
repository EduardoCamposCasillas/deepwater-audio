#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "gui/ModulationTargets.h"

namespace undertow::gui
{

// Puente entre "arrastrar para modular" y la matriz de 8 rutas. No hay un sistema de modulación aparte: soltar una
// fuente sobre una perilla rellena una ruta de la matriz (fuente, destino, amount). Así todo lo que se hace con el
// ratón queda guardado en el proyecto, es automatizable y se ve igual en la pestaña "Modulación".
//
// Solo se usa desde el hilo de mensajes. Cada cambio pasa por setValueNotifyingHost con su gesto (begin/end):
// FL lo registra como un cambio del usuario y el audio lo recibe por los atómicos del APVTS, como una perilla más.
class ModulationRouter
{
public:
    explicit ModulationRouter (juce::AudioProcessorValueTreeState& apvts)
    {
        for (size_t s = 0; s < slots.size(); ++s)
        {
            const auto& ids = params::modSlots[s];
            slots[s] = { apvts.getParameter (ids.source), apvts.getParameter (ids.destination), apvts.getParameter (ids.amount) };
            jassert (slots[s].source != nullptr && slots[s].destination != nullptr && slots[s].amount != nullptr);
        }
    }

    [[nodiscard]] ModSlots read() const
    {
        ModSlots result;
        for (size_t s = 0; s < slots.size(); ++s)
        {
            const auto& slot = slots[s];
            result[s].source = static_cast<synth::ModSource> (juce::roundToInt (slot.source->convertFrom0to1 (slot.source->getValue())));
            result[s].destination =
                static_cast<synth::ModDestination> (juce::roundToInt (slot.destination->convertFrom0to1 (slot.destination->getValue())));
            result[s].amount = slot.amount->convertFrom0to1 (slot.amount->getValue());
        }
        return result;
    }

    // Conecta la fuente con el destino. Devuelve la ruta usada, o −1 si la matriz está llena.
    int connect (synth::ModSource source, synth::ModDestination destination)
    {
        const auto current = read();
        const int index = findSlotFor (current, source, destination);
        if (index < 0)
            return -1;

        const auto& existing = current[static_cast<size_t> (index)];
        if (existing.source == source && existing.destination == destination)
            return index; // ya estaba conectada: se conserva su amount

        // Orden: amount → destino → fuente. Mientras falte la fuente la ruta no suena, así que el audio nunca ve
        // una combinación a medias (p. ej. la fuente nueva con el destino viejo de la ruta).
        auto& slot = slots[static_cast<size_t> (index)];
        setAsGesture (*slot.source, 0.0f);
        setAsGesture (*slot.amount, defaultDropAmount);
        setAsGesture (*slot.destination, static_cast<float> (destination));
        setAsGesture (*slot.source, static_cast<float> (source));
        return index;
    }

    // Quita la ruta: primero la fuente (deja de sonar con el fundido de 5 ms de la voz) y después lo demás.
    void remove (int index)
    {
        auto& slot = slots[static_cast<size_t> (index)];
        setAsGesture (*slot.source, 0.0f);
        setAsGesture (*slot.destination, 0.0f);
        setAsGesture (*slot.amount, 0.0f);
    }

    // Arrastrar el anillo de una perilla = un solo gesto de automatización, de mouseDown a mouseUp.
    void beginAmountGesture (int index) { slots[static_cast<size_t> (index)].amount->beginChangeGesture(); }
    void setAmount (int index, float amount)
    {
        auto& parameter = *slots[static_cast<size_t> (index)].amount;
        parameter.setValueNotifyingHost (parameter.convertTo0to1 (juce::jlimit (-1.0f, 1.0f, amount)));
    }
    void endAmountGesture (int index) { slots[static_cast<size_t> (index)].amount->endChangeGesture(); }

private:
    static void setAsGesture (juce::RangedAudioParameter& parameter, float value)
    {
        parameter.beginChangeGesture();
        parameter.setValueNotifyingHost (parameter.convertTo0to1 (value));
        parameter.endChangeGesture();
    }

    struct SlotParameters
    {
        juce::RangedAudioParameter* source = nullptr;
        juce::RangedAudioParameter* destination = nullptr;
        juce::RangedAudioParameter* amount = nullptr;
    };
    std::array<SlotParameters, synth::numModSlots> slots {};
};

} // namespace undertow::gui
