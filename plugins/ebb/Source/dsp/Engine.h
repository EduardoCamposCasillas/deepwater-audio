#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

#include "deepwater/dsp/Smoothing.h"
#include "dsp/Bands.h"
#include "dsp/CurveShapes.h"
#include "dsp/HostClock.h"
#include "dsp/LiveScope.h"
#include "dsp/Lookahead.h"
#include "dsp/TempoCurve.h"
#include "dsp/Triggers.h"

namespace ebb::dsp
{

// Ajustes de la curva, en unidades reales. El procesador los lee de los parámetros una vez por bloque. En los modos MIDI
// (Fase 4) y audio (Fase 5) la división no marca cada cuánto hay golpe sino el LARGO de la curva, al tempo del host.
struct TempoSettings
{
    TriggerMode mode = TriggerMode::tempo;
    bool velocitySensitive = false; // modo MIDI: la velocidad de la nota escala la profundidad
    int division = defaultDuckDivision;
    float depth = 0.7f;        // 0–1: cuánto baja el volumen en el golpe (1 = silencio)
    float duration = 0.7f;     // 0–1: fracción del ciclo que tarda en volver
    float smoothingMs = 2.0f;  // tiempo en que la ganancia hace ~95 % de un cambio (0 = sin suavizar)
    DuckBand band = DuckBand::full;          // Fase 6: qué parte del espectro baja
    float crossoverHz = defaultCrossoverHz;  // Fase 6: dónde se parten graves y agudos
    float mix = 1.0f;                        // Fase 6: 0–1, cuánto del efecto se oye (0 = señal sin ducking)
};

// Cadena de audio completa del ducker, sin JUCE (se prueba en tests/EngineTests.cpp):
//
//   reloj del host ── curva de tempo ── profundidad ── suavizado ── mix ── ganancia
//   o disparos (MIDI / sidechain)                                              │
//   entrada ── lookahead ──┬──── banda (todo / graves / agudos) × ganancia ───────┐
//                          │                                                      ├─ fundido de bypass ── salida
//                          └──────────────── señal original (dry) ────────────────┘
//
// Modo tempo: la curva se calcula en cada muestra desde la POSICIÓN del host (no con un contador propio), así que un loop,
// un salto o un cambio de tempo la dejan cayendo en su sitio. Modo MIDI: cada nota reinicia la curva en su muestra
// exacta y desde ahí cuenta negras al tempo del host (también con el host parado: se puede probar tocando notas). Modo
// audio: igual, pero los disparos los detecta el procesador en el sidechain. El lookahead retrasa la entrada (también la
// del bypass, para que la latencia no cambie), así la curva empieza antes de que llegue el golpe. La ganancia se aplica
// a toda la señal o a una sola banda (Bands.h, Fase 6).
// Mix dry/wet: en un efecto que solo cambia el volumen, mezclar en paralelo la señal original (en fase) con la procesada
// da (1 − mix)·x + mix·g·x = (1 − mix·(1 − g))·x: lo mismo que escalar la reducción. Así se implementa, sin una segunda
// copia de la señal (y en modo bandas, sin riesgo de que la original y la procesada lleguen con fases distintas).
// Para que la ganancia nunca salte hay tres capas:
//   1. Profundidad y duración pasan por rampas lineales (un giro de perilla o una automatización por bloques).
//   2. Cambiar de división, de forma de curva, de modo o de sensibilidad a la velocidad (o parar el host) cambia la
//      curva de golpe: se calcula la diferencia entre la curva vieja y la nueva en ese instante y se desvanece en
//      30 ms ("glide"). La curva nueva entra ya en su sitio.
//   3. El suavizado final (perilla) redondea los saltos que quedan: los golpes y los saltos de la línea de tiempo. Un
//      golpe que llega antes de que la curva termine baja desde la ganancia actual, no desde 1.
// Fase 7: para la GUI, cuenta los golpes, dice por dónde va la curva y (si hay visor) le pasa cada muestra: entrada,
// salida, ganancia que sonó y si ahí empezó una curva. Solo lee: el sonido no cambia.
class Engine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double bypassFadeSeconds = 0.02;
    static constexpr double depthRampSeconds = 0.02;
    static constexpr double durationRampSeconds = 0.05;
    static constexpr double glideSeconds = 0.03;
    static constexpr double maxLookaheadSeconds = 0.01;
    static constexpr double mixRampSeconds = 0.02;

    // Fuera del hilo de audio (reserva el lookahead).
    void prepare (double newSampleRate, bool bypassed, const TempoSettings& settings, int lookaheadSamples = 0)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        lookahead.prepare (sampleRate, maxLookaheadSamples (sampleRate), lookaheadSamples);
        bypassFade.setTime (bypassFadeSeconds, sampleRate);
        processing = ! bypassed;
        bypassFade.snap (processing);

        depthRamp.setTime (depthRampSeconds, sampleRate);
        depthRamp.snap (std::clamp (settings.depth, 0.0f, 1.0f));
        durationRamp.setTime (durationRampSeconds, sampleRate);
        durationRamp.snap (std::clamp (settings.duration, 0.0f, 1.0f));
        division = settings.division;
        mode = settings.mode;
        velocitySensitive = settings.velocitySensitive;
        smoothingMs = -1.0f;
        updateSmoothing (settings.smoothingMs);
        mixRamp.setTime (mixRampSeconds, sampleRate);
        mixRamp.snap (std::clamp (settings.mix, 0.0f, 1.0f));
        bands.prepare (sampleRate, settings.band, settings.crossoverHz);

        glideSamples = std::max (1, static_cast<int> (std::lround (glideSeconds * sampleRate)));
        glideOffset = 0.0f;
        glideRemaining = 0;
        gain = 1.0f;
        minGain = 1.0f;
        lastCurveBeats = -1.0;
        hits = 0;
        position = -1.0f;
        primed = false;
        state = {};
        hit = {};
    }

    void setBypassed (bool bypassed) noexcept { processing = ! bypassed; }

    // Fase 7: el visor en vivo que recibe cada muestra (nullptr = ninguno: la GUI está cerrada). No cambia el sonido.
    void setScope (LiveScope* newScope) noexcept { scope = newScope; }

    // Retraso de la entrada en muestras (hasta maxLookaheadSamples). Un cambio se hace con un fundido cruzado de 20 ms.
    void setLookahead (int samples) noexcept { lookahead.setDelay (samples); }

    [[nodiscard]] static int maxLookaheadSamples (double rate) noexcept
    {
        return static_cast<int> (std::ceil (maxLookaheadSeconds * rate));
    }

    // Forma de la curva (Fase 3). Se aplica al principio del siguiente process(), con glide desde la forma anterior.
    // Copia unos 400 bytes: llamarla solo cuando la curva cambió.
    void setCurve (const BreakpointCurve& newCurve) noexcept
    {
        pendingCurve = newCurve;
        curvePending = true;
    }

    // Una vez por bloque, antes de process().
    void setSettings (const TempoSettings& settings) noexcept
    {
        depthRamp.setTarget (std::clamp (settings.depth, 0.0f, 1.0f));
        durationRamp.setTarget (std::clamp (settings.duration, 0.0f, 1.0f));
        division = settings.division;
        mode = settings.mode;
        velocitySensitive = settings.velocitySensitive;
        updateSmoothing (settings.smoothingMs);
        mixRamp.setTarget (std::clamp (settings.mix, 0.0f, 1.0f));
        bands.setBand (settings.band);
        bands.setCrossover (settings.crossoverHz);
    }

    // Procesa en el sitio. El reloj ya tiene que haber recibido el bloque (HostClock::beginBlock). Con más de
    // maxChannels canales, los de más se dejan intactos. 'triggers': los disparos del bloque (MIDI o sidechain), ordenados
    // por muestra (en modo tempo se ignoran); varios en la misma muestra = gana el último.
    void process (float* const* channels, int numChannels, int numSamples, const HostClock& clock,
                  std::span<const Trigger> triggers = {}) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);
        beginBlock (clock);
        minGain = 1.0f;
        size_t nextTrigger = isHitMode (state.mode) ? 0 : triggers.size();

        for (int i = 0; i < numSamples; ++i)
        {
            bool hitHere = false;
            for (; nextTrigger < triggers.size() && triggers[nextTrigger].offset <= i; ++nextTrigger)
            {
                startHit (triggers[nextTrigger].velocity);
                hitHere = true;
            }

            const float duckGain = applyMix (nextGain (clock.ppqAt (i), hitHere));
            const float wet = bypassFade.next (processing); // 1 = procesado, 0 = original
            const float heardGain = 1.0f - wet * (1.0f - duckGain);
            minGain = std::min (minGain, heardGain);

            lookahead.advance();
            bands.advance();
            float inputPeak = 0.0f, outputPeak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float& sample = channels[ch][i];
                const float dry = lookahead.process (ch, sample);
                const float processed = bands.process (ch, dry, duckGain);

                // Fuera del fundido se copia la señal tal cual: con el bypass puesto (o profundidad 0), bit a bit.
                sample = wet == 1.0f ? processed : wet == 0.0f ? dry : dry + (processed - dry) * wet;
                inputPeak = std::max (inputPeak, std::abs (dry));
                outputPeak = std::max (outputPeak, std::abs (sample));
            }
            if (scope != nullptr)
                scope->write (i, inputPeak, outputPeak, heardGain, hitHere);
        }
        updatePosition();
    }

    // Ganancia más baja que sonó en el último bloque (1 = sin ducking), para el medidor de la GUI. En modo bandas es la de
    // la banda que baja.
    [[nodiscard]] float blockMinGain() const noexcept { return minGain; }

    // Fase 7, para la GUI. Cuántas curvas empezaron desde prepare(): golpes MIDI o del sidechain, y en modo tempo cada
    // inicio de ciclo (también al arrancar justo en uno, o al volver de un loop a un golpe).
    [[nodiscard]] std::uint32_t hitCount() const noexcept { return hits; }

    // Dónde va la curva al final del último bloque, en el eje x del editor: 0 = el golpe, 1 = el final de la DURACIÓN
    // (pasada la duración se queda en 1, en el último punto). −1 = no suena ninguna curva (tempo parado, o antes del
    // primer golpe en MIDI y audio).
    [[nodiscard]] float curvePosition() const noexcept { return position; }

private:
    // Lo que decide qué curva suena. Si cambia de un bloque al siguiente, la curva saltaría: se hace glide.
    struct CurveState
    {
        TriggerMode mode = TriggerMode::tempo;
        double period = 1.0;    // tempo: duración del ciclo; MIDI/audio: largo de la curva (con duración 100 %); en negras
        bool running = false;   // el host avanza (reproduciendo o reloj libre)
        bool velocitySensitive = false;
        bool hasHit = false;    // MIDI/audio: ya hubo un golpe (antes del primero no hay ducking)
    };

    // El último golpe (MIDI o sidechain): cuánto lleva sonando su curva y con qué velocidad llegó.
    struct MidiHit
    {
        double beats = 0.0;     // negras desde la nota
        bool finished = false;  // la curva ya pasó su largo: se queda en el valor del último punto
        float velocity = 1.0f;
    };

    // Forma de la curva (0–1) en una posición, con un estado dado. Modo tempo parado: sin ducking (la canción no avanza
    // y la curva quedaría congelada a medias: el bajo sonaría atenuado al previsualizar notas en el Piano roll).
    [[nodiscard]] float shapeAt (double ppq, const CurveState& s, float duration) const noexcept
    {
        return shapeFromBeats (curveBeats (ppq, s), s, duration);
    }

    // Negras desde que empezó la curva que suena: en tempo, la posición en el ciclo; en MIDI/audio, desde el último
    // golpe. −1 = no suena ninguna.
    [[nodiscard]] double curveBeats (double ppq, const CurveState& s) const noexcept
    {
        if (isHitMode (s.mode))
            return s.hasHit ? hit.beats : -1.0;
        if (! s.running)
            return -1.0;
        return positionInCycle (ppq, origin, quartersPerBar, s.period);
    }

    [[nodiscard]] float shapeFromBeats (double beats, const CurveState& s, float duration) const noexcept
    {
        if (beats < 0.0)
            return 0.0f;
        if (isHitMode (s.mode))
        {
            const double length = s.period * static_cast<double> (std::max (duration, 1.0e-3f));
            const float scale = s.velocitySensitive ? hit.velocity : 1.0f;
            return scale * duckAt (curve, hit.finished ? length : std::min (beats, length), s.period, duration);
        }
        return duckAt (curve, beats, s.period, duration);
    }

    // Para la GUI, con la última muestra del bloque.
    void updatePosition() noexcept
    {
        if (lastCurveBeats < 0.0)
        {
            position = -1.0f;
            return;
        }
        const double length = state.period * static_cast<double> (std::max (durationRamp.value, 1.0e-3f));
        const bool finished = isHitMode (state.mode) && hit.finished;
        position = finished ? 1.0f : static_cast<float> (std::min (1.0, lastCurveBeats / length));
    }

    void startHit (float velocity) noexcept
    {
        hit = { 0.0, false, std::clamp (velocity, 0.0f, 1.0f) };
        state.hasHit = true;
        ++hits;
    }

    // Avanza el golpe una muestra. Termina al pasar su largo (con la duración de ese momento): así, si después se
    // alarga la división, una curva que ya terminó no vuelve a sonar a medias.
    void advanceHit (float duration) noexcept
    {
        if (! state.hasHit || hit.finished)
            return;
        hit.beats += beatsPerSample;
        hit.finished = hit.beats >= state.period * static_cast<double> (std::max (duration, 1.0e-3f));
    }

    void applyPendingCurve() noexcept
    {
        if (curvePending)
            curve = pendingCurve;
        curvePending = false;
    }

    [[nodiscard]] float glideValue() const noexcept
    {
        return glideOffset * static_cast<float> (glideRemaining) / static_cast<float> (glideSamples);
    }

    void beginBlock (const HostClock& clock) noexcept
    {
        const auto signature = clock.timeSignature();
        origin = clock.barOrigin();
        quartersPerBar = signature.quartersPerBar();
        beatsPerSample = clock.getBeatsPerSample();
        maxBeatsPerSample = clock.maxBeatsPerSample();

        CurveState next;
        next.mode = mode;
        next.period = divisionBeats (division, signature);
        next.running = clock.isRunning();
        next.velocitySensitive = velocitySensitive;
        // Al entrar en modo MIDI o audio se empieza sin golpe (no revive uno viejo), y al parar el host se suelta el que
        // sonaba, como en el modo tempo. Con el host parado los golpes nuevos sí disparan (se puede probar tocando el
        // teclado o con el kick sonando en el Standalone).
        const bool stopped = state.running && ! next.running;
        next.hasHit = isHitMode (next.mode) && next.mode == state.mode && state.hasHit && ! stopped;

        if (! primed)
        {
            primed = true;
            state = next;
            applyPendingCurve();
            return;
        }

        // La curva saltaría: se mide el salto en la primera muestra del bloque y se desvanece. Arrancar el host NO
        // desvanece: el primer golpe suena entero.
        const bool changed = next.period != state.period || next.mode != state.mode || stopped
                             || next.velocitySensitive != state.velocitySensitive || next.hasHit != state.hasHit || curvePending;
        if (changed)
        {
            const double ppq = clock.ppqAt (0);
            const float duration = durationRamp.value;
            const float before = std::clamp (shapeAt (ppq, state, duration) + glideValue(), 0.0f, 1.0f);
            applyPendingCurve();
            const float after = shapeAt (ppq, next, duration);
            glideOffset = before - after;
            glideRemaining = glideSamples;
        }
        // Otro ciclo u otro modo: la posición anterior ya no sirve para reconocer el inicio de un ciclo.
        if (next.period != state.period || next.mode != state.mode)
            lastCurveBeats = -1.0;
        state = next;
    }

    // 'hitHere' (entrada y salida): ya hubo un golpe MIDI o del sidechain en esta muestra; en modo tempo se marca aquí
    // si empieza un ciclo.
    float nextGain (double ppq, bool& hitHere) noexcept
    {
        const float duration = durationRamp.next();
        const float depth = depthRamp.next();

        const double beats = curveBeats (ppq, state);
        float shape = shapeFromBeats (beats, state, duration);
        // Inicio de ciclo: la posición cae en la primera muestra del ciclo y viene de más atrás (o de no sonar). Un loop
        // o un salto a mitad de ciclo no cuenta: ahí la curva no empieza.
        if (! isHitMode (state.mode) && beats >= 0.0 && beats < maxBeatsPerSample + 1.0e-9
            && (lastCurveBeats < 0.0 || beats < lastCurveBeats))
        {
            hitHere = true;
            ++hits;
        }
        lastCurveBeats = beats;
        advanceHit (duration);
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

    // Con mix 100 % la ganancia pasa tal cual (bit a bit igual que antes de la Fase 6).
    float applyMix (float duckGain) noexcept
    {
        const float mix = mixRamp.next();
        return mix >= 1.0f ? duckGain : 1.0f - mix * (1.0f - duckGain);
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
    LookaheadDelay lookahead;

    deepwater::dsp::LinearRamp depthRamp, durationRamp, mixRamp;
    BandDucker bands;
    int division = defaultDuckDivision;
    TriggerMode mode = TriggerMode::tempo;
    bool velocitySensitive = false;
    BreakpointCurve curve = defaultCurve();
    BreakpointCurve pendingCurve;
    bool curvePending = false;
    float smoothingMs = 0.0f;
    float smoothingCoef = 1.0f;

    // Estado del bloque actual.
    bool primed = false;
    CurveState state;
    MidiHit hit;
    double origin = 0.0;
    double quartersPerBar = 4.0;
    double beatsPerSample = 0.0;
    double maxBeatsPerSample = 0.0; // lo más que avanza la posición por muestra (el reloj puede ir un 10 % más rápido)

    float glideOffset = 0.0f;
    int glideRemaining = 0;
    int glideSamples = 1;

    float gain = 1.0f;
    float minGain = 1.0f;

    // Fase 7: para la GUI.
    LiveScope* scope = nullptr;
    double lastCurveBeats = -1.0; // curveBeats de la muestra anterior
    std::uint32_t hits = 0;
    float position = -1.0f;
};

} // namespace ebb::dsp
