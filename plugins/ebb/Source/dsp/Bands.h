#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "deepwater/dsp/Crossover.h"
#include "deepwater/dsp/Smoothing.h"

namespace ebb::dsp
{

// --- Fase 6: ducking por bandas ---------------------------------------------------------------------------------------
// El kick y el bajo chocan en los graves (40–120 Hz), pero el bajo también tiene armónicos arriba (el "gruñido" de un
// 808, el ataque de un bajo de dedo) que no estorban al kick. Si se atenúa la señal entera, esos armónicos también
// bajan y se oye el bombeo. Partiendo la señal en dos bandas con un crossover Linkwitz-Riley de 24 dB/oct
// (deepwater/dsp/Crossover.h) y aplicando la ganancia solo a una, el kick gana su hueco en los graves y lo de arriba
// sigue sonando igual.
//
// Sin ducking (ganancia 1) la suma de las bandas es plana: LR4 da graves + agudos = allpass, que solo gira la fase
// alrededor del cruce. En "Todo" no se parte nada: la señal pasa tal cual (bit a bit sin ducking, como hasta la Fase 5).

// Qué parte del espectro baja con el golpe. El parámetro "band" guarda el índice: solo añadir al final.
enum class DuckBand : std::uint8_t
{
    full, // toda la señal (Fases 2–5)
    low,  // solo por debajo del cruce: el uso normal (bajo contra kick)
    high  // solo por encima: p. ej. que los platillos o la cola de una reverb respiren sin tocar el sub
};

inline constexpr std::array<const char*, 3> duckBandNames { "Todo", "Graves", "Agudos" };

[[nodiscard]] inline DuckBand duckBandFromIndex (int index) noexcept
{
    return index == 1 ? DuckBand::low : index == 2 ? DuckBand::high : DuckBand::full;
}

// Rango de la perilla de cruce. 120 Hz: el sub y el cuerpo del kick quedan abajo, la mayoría de los armónicos del bajo
// arriba.
inline constexpr float crossoverMinHz = 20.0f;
inline constexpr float crossoverMaxHz = 5000.0f;
inline constexpr float defaultCrossoverHz = 120.0f;

// Respuesta en magnitud de cada banda de un LR4 a una frecuencia (para la GUI): |LP| = 1 / (1 + r⁴), |HP| = r⁴ / (1 + r⁴),
// con r = f / fc. Las dos suman 1: en fase, la suma es plana.
[[nodiscard]] inline float lowBandMagnitude (float hz, float crossoverHz) noexcept
{
    const float r = hz / std::max (crossoverHz, 1.0f);
    const float r4 = r * r * r * r;
    return 1.0f / (1.0f + r4);
}

// Aplica la ganancia del ducking a una banda. Todos los canales comparten la frecuencia de cruce (suavizada en octavas:
// así un giro de perilla no "chirría") y la banda elegida. Cambiar de banda sonando haría saltar la señal (de x a su
// versión con la fase girada, o de graves atenuados a agudos atenuados): se hace un fundido cruzado de 20 ms entre la
// salida vieja y la nueva. Si llega otro cambio durante el fundido, espera a que termine.
class BandDucker
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double crossoverRampSeconds = 0.02;
    static constexpr double bandFadeSeconds = 0.02;

    void prepare (double newSampleRate, DuckBand initialBand, float crossoverHz) noexcept
    {
        sampleRate = newSampleRate;
        for (auto& split : splits)
            split.reset();
        octave.setTime (crossoverRampSeconds, sampleRate);
        octave.snap (toOctave (crossoverHz));
        updateCoefficients();
        band = previousBand = pendingBand = initialBand;
        fadeSamples = std::max (1, static_cast<int> (std::lround (bandFadeSeconds * sampleRate)));
        fadeRemaining = 0;
        newWeight = 1.0f;
    }

    void setBand (DuckBand newBand) noexcept { pendingBand = newBand; }
    void setCrossover (float hz) noexcept { octave.setTarget (toOctave (hz)); }

    [[nodiscard]] float getCrossoverHz() const noexcept { return std::exp2 (octave.value); }

    // Una vez por muestra, antes de process() de cada canal.
    void advance() noexcept
    {
        if (! octave.isSteady())
        {
            octave.next();
            updateCoefficients();
        }
        if (fadeRemaining == 0 && pendingBand != band)
        {
            previousBand = band;
            band = pendingBand;
            fadeRemaining = fadeSamples;
        }
        if (fadeRemaining > 0)
        {
            --fadeRemaining;
            newWeight = 1.0f - static_cast<float> (fadeRemaining) / static_cast<float> (fadeSamples);
        }
    }

    // La muestra 'dry' de un canal con la ganancia aplicada a la banda elegida. El divisor corre siempre (también en
    // "Todo"), para que al pasar a una banda sus filtros ya estén en régimen y no arranquen con un transitorio.
    [[nodiscard]] float process (int channel, float dry, float gain) noexcept
    {
        const auto bands = splits[static_cast<size_t> (channel)].process (dry, coefficients);
        const float now = route (band, dry, bands, gain);
        if (newWeight >= 1.0f)
            return now;
        const float before = route (previousBand, dry, bands, gain);
        return before + (now - before) * newWeight;
    }

private:
    [[nodiscard]] static float toOctave (float hz) noexcept
    {
        return std::log2 (std::clamp (hz, crossoverMinHz, crossoverMaxHz));
    }

    [[nodiscard]] static float route (DuckBand which, float dry, deepwater::dsp::LinkwitzRileySplit::Bands bands, float gain) noexcept
    {
        switch (which)
        {
            case DuckBand::low:  return bands.low * gain + bands.high;
            case DuckBand::high: return bands.low + bands.high * gain;
            case DuckBand::full: break;
        }
        return dry * gain;
    }

    void updateCoefficients() noexcept
    {
        coefficients = deepwater::dsp::LinkwitzRiley::forFrequency (std::exp2 (octave.value), sampleRate);
    }

    double sampleRate = 44100.0;
    std::array<deepwater::dsp::LinkwitzRileySplit, maxChannels> splits;
    deepwater::dsp::LinearRamp octave; // frecuencia de cruce en octavas (log2 Hz)
    deepwater::dsp::LinkwitzRiley::Coefficients coefficients;

    DuckBand band = DuckBand::full, previousBand = DuckBand::full, pendingBand = DuckBand::full;
    int fadeSamples = 1;
    int fadeRemaining = 0;
    float newWeight = 1.0f; // peso de la banda actual en el fundido (1 = sin fundido)
};

} // namespace ebb::dsp
