#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

#include "Parameters.h"
#include "synth/Modulation.h"

// Lógica de "arrastrar para modular" que no depende de JUCE (la verifican los tests):
//  - qué perilla corresponde a cada destino de la matriz y con qué escala se dibuja su anillo;
//  - qué fuentes son bipolares (oscilan alrededor del valor de la perilla) o unipolares (solo suman);
//  - en qué ruta de la matriz se guarda una conexión nueva.
namespace undertow::gui
{

// Una perilla que es destino de la matriz. 'scale' = cuánto de la perilla (0..1, en su escala normalizada)
// recorre la modulación con amount 100 % y la fuente al máximo.
struct ModulationTarget
{
    synth::ModDestination destination;
    const char* parameterId;
    float scale;
};

// Cutoff: 100 % = 10 octavas, pero la perilla (20 Hz–20 kHz) mide log2 (1000) = 9.97 octavas.
inline constexpr float cutoffKnobScale = synth::cutoffModulationOctaves / 9.9657842847f; // log2 (1000)

// Pitch se dibuja sobre la perilla Semi (−12..+12 = 24 semitonos): 100 % = ±24 st = toda la perilla.
// Volume y Global Pitch no tienen una perilla propia: solo se conectan desde la matriz.
inline constexpr std::array<ModulationTarget, 17> modulationTargets { {
    { synth::ModDestination::oscAPosition, params::oscillators[0].position, 1.0f },
    { synth::ModDestination::oscAPitch, params::oscillators[0].semitones,
      synth::pitchModulationSemitones / 24.0f },
    { synth::ModDestination::oscALevel, params::oscillators[0].level, 1.0f },
    { synth::ModDestination::oscADetune, params::oscillators[0].detune, 1.0f },
    { synth::ModDestination::oscAWarp, params::oscillatorWarps[0].warpAmount, 1.0f },
    { synth::ModDestination::oscAFm, params::oscillatorWarps[0].fmAmount, 1.0f },
    { synth::ModDestination::oscBPosition, params::oscillators[1].position, 1.0f },
    { synth::ModDestination::oscBPitch, params::oscillators[1].semitones,
      synth::pitchModulationSemitones / 24.0f },
    { synth::ModDestination::oscBLevel, params::oscillators[1].level, 1.0f },
    { synth::ModDestination::oscBDetune, params::oscillators[1].detune, 1.0f },
    { synth::ModDestination::oscBWarp, params::oscillatorWarps[1].warpAmount, 1.0f },
    { synth::ModDestination::oscBFm, params::oscillatorWarps[1].fmAmount, 1.0f },
    { synth::ModDestination::subLevel, params::subLevel, 1.0f },
    { synth::ModDestination::noiseLevel, params::noiseLevel, 1.0f },
    { synth::ModDestination::filterCutoff, params::filter1Cutoff, cutoffKnobScale },
    { synth::ModDestination::filterResonance, params::filter1Resonance, 1.0f },
    { synth::ModDestination::filterDrive, params::filter1Drive, 1.0f },
} };

[[nodiscard]] inline std::optional<ModulationTarget> targetForParameter (std::string_view parameterId) noexcept
{
    for (const auto& target : modulationTargets)
        if (parameterId == target.parameterId)
            return target;
    return std::nullopt;
}

[[nodiscard]] inline std::optional<ModulationTarget> targetForDestination (synth::ModDestination destination) noexcept
{
    for (const auto& target : modulationTargets)
        if (target.destination == destination)
            return target;
    return std::nullopt;
}

// LFO y Key van de −1 a 1: mueven el destino hacia los dos lados del valor de la perilla.
// Las envolventes, la velocity y los controladores van de 0 a 1: solo lo empujan en el sentido del amount.
[[nodiscard]] constexpr bool isBipolar (synth::ModSource source) noexcept
{
    return source == synth::ModSource::lfo1 || source == synth::ModSource::lfo2 || source == synth::ModSource::key;
}

// Tramo de la perilla (0..1) que recorre una ruta: lo que se dibuja como anillo de color.
struct ModulationRange
{
    float from = 0.0f;
    float to = 0.0f;
};

[[nodiscard]] inline ModulationRange modulationRange (float knobValue, float amount, float scale, bool bipolar) noexcept
{
    const float reach = amount * scale;
    const float low = bipolar ? knobValue - std::abs (reach) : std::min (knobValue, knobValue + reach);
    const float high = bipolar ? knobValue + std::abs (reach) : std::max (knobValue, knobValue + reach);
    return { std::clamp (low, 0.0f, 1.0f), std::clamp (high, 0.0f, 1.0f) };
}

using ModSlots = std::array<synth::ModSlot, synth::numModSlots>;

// Rutas que llegan a un destino (índices de la matriz), en orden.
[[nodiscard]] inline std::vector<int> routesTo (const ModSlots& slots, synth::ModDestination destination)
{
    std::vector<int> result;
    for (size_t s = 0; s < slots.size(); ++s)
        if (slots[s].isRouted() && slots[s].destination == destination)
            result.push_back (static_cast<int> (s));
    return result;
}

// Dónde guardar la conexión fuente → destino:
//  1. si ya existe, esa misma ruta (soltar dos veces la misma fuente no duplica la modulación);
//  2. si no, la primera ruta vacía (fuente y destino en None);
//  3. si no, la primera a medio configurar en la matriz (solo fuente o solo destino);
//  4. −1 si las 8 rutas están en uso.
[[nodiscard]] inline int findSlotFor (const ModSlots& slots, synth::ModSource source, synth::ModDestination destination) noexcept
{
    for (size_t s = 0; s < slots.size(); ++s)
        if (slots[s].source == source && slots[s].destination == destination)
            return static_cast<int> (s);

    for (size_t s = 0; s < slots.size(); ++s)
        if (slots[s].source == synth::ModSource::none && slots[s].destination == synth::ModDestination::none)
            return static_cast<int> (s);

    for (size_t s = 0; s < slots.size(); ++s)
        if (! slots[s].isRouted())
            return static_cast<int> (s);

    return -1;
}

// Amount con el que nace una conexión nueva: se oye en seguida, pero deja margen para subir o bajar.
inline constexpr float defaultDropAmount = 0.25f;

} // namespace undertow::gui
