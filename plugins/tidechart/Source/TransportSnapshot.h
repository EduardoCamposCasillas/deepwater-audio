#pragma once

#include <cstdint>

#include "deepwater/dsp/HostClock.h"

// Lo que el audio publica para la GUI en cada bloque: el estado del reloj musical y de las notas que suenan. Va entero
// en un TripleBuffer para que la GUI nunca mezcle datos de dos bloques (p. ej. el compás de uno con la posición del
// siguiente).
struct TransportSnapshot
{
    double ppq = 0.0;
    double bpm = deepwater::dsp::HostClock::defaultBpm;
    deepwater::dsp::TimeSignature timeSignature;
    double lastBarStart = 0.0;
    bool hasLastBarStart = false;
    deepwater::dsp::ClockSource source = deepwater::dsp::ClockSource::freeRunning;
    bool looping = false;
    std::uint32_t jumps = 0;
    double sampleRate = 0.0;
    int blockSize = 0;

    // Notas que suenan en el receptor por lo que salió de Tidechart: un bit por canal para cada nota (bit 0 = canal 1).
    std::uint16_t soundingChannels[128] {};
    int soundingCount = 0;
    std::uint32_t releases = 0; // cuántas veces se apagó todo (pánico, reinicio del audio, cargar un proyecto)
    bool bypassed = false;
};
