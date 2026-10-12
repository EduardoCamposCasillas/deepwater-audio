#pragma once

#include <algorithm>
#include <cmath>

#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/EnvelopeFollower.h"

namespace deepwater::dsp
{

// Detector de transitorios (golpes) por umbral: dice en qué muestra exacta empieza cada golpe de una señal, p. ej. cada
// kick que llega por un sidechain. Nació en Ebb (Fase 5); el compresor y cualquier "trigger" lo pueden reutilizar.
//
//   nivel ── envolvente (ataque 0, relajación 50 ms) ──┬── ≥ umbral y armado → GOLPE (se desarma)
//                                                      └── < umbral − 6 dB, pasada la retención → se rearma
//
// - Ataque 0: la envolvente salta al primer pico, así el disparo cae en la primera muestra que pasa el umbral.
// - Relajación de 50 ms: suficiente para no caer entre dos ciclos del cuerpo de un kick (a 30 Hz baja menos de 3 dB por
//   medio ciclo), y corta para seguir la cola del golpe.
// - Histéresis (disparador de Schmitt): para volver a disparar, la envolvente tiene que bajar 6 dB por debajo del umbral.
//   Sin ella, la cola de un golpe que ronda el umbral dispararía muchas veces.
// - Retención: tiempo mínimo entre golpes. Durante ese tiempo se ignora todo (tampoco se rearma): un kick con dos ataques
//   (click + cuerpo, o un flam) dispara una sola vez.
// Limitación: si la cola de un golpe no baja de "umbral − 6 dB" antes del siguiente, el siguiente no dispara. Se arregla
// subiendo el umbral (o con un detector que compare una envolvente rápida con una lenta, más difícil de ajustar).
class TransientDetector
{
public:
    static constexpr double releaseSeconds = 0.05;
    static constexpr float hysteresisDb = 6.0f;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        follower.prepare (sampleRate);
        follower.setTimes (0.0, releaseSeconds);
        setHoldSeconds (holdSeconds);
        reset();
    }

    void reset() noexcept
    {
        follower.reset();
        armed = true;
        holdRemaining = 0;
    }

    void setThresholdDb (float db) noexcept
    {
        threshold = static_cast<double> (dbToGain (db));
        rearmLevel = static_cast<double> (dbToGain (db - hysteresisDb));
    }

    void setHoldSeconds (double seconds) noexcept
    {
        holdSeconds = std::max (0.0, seconds);
        holdSamples = static_cast<int> (std::lround (holdSeconds * sampleRate));
    }

    // Una muestra. level = nivel ya rectificado (|x|, o el mayor |x| de los canales). true = empieza un golpe aquí.
    bool process (float level) noexcept
    {
        const double envelope = follower.process (static_cast<double> (level));
        if (holdRemaining > 0)
        {
            --holdRemaining;
            return false;
        }
        if (! armed)
        {
            armed = envelope < rearmLevel;
            return false;
        }
        if (envelope < threshold)
            return false;
        armed = false;
        holdRemaining = holdSamples;
        return true;
    }

    // Envolvente actual (amplitud, 0–1 para una señal a 0 dBFS): para dibujar el nivel contra el umbral.
    [[nodiscard]] float getEnvelope() const noexcept { return static_cast<float> (follower.getValue()); }

private:
    EnvelopeFollower follower;
    double sampleRate = 44100.0;
    double threshold = 0.063, rearmLevel = 0.032; // −24 dB y −30 dB
    double holdSeconds = 0.04;
    int holdSamples = 0;
    int holdRemaining = 0;
    bool armed = true;
};

} // namespace deepwater::dsp
