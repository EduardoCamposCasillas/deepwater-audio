#pragma once

#include <cstddef>
#include <vector>

namespace fathom::edit
{

// Historial de deshacer/rehacer, sin JUCE (se prueba en tests/EngineTests.cpp).
//
// Cada paso guarda solo los parámetros que cambiaron (su índice, el valor de antes y el de después), no una foto entera
// del plugin: así deshacer un paso no pisa lo que haya cambiado otra cosa en medio (la automatización del host, por
// ejemplo). Los valores son los normalizados (0-1) de los parámetros, los mismos con los que se guardan.
//
//   pasos:  [A] [B] [C] [D]          deshacer dos veces →  [A] [B] | [C] [D]   (C y D se pueden rehacer)
//                          ^ posición                           ^
//
// Hacer un cambio nuevo después de deshacer borra lo que se podía rehacer (como en cualquier editor).
class UndoStack
{
public:
    struct Change
    {
        std::vector<int> indices;
        std::vector<float> before, after;
    };

    explicit UndoStack (size_t maxSteps = 200) : limit (maxSteps < 1 ? 1 : maxSteps) {}

    // Guarda un paso comparando el estado de antes con el de después (mismo tamaño). Devuelve false si nada cambió.
    bool record (const std::vector<float>& before, const std::vector<float>& after)
    {
        Change change;
        const size_t count = before.size() < after.size() ? before.size() : after.size();
        for (size_t i = 0; i < count; ++i)
            if (before[i] != after[i])
            {
                change.indices.push_back (static_cast<int> (i));
                change.before.push_back (before[i]);
                change.after.push_back (after[i]);
            }
        if (change.indices.empty())
            return false;

        changes.resize (position); // lo que se podía rehacer ya no vale
        changes.push_back (std::move (change));
        if (changes.size() > limit)
            changes.erase (changes.begin());
        position = changes.size();
        return true;
    }

    [[nodiscard]] bool canUndo() const noexcept { return position > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return position < changes.size(); }
    [[nodiscard]] size_t size() const noexcept { return changes.size(); }

    // El paso a deshacer (hay que volver sus parámetros a 'before') o nullptr si no hay.
    const Change* undo() noexcept { return canUndo() ? &changes[--position] : nullptr; }

    // El paso a rehacer (sus parámetros a 'after') o nullptr si no hay.
    const Change* redo() noexcept { return canRedo() ? &changes[position++] : nullptr; }

    void clear() noexcept
    {
        changes.clear();
        position = 0;
    }

private:
    std::vector<Change> changes;
    size_t position = 0; // cuántos pasos están "hechos"
    size_t limit;
};

} // namespace fathom::edit
