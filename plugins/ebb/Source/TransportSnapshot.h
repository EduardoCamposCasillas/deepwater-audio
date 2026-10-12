#pragma once

#include <cstdint>

#include "dsp/HostClock.h"

// Lo que el audio publica para la GUI en cada bloque: el estado del reloj musical. Va entero en un TripleBuffer para
// que la GUI nunca mezcle datos de dos bloques (p. ej. el compás de uno con la posición del siguiente).
struct TransportSnapshot
{
    double ppq = 0.0;
    double bpm = ebb::dsp::HostClock::defaultBpm;
    ebb::dsp::TimeSignature timeSignature;
    double lastBarStart = 0.0;
    bool hasLastBarStart = false;
    ebb::dsp::ClockSource source = ebb::dsp::ClockSource::freeRunning;
    bool looping = false;
    std::uint32_t jumps = 0;
    double sampleRate = 0.0;
    int blockSize = 0;
};
