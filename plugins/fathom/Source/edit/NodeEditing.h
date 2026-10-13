#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "dsp/Band.h"

// Las cuentas de la edición sobre el visor, sin JUCE (se prueban en tests/EngineTests.cpp): qué le pasa a una banda al
// arrastrar su nodo o girar la rueda, qué banda se usa al crear una y a qué nota se "pega" una frecuencia.
namespace fathom::edit
{

// Cuántos dB cambia la GANANCIA por cada dB que sube el nodo. El nodo va sobre la curva de la banda en su frecuencia: en
// la campana eso es la ganancia entera, pero en un shelf es el punto medio (la mitad). Con el factor 2, el nodo de un
// shelf sigue al mouse igual que el de una campana.
constexpr float gainPerNodeDb (dsp::Shape shape) noexcept
{
    return shape == dsp::Shape::lowShelf || shape == dsp::Shape::highShelf ? 2.0f : 1.0f;
}

// La banda después de arrastrar su nodo 'octaves' octavas a la derecha y 'nodeDb' dB hacia arriba desde 'start' (el
// estado al empezar a arrastrar). Arrastrar en octavas y no en Hz: varias bandas elegidas se mueven juntas sin
// deformarse (una a 100 Hz y otra a 1 kHz siguen a una década de distancia). Las formas sin ganancia solo se mueven en
// horizontal. La frecuencia se limita a lo que se ve en el visor.
inline dsp::BandSettings dragged (const dsp::BandSettings& start, double octaves, double nodeDb, float minHz, float maxHz) noexcept
{
    auto result = start;
    const double hz = static_cast<double> (start.frequency) * std::exp2 (octaves);
    result.frequency = static_cast<float> (std::clamp (hz, static_cast<double> (minHz), static_cast<double> (maxHz)));
    if (dsp::usesGain (start.shape))
        result.gainDb = std::clamp (start.gainDb + static_cast<float> (nodeDb) * gainPerNodeDb (start.shape),
                                    dsp::BandSettings::minGainDb, dsp::BandSettings::maxGainDb);
    return result;
}

// El Q multiplicado por 'factor' (la rueda: cada paso lo multiplica o divide por lo mismo, como la perilla log).
inline float scaledQ (float q, double factor) noexcept
{
    return static_cast<float> (std::clamp (static_cast<double> (q) * factor, static_cast<double> (dsp::BandSettings::minQ),
                                           static_cast<double> (dsp::BandSettings::maxQ)));
}

// La forma de una banda creada con doble clic: en los extremos del visor, un corte (lo que más se pone ahí: el low cut
// bajo 25 Hz, el high cut sobre 16 kHz); en el resto, una campana.
inline dsp::Shape shapeForNewBand (float hz) noexcept
{
    if (hz < 25.0f)
        return dsp::Shape::lowCut;
    if (hz > 16000.0f)
        return dsp::Shape::highCut;
    return dsp::Shape::bell;
}

// La primera banda libre (ni encendida ni tocada) o −1 si las 24 están en uso.
template <size_t numBands>
int freeBand (const std::array<bool, numBands>& inUse) noexcept
{
    for (size_t b = 0; b < numBands; ++b)
        if (! inUse[b])
            return static_cast<int> (b);
    return -1;
}

// La frecuencia exacta de la nota más cercana (temperamento igual, A5 de FL = nota MIDI 69 = 440 Hz).
inline float nearestNoteHz (float hz) noexcept
{
    const double note = std::round (69.0 + 12.0 * std::log2 (static_cast<double> (hz) / 440.0));
    return static_cast<float> (440.0 * std::exp2 ((note - 69.0) / 12.0));
}

// La nota MIDI (con decimales) de una frecuencia, y al revés: el teclado del eje reparte las teclas con estas dos.
inline double hzToNote (double hz) noexcept { return 69.0 + 12.0 * std::log2 (hz / 440.0); }
inline double noteToHz (double note) noexcept { return 440.0 * std::exp2 ((note - 69.0) / 12.0); }

// Si una nota es una tecla negra (C#, D#, F#, G#, A#).
constexpr bool isBlackKey (int note) noexcept
{
    const int pitchClass = ((note % 12) + 12) % 12;
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

} // namespace fathom::edit
