#pragma once

#include <map>
#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "edit/UndoStack.h"

namespace fathom::edit
{

// Deshacer/rehacer de lo que el usuario edita en la GUI (hilo de mensajes).
//
// ¿Cómo sabe qué fue una edición? Por los GESTOS de los parámetros: cada control de JUCE avisa "empiezo a mover" y
// "terminé" (beginChangeGesture/endChangeGesture) alrededor de lo que cambia, y el visor hace lo mismo al arrastrar un
// nodo. La automatización del host no manda gestos, así que no entra al historial (deshacer no debe pelear con FL).
// Al abrirse el primer gesto se toma la foto "antes"; al cerrarse el último, la foto "después", y la diferencia es un
// paso. Varios gestos a la vez (arrastrar tres nodos: frecuencia y ganancia de cada uno) son UN paso; para agrupar
// cambios que no se solapan en el tiempo está beginTransaction/endTransaction.
//
// Se eligió esto en vez del UndoManager de JUCE dentro del AudioProcessorValueTreeState porque ese registra los cambios
// cuando el estado se copia al ValueTree (con un temporizador, mezclando la automatización del host) y no por gesto.
class EditHistory final : private juce::AudioProcessorParameter::Listener
{
public:
    EditHistory() = default;
    ~EditHistory() override { detach(); }

    // Los parámetros que cubre el historial (todos menos el bypass: deshacer no debe prender ni apagar el plugin).
    void attach (std::vector<juce::RangedAudioParameter*> parametersToTrack)
    {
        detach();
        parameters = std::move (parametersToTrack);
        inGesture.assign (parameters.size(), false);
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            parameters[i]->addListener (this);
            indexOf[parameters[i]->getParameterIndex()] = static_cast<int> (i);
        }
    }

    void beginTransaction()
    {
        if (open() == 0)
            before = snapshot();
        ++transactions;
    }

    void endTransaction()
    {
        if (transactions == 0)
            return;
        --transactions;
        closeIfDone();
    }

    bool undo() { return apply (stack.undo(), false); }
    bool redo() { return apply (stack.redo(), true); }
    [[nodiscard]] bool canUndo() const noexcept { return stack.canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return stack.canRedo(); }

    // True si no hay ninguna edición a medias (nadie arrastrando un nodo ni girando una perilla).
    [[nodiscard]] bool isIdle() const noexcept { return open() == 0; }

    // Los parámetros que cubre, en el orden de snapshot() y load().
    [[nodiscard]] const std::vector<juce::RangedAudioParameter*>& getTrackedParameters() const noexcept { return parameters; }

    // Los valores normalizados (0-1) de todos los parámetros que cubre.
    std::vector<float> snapshot() const
    {
        std::vector<float> values (parameters.size());
        for (size_t i = 0; i < parameters.size(); ++i)
            values[i] = parameters[i]->getValue();
        return values;
    }

    // Pone todos los parámetros en esos valores SIN guardar un paso (la comparación A/B: cada lado tiene su historial).
    // Con gestos, como deshacer: el host graba el cambio si está grabando automatización.
    void load (const std::vector<float>& values)
    {
        applying = true;
        for (size_t i = 0; i < parameters.size() && i < values.size(); ++i)
            if (parameters[i]->getValue() != values[i])
            {
                parameters[i]->beginChangeGesture();
                parameters[i]->setValueNotifyingHost (values[i]);
                parameters[i]->endChangeGesture();
            }
        applying = false;
    }

    // Cambia el historial de deshacer por otro (el del otro lado de la comparación A/B). Solo sin ediciones a medias.
    void swapStack (UndoStack& other) noexcept
    {
        jassert (isIdle());
        std::swap (stack, other);
    }

private:
    int open() const noexcept { return transactions + gestures; }

    void closeIfDone()
    {
        if (open() == 0)
            stack.record (before, snapshot());
    }

    bool apply (const UndoStack::Change* change, bool forward)
    {
        if (change == nullptr)
            return false;
        // Con gestos: el host graba el cambio como si lo hubiera hecho el usuario (si está grabando automatización).
        applying = true;
        for (size_t i = 0; i < change->indices.size(); ++i)
        {
            auto* parameter = parameters[static_cast<size_t> (change->indices[i])];
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (forward ? change->after[i] : change->before[i]);
            parameter->endChangeGesture();
        }
        applying = false;
        return true;
    }

    void parameterValueChanged (int, float) override {} // puede llegar desde el audio: aquí no se hace nada

    void parameterGestureChanged (int parameterIndex, bool starting) override
    {
        // Los gestos los manda la GUI; si alguno llegara de otro hilo, se ignora (el historial es del hilo de mensajes).
        if (applying || ! juce::MessageManager::existsAndIsCurrentThread())
            return;
        const auto found = indexOf.find (parameterIndex);
        if (found == indexOf.end())
            return;
        // Por parámetro: un "empiezo" repetido o un "terminé" sin "empiezo" no desarman la cuenta.
        auto&& active = inGesture[static_cast<size_t> (found->second)];
        if (starting == static_cast<bool> (active))
            return;
        if (starting)
        {
            if (open() == 0)
                before = snapshot();
            active = true;
            ++gestures;
        }
        else
        {
            active = false;
            --gestures;
            closeIfDone();
        }
    }

    void detach()
    {
        for (auto* parameter : parameters)
            parameter->removeListener (this);
        parameters.clear();
        indexOf.clear();
    }

    std::vector<juce::RangedAudioParameter*> parameters;
    std::map<int, int> indexOf; // índice del parámetro en el procesador → posición en 'parameters'
    std::vector<bool> inGesture;
    int gestures = 0, transactions = 0;
    bool applying = false;
    std::vector<float> before;
    UndoStack stack;
};

} // namespace fathom::edit
