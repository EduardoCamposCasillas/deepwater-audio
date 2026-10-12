#pragma once

#include <array>

#include "deepwater/dsp/BreakpointCurve.h"

namespace ebb::dsp
{

// Fase 3: las formas de ducking de fábrica. Cada una es una curva por puntos (deepwater::dsp::BreakpointCurve) que
// el editor puede cargar y luego modificar. Ejes de la curva:
//   x: 0 = el golpe, 1 = el final de la DURACIÓN (después, la curva se queda en el valor del último punto)
//   y: nivel de volumen relativo, 1 = volumen completo, 0 = la PROFUNDIDAD entera
// Así la misma forma sirve con cualquier profundidad y duración: ganancia = 1 − profundidad · (1 − y).
//
// La curva elegida se guarda con el proyecto como puntos (no como un índice), así que esta lista se puede cambiar sin
// romper nada; solo la usa el menú FORMA.
using deepwater::dsp::BreakpointCurve;

struct CurveShape
{
    const char* name;
    BreakpointCurve curve;
};

// "Clásica" es la curva fija de la Fase 2, la smoothstep x²·(3 − 2x), aproximada con 5 puntos y tensiones ajustadas
// numéricamente (error máximo 0.006: unos 0.03 dB con profundidad 70 %). Es la forma por defecto, así que los
// proyectos guardados en la Fase 2 (sin curva propia) suenan prácticamente igual.
inline const std::array<CurveShape, 8> curveShapes { {
    { "Cl\xc3\xa1sica",
      { { 0.0f, 0.0f, 0.2355f }, { 0.25f, 0.15625f, 0.034f }, { 0.5f, 0.5f, -0.034f }, { 0.75f, 0.84375f, -0.2355f }, { 1.0f, 1.0f } } },
    // Sube a ritmo constante: la referencia para comparar las demás.
    { "Lineal", { { 0.0f, 0.0f }, { 1.0f, 1.0f } } },
    // Vuelve enseguida y se acerca despacio al final: ducking que se nota poco (mezcla).
    { "R\xc3\xa1pida", { { 0.0f, 0.0f, -0.6f }, { 1.0f, 1.0f } } },
    // Se queda abajo casi todo el tiempo y sube al final: bombeo muy marcado ("sidechain de house").
    { "Lenta", { { 0.0f, 0.0f, 0.6f }, { 1.0f, 1.0f } } },
    // Abajo y quieto durante un tercio (deja pasar el kick entero) y luego una S hasta arriba: para kicks largos.
    { "Retenci\xc3\xb3n", { { 0.0f, 0.0f }, { 0.35f, 0.0f, 0.45f }, { 0.675f, 0.5f, -0.45f }, { 1.0f, 1.0f } } },
    // Empieza arriba y baja en el primer 10 %: la caída no es instantánea, así no hace clic ni con suavizado 0.
    { "Entrada suave", { { 0.0f, 1.0f }, { 0.1f, 0.0f, 0.35f }, { 1.0f, 1.0f } } },
    // Un segundo golpe más débil a la mitad, como un eco del primero: ritmo extra con un solo disparo.
    { "Eco", { { 0.0f, 0.0f, 0.35f }, { 0.42f, 1.0f }, { 0.46f, 1.0f }, { 0.5f, 0.4f, 0.35f }, { 1.0f, 1.0f } } },
    // Silencio y de vuelta de golpe a la mitad: corta como una puerta (efecto "stutter"; mejor con algo de suavizado).
    { "Puerta", { { 0.0f, 0.0f }, { 0.5f, 0.0f }, { 0.52f, 1.0f }, { 1.0f, 1.0f } } },
} };

inline constexpr int defaultCurveShape = 0;

[[nodiscard]] inline const BreakpointCurve& defaultCurve() noexcept
{
    return curveShapes[defaultCurveShape].curve;
}

// Índice de la forma de fábrica idéntica a 'curve', o −1 si es una curva propia ("Personalizada").
[[nodiscard]] inline int findCurveShape (const BreakpointCurve& curve) noexcept
{
    for (size_t i = 0; i < curveShapes.size(); ++i)
        if (curveShapes[i].curve == curve)
            return static_cast<int> (i);
    return -1;
}

} // namespace ebb::dsp
