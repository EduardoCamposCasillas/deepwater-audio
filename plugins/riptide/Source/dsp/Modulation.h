#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/EnvelopeFollower.h"
#include "deepwater/dsp/Lfo.h"
#include "deepwater/dsp/Smoothing.h"
#include "dsp/Bands.h"
#include "dsp/Tone.h"

namespace riptide::dsp
{

// --- Fase 7: dinámica y modulación -----------------------------------------------------------------------------------
// Un saturador estático satura igual el golpe del bombo que la cola de la reverb. Dos fuentes de modulación mueven sus
// perillas solas, cada una hacia un destino:
//  - ENVOLVENTE (envelope follower): sigue el volumen de la entrada. Con amount positivo, lo fuerte satura más (los
//    golpes "rompen" y lo suave queda limpio); con negativo, lo fuerte satura MENOS (los transitorios pasan limpios y la
//    saturación engorda las colas y lo que suena bajito).
//  - LFO: mueve el destino en ciclos, libre (Hz) o al tempo de FL (1/4, 1/8 T...) y en fase con el compás.
//
// Convención de escala (la misma de Undertow): amount 100 % con la fuente al máximo mueve el destino TODO el recorrido
// de su perilla; 50 % = media perilla, en cualquier destino. La envolvente va de 0 a 1 (solo sube o solo baja el
// destino); el LFO, de −1 a +1 (lo mueve hacia los dos lados). Las dos fuentes se suman si van al mismo destino, y el
// resultado se limita al rango de la perilla.

// El orden se guarda en el proyecto (índice del parámetro): solo añadir al final.
enum class ModTarget { drive, mix, emphasis, emphasisFreq, tilt, band1Drive, band2Drive, band3Drive, band4Drive };
inline constexpr int numModTargets = 9;
inline constexpr std::array<const char*, numModTargets> modTargetNames { "Drive", "Mix", "Emphasis", "Emph Freq", "Tilt",
                                                                         "Band 1 Drive", "Band 2 Drive", "Band 3 Drive",
                                                                         "Band 4 Drive" };

inline constexpr float maxDriveDb = 24.0f; // rango de la perilla Input (el drive global)

// Recorrido completo de la perilla de cada destino, en sus unidades: dB, mezcla 0..1 u octavas.
inline const std::array<float, numModTargets> modTargetSpans {
    2.0f * maxDriveDb,                                // Drive: −24..+24 dB
    1.0f,                                             // Mix: 0..100 %
    2.0f * maxEmphasisDb,                             // Emphasis: −12..+12 dB
    std::log2 (emphasisMaxHz / emphasisMinHz),        // Emph Freq: 50 Hz..10 kHz = 7.6 octavas
    2.0f * maxTiltDb,                                 // Tilt: −6..+6 dB
    2.0f * maxBandDriveDb, 2.0f * maxBandDriveDb, 2.0f * maxBandDriveDb, 2.0f * maxBandDriveDb,
};

[[nodiscard]] inline ModTarget bandDriveTarget (size_t band) noexcept
{
    return static_cast<ModTarget> (static_cast<int> (ModTarget::band1Drive) + static_cast<int> (band));
}

// Rangos de las perillas de las fuentes (los usa también el procesador para crear los parámetros).
inline constexpr float envelopeMinAttackMs = 0.1f, envelopeMaxAttackMs = 100.0f;
inline constexpr float envelopeMinReleaseMs = 5.0f, envelopeMaxReleaseMs = 2000.0f;
inline constexpr float maxSensitivityDb = 24.0f;
inline constexpr float lfoMinHz = 0.02f, lfoMaxHz = 20.0f;

struct EnvelopeSettings
{
    float attackMs = 5.0f;
    float releaseMs = 150.0f;
    float sensitivityDb = 0.0f; // ganancia del detector: más sensibilidad = llega al máximo con menos nivel
    ModTarget target = ModTarget::drive;
    float amount = 0.0f;        // −1..+1
};

struct LfoSettings
{
    deepwater::dsp::LfoShape shape = deepwater::dsp::LfoShape::sine;
    bool sync = true;
    float rateHz = 1.0f;        // sin sync
    int division = deepwater::dsp::defaultLfoDivision; // con sync (índice en lfoDivisions)
    ModTarget target = ModTarget::drive;
    float amount = 0.0f;        // −1..+1
};

struct ModulationSettings
{
    EnvelopeSettings envelope {};
    LfoSettings lfo {};
};

// Tempo y posición de la canción que informa el host. Sin host (Standalone) se usan 120 BPM.
struct Transport
{
    double bpm = 120.0;
    double ppqPosition = 0.0; // posición en negras desde el inicio de la canción
    bool isPlaying = false;
    bool hasPosition = false;
};

// Una fuente conectada a un destino con una cantidad. La cantidad se suaviza (20 ms) y cambiar de destino no salta: la
// ruta baja a 0 en 20 ms, cambia de destino y vuelve a subir (si no, el destino viejo perdería su modulación de golpe).
// Mientras baja conserva la cantidad VIEJA: si destino y cantidad cambian a la vez (un preset, la automatización), la
// cantidad nueva no puede llegar al destino viejo. Llega al nuevo cuando la ruta está en 0, sin saltos.
class ModRoute
{
public:
    static constexpr double fadeSeconds = 0.02;

    void prepare (double sampleRate, ModTarget target, float amount) noexcept
    {
        amountRamp.setTime (fadeSeconds, sampleRate);
        amountRamp.snap (std::clamp (amount, -1.0f, 1.0f));
        fade.setTime (fadeSeconds, sampleRate);
        fade.snap (true);
        requested = active = target;
    }

    void set (ModTarget target, float amount) noexcept
    {
        requested = target;
        amountRamp.setTarget (std::clamp (amount, -1.0f, 1.0f));
    }

    // La profundidad de esta muestra (cantidad × fundido de destino). 0 = la ruta no mueve nada.
    float next() noexcept
    {
        if (requested != active)
        {
            const float gain = fade.next (false);
            const float oldAmount = amountRamp.value; // la rampa no avanza: sigue en la cantidad vieja
            if (gain == 0.0f)
            {
                active = requested;
                amountRamp.snap (amountRamp.target);
            }
            return oldAmount * gain;
        }
        const float gain = fade.next (true);
        const float amount = amountRamp.next();
        return gain == 1.0f ? amount : amount * gain;
    }

    [[nodiscard]] ModTarget target() const noexcept { return active; }

private:
    deepwater::dsp::LinearRamp amountRamp;
    deepwater::dsp::EffectSwitch fade;
    ModTarget requested = ModTarget::drive, active = ModTarget::drive;
};

// Las dos fuentes y sus rutas. Corre una vez por muestra para todos los canales (L y R reciben la misma modulación: la
// imagen estéreo no se mueve) y devuelve cuánto se mueve cada destino, en sus unidades.
class Modulator
{
public:
    using Offsets = std::array<float, numModTargets>;

    // La envolvente se mapea en dB: 0 con la entrada a −40 dBFS (o menos) y 1 a 0 dBFS, antes de la sensibilidad. En dB
    // porque así se oye el volumen: subir 6 dB mueve el destino lo mismo a cualquier nivel.
    static constexpr float envelopeFloorDb = -40.0f;
    // Suavizado del LFO (2 ms): redondea los saltos de la cuadrada y del S&H (sin él, un salto de drive es un clic). Al
    // seno y al triángulo casi no los toca.
    static constexpr double lfoSmoothingSeconds = 0.002;
    static constexpr std::uint32_t lfoSeed = 0x52505431u; // S&H: la misma serie "al azar" cada vez

    void prepare (double newSampleRate, const ModulationSettings& initial) noexcept
    {
        sampleRate = newSampleRate;
        follower.prepare (sampleRate);
        sensitivity.setTime (ModRoute::fadeSeconds, sampleRate);
        lfo.reset (lfoSeed);
        lfoCoef = static_cast<float> (1.0 - std::exp (-1.0 / (lfoSmoothingSeconds * sampleRate)));
        lfoValue = smoothedLfo = 0.0f;
        envelopeValue = 0.0f;
        envelopeRoute.prepare (sampleRate, initial.envelope.target, initial.envelope.amount);
        lfoRoute.prepare (sampleRate, initial.lfo.target, initial.lfo.amount);
        setSettings (initial);
        sensitivity.snap (sensitivity.target);
        offsets.fill (0.0f);
    }

    // Una vez por bloque.
    void setSettings (const ModulationSettings& newSettings) noexcept
    {
        settings = newSettings;
        const auto& envelope = settings.envelope;
        follower.setTimes (std::clamp (envelope.attackMs, envelopeMinAttackMs, envelopeMaxAttackMs) / 1000.0,
                           std::clamp (envelope.releaseMs, envelopeMinReleaseMs, envelopeMaxReleaseMs) / 1000.0);
        sensitivity.setTarget (std::clamp (envelope.sensitivityDb, -maxSensitivityDb, maxSensitivityDb));
        envelopeRoute.set (envelope.target, envelope.amount);
        lfo.setShape (settings.lfo.shape);
        lfoRoute.set (settings.lfo.target, settings.lfo.amount);
        updateLfoIncrement();
    }

    // Una vez por bloque, antes de procesarlo. Con sync y la canción sonando la fase sale de la posición en negras: un
    // LFO de 1/4 cae siempre en el pulso, aunque se empiece a reproducir a mitad de compás o se repita un loop.
    void setTransport (const Transport& newTransport) noexcept
    {
        transport = newTransport;
        if (! (transport.bpm > 0.0))
            transport.bpm = 120.0;
        updateLfoIncrement();
        if (settings.lfo.sync && transport.isPlaying && transport.hasPosition)
        {
            const double cycles = transport.ppqPosition / division().beats;
            const double whole = std::floor (cycles);
            lfo.syncTo ({ cycles - whole, static_cast<std::uint32_t> (static_cast<std::int64_t> (whole)) }, lfoSeed);
        }
    }

    // Una vez por muestra. level = el mayor |x| de los canales de la entrada (sin Input ni tono: lo que llega al plugin).
    const Offsets& advance (float level) noexcept
    {
        // Envolvente: picos → dB (+ sensibilidad) → 0..1.
        const auto envelope = static_cast<float> (follower.process (static_cast<double> (level)));
        const float db = deepwater::dsp::gainToDb (envelope) + sensitivity.next();
        envelopeValue = std::clamp ((db - envelopeFloorDb) / -envelopeFloorDb, 0.0f, 1.0f);

        lfoValue = lfo.getValue();
        lfo.advance (lfoIncrement);
        smoothedLfo += (lfoValue - smoothedLfo) * lfoCoef;

        offsets.fill (0.0f);
        if (const float depth = envelopeRoute.next(); depth != 0.0f)
            add (envelopeRoute.target(), envelopeValue * depth);
        if (const float depth = lfoRoute.next(); depth != 0.0f)
            add (lfoRoute.target(), smoothedLfo * depth);
        return offsets;
    }

    // Lo que muestra la GUI: el valor de cada fuente (envolvente 0..1, LFO −1..+1) y cuánto se mueve cada destino.
    [[nodiscard]] float getEnvelopeValue() const noexcept { return envelopeValue; }
    [[nodiscard]] float getLfoValue() const noexcept { return smoothedLfo; }
    [[nodiscard]] const Offsets& getOffsets() const noexcept { return offsets; }

private:
    [[nodiscard]] const deepwater::dsp::LfoDivision& division() const noexcept
    {
        const int last = static_cast<int> (deepwater::dsp::lfoDivisions.size()) - 1;
        return deepwater::dsp::lfoDivisions[static_cast<size_t> (std::clamp (settings.lfo.division, 0, last))];
    }

    void updateLfoIncrement() noexcept
    {
        const double hz = settings.lfo.sync ? deepwater::dsp::lfoDivisionHz (division(), transport.bpm)
                                            : static_cast<double> (std::clamp (settings.lfo.rateHz, lfoMinHz, lfoMaxHz));
        lfoIncrement = hz / sampleRate;
    }

    void add (ModTarget target, float amount) noexcept
    {
        const auto index = static_cast<size_t> (target);
        offsets[index] += amount * modTargetSpans[index];
    }

    double sampleRate = 44100.0;
    ModulationSettings settings {};
    Transport transport {};

    deepwater::dsp::EnvelopeFollower follower;
    deepwater::dsp::LinearRamp sensitivity;
    float envelopeValue = 0.0f;

    deepwater::dsp::Lfo lfo;
    double lfoIncrement = 0.0;
    float lfoValue = 0.0f, smoothedLfo = 0.0f, lfoCoef = 1.0f;

    ModRoute envelopeRoute, lfoRoute;
    Offsets offsets {};
};

} // namespace riptide::dsp
