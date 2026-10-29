#pragma once

#include <algorithm>
#include <cmath>

#include "deepwater/theory/Progression.h"

// Fase 8: editar la progresión como una línea de tiempo. Las operaciones (mover un paso, cambiar su duración, insertar)
// y la geometría de la línea (dónde empieza cada paso, qué hay bajo el ratón, dónde caería un paso arrastrado), sin
// JUCE: la vista solo dibuja y traduce el ratón a estas funciones, y los tests las prueban sin GUI.
namespace tidechart
{

// El paso 'from' pasa a la posición 'to' (la que ocupa al terminar); los demás conservan su orden.
[[nodiscard]] inline deepwater::theory::Progression moveStep (const deepwater::theory::Progression& progression, int from, int to)
{
    const int count = static_cast<int> (progression.steps.size());
    if (from < 0 || from >= count)
        return progression;
    to = std::clamp (to, 0, count - 1);
    deepwater::theory::Progression result;
    const auto moved = progression.steps[static_cast<std::size_t> (from)];
    for (int i = 0, kept = 0; i < count; ++i)
    {
        if (i == from)
            continue;
        if (kept == to)
            result.steps.push_back (moved);
        result.steps.push_back (progression.steps[static_cast<std::size_t> (i)]);
        ++kept;
    }
    if (static_cast<int> (result.steps.size()) < count)
        result.steps.push_back (moved); // 'to' era el último
    return result;
}

// El paso 'index' dura 'beats' tiempos (1..maxStepBeats); los de después se corren.
[[nodiscard]] inline deepwater::theory::Progression resizeStep (const deepwater::theory::Progression& progression, int index, int beats)
{
    auto result = progression;
    if (index >= 0 && index < static_cast<int> (result.steps.size()))
        result.steps[static_cast<std::size_t> (index)].beats = deepwater::theory::Progression::clampBeats (beats);
    return result;
}

// Un paso nuevo en la posición 'index' (al final si se pasa). Si ya hay maxProgressionSteps, nada cambia.
[[nodiscard]] inline deepwater::theory::Progression insertStep (const deepwater::theory::Progression& progression, int index,
                                                               const deepwater::theory::ProgressionStep& step)
{
    const int count = static_cast<int> (progression.steps.size());
    if (count >= deepwater::theory::maxProgressionSteps)
        return progression;
    index = std::clamp (index, 0, count);
    deepwater::theory::Progression result;
    for (int i = 0; i < count; ++i)
    {
        if (i == index)
            result.steps.push_back (step);
        result.steps.push_back (progression.steps[static_cast<std::size_t> (i)]);
    }
    if (index == count)
        result.steps.push_back (step);
    return result;
}

[[nodiscard]] inline deepwater::theory::Progression removeStep (const deepwater::theory::Progression& progression, int index)
{
    deepwater::theory::Progression result;
    for (int i = 0; i < static_cast<int> (progression.steps.size()); ++i)
        if (i != index)
            result.steps.push_back (progression.steps[static_cast<std::size_t> (i)]);
    return result;
}

// La línea de tiempo: x = left + tiempos × pixelsPerBeat. Se muestran al menos 4 compases (16 tiempos), así una
// progresión corta no ocupa todo el ancho con un solo acorde gigante y los anchos se comparan entre progresiones.
struct Timeline
{
    static constexpr int minVisibleBeats = 16;
    static constexpr double edgeGrab = 5.0; // píxeles a cada lado del borde derecho de un paso para cambiar su duración

    double left = 0.0;
    double pixelsPerBeat = 1.0;

    [[nodiscard]] static Timeline fit (const deepwater::theory::Progression& progression, double left, double width) noexcept
    {
        const int beats = std::max (progression.totalBeats(), minVisibleBeats);
        return { left, width / static_cast<double> (beats) };
    }

    [[nodiscard]] double x (double beat) const noexcept { return left + beat * pixelsPerBeat; }
    [[nodiscard]] double beatAt (double xPosition) const noexcept { return (xPosition - left) / pixelsPerBeat; }

    // Dónde empieza y termina el paso 'index', en tiempos.
    [[nodiscard]] static double startBeat (const deepwater::theory::Progression& progression, int index) noexcept
    {
        return progression.stepStart (index) / static_cast<double> (deepwater::theory::sixteenthsPerQuarter);
    }
    [[nodiscard]] static double endBeat (const deepwater::theory::Progression& progression, int index) noexcept
    {
        return startBeat (progression, index + 1);
    }

    // Qué hay en 'xPosition': el paso, y si es su borde derecho (para cambiar la duración). −1 = ninguno. El borde gana
    // en la mitad derecha del paso y en la izquierda del siguiente, y solo si el paso mide al menos 3 veces la zona del
    // borde (si no, se lo comería entero).
    struct Hit
    {
        int step = -1;
        bool edge = false;
    };

    [[nodiscard]] Hit hit (const deepwater::theory::Progression& progression, double xPosition) const noexcept
    {
        const int count = static_cast<int> (progression.steps.size());
        for (int i = 0; i < count; ++i)
        {
            const double right = x (endBeat (progression, i));
            const double width = right - x (startBeat (progression, i));
            if (width >= 3.0 * edgeGrab && std::abs (xPosition - right) <= edgeGrab)
                return { i, true };
        }
        for (int i = 0; i < count; ++i)
            if (xPosition >= x (startBeat (progression, i)) && xPosition < x (endBeat (progression, i)))
                return { i, false };
        return {};
    }

    // Arrastrando el paso 'from' con su centro en 'centreX': la posición donde caería = cuántos de los OTROS pasos
    // tienen su centro a la izquierda.
    [[nodiscard]] int dropIndex (const deepwater::theory::Progression& progression, int from, double centreX) const noexcept
    {
        int index = 0;
        for (int i = 0; i < static_cast<int> (progression.steps.size()); ++i)
            if (i != from && x ((startBeat (progression, i) + endBeat (progression, i)) * 0.5) < centreX)
                ++index;
        return index;
    }

    // Arrastrando el borde derecho del paso 'index' desde 'pressX' hasta 'dragX': su duración nueva, en tiempos
    // enteros (redondeando: el borde salta al tiempo más cercano).
    [[nodiscard]] int resizedBeats (const deepwater::theory::Progression& progression, int index, double pressX, double dragX) const noexcept
    {
        if (index < 0 || index >= static_cast<int> (progression.steps.size()))
            return 1;
        const int original = deepwater::theory::Progression::clampBeats (progression.steps[static_cast<std::size_t> (index)].beats);
        return deepwater::theory::Progression::clampBeats (original + static_cast<int> (std::lround ((dragX - pressX) / pixelsPerBeat)));
    }
};

// Dónde está FL dentro de la vuelta de la progresión, en tiempos (0..totalBeats), o −1 si está vacía. La progresión
// se repite desde el principio de la canción, así que es el resto de la posición.
[[nodiscard]] inline double loopBeat (const deepwater::theory::Progression& progression, double ppq) noexcept
{
    const int total = progression.totalBeats();
    if (total <= 0)
        return -1.0;
    const double beat = std::fmod (ppq, static_cast<double> (total));
    return beat < 0.0 ? beat + total : beat;
}

} // namespace tidechart
