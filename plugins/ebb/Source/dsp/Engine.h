#pragma once

#include <algorithm>
#include <cmath>

#include "deepwater/dsp/Smoothing.h"
#include "dsp/HostClock.h"
#include "dsp/TempoCurve.h"

namespace ebb::dsp
{

// Ajustes del modo tempo (Fase 2), en unidades reales. El procesador los lee de los parámetros una vez por bloque.
struct TempoSettings
{
    int division = defaultDuckDivision;
    float depth = 0.7f;        // 0–1: cuánto baja el volumen en el golpe (1 = silencio)
    float duration = 0.7f;     // 0–1: fracción del ciclo que tarda en volver
    float smoothingMs = 2.0f;  // tiempo en que la ganancia hace ~95 % de un cambio (0 = sin suavizar)
};

// Cadena de audio completa del ducker, sin JUCE (se prueba en tests/EngineTests.cpp):
//
//   reloj del host ── curva de tempo ── profundidad ── suavizado ── ganancia
//                                                                     │
//   entrada ──┬──────────────────────────────────────────── × ganancia ──┐
//             │                                                          ├─ fundido de bypass ── salida
//             └──────────────── señal original (dry) ────────────────────┘
//
// La curva se calcula en cada muestra desde la POSICIÓN del host (no con un contador propio), así que un loop, un salto
// o un cambio de tempo la dejan cayendo en su sitio. Para que la ganancia nunca salte hay tres capas:
//   1. Profundidad y duración pasan por rampas lineales (un giro de perilla o una automatización por bloques).
//   2. Cambiar de división (o parar el host) cambia la curva de golpe: se calcula la diferencia entre la curva vieja y
//      la nueva en ese instante y se desvanece en 30 ms ("glide"). La curva nueva entra ya en su sitio.
//   3. El suavizado final (perilla) redondea los saltos que quedan: los golpes y los saltos de la línea de tiempo. Un
//      golpe que llega antes de que la curva termine baja desde la ganancia actual, no desde 1.
class Engine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double bypassFadeSeconds = 0.02;
    static constexpr double depthRampSeconds = 0.02;
    static constexpr double durationRampSeconds = 0.05;
    static constexpr double glideSeconds = 0.03;

    void prepare (double newSampleRate, bool bypassed, const TempoSettings& settings) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        bypassFade.setTime (bypassFadeSeconds, sampleRate);
        processing = ! bypassed;
        bypassFade.snap (processing);

        depthRamp.setTime (depthRampSeconds, sampleRate);
        depthRamp.snap (std::clamp (settings.depth, 0.0f, 1.0f));
        durationRamp.setTime (durationRampSeconds, sampleRate);
        durationRamp.snap (std::clamp (settings.duration, 0.0f, 1.0f));
        division = settings.division;
        smoothingMs = -1.0f;
        updateSmoothing (settings.smoothingMs);

        glideSamples = std::max (1, static_cast<int> (std::lround (glideSeconds * sampleRate)));
        glideOffset = 0.0f;
        glideRemaining = 0;
        gain = 1.0f;
        minGain = 1.0f;
        primed = false;
    }

    void setBypassed (bool bypassed) noexcept { processing = ! bypassed; }

    // Una vez por bloque, antes de process().
    void setSettings (const TempoSettings& settings) noexcept
    {
        depthRamp.setTarget (std::clamp (settings.depth, 0.0f, 1.0f));
        durationRamp.setTarget (std::clamp (settings.duration, 0.0f, 1.0f));
        division = settings.division;
        updateSmoothing (settings.smoothingMs);
    }

    // Procesa en el sitio. El reloj ya tiene que haber recibido el bloque (HostClock::beginBlock). Con más de
    // maxChannels canales, los de más se dejan intactos.
    void process (float* const* channels, int numChannels, int numSamples, const HostClock& clock) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);
        beginBlock (clock);
        minGain = 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float duckGain = nextGain (clock.ppqAt (i));
            const float wet = bypassFade.next (processing); // 1 = procesado, 0 = original
            minGain = std::min (minGain, 1.0f - wet * (1.0f - duckGain));

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float& sample = channels[ch][i];
                const float dry = sample;
                const float processed = dry * duckGain;

                // Fuera del fundido se copia la señal tal cual: con el bypass puesto (o profundidad 0), bit a bit.
                sample = wet == 1.0f ? processed : wet == 0.0f ? dry : dry + (processed - dry) * wet;
            }
        }
    }

    // Ganancia más baja que sonó en el último bloque (1 = sin ducking), para el medidor de la GUI.
    [[nodiscard]] float blockMinGain() const noexcept { return minGain; }

private:
    // Forma de la curva (0–1) en una posición, con un período y un estado dados. Parado no hay ducking: la canción no
    // avanza y la curva quedaría congelada a medias (el bajo sonaría atenuado al previsualizar notas en el Piano roll).
    [[nodiscard]] float shapeAt (double ppq, double cyclePeriod, bool isActive, float duration) const noexcept
    {
        if (! isActive)
            return 0.0f;
        return duckAt (positionInCycle (ppq, origin, quartersPerBar, cyclePeriod), cyclePeriod, duration);
    }

    [[nodiscard]] float glideValue() const noexcept
    {
        return glideOffset * static_cast<float> (glideRemaining) / static_cast<float> (glideSamples);
    }

    void beginBlock (const HostClock& clock) noexcept
    {
        const auto signature = clock.timeSignature();
        const double newPeriod = divisionBeats (division, signature);
        const bool nowActive = clock.isRunning();
        origin = clock.barOrigin();
        quartersPerBar = signature.quartersPerBar();

        if (! primed)
        {
            primed = true;
            period = newPeriod;
            active = nowActive;
            return;
        }

        // Cambio de división (o de compás, con "1 bar") o el host se paró: la curva saltaría. Se mide el salto en la
        // primera muestra del bloque y se desvanece. Arrancar NO desvanece: el primer golpe tiene que sonar entero.
        if (newPeriod != period || (active && ! nowActive))
        {
            const double ppq = clock.ppqAt (0);
            const float duration = durationRamp.value;
            const float before = std::clamp (shapeAt (ppq, period, active, duration) + glideValue(), 0.0f, 1.0f);
            const float after = shapeAt (ppq, newPeriod, nowActive, duration);
            glideOffset = before - after;
            glideRemaining = glideSamples;
        }
        period = newPeriod;
        active = nowActive;
    }

    float nextGain (double ppq) noexcept
    {
        const float duration = durationRamp.next();
        const float depth = depthRamp.next();

        float shape = shapeAt (ppq, period, active, duration);
        if (glideRemaining > 0)
        {
            shape = std::clamp (shape + glideValue(), 0.0f, 1.0f);
            --glideRemaining;
        }

        // Suavizado: filtro de 1 polo sobre la ganancia (se acerca un porcentaje fijo por muestra). Muy cerca del
        // objetivo se copia, para que sin ducking la ganancia sea 1 exacto y la señal pase bit a bit.
        const float target = 1.0f - depth * shape;
        gain += (target - gain) * smoothingCoef;
        if (std::abs (target - gain) < 1.0e-6f)
            gain = target;
        return gain;
    }

    void updateSmoothing (float newMs) noexcept
    {
        newMs = std::max (0.0f, newMs);
        if (newMs == smoothingMs)
            return;
        smoothingMs = newMs;
        // En 3 constantes de tiempo un filtro de 1 polo hace el 95 % del camino: así "2 ms" significa que un salto se
        // completa (casi) en 2 ms, que es como se percibe.
        smoothingCoef = newMs <= 0.0f ? 1.0f : static_cast<float> (1.0 - std::exp (-3.0 / (newMs * 0.001 * sampleRate)));
    }

    double sampleRate = 44100.0;
    deepwater::dsp::EffectSwitch bypassFade;
    bool processing = true;

    deepwater::dsp::LinearRamp depthRamp, durationRamp;
    int division = defaultDuckDivision;
    float smoothingMs = 0.0f;
    float smoothingCoef = 1.0f;

    // Estado del bloque actual.
    bool primed = false;
    bool active = false;
    double period = 1.0;
    double origin = 0.0;
    double quartersPerBar = 4.0;

    float glideOffset = 0.0f;
    int glideRemaining = 0;
    int glideSamples = 1;

    float gain = 1.0f;
    float minGain = 1.0f;
};

} // namespace ebb::dsp
