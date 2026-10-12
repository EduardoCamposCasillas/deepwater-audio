#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>

namespace deepwater::dsp
{

// Curva por puntos con tensión (nació en Ebb, Fase 3): una función de x ∈ [0, 1] a y ∈ [0, 1] dibujada a mano, como
// las de un editor de envolventes o de LFO. Entre dos puntos seguidos hay un segmento, y cada segmento se dobla según
// la "tensión" del punto donde empieza:
//
//   tensión 0      → línea recta
//   tensión > 0    → arranca lenta y termina rápida (se queda cerca del punto de partida y luego corre)
//   tensión < 0    → arranca rápida y termina lenta
//
// La forma doblada es una exponencial normalizada, (e^(k·t) − 1) / (e^k − 1) con k = tensión · maxBend: vale 0 en t = 0
// y 1 en t = 1 para cualquier k (así el segmento siempre une sus dos puntos) y, con k → 0, tiende a la recta t. Es la
// misma curva que hace la carga de un condensador, por eso suena "natural" en volúmenes.
//
// Tamaño fijo (sin memoria dinámica) y trivialmente copiable: se puede evaluar en el hilo de audio y mandarse entre
// hilos por un TripleBuffer. El primer punto está siempre en x = 0 y el último en x = 1 (solo cambia su y), así la
// curva está definida en todo el rango.
struct CurvePoint
{
    float x = 0.0f;
    float y = 0.0f;
    float tension = 0.0f; // del segmento que SALE de este punto (el del último punto no se usa)

    friend bool operator== (const CurvePoint&, const CurvePoint&) = default;
};

class BreakpointCurve
{
public:
    static constexpr int maxPoints = 32;
    static constexpr float maxBend = 8.0f; // k con tensión ±1: a mitad del segmento va al 2 % (o al 98 %)

    // Recta de (0, 0) a (1, 1).
    BreakpointCurve() noexcept
    {
        points[0] = { 0.0f, 0.0f, 0.0f };
        points[1] = { 1.0f, 1.0f, 0.0f };
    }

    // Desde una lista de puntos en cualquier orden y con cualquier valor (se sanea). Si hay más de maxPoints, se
    // quedan los primeros.
    BreakpointCurve (const CurvePoint* first, int numPoints) noexcept
    {
        count = std::clamp (numPoints, 0, maxPoints);
        std::copy (first, first + count, points.begin());
        sanitise();
    }

    BreakpointCurve (std::initializer_list<CurvePoint> list) noexcept
        : BreakpointCurve (list.begin(), static_cast<int> (list.size()))
    {
    }

    [[nodiscard]] int size() const noexcept { return count; }
    [[nodiscard]] const CurvePoint& operator[] (int index) const noexcept { return points[static_cast<size_t> (index)]; }

    // El segmento doblado: t ∈ [0, 1] → [0, 1].
    [[nodiscard]] static float bend (float t, float tension) noexcept
    {
        const float k = std::clamp (tension, -1.0f, 1.0f) * maxBend;
        if (std::abs (k) < 1.0e-4f)
            return t;
        return std::expm1 (k * t) / std::expm1 (k);
    }

    // Valor de la curva en x (fuera de [0, 1], el del extremo). Búsqueda binaria del segmento: O(log n) por muestra.
    [[nodiscard]] float valueAt (float x) const noexcept
    {
        if (x <= points[0].x)
            return points[0].y;
        const int last = count - 1;
        if (x >= points[static_cast<size_t> (last)].x)
            return points[static_cast<size_t> (last)].y;

        // Primer punto con x mayor que la pedida: el segmento empieza en el anterior. Con dos puntos en la misma x
        // (un escalón vertical) gana el de la derecha.
        const auto* end = std::upper_bound (points.data() + 1, points.data() + count, x,
                                            [] (float value, const CurvePoint& p) { return value < p.x; });
        const auto& a = *(end - 1);
        const auto& b = *end;
        const float width = b.x - a.x;
        const float t = width > 0.0f ? (x - a.x) / width : 1.0f;
        return a.y + (b.y - a.y) * bend (t, a.tension);
    }

    // --- Edición (fuera del audio: GUI y carga de estado) ---------------------------------------------------------

    // Inserta un punto en su lugar por x (empujado dentro de (0, 1)). Devuelve su índice, o −1 si ya no caben más.
    // El segmento partido conserva su tensión en las dos mitades (no es idéntico, pero se parece).
    int insert (float x, float y) noexcept
    {
        if (count >= maxPoints)
            return -1;
        x = std::clamp (x, 0.0f, 1.0f);
        int index = 1;
        while (index < count - 1 && points[static_cast<size_t> (index)].x <= x)
            ++index;
        for (int i = count; i > index; --i)
            points[static_cast<size_t> (i)] = points[static_cast<size_t> (i - 1)];
        points[static_cast<size_t> (index)] = { x, std::clamp (y, 0.0f, 1.0f), points[static_cast<size_t> (index - 1)].tension };
        ++count;
        return index;
    }

    // Quita un punto interior (los extremos no se pueden quitar). Devuelve false si no se quitó.
    bool remove (int index) noexcept
    {
        if (index <= 0 || index >= count - 1)
            return false;
        for (int i = index; i < count - 1; ++i)
            points[static_cast<size_t> (i)] = points[static_cast<size_t> (i + 1)];
        --count;
        return true;
    }

    // Mueve un punto sin dejar que adelante o pase a sus vecinos (el orden por x no cambia nunca). Los extremos solo
    // se mueven en y.
    void move (int index, float x, float y) noexcept
    {
        if (index < 0 || index >= count)
            return;
        auto& point = points[static_cast<size_t> (index)];
        point.y = std::clamp (y, 0.0f, 1.0f);
        if (index > 0 && index < count - 1)
            point.x = std::clamp (x, points[static_cast<size_t> (index - 1)].x, points[static_cast<size_t> (index + 1)].x);
    }

    void setTension (int index, float tension) noexcept
    {
        if (index >= 0 && index < count - 1)
            points[static_cast<size_t> (index)].tension = std::clamp (tension, -1.0f, 1.0f);
    }

    // Deja la curva válida venga de donde venga (un estado guardado a mano, un preset dañado): al menos dos puntos,
    // valores dentro de rango, ordenados por x y con los extremos en 0 y 1. Los NaN se vuelven 0.
    void sanitise() noexcept
    {
        if (count < 2)
        {
            *this = BreakpointCurve();
            return;
        }
        for (int i = 0; i < count; ++i)
        {
            auto& p = points[static_cast<size_t> (i)];
            p.x = std::isfinite (p.x) ? std::clamp (p.x, 0.0f, 1.0f) : 0.0f;
            p.y = std::isfinite (p.y) ? std::clamp (p.y, 0.0f, 1.0f) : 0.0f;
            p.tension = std::isfinite (p.tension) ? std::clamp (p.tension, -1.0f, 1.0f) : 0.0f;
        }
        std::stable_sort (points.begin(), points.begin() + count, [] (const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
        points[0].x = 0.0f;
        points[static_cast<size_t> (count - 1)].x = 1.0f;
        points[static_cast<size_t> (count - 1)].tension = 0.0f;
    }

    friend bool operator== (const BreakpointCurve& a, const BreakpointCurve& b) noexcept
    {
        return a.count == b.count && std::equal (a.points.begin(), a.points.begin() + a.count, b.points.begin());
    }

private:
    std::array<CurvePoint, maxPoints> points {};
    int count = 2;
};

} // namespace deepwater::dsp
