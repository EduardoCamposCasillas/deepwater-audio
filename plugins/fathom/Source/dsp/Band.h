#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "deepwater/dsp/EqFilter.h"
#include "deepwater/dsp/Smoothing.h"

namespace fathom::dsp
{

// Los ajustes de una banda tal como llegan de los parámetros.
struct BandSettings
{
    bool enabled = true;
    float frequency = 1000.0f; // Hz
    float gainDb = 0.0f;
    float q = 1.0f;

    static constexpr float minFrequency = 10.0f;
    static constexpr float maxFrequency = 30000.0f; // encima de Nyquist el filtro se limita a 0.49·fs
    static constexpr float minGainDb = -30.0f;
    static constexpr float maxGainDb = 30.0f;
    static constexpr float minQ = 0.025f;
    static constexpr float maxQ = 40.0f;
};

// Fase 2: una banda campana (peaking) estéreo con los parámetros suavizados.
//
// Mover un filtro de golpe es un clic: los coeficientes saltan y el estado del filtro (lo que "recuerda" de las
// muestras anteriores) de pronto no corresponde a la nueva respuesta. Por eso no se suavizan los coeficientes sino los
// PARÁMETROS, cada uno en el dominio en que se oye: la frecuencia en octavas (log2 Hz: de 100 a 200 Hz tarda lo mismo
// que de 5 a 10 kHz), la ganancia en dB y el Q en log (de 0.5 a 1 lo mismo que de 4 a 8). Mientras alguna rampa se
// mueve, los coeficientes se recalculan en cada muestra (el SVF TPT es estable aunque cambien en cada muestra). Quietos,
// no se recalcula nada.
//
// Apagar la banda no la quita de golpe: su ganancia efectiva baja en rampa a 0 dB, y en 0 dB la campana es la señal
// intacta bit a bit.
class Band
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double rampSeconds = 0.02;

    void prepare (double newSampleRate, const BandSettings& settings) noexcept
    {
        sampleRate = newSampleRate;
        for (auto* ramp : { &octaves, &gainDb, &logQ })
            ramp->setTime (rampSeconds, sampleRate);
        const auto targets = targetsFor (settings);
        octaves.snap (targets[0]);
        gainDb.snap (targets[1]);
        logQ.snap (targets[2]);
        for (auto& filter : filters)
            filter.reset();
        updateCoefficients();
    }

    void setSettings (const BandSettings& settings) noexcept
    {
        const auto targets = targetsFor (settings);
        octaves.setTarget (targets[0]);
        gainDb.setTarget (targets[1]);
        logQ.setTarget (targets[2]);
    }

    // Una vez por muestra, antes de process(): avanza las rampas.
    void advance() noexcept
    {
        if (octaves.isSteady() && gainDb.isSteady() && logQ.isSteady())
            return;
        octaves.next();
        gainDb.next();
        logQ.next();
        updateCoefficients();
    }

    [[nodiscard]] float process (int channel, float x) noexcept
    {
        return filters[static_cast<size_t> (channel)].process (x, coefficients);
    }

    // Los coeficientes de una banda con estos ajustes, ya quieta. La GUI los usa para dibujar la curva: son los MISMOS
    // números con los que termina sonando el audio.
    static deepwater::dsp::EqCoefficients coefficientsFor (const BandSettings& settings, double sampleRate) noexcept
    {
        const auto targets = targetsFor (settings);
        return makeCoefficients (targets[0], targets[1], targets[2], sampleRate);
    }

    [[nodiscard]] const deepwater::dsp::EqCoefficients& getCoefficients() const noexcept { return coefficients; }

private:
    // {octavas, dB, log2 Q}. Apagada = 0 dB (la frecuencia y el Q se quedan donde están).
    static std::array<float, 3> targetsFor (const BandSettings& s) noexcept
    {
        const float hz = std::clamp (s.frequency, BandSettings::minFrequency, BandSettings::maxFrequency);
        const float db = s.enabled ? std::clamp (s.gainDb, BandSettings::minGainDb, BandSettings::maxGainDb) : 0.0f;
        const float q = std::clamp (s.q, BandSettings::minQ, BandSettings::maxQ);
        return { std::log2 (hz), db, std::log2 (q) };
    }

    static deepwater::dsp::EqCoefficients makeCoefficients (float octave, float db, float logQ, double sampleRate) noexcept
    {
        return deepwater::dsp::EqCoefficients::bell (std::exp2 (static_cast<double> (octave)), std::exp2 (static_cast<double> (logQ)),
                                                     static_cast<double> (db), sampleRate);
    }

    void updateCoefficients() noexcept { coefficients = makeCoefficients (octaves.value, gainDb.value, logQ.value, sampleRate); }

    double sampleRate = 44100.0;
    deepwater::dsp::LinearRamp octaves, gainDb, logQ;
    deepwater::dsp::EqCoefficients coefficients;
    std::array<deepwater::dsp::EqFilter, maxChannels> filters;
};

} // namespace fathom::dsp
