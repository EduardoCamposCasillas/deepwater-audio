#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "deepwater/dsp/FastMath.h"
#include "deepwater/dsp/OnePole.h"
#include "deepwater/dsp/Oversampler.h"
#include "deepwater/dsp/Svf.h"

namespace riptide::dsp
{

using deepwater::dsp::OnePole;

// Modos de saturación, en el orden del selector. El proyecto de FL guarda el ÍNDICE: solo añadir al final.
enum class Mode { tape, tube, transistor, hardClip, rectify, fold };
inline constexpr int numModes = 6;
inline constexpr std::array<const char*, numModes> modeNames { "Tape", "Tube", "Transistor", "Hard Clip", "Rectify", "Fold" };

// --- Curvas de transferencia -----------------------------------------------------------------------------------------
// Cada modo es, ante todo, una curva y = f(x): cómo responde la salida a cada nivel de entrada. Lo que la hace sonar
// distinta es su FORMA:
//  - la rodilla (dónde y qué tan de golpe deja de ser recta) decide desde qué nivel empieza a ensuciar y si lo hace
//    poco a poco (cálido, compresión) o de golpe (duro, agresivo);
//  - la simetría decide qué armónicos aparecen: una curva impar (f(−x) = −f(x)) trata igual los dos semiciclos y solo
//    crea armónicos impares (3º, 5º...: sonido "hueco", de cuadrada); una asimétrica crea también los pares (2º, 4º:
//    octavas, sonido "lleno", cálido).
// Todas (salvo Rectify, que no deja pasar la onda tal cual) tienen pendiente 1 en el origen: una señal muy baja sale
// con el mismo volumen en todos los modos y la comparación entre modos es justa.
namespace curves
{
    // Cinta: sigmoide algebraica x / √(1 + x²). Su rodilla es la más blanda de todas (se acerca a ±1 muy despacio):
    // empieza a redondear antes que tanh, pero nunca "choca". Es compresión más que distorsión.
    [[nodiscard]] inline float tape (float x) noexcept { return x / std::sqrt (1.0f + x * x); }

    // Bulbo: tanh desplazada por un bias. Los picos positivos se aplastan antes (techo +0.77) que los negativos
    // (techo −1.41): la asimetría crea armónicos pares. Restar tanh(bias) deja f(0) = 0 y el factor devuelve la
    // pendiente 1 en el origen (1 / tanh'(bias)). El desplazamiento de DC que genera lo quita el filtro DC del motor.
    inline constexpr float tubeTanhBias = 0.29131261f; // tanh(0.3)
    inline constexpr float tubeSlope = 1.09273261f;    // 1 / (1 − tanh²(0.3))
    [[nodiscard]] inline float tube (float x) noexcept
    {
        return (deepwater::dsp::fastTanh (x + 0.3f) - tubeTanhBias) * tubeSlope;
    }

    // Transistor: x / (1 + x⁴)^(1/4). La misma familia que la cinta con exponente 4 en vez de 2: casi recta hasta
    // cerca de 1 y una rodilla mucho más corta. Limpio con señal moderada y áspero (armónicos impares altos) cuando se
    // pasa. Simétrica: solo impares.
    [[nodiscard]] inline float transistor (float x) noexcept
    {
        const float x2 = x * x;
        return x / std::sqrt (std::sqrt (1.0f + x2 * x2));
    }

    // Clip duro: perfectamente recta hasta ±1 y plana después. La esquina tiene "infinitos" armónicos: es el modo con
    // más aliasing (oversampling alto).
    [[nodiscard]] inline float hardClip (float x) noexcept { return std::clamp (x, -1.0f, 1.0f); }

    // Rectificador de onda completa: |x|, el semiciclo negativo se da vuelta hacia arriba. La onda se repite el doble
    // de veces: la fundamental desaparece y aparece una octava arriba (2º armónico) más sus pares. No basta solo: la
    // onda queda toda por encima de 0 (mucha DC). El Saturator le quita la DC y DESPUÉS la satura (ver abajo).
    [[nodiscard]] inline float rectify (float x) noexcept { return std::abs (x); }

    // Wavefolder senoidal: sin(x). Hasta x = π/2 se parece a una saturación suave; pasado ese punto, en vez de
    // aplanarse, la onda "se dobla" hacia abajo, y con más drive se dobla otra vez. Cada pliegue agrega picos nuevos:
    // el timbre cambia (y se vuelve metálico) al subir el nivel. Simétrica: solo impares.
    [[nodiscard]] inline float fold (float x) noexcept { return std::sin (x); }

    // Fase 8: la curva "estática" de cada modo, la que dibuja la GUI. Es la misma función que suena, sin los filtros
    // que la rodean: Tape sin el énfasis ni la campana de graves (dependen de la frecuencia, no del nivel) y Rectify sin
    // el high-pass que le quita la DC (depende de la señal), o sea tanh(|x|).
    [[nodiscard]] inline float forMode (Mode mode, float x) noexcept
    {
        switch (mode)
        {
            case Mode::tape:       return tape (x);
            case Mode::tube:       return tube (x);
            case Mode::transistor: return transistor (x);
            case Mode::hardClip:   return hardClip (x);
            case Mode::rectify:    return deepwater::dsp::fastTanh (rectify (x));
            case Mode::fold:       return fold (x);
        }
        return x;
    }
} // namespace curves

// Pre-énfasis y de-énfasis de la cinta. Una grabadora real sube los agudos antes de grabar y los baja al reproducir
// (para tapar el siseo de la cinta). Efecto secundario: los agudos llegan más fuertes a la cinta y se saturan antes.
// Con señal baja los dos filtros se anulan (respuesta plana); con drive, los agudos se comprimen más que los graves:
// platillos y transientes se redondean, el sonido se "suaviza".
//
// Énfasis: E(s) = (G·s + w) / (s + w) = 1 + (G − 1)·HP_w(s): sube G (+6 dB) por encima de w, 0 dB en graves.
// De-énfasis: su inversa exacta, (s + w) / (G·s + w) = 1 + (1/G − 1)·HP_{w/G}(s). Las dos usan la MISMA transformada
// bilineal (el g del de-énfasis es g/G, sin volver a "prewarpear"), así que en digital también son inversas exactas.
class TapeEmphasis
{
public:
    static constexpr double cornerHz = 4000.0; // donde el énfasis llega a su máximo; empieza a subir en cornerHz / G
    static constexpr float boost = 2.0f;       // G = +6 dB

    void prepare (double sampleRate) noexcept
    {
        const auto g = static_cast<float> (std::tan (std::numbers::pi * std::min (cornerHz, 0.45 * sampleRate) / sampleRate));
        pre = OnePole::coefficient (g);
        post = OnePole::coefficient (g / boost);
        reset();
    }

    void reset() noexcept
    {
        preFilter.reset();
        postFilter.reset();
    }

    [[nodiscard]] float emphasize (float x) noexcept { return x + (boost - 1.0f) * preFilter.highPass (x, pre); }
    [[nodiscard]] float deEmphasize (float x) noexcept { return x + (1.0f / boost - 1.0f) * postFilter.highPass (x, post); }

private:
    OnePole preFilter, postFilter;
    float pre = 0.0f, post = 0.0f;
};

// --- Saturador de un canal -------------------------------------------------------------------------------------------
// Un modo completo: (filtros previos) → ↑N → curva → ↓N → (filtros posteriores). Los filtros lineales van a la
// frecuencia del host (no crean armónicos: no necesitan oversampling); solo la curva va sobremuestreada.
//
//  - Tape:    énfasis → curva → de-énfasis → head bump (+2 dB en 70 Hz: la resonancia de graves de la cabeza
//             reproductora de una cinta real, que da ese "cuerpo" en el bajo).
//  - Rectify: |x| → high-pass de 20 Hz → tanh, las tres dentro del oversampling (como un pedal de octava). El orden
//             importa: saturar ANTES de rectificar no sirve, porque con drive alto tanh da casi una cuadrada y una
//             cuadrada rectificada es una constante (DC, silencio). Quitando la DC primero, la onda rectificada queda
//             centrada en 0 y tanh la convierte en una cuadrada una octava arriba. El high-pass trabaja a la frecuencia
//             sobremuestreada: su coeficiente depende del factor (se calculan todos en prepare).
//  - El resto: solo la curva.
class Saturator
{
public:
    static constexpr double headBumpHz = 70.0;
    static constexpr double headBumpQ = 0.8;
    static constexpr float headBumpDb = 2.0f;
    static constexpr double rectifyHighPassHz = 20.0;

    // Fuera del hilo de audio (calcula tangentes y diseña los filtros de oversampling).
    void prepare (double sampleRate, int numStages, Mode newMode) noexcept
    {
        oversampler.prepare (sampleRate, numStages);
        emphasis.prepare (sampleRate);
        headBump = deepwater::dsp::SvfStage::Coefficients::forCutoff (headBumpHz, headBumpQ, sampleRate);
        headBumpAmount = (std::pow (10.0f, headBumpDb / 20.0f) - 1.0f) * headBump.k;
        for (int stages = 0; stages <= deepwater::dsp::Oversampler::maxStages; ++stages)
        {
            const double rate = sampleRate * (1 << stages);
            rectifyG[static_cast<size_t> (stages)] = static_cast<float> (std::tan (std::numbers::pi * rectifyHighPassHz / rate));
        }
        setMode (newMode);
    }

    // Cambia de modo y borra toda la historia: el que llama tiene que dejarlo "calentar" antes de oírlo.
    void setMode (Mode newMode) noexcept
    {
        mode = newMode;
        reset();
    }

    void setNumStages (int numStages) noexcept
    {
        oversampler.setNumStages (numStages); // borra su historia
        reset();
    }

    void reset() noexcept
    {
        oversampler.reset();
        emphasis.reset();
        headBumpFilter.reset();
        rectifyCoefficient = OnePole::coefficient (rectifyG[static_cast<size_t> (oversampler.getNumStages())]);
        rectifyHighPass.reset();
    }

    [[nodiscard]] Mode getMode() const noexcept { return mode; }
    [[nodiscard]] int getLatency() const noexcept { return oversampler.getLatency(); }

    // El switch está FUERA del oversampler: cada caso le pasa su curva y el compilador la inlinea en el bucle
    // sobremuestreado (no se decide el modo en cada una de las N muestras).
    [[nodiscard]] float process (float x) noexcept
    {
        switch (mode)
        {
            case Mode::tape:
            {
                const float saturated = oversampler.process (emphasis.emphasize (x), curves::tape);
                const float flat = emphasis.deEmphasize (saturated);
                return flat + headBumpAmount * headBumpFilter.process (flat, headBump).bandPass;
            }
            case Mode::tube:       return oversampler.process (x, curves::tube);
            case Mode::transistor: return oversampler.process (x, curves::transistor);
            case Mode::hardClip:   return oversampler.process (x, curves::hardClip);
            case Mode::rectify:
                return oversampler.process (x, [this] (float v)
                                            { return deepwater::dsp::fastTanh (rectifyHighPass.highPass (curves::rectify (v), rectifyCoefficient)); });
            case Mode::fold:       return oversampler.process (x, curves::fold);
        }
        return x;
    }

private:
    Mode mode = Mode::tape;
    deepwater::dsp::Oversampler oversampler;
    TapeEmphasis emphasis;
    // Campana con el SVF: x + (A − 1)·k·bandPass. k·bandPass vale 1 justo en el centro, así que ahí la ganancia es A.
    deepwater::dsp::SvfStage headBumpFilter;
    deepwater::dsp::SvfStage::Coefficients headBump;
    float headBumpAmount = 0.0f;
    OnePole rectifyHighPass;
    float rectifyCoefficient = 0.0f;
    std::array<float, deepwater::dsp::Oversampler::maxStages + 1> rectifyG {};
};

} // namespace riptide::dsp
