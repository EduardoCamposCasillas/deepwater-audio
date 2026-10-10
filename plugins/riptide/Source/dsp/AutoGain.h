#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "deepwater/dsp/Decibels.h"
#include "dsp/Saturator.h"

namespace riptide::dsp
{

// --- Fase 5: compensación automática de volumen (auto gain) ----------------------------------------------------------
// Problema: saturar cambia el volumen. Subir Input sube el nivel (hasta que la curva empieza a recortar) y, pasada la
// rodilla, la curva comprime: los picos ya no crecen pero la energía sí. El oído cree que "más fuerte = mejor", así que
// comparar con el bypass sin igualar el volumen engaña. Auto gain hace esa igualación sola.
//
// Hay dos formas de hacerlo:
//  1. Medir y comparar: RMS de la entrada contra RMS de la salida, y corregir la diferencia. Mide de verdad cualquier
//     cosa, pero mira al pasado: si bajas el drive de golpe, la corrección vieja (pensada para mucha compresión) sigue
//     aplicada hasta que las medidas se ponen al día, y la salida da un salto de volumen de varios dB (con clip duro y
//     +24 dB de drive, hasta +20 dB). Y si se mide rápido para evitarlo, la corrección sigue a cada golpe de la batería
//     y deshace la compresión (pumping).
//  2. Predecir con un modelo (la que se usa aquí): la curva de cada modo es una función conocida, así que se puede
//     CALCULAR cuánto volumen pierde una señal de cada nivel al pasar por ella (CurveLoudness, una tabla por modo). El audio
//     solo mide lentamente el nivel de lo que entra a la curva SIN el drive (eso no depende de las perillas) y le suma el
//     drive actual. Mover el drive o el modo cambia la compensación al instante: nunca hay saltos de volumen.
//
//   compensación (dB) = pérdida de la curva para (nivel medido + drive) − drive
//
// Con señal baja la curva es recta (pérdida 0) y la compensación es exactamente −drive: la salida queda igual que la
// entrada. Con mucha saturación la pérdida crece con el nivel y la compensación sube lo que la curva comprimió.
// El modelo usa dos señales de prueba (un seno y ruido) y se queda con la compensación menor. La música real queda entre
// las dos: la salida sale igual o un poco MÁS BAJA que la entrada (medido: hasta 2 dB con ruido rosa y mucho drive),
// nunca más alta. Es el lado seguro: lo que suene mejor con auto gain no suena mejor por estar más fuerte.

// Cuánto volumen RMS pierde la señal al pasar por la curva de cada modo, según su nivel RMS de entrada. Se calcula una
// vez (fuera del hilo de audio) integrando la curva sobre las señales de prueba. Solo se modela la curva: los filtros
// lineales (tono, cabeza de la cinta) son ecualización y se oyen como tal.
class CurveLoudness
{
public:
    static constexpr float minDb = -60.0f; // por debajo todas las curvas son rectas
    static constexpr float maxDb = 48.0f;  // 0 dBFS + 24 dB de drive + 12 dB de emphasis, con margen
    static constexpr float stepDb = 0.5f;
    static constexpr int size = static_cast<int> ((maxDb - minDb) / stepDb) + 1;

    CurveLoudness() noexcept
    {
        for (int m = 0; m < numModes; ++m)
            for (int i = 0; i < size; ++i)
            {
                const auto mode = static_cast<Mode> (m);
                const float levelDb = minDb + stepDb * static_cast<float> (i);
                table[static_cast<size_t> (m)][static_cast<size_t> (i)] =
                    std::min (measureLossDb (mode, levelDb, Signal::sine), measureLossDb (mode, levelDb, Signal::noise));
            }
    }

    // Interpolación lineal en la tabla (fuera de rango: el extremo).
    [[nodiscard]] float lossDb (Mode mode, float inputRmsDb) const noexcept
    {
        const float position = std::clamp ((inputRmsDb - minDb) / stepDb, 0.0f, static_cast<float> (size - 1));
        const int index = std::min (static_cast<int> (position), size - 2);
        const float fraction = position - static_cast<float> (index);
        const auto& row = table[static_cast<size_t> (mode)];
        const float a = row[static_cast<size_t> (index)];
        return a + (row[static_cast<size_t> (index) + 1] - a) * fraction;
    }

private:
    // Las dos señales extremas: un tono puro (el seno: cresta de 3 dB, el bajo, un 808) y el ruido gaussiano (cresta
    // alta, una mezcla completa, platillos). Cada curva pierde distinto con cada una: las que comprimen pierden más con
    // ruido (sus picos llegan más arriba), Rectify pierde más con el seno. La tabla guarda la MENOR de las dos pérdidas:
    // la compensación nunca deja la salida más fuerte que la entrada con ninguna de las dos.
    enum class Signal { sine, noise };

    // RMS de entrada / RMS de salida (en dB) de esa señal a ese nivel. Se quita la media de la salida, como hace el filtro
    // DC del motor (Tube y Rectify crean DC que no se oye).
    static float measureLossDb (Mode mode, float inputRmsDb, Signal signal) noexcept
    {
        const double rms = std::pow (10.0, inputRmsDb / 20.0);
        // Wavefold dobla la onda una vez por cada π de amplitud: con mucho drive hace falta muestrear muy fino.
        const int points = (mode == Mode::fold ? 8 : 1) * (signal == Signal::sine ? 512 : 2048);

        // Cada valor de la señal con su peso (cuánto tiempo pasa la señal en él): el seno se muestrea en un período
        // (pesos iguales); el ruido, su campana de Gauss entre −6σ y +6σ.
        const auto valueAt = [&] (int k, double& weight)
        {
            if (signal == Signal::sine)
            {
                weight = 1.0;
                return std::sqrt (2.0) * rms * std::sin (2.0 * std::numbers::pi * (k + 0.5) / points);
            }
            const double z = -6.0 + 12.0 * (k + 0.5) / points;
            weight = std::exp (-0.5 * z * z);
            return rms * z;
        };

        // Rectify: |x| → (high-pass: le quita la DC) → tanh. La DC de |x| es su media.
        double rectifiedMean = 0.0, totalWeight = 0.0;
        for (int k = 0; k < points; ++k)
        {
            double weight = 0.0;
            const double x = valueAt (k, weight);
            rectifiedMean += std::abs (x) * weight;
            totalWeight += weight;
        }
        rectifiedMean /= totalWeight;

        double sum = 0.0, sumSquares = 0.0;
        for (int k = 0; k < points; ++k)
        {
            double weight = 0.0;
            const auto x = static_cast<float> (valueAt (k, weight));
            double y = 0.0;
            switch (mode)
            {
                case Mode::tape:       y = curves::tape (x); break;
                case Mode::tube:       y = curves::tube (x); break;
                case Mode::transistor: y = curves::transistor (x); break;
                case Mode::hardClip:   y = curves::hardClip (x); break;
                case Mode::rectify:    y = std::tanh (curves::rectify (x) - rectifiedMean); break;
                case Mode::fold:       y = curves::fold (x); break;
            }
            sum += weight * y;
            sumSquares += weight * y * y;
        }
        const double mean = sum / totalWeight;
        const double outputRms = std::sqrt (std::max (1.0e-30, sumSquares / totalWeight - mean * mean));
        return static_cast<float> (20.0 * std::log10 (rms / outputRms));
    }

    std::array<std::array<float, size>, numModes> table {};
};

// La tabla no depende de la frecuencia de muestreo: se calcula una sola vez para todas las instancias. La primera
// llamada debe hacerse fuera del hilo de audio (Engine::prepare).
[[nodiscard]] inline const CurveLoudness& curveLoudness() noexcept
{
    static const CurveLoudness instance;
    return instance;
}

// La parte que corre en el audio: el nivel lento de lo que entra a la curva sin el drive, y la ganancia que iguala.
class AutoGain
{
public:
    // Constante de tiempo del nivel: 1 s. Más corta, la compensación seguiría a cada golpe de batería (pumping); más
    // larga, tardaría en ajustarse al cambiar de sección.
    static constexpr double levelSeconds = 1.0;
    // Puerta: con la entrada por debajo de −60 dBFS (silencio, colas) el nivel no se actualiza. Así, cuando la música
    // vuelve, la compensación sigue donde estaba en vez de empezar desde "señal baja". Su detector es rápido (10 ms): lo
    // que tarda en cerrarse tras un corte brusco es lo que el nivel lento alcanza a caer (unos 0.5 dB).
    static constexpr double gateSeconds = 0.01;
    static constexpr float gateDb = -60.0f;
    static constexpr float limitDb = 36.0f; // por seguridad, nunca más de ±36 dB

    void prepare (double sampleRate) noexcept
    {
        table = &curveLoudness();
        slowCoef = 1.0 - std::exp (-1.0 / (levelSeconds * sampleRate));
        fastCoef = 1.0 - std::exp (-1.0 / (gateSeconds * sampleRate));
        gatePower = std::pow (10.0, gateDb / 10.0);
        limitGain = deepwater::dsp::dbToGain (limitDb);
        reset();
    }

    void reset() noexcept { slowPower = fastPower = 0.0; }

    // power = media por canal de x² de la señal que entra a la curva, sin el drive (una vez por muestra).
    void push (double power) noexcept
    {
        fastPower += (power - fastPower) * fastCoef;
        if (fastPower >= gatePower)
            slowPower += (power - slowPower) * slowCoef; // en double: el coeficiente es diminuto (~2·10⁻⁵)
    }

    // Ganancia lineal que iguala el volumen. drive = ganancia de Input con la que se saturó lo que está saliendo ahora;
    // durante un cruce de modos, la pérdida de los dos modos se mezcla igual que su sonido.
    [[nodiscard]] float gain (float drive, Mode active, Mode incoming, float blend) noexcept
    {
        const float levelDb = 10.0f * static_cast<float> (std::log10 (std::max (1.0e-12, slowPower * drive * drive)));
        float loss = table->lossDb (active, levelDb);
        if (blend > 0.0f)
            loss += (table->lossDb (incoming, levelDb) - loss) * blend;
        current = std::clamp (deepwater::dsp::dbToGain (loss) / drive, 1.0f / limitGain, limitGain);
        return current;
    }

    // La última ganancia calculada (para mostrarla en la GUI).
    [[nodiscard]] float getGain() const noexcept { return current; }

private:
    const CurveLoudness* table = nullptr;
    double slowCoef = 0.0, fastCoef = 0.0, gatePower = 0.0;
    double slowPower = 0.0, fastPower = 0.0;
    float limitGain = 1.0f;
    float current = 1.0f;
};

} // namespace riptide::dsp
