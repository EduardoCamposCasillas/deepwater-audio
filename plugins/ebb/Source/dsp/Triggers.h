#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>

#include "deepwater/dsp/TransientDetector.h"

namespace ebb::dsp
{

// Qué dispara la curva. El parámetro "mode" guarda el índice: solo añadir al final.
enum class TriggerMode : std::uint8_t
{
    tempo, // la cuadrícula de la división, desde la posición del host (Fase 2)
    midi,  // cada nota MIDI recibida (Fase 4): sigue el patrón real del kick
    audio  // cada golpe detectado en la entrada sidechain (Fase 5): sigue el kick grabado, con su swing
};

inline constexpr std::array<const char*, 3> triggerModeNames { "Tempo", "MIDI", "Audio" };

[[nodiscard]] inline TriggerMode triggerModeFromIndex (int index) noexcept
{
    return index == 1 ? TriggerMode::midi : index == 2 ? TriggerMode::audio : TriggerMode::tempo;
}

// MIDI y audio funcionan igual una vez llega el disparo: cada golpe reinicia la curva (en tempo la decide la posición).
[[nodiscard]] inline bool isHitMode (TriggerMode mode) noexcept
{
    return mode != TriggerMode::tempo;
}

// Un disparo dentro del bloque actual: en qué muestra cae y con qué velocidad (0–1). El procesador traduce a esto las notas
// MIDI (ya filtradas por nota) o los golpes del sidechain (velocidad 1), y el motor reinicia la curva en esa muestra.
struct Trigger
{
    int offset = 0;
    float velocity = 1.0f;
};

// Filtro de nota: el parámetro "midiNote" guarda 0 = todas las notas, 1–128 = la nota MIDI 0–127.
inline constexpr int midiNoteFilterCount = 129;

[[nodiscard]] inline bool noteFilterAccepts (int filterIndex, int note) noexcept
{
    return filterIndex <= 0 || filterIndex - 1 == note;
}

// Nombre de una nota como lo escribe FL Studio: la nota 60 (do central) es C5 y la 69 (440 Hz) es A5, una octava más
// que la convención "científica" (C4) que usan otros programas. Se añade el número MIDI para no confundirse.
[[nodiscard]] inline std::string flNoteName (int note)
{
    static constexpr std::array<const char*, 12> names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    note = std::clamp (note, 0, 127);
    return std::string (names[static_cast<size_t> (note % 12)]) + std::to_string (note / 12) + " (" + std::to_string (note) + ")";
}

// Lo que encontró el detector en un bloque del sidechain.
struct AudioDetection
{
    int detected = 0;          // golpes detectados (todos, aunque no quepan en 'out')
    int stored = 0;            // los que se guardaron en 'out'
    float peakEnvelope = 0.0f; // envolvente más alta del bloque, para el medidor de la GUI
};

// Recorre el bloque del sidechain (el mayor |x| de sus canales, para que un kick en un solo lado también cuente) y deja
// en 'out' un disparo en cada muestra donde el detector ve empezar un golpe, ordenados. Si no caben, gana el último. Sin
// canales (sidechain desconectado) el detector recibe silencio: la envolvente cae y no hay golpes.
inline AudioDetection detectAudioTriggers (deepwater::dsp::TransientDetector& detector, const float* const* channels,
                                           int numChannels, int numSamples, std::span<Trigger> out) noexcept
{
    AudioDetection result;
    for (int i = 0; i < numSamples; ++i)
    {
        float level = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            level = std::max (level, std::abs (channels[ch][i]));
        if (detector.process (level))
        {
            ++result.detected;
            if (out.empty())
                continue;
            if (static_cast<size_t> (result.stored) < out.size())
                out[static_cast<size_t> (result.stored++)] = { i, 1.0f };
            else
                out.back() = { i, 1.0f };
        }
        result.peakEnvelope = std::max (result.peakEnvelope, detector.getEnvelope());
    }
    return result;
}

} // namespace ebb::dsp
