#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "deepwater/dsp/Crossover.h"
#include "deepwater/dsp/Smoothing.h"
#include "dsp/Saturator.h"

namespace riptide::dsp
{

// --- Fase 6: multibanda ----------------------------------------------------------------------------------------------
// Saturar toda la señal junta tiene un problema: la parte más fuerte (casi siempre el bajo) es la que empuja la curva,
// y lo que se satura a la vez se mezcla (intermodulación: un bombo fuerte ensucia la voz que suena encima). Partiendo la
// señal en bandas cada una se satura por separado, con su propio modo y drive: graves limpios y agudos con mordida,
// o un 808 saturado sin tocar los platillos.
//
// Las bandas salen de un divisor Linkwitz-Riley de 24 dB/oct (deepwater/dsp/Crossover.h): sin saturación la suma es
// plana (solo gira la fase alrededor de cada cruce). La señal original del mix y del delta pasa por un divisor
// idéntico, así que las dos siguen en fase.

inline constexpr int maxBands = 4;
inline constexpr int maxCrossovers = maxBands - 1;
using Splitter = deepwater::dsp::MultibandSplitter<maxBands>;

// Rango de las perillas de cruce y sus valores por defecto: con 2 bandas, sub/graves contra el resto (120 Hz); con 4,
// graves | medios graves | medios agudos | agudos.
inline constexpr float crossoverMinHz = 20.0f;
inline constexpr float crossoverMaxHz = 16000.0f;
inline constexpr std::array<float, maxCrossovers> defaultCrossoverHz { 120.0f, 1000.0f, 5000.0f };
// Dos cruces nunca quedan a menos de media octava: más juntos, la banda del medio casi no tendría nada propio (con
// 24 dB/oct cada cruce ocupa más o menos una octava).
inline constexpr float minCrossoverSpacingOctaves = 0.5f;
inline constexpr float maxBandDriveDb = 24.0f;

struct BandSettings
{
    bool ownMode = false;   // false = la banda usa el modo global
    Mode mode = Mode::tape; // el propio (si ownMode)
    float driveDb = 0.0f;   // drive extra de la banda, sobre el Input global
    bool solo = false;
    bool mute = false;
};

// Las frecuencias de los cruces que de verdad se usan: ordenadas y separadas al menos media octava. Si una perilla pasa
// a otra, la empuja (no se intercambian: las bandas no cambian de sitio). Solo cuentan los cruces en uso (bandas − 1).
[[nodiscard]] inline std::array<float, maxCrossovers> spreadCrossovers (const std::array<float, maxCrossovers>& hz, int numBands) noexcept
{
    const int count = std::clamp (numBands, 1, maxBands) - 1;
    const float minOctave = std::log2 (crossoverMinHz);
    const float maxOctave = std::log2 (crossoverMaxHz);
    std::array<float, maxCrossovers> octaves {};
    for (size_t i = 0; i < octaves.size(); ++i)
        octaves[i] = std::log2 (std::clamp (hz[i], crossoverMinHz, crossoverMaxHz));
    // Hacia arriba: cada cruce, al menos media octava sobre el anterior...
    for (int i = 1; i < count; ++i)
        octaves[static_cast<size_t> (i)] = std::max (octaves[static_cast<size_t> (i)], octaves[static_cast<size_t> (i - 1)] + minCrossoverSpacingOctaves);
    // ...y hacia abajo si el último se salió del rango.
    if (count > 0)
        octaves[static_cast<size_t> (count - 1)] = std::min (octaves[static_cast<size_t> (count - 1)], maxOctave);
    for (int i = count - 2; i >= 0; --i)
        octaves[static_cast<size_t> (i)] = std::min (octaves[static_cast<size_t> (i)], octaves[static_cast<size_t> (i + 1)] - minCrossoverSpacingOctaves);
    std::array<float, maxCrossovers> result {};
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = std::exp2 (std::max (octaves[i], minOctave));
    return result;
}

// Las frecuencias de los cruces, suavizadas en octavas (como las del tono), y sus coeficientes: una vez por muestra para
// todos los canales y para los dos divisores (el de la saturada y el de la original). tan() solo mientras una se mueve.
class CrossoverControls
{
public:
    static constexpr double rampSeconds = 0.02;

    void prepare (double newSampleRate, const std::array<float, maxCrossovers>& hz, int numBands) noexcept
    {
        sampleRate = newSampleRate;
        for (auto& ramp : octaves)
            ramp.setTime (rampSeconds, sampleRate);
        setFrequencies (hz, numBands);
        snap();
    }

    // Salta a los objetivos sin suavizar (al preparar, o en silencio al cambiar el número de bandas).
    void snap() noexcept
    {
        for (size_t i = 0; i < octaves.size(); ++i)
        {
            octaves[i].snap (octaves[i].target);
            update (i);
        }
    }

    void setFrequencies (const std::array<float, maxCrossovers>& hz, int numBands) noexcept
    {
        const auto spread = spreadCrossovers (hz, numBands);
        for (size_t i = 0; i < octaves.size(); ++i)
            octaves[i].setTarget (std::log2 (spread[i]));
    }

    void advance() noexcept
    {
        for (size_t i = 0; i < octaves.size(); ++i)
            if (! octaves[i].isSteady())
            {
                octaves[i].next();
                update (i);
            }
    }

    [[nodiscard]] float getFrequency (int index) const noexcept { return std::exp2 (octaves[static_cast<size_t> (index)].value); }

    Splitter::CrossoverCoefficients coefficients {};

private:
    void update (size_t i) noexcept
    {
        coefficients[i] = deepwater::dsp::LinkwitzRiley::forFrequency (std::exp2 (octaves[i].value), sampleRate);
    }

    double sampleRate = 44100.0;
    std::array<deepwater::dsp::LinearRamp, maxCrossovers> octaves;
};

} // namespace riptide::dsp
