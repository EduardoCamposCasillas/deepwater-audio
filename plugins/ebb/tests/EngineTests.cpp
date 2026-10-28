// Tests de Ebb sin JUCE: el motor de audio (los del reloj musical se fueron con él a libs/dsp). Se lanzan con ctest. Si algo falla,
// imprime el archivo, la línea y la condición.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <xmmintrin.h>

#include "deepwater/dsp/TransientDetector.h"
#include "dsp/Bands.h"
#include "dsp/CurveShapes.h"
#include "dsp/Engine.h"
#include "dsp/HostClock.h"
#include "dsp/LiveScope.h"
#include "dsp/TempoCurve.h"
#include "dsp/Triggers.h"

namespace
{
int failures = 0;

#define CHECK(condition)                                                                   \
    do                                                                                     \
    {                                                                                      \
        if (! (condition))                                                                 \
        {                                                                                  \
            std::printf ("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #condition);           \
            ++failures;                                                                    \
        }                                                                                  \
    } while (false)

using namespace ebb::dsp;

constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };

bool near (double a, double b, double tolerance = 1.0e-9)
{
    return std::abs (a - b) <= tolerance;
}

// Generador de tamaños de bloque "al azar" pero repetible (FL cambia el tamaño del bloque de un callback a otro).
struct BlockSizes
{
    std::uint32_t state = 12345;
    int next (int maxSize) noexcept
    {
        state = state * 1664525u + 1013904223u;
        return 1 + static_cast<int> ((state >> 8) % static_cast<std::uint32_t> (maxSize));
    }
};

TransportInfo playingAt (double ppq, double bpm, TimeSignature signature = {})
{
    TransportInfo info;
    info.hasHost = true;
    info.isPlaying = true;
    info.bpm = bpm;
    info.ppqPosition = ppq;
    info.timeSignature = signature;
    return info;
}

// Ajustes "de laboratorio": curva profunda, sin suavizado, para leer la curva tal cual en la salida.
TempoSettings rawCurve (int division = defaultDuckDivision, float duration = 0.5f)
{
    TempoSettings settings;
    settings.division = division;
    settings.depth = 1.0f;
    settings.duration = duration;
    settings.smoothingMs = 0.0f;
    return settings;
}

// Simula a FL reproduciendo desde 'startPpq' con bloques de tamaño variable y una señal constante 1: la salida ES la
// ganancia. 'beforeBlock (muestra, settings, info)' puede cambiar los ajustes o el transporte antes de cada bloque.
template <typename BeforeBlock>
std::vector<float> renderGain (double sampleRate, double bpm, TempoSettings settings, int totalSamples, int maxBlock,
                               BeforeBlock beforeBlock, TimeSignature signature = {})
{
    HostClock clock;
    clock.prepare (sampleRate);
    Engine engine;
    engine.prepare (sampleRate, false, settings);

    std::vector<float> out (static_cast<size_t> (totalSamples), 1.0f);
    BlockSizes sizes;
    const double perSample = bpm / (60.0 * sampleRate);
    double ppq = 0.0;
    for (int done = 0; done < totalSamples;)
    {
        const int numSamples = std::min (sizes.next (maxBlock), totalSamples - done);
        auto info = playingAt (ppq, bpm, signature);
        beforeBlock (done, settings, info);
        if (info.ppqPosition)
            ppq = *info.ppqPosition;

        clock.beginBlock (info, numSamples);
        engine.setSettings (settings);
        float* channel = out.data() + done;
        engine.process (&channel, 1, numSamples, clock);

        if (info.isPlaying)
            ppq += numSamples * perSample;
        done += numSamples;
    }
    return out;
}

std::vector<float> renderSteady (double sampleRate, double bpm, TempoSettings settings, int totalSamples, int maxBlock,
                                 TimeSignature signature = {})
{
    return renderGain (sampleRate, bpm, settings, totalSamples, maxBlock, [] (int, TempoSettings&, TransportInfo&) {}, signature);
}

// El mayor cambio de ganancia entre dos muestras seguidas en [from, to): un salto grande = un clic.
float maxStep (const std::vector<float>& gain, size_t from, size_t to)
{
    float largest = 0.0f;
    for (size_t i = std::max<size_t> (from, 1); i < std::min (to, gain.size()); ++i)
        largest = std::max (largest, std::abs (gain[i] - gain[i - 1]));
    return largest;
}

// Una nota MIDI en una muestra absoluta de la simulación.
struct Note
{
    int sample;
    float velocity = 1.0f;
};

TempoSettings midiCurve (int division = defaultDuckDivision, float duration = 0.5f)
{
    TempoSettings settings = rawCurve (division, duration);
    settings.mode = TriggerMode::midi;
    return settings;
}

// Como renderGain, pero además entrega las notas a su bloque, con su posición dentro de él (como hace el procesador con
// el MidiBuffer). 'beforeBlock' puede cambiar ajustes o transporte; con el host parado la posición no avanza.
template <typename BeforeBlock>
std::vector<float> renderMidi (double sampleRate, double bpm, TempoSettings settings, int totalSamples, int maxBlock,
                               const std::vector<Note>& notes, BeforeBlock beforeBlock)
{
    HostClock clock;
    clock.prepare (sampleRate);
    Engine engine;
    engine.prepare (sampleRate, false, settings);

    std::vector<float> out (static_cast<size_t> (totalSamples), 1.0f);
    std::vector<Trigger> triggers;
    BlockSizes sizes;
    const double perSample = bpm / (60.0 * sampleRate);
    double ppq = 0.0;
    for (int done = 0; done < totalSamples;)
    {
        const int numSamples = std::min (sizes.next (maxBlock), totalSamples - done);
        auto info = playingAt (ppq, bpm);
        beforeBlock (done, settings, info);

        triggers.clear();
        for (const auto& note : notes)
            if (note.sample >= done && note.sample < done + numSamples)
                triggers.push_back ({ note.sample - done, note.velocity });

        clock.beginBlock (info, numSamples);
        engine.setSettings (settings);
        float* channel = out.data() + done;
        engine.process (&channel, 1, numSamples, clock, triggers);

        if (info.isPlaying)
            ppq += numSamples * perSample;
        done += numSamples;
    }
    return out;
}

std::vector<float> renderMidi (double sampleRate, double bpm, TempoSettings settings, int totalSamples, int maxBlock,
                               const std::vector<Note>& notes)
{
    return renderMidi (sampleRate, bpm, settings, totalSamples, maxBlock, notes, [] (int, TempoSettings&, TransportInfo&) {});
}

void testTempoCurve()
{
    std::printf ("Curva de tempo: divisiones, posicion en el ciclo y forma\n");

    CHECK (near (divisionBeats (2, { 4, 4 }), 1.0));
    CHECK (near (divisionBeats (0, { 4, 4 }), 4.0));
    CHECK (near (divisionBeats (0, { 7, 8 }), 3.5)); // "1 bar" = lo que dure el compás
    CHECK (near (divisionBeats (6, { 4, 4 }), 2.0 / 3.0));
    CHECK (near (divisionBeats (99, { 4, 4 }), 1.0 / 6.0)); // índice fuera de rango: el más cercano

    CHECK (near (positionInCycle (9.5, 0.0, 4.0, 1.0), 0.5));
    CHECK (near (positionInCycle (2.0, 0.0, 4.0, 2.0 / 3.0), 0.0, 1.0e-9));   // tresillo: el 4.º golpe cae en la negra 2
    CHECK (near (positionInCycle (0.9999999999, 0.0, 4.0, 1.0), 0.0, 1.0e-9)); // redondeo del host: ya es el golpe
    CHECK (near (positionInCycle (-0.25, 0.0, 4.0, 1.0), 0.75));               // pre-roll
    // 1/2 T (4/3 de negra) no cabe justo en 3/4: la cuadrícula se reinicia en el "1" de cada compás.
    CHECK (near (positionInCycle (3.0, 0.0, 3.0, 4.0 / 3.0), 0.0, 1.0e-9));
    // Inicio de compás del host (p. ej. tras un cambio de compás en el 10): se cuenta desde ahí.
    CHECK (near (positionInCycle (10.5, 10.0, 3.0, 1.0), 0.5));

    // La curva se estira a lo largo de la duración: con un ciclo de 1 negra y duración 50 %, la mitad de la bajada
    // cae en 0.25 negras.
    const auto& classic = defaultCurve();
    CHECK (duckAt (classic, 0.0, 1.0, 0.5f) == 1.0f && duckAt (classic, 0.5, 1.0, 0.5f) == 0.0f && duckAt (classic, 0.9, 1.0, 0.5f) == 0.0f);
    CHECK (std::abs (duckAt (classic, 0.25, 1.0, 0.5f) - 0.5f) < 1.0e-6f);
}

void testCurveShapes()
{
    std::printf ("Formas de curva: validas, la Clasica = la smoothstep de la Fase 2, y se reconocen\n");

    // Todas las formas de fábrica ya vienen saneadas (el constructor las ordena): empiezan en 0, terminan en 1 y
    // terminan arriba (sin ducking pasada la duración), así que con cualquier ajuste el volumen vuelve entero.
    bool valid = true;
    for (size_t i = 0; i < curveShapes.size(); ++i)
    {
        const auto& curve = curveShapes[i].curve;
        valid = valid && curve.size() >= 2 && curve[0].x == 0.0f && curve[curve.size() - 1].x == 1.0f;
        valid = valid && curve.valueAt (1.0f) == 1.0f;
        valid = valid && findCurveShape (curve) == static_cast<int> (i); // dos formas iguales confundirían el menú
    }
    CHECK (valid);
    CHECK (findCurveShape (BreakpointCurve { { 0.0f, 0.0f }, { 0.3f, 0.7f }, { 1.0f, 1.0f } }) == -1);

    // La Clásica reproduce la curva fija de la Fase 2 (x²·(3 − 2x)) con un error que no se oye.
    float error = 0.0f;
    for (int i = 0; i <= 1000; ++i)
    {
        const float x = static_cast<float> (i) / 1000.0f;
        error = std::max (error, std::abs (defaultCurve().valueAt (x) - x * x * (3.0f - 2.0f * x)));
    }
    CHECK (error < 0.006f);
}

void testCurveChangeAndHold()
{
    std::printf ("Formas en el motor: cambiar de forma sonando no salta; una curva que termina abajo se queda abajo\n");

    constexpr double sampleRate = 48000.0;
    constexpr int beat = 24000; // 120 BPM

    // Cambio de Lenta a Rápida a mitad de la subida (negra 0.3, donde más se separan): sin glide, salto de ~0.5.
    for (const size_t to : { size_t { 2 }, size_t { 7 } })
    {
        HostClock clock;
        clock.prepare (sampleRate);
        Engine engine;
        const TempoSettings settings = rawCurve (2, 0.7f);
        engine.prepare (sampleRate, false, settings);
        engine.setCurve (curveShapes[3].curve);

        std::vector<float> gain (static_cast<size_t> (beat - 10), 1.0f);
        double ppq = 0.0;
        bool changed = false;
        for (size_t done = 0; done < gain.size(); done += 64)
        {
            const int numSamples = static_cast<int> (std::min<size_t> (64, gain.size() - done));
            if (! changed && done >= static_cast<size_t> (0.3 * beat))
            {
                engine.setCurve (curveShapes[to].curve);
                changed = true;
            }
            clock.beginBlock (playingAt (ppq, 120.0), numSamples);
            ppq += numSamples / static_cast<double> (beat);
            float* channel = gain.data() + done;
            engine.process (&channel, 1, numSamples, clock);
        }
        // Puerta (7) sube de golpe en x = 0.5 (negra 0.35): ese escalón es la forma, no un clic del cambio.
        const size_t until = to == 7 ? static_cast<size_t> (0.34 * beat) : gain.size();
        const float step = maxStep (gain, static_cast<size_t> (0.25 * beat), until);
        if (step >= 0.01f)
            std::printf ("  Lenta -> %s: salto de %.4f\n", curveShapes[to].name, static_cast<double> (step));
        CHECK (step < 0.01f);
    }

    // Una curva propia que termina en 0.4: pasada la duración la ganancia se queda en 1 − profundidad · 0.6.
    HostClock clock;
    clock.prepare (sampleRate);
    Engine engine;
    TempoSettings settings = rawCurve (2, 0.5f);
    settings.depth = 0.5f;
    engine.prepare (sampleRate, false, settings);
    engine.setCurve (BreakpointCurve { { 0.0f, 0.0f }, { 1.0f, 0.4f } });
    std::vector<float> gain (static_cast<size_t> (beat + 1), 1.0f);
    clock.beginBlock (playingAt (0.0, 120.0), static_cast<int> (gain.size()));
    float* channel = gain.data();
    engine.process (&channel, 1, static_cast<int> (gain.size()), clock);
    CHECK (gain[0] == 0.5f);                                          // golpe, y sin glide al arrancar
    CHECK (std::abs (gain[static_cast<size_t> (0.75 * beat)] - 0.7f) < 1.0e-6f);
    CHECK (std::abs (gain[static_cast<size_t> (beat - 1)] - 0.7f) < 1.0e-6f);
    CHECK (gain[static_cast<size_t> (beat)] == 0.5f);                 // siguiente golpe: salta (lo redondea el suavizado)
}

void testHitsOnTheGrid()
{
    std::printf ("Modo tempo: los golpes caen en la muestra exacta con cualquier bloque y sample rate\n");

    for (const double sampleRate : sampleRates)
    {
        // 120 BPM: una negra = fs / 2 muestras (entero en las 4 frecuencias).
        const int beat = static_cast<int> (sampleRate / 2.0);
        const auto gain = renderSteady (sampleRate, 120.0, rawCurve(), 4 * beat + 10, 4096);

        bool onGrid = true;
        for (int b = 0; b < 4; ++b)
        {
            const auto hit = static_cast<size_t> (b * beat);
            const auto quarter = static_cast<size_t> (beat / 4);
            onGrid = onGrid && gain[hit] == 0.0f;                                     // profundidad 100 % = silencio
            onGrid = onGrid && (b == 0 || gain[hit - 1] == 1.0f);                     // antes del golpe ya había vuelto
            onGrid = onGrid && std::abs (gain[hit + quarter] - 0.5f) < 1.0e-3f;       // a mitad de la bajada, la mitad
            onGrid = onGrid && gain[hit + 2 * quarter + 1] == 1.0f;                   // pasada la duración (50 %), entero
        }
        CHECK (onGrid);

        // El mismo resultado con bloques de 1 a 7 muestras que con bloques grandes.
        const auto small = renderSteady (sampleRate, 120.0, rawCurve(), 4 * beat + 10, 7);
        float difference = 0.0f;
        for (size_t i = 0; i < gain.size(); ++i)
            difference = std::max (difference, std::abs (gain[i] - small[i]));
        CHECK (difference < 1.0e-4f);
    }

    // Tresillo de negra a 48 kHz: un golpe cada 2/3 de negra = 16000 muestras.
    const auto triplets = renderSteady (48000.0, 120.0, rawCurve (6), 48001, 512);
    CHECK (triplets[0] == 0.0f && triplets[16000] == 0.0f && triplets[32000] == 0.0f && triplets[48000] == 0.0f);
    CHECK (triplets[15999] == 1.0f);

    // "1 bar" en 7/8: un golpe cada 3.5 negras = 84000 muestras.
    const auto bars = renderSteady (48000.0, 120.0, rawCurve (0), 84001, 1024, { 7, 8 });
    CHECK (bars[0] == 0.0f && bars[84000] == 0.0f && bars[83999] == 1.0f);
}

void testNoClicksOnChanges()
{
    std::printf ("Modo tempo: cambiar division, duracion o profundidad sonando no hace saltar la ganancia\n");

    constexpr double sampleRate = 48000.0;
    constexpr int beat = 24000; // 120 BPM
    const auto at = [] (double beats) { return static_cast<size_t> (beats * beat); };

    // Sin suavizado, para que solo actúen las rampas y el glide. Cada cambio llega hacia la negra 0.3, lejos de un golpe;
    // se mira hasta justo antes del siguiente golpe de la curva nueva (ese sí es un salto, y es a propósito).
    struct Change
    {
        const char* name;
        void (*apply) (TempoSettings&);
        double nextHit;
    };
    const Change changes[] {
        { "division 1/4 -> 1/2", [] (TempoSettings& s) { s.division = 1; }, 2.0 },
        { "division 1/4 -> 1/16 T", [] (TempoSettings& s) { s.division = 8; }, 1.0 / 3.0 },
        { "duracion 70 % -> 10 %", [] (TempoSettings& s) { s.duration = 0.1f; }, 1.0 },
        { "profundidad 100 % -> 0 %", [] (TempoSettings& s) { s.depth = 0.0f; }, 1.0 },
    };
    for (const auto& change : changes)
    {
        const auto gain = renderGain (sampleRate, 120.0, rawCurve (2, 0.7f), 2 * beat, 64,
                                      [&] (int sample, TempoSettings& s, TransportInfo&) {
                                          if (static_cast<size_t> (sample) >= at (0.3))
                                              change.apply (s);
                                      });
        const float step = maxStep (gain, at (0.25), at (change.nextHit) - 2);
        if (step >= 0.01f)
            std::printf ("  %s: salto de %.4f\n", change.name, static_cast<double> (step));
        CHECK (step < 0.01f);
    }
}

void testRetriggerAndJumps()
{
    std::printf ("Modo tempo: un loop a mitad de curva baja desde la ganancia actual; el suavizado limita cada paso\n");

    constexpr double sampleRate = 48000.0;
    TempoSettings settings = rawCurve (2, 1.0f);
    settings.depth = 0.8f;
    settings.smoothingMs = 5.0f;
    const float coef = static_cast<float> (1.0 - std::exp (-3.0 / (0.005 * sampleRate)));

    // Loop de FL en la negra 1.6 (a mitad de la subida): vuelve a 0, donde hay un golpe.
    const int jumpAt = static_cast<int> (1.6 * 24000);
    int jumpedAt = -1;
    const auto gain = renderGain (sampleRate, 120.0, settings, 3 * 24000, 256, [&] (int sample, TempoSettings&, TransportInfo& info) {
        if (jumpedAt < 0 && sample >= jumpAt)
        {
            info.ppqPosition = 0.0;
            jumpedAt = sample;
        }
    });
    CHECK (jumpedAt > 0);
    CHECK (maxStep (gain, 1, gain.size()) <= coef * 1.0001f);
    // El golpe del loop arranca donde estaba la ganancia (a medio subir), no desde arriba, y baja.
    const auto jump = static_cast<size_t> (jumpedAt);
    CHECK (gain[jump - 1] > 0.3f && gain[jump - 1] < 0.95f);
    CHECK (gain[jump + 480] < gain[jump - 1] - 0.2f);
}

void testStoppedAndFreeRunning()
{
    std::printf ("Modo tempo: parado = sin ducking (sin clic al parar), sin host = reloj libre a 120 BPM\n");

    constexpr double sampleRate = 44100.0;
    const int glideSamples = static_cast<int> (std::lround (Engine::glideSeconds * sampleRate));

    // Se para hacia la negra 0.2 (con el volumen abajo) y sigue parado: la ganancia sube en el glide y queda en 1 exacto.
    const auto gain = renderGain (sampleRate, 120.0, rawCurve (2, 0.9f), 44100, 128, [] (int sample, TempoSettings&, TransportInfo& info) {
        if (sample >= 4410)
            info.isPlaying = false;
    });
    CHECK (gain[4000] < 0.5f);
    CHECK (maxStep (gain, 4000, gain.size()) <= 1.0f / static_cast<float> (glideSamples) + 1.0e-3f);
    CHECK (gain[static_cast<size_t> (4410 + 128 + glideSamples)] == 1.0f);
    CHECK (gain.back() == 1.0f);

    // Sin host: el reloj libre arranca en 0 y la curva golpea a 120 BPM.
    HostClock clock;
    clock.prepare (sampleRate);
    Engine engine;
    engine.prepare (sampleRate, false, rawCurve());
    std::vector<float> freeGain (22051, 1.0f);
    for (size_t done = 0; done < freeGain.size(); done += 441)
    {
        const int numSamples = static_cast<int> (std::min<size_t> (441, freeGain.size() - done));
        clock.beginBlock ({}, numSamples);
        float* channel = freeGain.data() + done;
        engine.process (&channel, 1, numSamples, clock);
    }
    CHECK (freeGain[0] == 0.0f && freeGain[22050] == 0.0f && freeGain[22049] == 1.0f);
}

void testEnginePassthrough()
{
    std::printf ("Motor: profundidad 0 y bypass dejan la senal bit a bit, tambien al conmutar el bypass\n");

    for (const double sampleRate : sampleRates)
        for (const int channels : { 1, 2 })
            for (const bool bypassed : { false, true })
            {
                // Profundidad 0: la curva corre pero no toca nada. Con bypass: profundidad 100 % y la señal pasa igual.
                TempoSettings settings = rawCurve();
                settings.depth = bypassed ? 1.0f : 0.0f;

                HostClock clock;
                clock.prepare (sampleRate);
                Engine engine;
                engine.prepare (sampleRate, bypassed, settings);

                BlockSizes sizes;
                std::uint32_t noise = 1;
                double ppq = 0.0;
                bool identical = true;
                for (int block = 0; block < 400; ++block)
                {
                    const int numSamples = sizes.next (1024);
                    std::vector<float> left (static_cast<size_t> (numSamples)), right (left.size());
                    for (size_t i = 0; i < left.size(); ++i)
                    {
                        noise = noise * 1664525u + 1013904223u;
                        left[i] = static_cast<float> (static_cast<double> (noise >> 8) / 8388608.0 - 1.0);
                        right[i] = -0.5f * left[i];
                    }
                    const auto originalLeft = left;
                    const auto originalRight = right;

                    // Sin ducking, el bypass puede ir y venir: el fundido mezcla dos señales idénticas.
                    if (! bypassed && block % 37 == 0)
                        engine.setBypassed (block % 74 == 0);

                    clock.beginBlock (playingAt (ppq, 97.0), numSamples);
                    ppq += numSamples * 97.0 / (60.0 * sampleRate);
                    engine.setSettings (settings);
                    std::array<float*, 2> pointers { left.data(), right.data() };
                    engine.process (pointers.data(), channels, numSamples, clock);
                    identical = identical && left == originalLeft && right == originalRight;
                }
                CHECK (identical);
            }
}

void testTriggerHelpers()
{
    std::printf ("Disparos: filtro de nota y nombres como en FL (C5 = 60)\n");

    CHECK (noteFilterAccepts (0, 36) && noteFilterAccepts (0, 127));      // 0 = todas
    CHECK (noteFilterAccepts (61, 60) && ! noteFilterAccepts (61, 62));   // 61 = solo la nota 60
    CHECK (flNoteName (60) == "C5 (60)");
    CHECK (flNoteName (69) == "A5 (69)");
    CHECK (flNoteName (37) == "C#3 (37)");
    CHECK (flNoteName (0) == "C0 (0)" && flNoteName (127) == "G10 (127)");
    CHECK (triggerModeFromIndex (0) == TriggerMode::tempo && triggerModeFromIndex (1) == TriggerMode::midi);
    CHECK (triggerModeFromIndex (2) == TriggerMode::audio && triggerModeFromIndex (7) == TriggerMode::tempo);
    CHECK (! isHitMode (TriggerMode::tempo) && isHitMode (TriggerMode::midi) && isHitMode (TriggerMode::audio));
}

void testMidiHits()
{
    std::printf ("Modo MIDI: cada nota dispara en su muestra exacta (dembow), con cualquier bloque y sample rate\n");

    for (const double sampleRate : sampleRates)
    {
        // 95 BPM y un kick de dembow desplazado: pasos 1, 7, 11 y 15 del compás (0, 1.5, 2.5 y 3.5 negras).
        // Largo 1/8 al 100 %: la curva dura media negra, menos que la distancia entre golpes, así que vuelve a 1.
        const double beat = sampleRate * 60.0 / 95.0;
        std::vector<Note> notes;
        for (const double at : { 0.0, 1.5, 2.5, 3.5 })
            notes.push_back ({ 1000 + static_cast<int> (std::lround (at * beat)) });
        const int total = notes.back().sample + static_cast<int> (beat);
        const auto gain = renderMidi (sampleRate, 95.0, midiCurve (3, 1.0f), total, 4096, notes);

        bool exact = gain[999] == 1.0f; // antes de la primera nota no hay ducking
        for (const auto& note : notes)
        {
            const auto hit = static_cast<size_t> (note.sample);
            const auto quarterCurve = static_cast<size_t> (std::lround (beat * 0.125));
            exact = exact && gain[hit] == 0.0f && gain[hit - 1] == 1.0f;
            exact = exact && std::abs (gain[hit + quarterCurve] - defaultCurve().valueAt (0.25f)) < 2.0e-3f;
            exact = exact && gain[hit + static_cast<size_t> (beat * 0.5) + 2] == 1.0f; // pasado el largo, entero
        }
        CHECK (exact);

        // Igual con bloques de 1 a 7 muestras.
        const auto small = renderMidi (sampleRate, 95.0, midiCurve (3, 1.0f), total, 7, notes);
        float difference = 0.0f;
        for (size_t i = 0; i < gain.size(); ++i)
            difference = std::max (difference, std::abs (gain[i] - small[i]));
        CHECK (difference < 1.0e-4f);
    }

    // En modo tempo las notas no hacen nada: la salida es idéntica a la de renderGain.
    const std::vector<Note> notes { { 300 }, { 7000 }, { 15000 } };
    const auto tempo = renderMidi (48000.0, 120.0, rawCurve(), 30000, 512, notes);
    CHECK (tempo == renderSteady (48000.0, 120.0, rawCurve(), 30000, 512));

    // Velocidad: con la opción encendida, una nota a velocidad 0.5 baja la mitad.
    TempoSettings velocity = midiCurve();
    velocity.velocitySensitive = true;
    const auto soft = renderMidi (48000.0, 120.0, velocity, 20000, 256, { { 100, 0.5f }, { 10000, 1.0f } });
    CHECK (soft[100] == 0.5f && soft[10000] == 0.0f);
    const auto flat = renderMidi (48000.0, 120.0, midiCurve(), 20000, 256, { { 100, 0.5f } });
    CHECK (flat[100] == 0.0f);
}

void testMidiRetriggerAndChanges()
{
    std::printf ("Modo MIDI: redisparo desde la ganancia actual; cambiar modo, largo o velocidad sonando sin saltos\n");

    constexpr double sampleRate = 48000.0;
    constexpr int beat = 24000; // 120 BPM

    // Redisparo a mitad de la subida, con suavizado: arranca donde estaba la ganancia y cada paso queda limitado.
    TempoSettings smooth = midiCurve (2, 1.0f);
    smooth.depth = 0.8f;
    smooth.smoothingMs = 5.0f;
    const float coef = static_cast<float> (1.0 - std::exp (-3.0 / (0.005 * sampleRate)));
    const int second = static_cast<int> (0.6 * beat);
    const auto gain = renderMidi (sampleRate, 120.0, smooth, 2 * beat, 256, { { 0 }, { second } });
    CHECK (maxStep (gain, 1, gain.size()) <= coef * 1.0001f);
    CHECK (gain[static_cast<size_t> (second - 1)] > 0.3f && gain[static_cast<size_t> (second - 1)] < 0.95f);
    CHECK (gain[static_cast<size_t> (second + 480)] < gain[static_cast<size_t> (second - 1)] - 0.2f);

    // Cambios con una curva sonando (nota en 0, cambio hacia la negra 0.3), sin suavizado: solo actúa el glide.
    struct Change
    {
        const char* name;
        TempoSettings start;
        void (*apply) (TempoSettings&);
    };
    TempoSettings velocityOn = midiCurve (2, 0.9f);
    velocityOn.velocitySensitive = true;
    const Change changes[] {
        { "largo 1/4 -> 1/16", midiCurve (2, 0.9f), [] (TempoSettings& s) { s.division = 4; } },
        { "largo 1/4 -> 1 bar", midiCurve (2, 0.9f), [] (TempoSettings& s) { s.division = 0; } },
        { "MIDI -> tempo", midiCurve (2, 0.9f), [] (TempoSettings& s) { s.mode = TriggerMode::tempo; } },
        { "tempo -> MIDI", rawCurve (2, 0.9f), [] (TempoSettings& s) { s.mode = TriggerMode::midi; } },
        { "velocidad on -> off", velocityOn, [] (TempoSettings& s) { s.velocitySensitive = false; } },
    };
    for (const auto& change : changes)
    {
        const auto changed = renderMidi (sampleRate, 120.0, change.start, beat - 10, 64, { { 0, 0.4f } },
                                         [&] (int sample, TempoSettings& s, TransportInfo&) {
                                             if (sample >= static_cast<int> (0.3 * beat))
                                                 change.apply (s);
                                         });
        const float step = maxStep (changed, static_cast<size_t> (0.25 * beat), changed.size());
        if (step >= 0.01f)
            std::printf ("  %s: salto de %.4f\n", change.name, static_cast<double> (step));
        CHECK (step < 0.01f);
    }
}

void testMidiHoldAndTransport()
{
    std::printf ("Modo MIDI: curva terminada no revive, notas con FL parado, parar suelta el golpe sin clic\n");

    constexpr double sampleRate = 44100.0;
    const int glideSamples = static_cast<int> (std::lround (Engine::glideSeconds * sampleRate));

    // Una curva que termina en 0.4 (se queda a 1 − 0.6 = 0.4 de ganancia con profundidad 100 %). Terminada la de 1/16,
    // pasar a "1 bar" no la hace sonar de nuevo a medias: sigue quieta en 0.4.
    HostClock clock;
    clock.prepare (sampleRate);
    Engine engine;
    TempoSettings settings = midiCurve (4, 1.0f);
    engine.prepare (sampleRate, false, settings);
    engine.setCurve (BreakpointCurve { { 0.0f, 0.0f }, { 1.0f, 0.4f } });
    std::vector<float> held (20000, 1.0f);
    const Trigger hit[] { { 0, 1.0f } };
    double ppq = 0.0;
    for (size_t done = 0; done < held.size(); done += 500)
    {
        if (done == 10000)
            settings.division = 0;
        clock.beginBlock (playingAt (ppq, 120.0), 500);
        ppq += 500 * 120.0 / (60.0 * sampleRate);
        engine.setSettings (settings);
        float* channel = held.data() + done;
        engine.process (&channel, 1, 500, clock, done == 0 ? std::span<const Trigger> (hit) : std::span<const Trigger>());
    }
    CHECK (held[0] == 0.0f);
    CHECK (std::abs (held[9999] - 0.4f) < 1.0e-6f && std::abs (held.back() - 0.4f) < 1.0e-6f);

    // Con FL parado las notas disparan igual (al último tempo conocido): 1/8 a 120 BPM = 11025 muestras.
    const auto stopped = renderMidi (sampleRate, 120.0, midiCurve (3, 1.0f), 30000, 512, { { 5000 } },
                                     [] (int, TempoSettings&, TransportInfo& info) { info.isPlaying = false; });
    CHECK (stopped[4999] == 1.0f && stopped[5000] == 0.0f);
    CHECK (stopped[5000 + 5512] > 0.3f && stopped[5000 + 5512] < 0.7f);
    CHECK (stopped[5000 + 11025 + 1] == 1.0f);

    // Parar FL con una curva sonando: el golpe se suelta con el glide (sin clic) y queda en 1 exacto.
    const auto release = renderMidi (sampleRate, 120.0, midiCurve (1, 1.0f), 44100, 128, { { 0 } },
                                     [] (int sample, TempoSettings&, TransportInfo& info) {
                                         if (sample >= 4410)
                                             info.isPlaying = false;
                                     });
    CHECK (release[4000] < 0.5f);
    CHECK (maxStep (release, 4000, release.size()) <= 1.0f / static_cast<float> (glideSamples) + 1.0e-3f);
    CHECK (release[static_cast<size_t> (4410 + 128 + glideSamples)] == 1.0f);
}

// --- Fase 5: modo audio y lookahead ----------------------------------------------------------------------------------

// Kick sintético para el sidechain: un coseno de 55 Hz (empieza en su pico, así el golpe empieza en la muestra exacta)
// que se apaga con τ = 60 ms, en cada una de las muestras 'onsets'.
std::vector<float> kickTrack (double sampleRate, int totalSamples, const std::vector<int>& onsets)
{
    std::vector<float> track (static_cast<size_t> (totalSamples), 0.0f);
    for (const int onset : onsets)
        for (int i = onset; i < totalSamples; ++i)
        {
            const double t = static_cast<double> (i - onset) / sampleRate;
            track[static_cast<size_t> (i)] += static_cast<float> (0.9 * std::cos (2.0 * 3.141592653589793 * 55.0 * t) * std::exp (-t / 0.06));
        }
    return track;
}

TempoSettings audioCurve (int division = defaultDuckDivision, float duration = 0.5f)
{
    TempoSettings settings = rawCurve (division, duration);
    settings.mode = TriggerMode::audio;
    return settings;
}

// Como el procesador: en cada bloque el detector recorre el sidechain y sus disparos van al motor, que procesa 'main'
// (mono) con el lookahead dado. Con 'main' constante 1 la salida es la ganancia.
std::vector<float> renderAudio (double sampleRate, double bpm, const TempoSettings& settings, std::vector<float> main,
                                const std::vector<float>& sidechain, int maxBlock, int lookahead = 0)
{
    HostClock clock;
    clock.prepare (sampleRate);
    Engine engine;
    engine.prepare (sampleRate, false, settings, lookahead);
    deepwater::dsp::TransientDetector detector;
    detector.prepare (sampleRate);
    detector.setThresholdDb (-18.0f);
    detector.setHoldSeconds (0.03);

    std::array<Trigger, 256> triggers {};
    BlockSizes sizes;
    const double perSample = bpm / (60.0 * sampleRate);
    double ppq = 0.0;
    const int totalSamples = static_cast<int> (main.size());
    for (int done = 0; done < totalSamples;)
    {
        const int numSamples = std::min (sizes.next (maxBlock), totalSamples - done);
        const float* scChannel = sidechain.data() + done;
        const auto detection = detectAudioTriggers (detector, &scChannel, 1, numSamples, triggers);
        clock.beginBlock (playingAt (ppq, bpm), numSamples);
        engine.setSettings (settings);
        float* channel = main.data() + done;
        engine.process (&channel, 1, numSamples, clock, std::span<const Trigger> (triggers.data(), static_cast<size_t> (detection.stored)));
        ppq += numSamples * perSample;
        done += numSamples;
    }
    return main;
}

void testAudioHits()
{
    std::printf ("Modo audio: cada kick del sidechain dispara en su muestra exacta (dembow), con cualquier bloque y sample rate\n");

    for (const double sampleRate : sampleRates)
    {
        // El mismo patrón que en el modo MIDI, pero ahora es audio: pasos 1, 7, 11 y 15 a 95 BPM.
        const double beat = sampleRate * 60.0 / 95.0;
        std::vector<int> onsets;
        for (const double at : { 0.0, 1.5, 2.5, 3.5 })
            onsets.push_back (1000 + static_cast<int> (std::lround (at * beat)));
        const int total = onsets.back() + static_cast<int> (beat);
        const auto kicks = kickTrack (sampleRate, total, onsets);
        const std::vector<float> ones (static_cast<size_t> (total), 1.0f);
        const auto gain = renderAudio (sampleRate, 95.0, audioCurve (3, 1.0f), ones, kicks, 4096);

        bool exact = gain[999] == 1.0f; // antes del primer kick no hay ducking
        for (const int onset : onsets)
        {
            const auto hit = static_cast<size_t> (onset);
            exact = exact && gain[hit] == 0.0f && gain[hit - 1] == 1.0f;
            exact = exact && gain[hit + static_cast<size_t> (beat * 0.5) + 2] == 1.0f; // pasado el largo, entero
        }
        CHECK (exact);

        // Igual con bloques de 1 a 7 muestras.
        CHECK (renderAudio (sampleRate, 95.0, audioCurve (3, 1.0f), ones, kicks, 7) == gain);
    }

    // Sin canales de sidechain (bus desconectado): el detector recibe silencio y no hay golpes.
    deepwater::dsp::TransientDetector detector;
    detector.prepare (48000.0);
    std::array<Trigger, 4> triggers {};
    const auto none = detectAudioTriggers (detector, nullptr, 0, 512, triggers);
    CHECK (none.detected == 0 && none.stored == 0 && none.peakEnvelope == 0.0f);

    // Estéreo: un kick solo en el canal derecho también cuenta (se mira el mayor |x| de los canales).
    std::vector<float> left (2000, 0.0f), right (2000, 0.0f);
    right[300] = 0.8f;
    const std::array<const float*, 2> stereo { left.data(), right.data() };
    const auto found = detectAudioTriggers (detector, stereo.data(), 2, 2000, triggers);
    CHECK (found.detected == 1 && found.stored == 1 && triggers[0].offset == 300);
}

void testLookahead()
{
    std::printf ("Lookahead: retraso exacto (también en bypass), la curva llega antes que el golpe, cambios sin clic\n");

    for (const double sampleRate : sampleRates)
    {
        // Sin ducking, la salida es la entrada retrasada exactamente 'lookahead' muestras, bit a bit.
        const int lookahead = Engine::maxLookaheadSamples (sampleRate);
        std::vector<float> input (8000);
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = std::sin (static_cast<float> (i) * 0.05f) * 0.5f + static_cast<float> (i % 7) * 0.01f;
        TempoSettings flat = rawCurve();
        flat.depth = 0.0f;
        for (const bool bypassed : { false, true })
        {
            HostClock clock;
            clock.prepare (sampleRate);
            Engine engine;
            engine.prepare (sampleRate, bypassed, flat, lookahead);
            auto output = input;
            BlockSizes sizes;
            for (int done = 0; done < static_cast<int> (output.size());)
            {
                const int numSamples = std::min (sizes.next (300), static_cast<int> (output.size()) - done);
                clock.beginBlock (playingAt (0.0, 120.0), numSamples);
                engine.setSettings (flat);
                float* channel = output.data() + done;
                engine.process (&channel, 1, numSamples, clock);
                done += numSamples;
            }
            bool delayed = true;
            for (size_t i = 0; i < output.size(); ++i)
                delayed = delayed && output[i] == (i < static_cast<size_t> (lookahead) ? 0.0f : input[i - static_cast<size_t> (lookahead)]);
            CHECK (delayed);
        }

        // Modo audio con lookahead: el kick del sidechain dispara la curva en su muestra, pero la señal principal (alineada
        // con el kick) sale 'lookahead' muestras después: cuando llega, la ganancia ya está abajo (con 2 ms de suavizado,
        // casi entera). Sin lookahead llega con la ganancia aún a medio camino.
        const int la = static_cast<int> (std::lround (0.005 * sampleRate));
        const int onset = 3000;
        const auto kick = kickTrack (sampleRate, 20000, { onset });
        std::vector<float> marker (20000, 0.0f);
        marker[static_cast<size_t> (onset)] = 1.0f; // la señal principal: un impulso alineado con el kick
        TempoSettings soft = audioCurve (2, 1.0f);
        soft.smoothingMs = 2.0f;
        const auto ahead = renderAudio (sampleRate, 120.0, soft, marker, kick, 512, la);
        const auto late = renderAudio (sampleRate, 120.0, soft, marker, kick, 512, 0);
        CHECK (ahead[static_cast<size_t> (onset + la)] < 0.01f);
        CHECK (late[static_cast<size_t> (onset)] > 0.5f);
    }

    // Cambiar el lookahead sonando (un seno de 100 Hz, de 0 a 5 ms = medio ciclo: de golpe saltaría hasta 2): el fundido
    // cruzado lo deja sin clic, y al terminar la salida es la entrada retrasada 5 ms exactos.
    constexpr double sampleRate = 48000.0;
    const auto sine = [] (size_t i) { return static_cast<float> (std::sin (2.0 * 3.141592653589793 * 100.0 * static_cast<double> (i) / sampleRate)); };
    std::vector<float> signal (24000);
    for (size_t i = 0; i < signal.size(); ++i)
        signal[i] = sine (i);
    TempoSettings flat = rawCurve();
    flat.depth = 0.0f;
    HostClock clock;
    clock.prepare (sampleRate);
    Engine engine;
    engine.prepare (sampleRate, false, flat, 0);
    for (size_t done = 0; done < signal.size(); done += 256)
    {
        if (done >= 4800)
            engine.setLookahead (240);
        const int numSamples = static_cast<int> (std::min<size_t> (256, signal.size() - done));
        clock.beginBlock (playingAt (0.0, 120.0), numSamples);
        engine.setSettings (flat);
        float* channel = signal.data() + done;
        engine.process (&channel, 1, numSamples, clock);
    }
    const float sineStep = static_cast<float> (2.0 * 3.141592653589793 * 100.0 / sampleRate); // paso máximo del seno
    CHECK (maxStep (signal, 1, signal.size()) < 2.0f * sineStep);
    bool settled = true;
    for (size_t i = 12000; i < signal.size(); ++i)
        settled = settled && std::abs (signal[i] - sine (i - 240)) < 1.0e-6f;
    CHECK (settled);
}


// Fase 6: procesa una señal mono (tempo, 120 BPM, reproduciendo desde 0) con bloques de tamaño variable.
// 'beforeBlock (muestra, settings)' puede cambiar los ajustes antes de cada bloque.
template <typename BeforeBlock>
std::vector<float> renderSignal (double sampleRate, TempoSettings settings, std::vector<float> signal, int maxBlock,
                                 BeforeBlock beforeBlock, bool bypassed = false)
{
    HostClock clock;
    clock.prepare (sampleRate);
    Engine engine;
    engine.prepare (sampleRate, bypassed, settings);
    BlockSizes sizes;
    const double perSample = 120.0 / (60.0 * sampleRate);
    for (int done = 0; done < static_cast<int> (signal.size());)
    {
        const int numSamples = std::min (sizes.next (maxBlock), static_cast<int> (signal.size()) - done);
        beforeBlock (done, settings);
        clock.beginBlock (playingAt (done * perSample, 120.0), numSamples);
        engine.setSettings (settings);
        float* channel = signal.data() + done;
        engine.process (&channel, 1, numSamples, clock);
        done += numSamples;
    }
    return signal;
}

std::vector<float> renderSignal (double sampleRate, TempoSettings settings, std::vector<float> signal, int maxBlock)
{
    return renderSignal (sampleRate, settings, std::move (signal), maxBlock, [] (int, TempoSettings&) {});
}

std::vector<float> sineWave (double sampleRate, double hz, int totalSamples, float amplitude = 1.0f)
{
    std::vector<float> out (static_cast<size_t> (totalSamples));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = amplitude * static_cast<float> (std::sin (2.0 * 3.141592653589793 * hz * static_cast<double> (i) / sampleRate));
    return out;
}

std::vector<float> noiseSignal (int totalSamples)
{
    std::vector<float> out (static_cast<size_t> (totalSamples));
    std::uint32_t state = 7;
    for (auto& sample : out)
    {
        state = state * 1664525u + 1013904223u;
        sample = static_cast<float> (static_cast<double> (state >> 8) / 8388608.0 - 1.0) * 0.5f;
    }
    return out;
}

float peakIn (const std::vector<float>& signal, size_t from, size_t to)
{
    float peak = 0.0f;
    for (size_t i = from; i < std::min (to, signal.size()); ++i)
        peak = std::max (peak, std::abs (signal[i]));
    return peak;
}

void testBands()
{
    std::printf ("Bandas: Graves / Agudos atenuan solo su banda, sin ducking la suma es plana, bypass bit a bit\n");

    for (const double sampleRate : sampleRates)
    {
        const int beat = static_cast<int> (sampleRate / 2.0);          // 120 BPM
        const auto hit = static_cast<size_t> (beat);                   // golpe de la negra 1
        const auto window = static_cast<size_t> (0.0125 * sampleRate); // 12.5 ms: medio ciclo de 40 Hz, la curva sigue abajo
        const auto before = hit - static_cast<size_t> (beat / 4);
        const int total = 2 * beat;

        // Graves con cruce a 120 Hz: un sub de 40 Hz casi desaparece en el golpe y un 5 kHz ni se entera. Agudos: al revés.
        // Antes del golpe (la curva ya volvió) los dos pasan enteros: graves + agudos suman 1.
        for (const auto band : { DuckBand::low, DuckBand::high })
        {
            TempoSettings settings = rawCurve (2, 0.5f);
            settings.band = band;
            const auto sub = renderSignal (sampleRate, settings, sineWave (sampleRate, 40.0, total), 512);
            const auto air = renderSignal (sampleRate, settings, sineWave (sampleRate, 5000.0, total), 512);
            const auto& ducked = band == DuckBand::low ? sub : air;
            const auto& kept = band == DuckBand::low ? air : sub;
            CHECK (peakIn (ducked, hit, hit + window) < 0.05f);
            CHECK (peakIn (kept, hit, hit + window) > 0.97f);
            CHECK (peakIn (sub, before, hit) > 0.97f);
            CHECK (peakIn (air, before, hit) > 0.97f);
        }

        // Sin ducking (profundidad 0) en modo Graves, la salida es la entrada por el allpass del cruce: plana, solo con la
        // fase girada.
        TempoSettings flat = rawCurve();
        flat.depth = 0.0f;
        flat.band = DuckBand::low;
        const auto noise = noiseSignal (total);
        const auto out = renderSignal (sampleRate, flat, noise, 300);
        deepwater::dsp::LinkwitzRileyAllPass reference;
        const auto coefficients = deepwater::dsp::LinkwitzRiley::forFrequency (defaultCrossoverHz, sampleRate);
        float error = 0.0f;
        for (size_t i = 0; i < noise.size(); ++i)
            error = std::max (error, std::abs (out[i] - reference.process (noise[i], coefficients)));
        CHECK (error < 1.0e-5f);

        // Con bypass, la salida es la entrada bit a bit también en modo bandas.
        TempoSettings deep = rawCurve();
        deep.band = DuckBand::low;
        CHECK (renderSignal (sampleRate, deep, noise, 300, [] (int, TempoSettings&) {}, true) == noise);

        // Igual con cualquier tamaño de bloque (el cruce y las bandas avanzan por muestra).
        deep.crossoverHz = 300.0f;
        CHECK (renderSignal (sampleRate, deep, noise, 7) == renderSignal (sampleRate, deep, noise, 4096));
    }
}

void testMix()
{
    std::printf ("Mix: equivale a escalar la profundidad (en Todo y en Graves); 0 = sin ducking; sin saltos\n");

    constexpr double sampleRate = 48000.0;
    const auto noise = noiseSignal (48000);
    for (const auto band : { DuckBand::full, DuckBand::low })
    {
        TempoSettings halfMix = rawCurve (3, 0.8f);
        halfMix.band = band;
        halfMix.depth = 0.8f;
        halfMix.mix = 0.5f;
        halfMix.smoothingMs = 2.0f;
        TempoSettings halfDepth = halfMix;
        halfDepth.depth = 0.4f;
        halfDepth.mix = 1.0f;
        const auto a = renderSignal (sampleRate, halfMix, noise, 512);
        const auto b = renderSignal (sampleRate, halfDepth, noise, 512);
        float error = 0.0f;
        for (size_t i = 0; i < a.size(); ++i)
            error = std::max (error, std::abs (a[i] - b[i]));
        CHECK (error < 1.0e-5f);
    }

    // Mix 0: sin ducking (en Todo, la entrada bit a bit).
    TempoSettings dry = rawCurve();
    dry.mix = 0.0f;
    CHECK (renderSignal (sampleRate, dry, noise, 512) == noise);

    // Bajar el mix sonando no hace saltar la ganancia (rampa de 20 ms).
    const auto gain = renderGain (sampleRate, 120.0, rawCurve (2, 1.0f), 24000, 64, [] (int sample, TempoSettings& s, TransportInfo&) {
        if (sample >= 2000)
            s.mix = 0.0f;
    });
    CHECK (maxStep (gain, 1500, gain.size()) < 0.01f);
}

void testBandChangesWhilePlaying()
{
    std::printf ("Bandas: cambiar de banda o mover el cruce sonando, sin clics\n");

    constexpr double sampleRate = 48000.0;
    constexpr int beat = 24000;
    // Un bajo de 80 Hz (cerca del cruce: ahí la fase de la suma gira más) con el volumen muy abajo durante casi todo el
    // ciclo: el peor caso para pasar de una banda a otra.
    TempoSettings settings = rawCurve (2, 1.0f);
    settings.depth = 0.9f;
    settings.smoothingMs = 2.0f;
    const auto bass = sineWave (sampleRate, 80.0, 2 * beat, 0.5f);
    const auto steady = renderSignal (sampleRate, settings, bass, 256);
    const float reference = maxStep (steady, 1, steady.size());

    // Todo → Graves → Agudos → Todo, cada cambio a mitad de la curva.
    const auto switched = renderSignal (sampleRate, settings, bass, 256, [] (int sample, TempoSettings& s) {
        s.band = sample < 3000 ? DuckBand::full : sample < 8000 ? DuckBand::low : sample < 14000 ? DuckBand::high : DuckBand::full;
    });
    const float switchStep = maxStep (switched, 1, switched.size());
    if (switchStep > 1.5f * reference)
        std::printf ("  cambio de banda: paso de %.4f (sin cambios: %.4f)\n", static_cast<double> (switchStep), static_cast<double> (reference));
    CHECK (switchStep <= 1.5f * reference);

    // Barrer el cruce entre 30 Hz y 2 kHz en modo Graves con ruido: la salida sigue acotada y finita.
    TempoSettings sweep = settings;
    sweep.band = DuckBand::low;
    const auto noise = noiseSignal (2 * beat);
    const auto swept = renderSignal (sampleRate, sweep, noise, 128, [] (int sample, TempoSettings& s) {
        s.crossoverHz = 30.0f * std::pow (2000.0f / 30.0f, std::abs (std::sin (static_cast<float> (sample) * 1.0e-4f)));
    });
    bool bounded = true;
    for (const float sample : swept)
        bounded = bounded && std::isfinite (sample) && std::abs (sample) < 2.0f;
    CHECK (bounded);

    // El cruce llega a su objetivo (rampa en octavas de 20 ms).
    BandDucker ducker;
    ducker.prepare (sampleRate, DuckBand::low, 100.0f);
    ducker.setCrossover (1600.0f);
    for (int i = 0; i < 2000; ++i)
        ducker.advance();
    CHECK (std::abs (ducker.getCrossoverHz() - 1600.0f) < 0.1f);
}

// Fase 7: simula a FL reproduciendo con el visor conectado. Las notas van como en renderMidi; 'beforeBlock' puede cambiar
// el transporte. Devuelve la salida (señal constante 0.5) para compararla con la de sin visor.
template <typename BeforeBlock>
std::vector<float> renderScope (Engine& engine, LiveScope* scope, double sampleRate, double bpm, TempoSettings settings,
                                int totalSamples, int maxBlock, const std::vector<Note>& notes, BeforeBlock beforeBlock)
{
    HostClock clock;
    clock.prepare (sampleRate);
    engine.prepare (sampleRate, false, settings);
    engine.setScope (scope);
    if (scope != nullptr)
        scope->reset();

    std::vector<float> out (static_cast<size_t> (totalSamples), 0.5f);
    std::vector<Trigger> triggers;
    BlockSizes sizes;
    const double perSample = bpm / (60.0 * sampleRate);
    double ppq = 0.0;
    for (int done = 0; done < totalSamples;)
    {
        const int numSamples = std::min (sizes.next (maxBlock), totalSamples - done);
        auto info = playingAt (ppq, bpm);
        beforeBlock (done, info);
        if (info.ppqPosition)
            ppq = *info.ppqPosition;

        triggers.clear();
        for (const auto& note : notes)
            if (note.sample >= done && note.sample < done + numSamples)
                triggers.push_back ({ note.sample - done, note.velocity });

        clock.beginBlock (info, numSamples);
        if (scope != nullptr)
            scope->beginBlock (clock, numSamples);
        engine.setSettings (settings);
        float* channel = out.data() + done;
        engine.process (&channel, 1, numSamples, clock, triggers);

        if (info.isPlaying)
            ppq += numSamples * perSample;
        done += numSamples;
    }
    return out;
}

void testLiveView()
{
    std::printf ("Visor en vivo: columnas ancladas al compas, golpes marcados, cursor de la curva, sin cambiar el sonido\n");

    constexpr double sampleRate = 48000.0;
    constexpr int beat = 24000; // 120 BPM
    const auto noChange = [] (int, TransportInfo&) {};
    const auto columnOf = [] (double beatsInWindow, double windowBeats) {
        return static_cast<int> (beatsInWindow / windowBeats * LiveScope::numColumns);
    };

    // Modo tempo 1/4, 2 compases desde el 0: 8 golpes, uno al principio de cada tiempo, y el visor no cambia el sonido.
    {
        Engine withScope, without;
        LiveScope scope;
        const auto seen = renderScope (withScope, &scope, sampleRate, 120.0, rawCurve(), 8 * beat + 100, 1024, {}, noChange);
        const auto plain = renderScope (without, nullptr, sampleRate, 120.0, rawCurve(), 8 * beat + 100, 1024, {}, noChange);
        CHECK (seen == plain);
        CHECK (withScope.hitCount() == 9); // 8 tiempos + el inicio del compás 3 (las últimas 100 muestras)

        bool marks = true;
        for (int c = 0; c < LiveScope::numColumns; ++c)
            marks = marks && scope.column (c).hit == (c % (LiveScope::numColumns / 4) == 0);
        CHECK (marks);
        // En el golpe la ganancia es 0 (profundidad 100 %); pasada la duración (media negra) vuelve a 1. La entrada es la
        // señal constante 0.5 y la salida, 0.5 × ganancia.
        CHECK (scope.column (0).gain == 0.0f && scope.column (0).input == 0.5f && scope.column (0).output < 0.1f);
        CHECK (scope.column (columnOf (0.75, 4.0)).gain == 1.0f);
        CHECK (scope.column (columnOf (0.75, 4.0)).sidechain == 0.0f);
    }

    // Curva a medias: 1/4 con duración 50 % (media negra de largo), parando a un cuarto de negra = mitad de la curva.
    {
        Engine engine;
        renderScope (engine, nullptr, sampleRate, 120.0, rawCurve(), beat / 4, 64, {}, noChange);
        CHECK (std::abs (engine.curvePosition() - 0.5f) < 1.0e-3f);
        // Pasada la duración se queda en 1 (el último punto) hasta el golpe siguiente.
        renderScope (engine, nullptr, sampleRate, 120.0, rawCurve(), 3 * beat / 4, 64, {}, noChange);
        CHECK (engine.curvePosition() == 1.0f);
    }

    // Empezar a mitad de un tiempo no es un golpe (la curva no empieza); un loop de 3.7 a 0.3 negras tampoco. El primer
    // golpe es el del tiempo 2.
    {
        Engine engine;
        int jumps = 0;
        renderScope (engine, nullptr, sampleRate, 120.0, rawCurve(), beat, 256, {}, [&jumps] (int done, TransportInfo& info) {
            if (done == 0)
                info.ppqPosition = 0.3;
            else if (*info.ppqPosition > 1.2 && jumps++ == 0)
                info.ppqPosition = 0.3;
        });
        CHECK (engine.hitCount() == 1);
    }

    // MIDI: un dembow desplazado (0, 1.5, 2.5 y 3.5 negras) con 2 compases en la ventana: el compás 1 cae en la primera
    // mitad y cada golpe en su columna.
    {
        Engine engine;
        LiveScope scope;
        scope.setBars (2);
        std::vector<Note> notes;
        for (const double at : { 0.0, 1.5, 2.5, 3.5 })
            notes.push_back ({ static_cast<int> (at * beat) });
        renderScope (engine, &scope, sampleRate, 120.0, midiCurve (3, 1.0f), 4 * beat, 512, notes, noChange);
        CHECK (engine.hitCount() == 4);
        int marked = 0;
        for (int c = 0; c < LiveScope::numColumns; ++c)
            marked += scope.column (c).hit ? 1 : 0;
        CHECK (marked == 4);
        // La posición del host se acumula en coma flotante: un golpe justo en el borde puede caer en la columna anterior.
        for (const double at : { 0.0, 1.5, 2.5, 3.5 })
            CHECK (scope.column (columnOf (at, 8.0)).hit || scope.column (columnOf (at, 8.0) - 1).hit);
        CHECK (std::abs (scope.getCursor() - 0.5f) < 1.0e-3f);
    }

    // Ventana de 2 compases en el compás 4 (ppq 12–16): es la 2.ª mitad de la ventana (compases 3–4).
    {
        LiveScope scope;
        scope.setBars (2);
        scope.reset();
        HostClock clock;
        clock.prepare (sampleRate);
        auto info = playingAt (13.0, 120.0);
        info.ppqLastBarStart = 12.0;
        clock.beginBlock (info, 64);
        scope.beginBlock (clock, 64);
        CHECK (scope.columnAt (0) == columnOf (5.0, 8.0));
        scope.addSidechain (0, 0.8f);
        scope.addSidechain (1, 0.3f);
        CHECK (scope.column (columnOf (5.0, 8.0)).sidechain == 0.8f);
        CHECK (LiveScope::validBars (3) == 4 && LiveScope::validBars (0) == 1 && LiveScope::validBars (9) == 4);
    }

    // Host parado: el barrido sigue al tempo conocido (para ver golpes tocados en vivo).
    {
        LiveScope scope;
        scope.reset();
        HostClock clock;
        clock.prepare (sampleRate);
        TransportInfo stopped = playingAt (2.0, 120.0);
        stopped.isPlaying = false;
        clock.beginBlock (stopped, beat);
        scope.beginBlock (clock, beat);
        const float first = scope.getCursor();
        clock.beginBlock (stopped, beat);
        scope.beginBlock (clock, beat);
        CHECK (std::abs (first - 0.25f) < 1.0e-4f && std::abs (scope.getCursor() - 0.5f) < 1.0e-4f);
    }
}

// --- Fase 9: el plugin entero sin JUCE (CPU y huella del sonido) --------------------------------------------------------
//
// ProcessorRig hace lo mismo que EbbAudioProcessor::processBlock, en el mismo orden: reloj → ajustes → disparos MIDI →
// detector del sidechain (siempre) → visor (si la GUI está abierta) → motor → sidechain al visor. Sirve para medir la CPU
// de lo que corre de verdad en el hilo de audio y para sacar una "huella" (un hash) del sonido de muchos escenarios, que
// tiene que ser idéntica antes y después de optimizar.

// Ruido blanco repetible en [−amplitude, amplitude].
std::vector<float> noise (size_t count, float amplitude, std::uint32_t seed)
{
    std::vector<float> out (count);
    for (auto& sample : out)
    {
        seed = seed * 1664525u + 1013904223u;
        sample = amplitude * (static_cast<float> (seed >> 8) / 8388608.0f - 1.0f);
    }
    return out;
}

// Un dembow con el kick desplazado a 120 BPM, en semicorcheas del compás: 1, 4, 5, 8, 9, 12, 13 y 15 (sirve de notas MIDI
// y de onsets del kick del sidechain). Muestras absolutas hasta 'totalSamples'.
std::vector<int> dembowOnsets (double sampleRate, int totalSamples)
{
    constexpr std::array<int, 8> steps { 0, 3, 4, 7, 8, 11, 12, 14 };
    const double sixteenth = sampleRate * 0.125; // 120 BPM
    std::vector<int> onsets;
    for (int bar = 0;; ++bar)
        for (const int step : steps)
        {
            const auto sample = static_cast<int> (std::lround ((bar * 16 + step) * sixteenth));
            if (sample >= totalSamples)
                return onsets;
            onsets.push_back (sample);
        }
}

// Kick seco (55 Hz, caída de 25 ms): baja del rearme del detector antes del siguiente golpe del dembow.
std::vector<float> dryKicks (double sampleRate, int totalSamples, const std::vector<int>& onsets)
{
    std::vector<float> track (static_cast<size_t> (totalSamples), 0.0f);
    const int length = static_cast<int> (0.12 * sampleRate);
    for (const int onset : onsets)
        for (int i = onset; i < std::min (totalSamples, onset + length); ++i)
        {
            const double t = static_cast<double> (i - onset) / sampleRate;
            track[static_cast<size_t> (i)] += static_cast<float> (0.8 * std::sin (2.0 * 3.141592653589793 * 55.0 * t) * std::exp (-t / 0.025));
        }
    return track;
}

// Curva propia con el máximo de puntos (32), en zigzag con tensiones: la búsqueda del segmento más larga.
BreakpointCurve zigzagCurve()
{
    std::array<deepwater::dsp::CurvePoint, BreakpointCurve::maxPoints> points {};
    for (int i = 0; i < BreakpointCurve::maxPoints; ++i)
    {
        const float x = static_cast<float> (i) / static_cast<float> (BreakpointCurve::maxPoints - 1);
        points[static_cast<size_t> (i)] = { x, (i % 2 == 0 ? x : x * 0.5f), (i % 3 - 1) * 0.5f };
    }
    return BreakpointCurve (points.data(), static_cast<int> (points.size()));
}

// El host simulado: tempo, compás, posición y si reproduce.
struct HostSim
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    double ppq = 0.0;
    bool playing = true;
    TimeSignature signature;

    TransportInfo info() const
    {
        TransportInfo t = playingAt (ppq, bpm, signature);
        t.isPlaying = playing;
        return t;
    }
    void advance (int numSamples) noexcept
    {
        if (playing)
            ppq += numSamples * bpm / (60.0 * sampleRate);
    }
};

class ProcessorRig
{
    double sampleRate;
    bool scopeOn;
    HostClock clock;
    Engine engine;
    deepwater::dsp::TransientDetector detector;
    LiveScope scope;
    std::array<Trigger, 256> audioTriggers {};

public:
    ProcessorRig (double rate, const TempoSettings& initial, int lookaheadSamples, bool liveView)
        : sampleRate (rate), scopeOn (liveView), settings (initial), lookahead (lookaheadSamples)
    {
        clock.prepare (sampleRate);
        engine.prepare (sampleRate, false, settings, lookahead);
        detector.prepare (sampleRate);
        scope.reset();
    }

    TempoSettings settings;
    bool bypassed = false;
    int lookahead = 0;
    float thresholdDb = -24.0f;

    void setCurve (const BreakpointCurve& curve) noexcept { engine.setCurve (curve); }
    void setLookaheadMs (float ms) noexcept
    {
        lookahead = std::clamp (static_cast<int> (std::lround (ms * 0.001 * sampleRate)), 0, Engine::maxLookaheadSamples (sampleRate));
    }

    // Un bloque, como processBlock. 'sidechain' = nullptr: bus desconectado. 'notes' = notas MIDI del bloque (ya con su
    // muestra dentro de él); solo disparan en modo MIDI.
    void process (float* const* channels, int numChannels, const float* sidechain, int numSamples, const TransportInfo& info,
                  std::span<const Trigger> notes)
    {
        clock.beginBlock (info, numSamples);
        engine.setBypassed (bypassed);
        engine.setSettings (settings);
        engine.setLookahead (lookahead);

        detector.setThresholdDb (thresholdDb);
        detector.setHoldSeconds (0.03);
        const bool audioMode = settings.mode == TriggerMode::audio;
        const auto detection = detectAudioTriggers (detector, &sidechain, sidechain != nullptr ? 1 : 0, numSamples,
                                                    audioMode ? std::span (audioTriggers) : std::span<Trigger>());
        const auto midi = settings.mode == TriggerMode::midi ? notes : std::span<const Trigger>();

        if (scopeOn)
            scope.beginBlock (clock, numSamples);
        engine.setScope (scopeOn ? &scope : nullptr);
        engine.process (channels, numChannels, numSamples, clock,
                        audioMode ? std::span<const Trigger> (audioTriggers.data(), static_cast<size_t> (detection.stored)) : midi);
        if (scopeOn && sidechain != nullptr)
            for (int i = 0; i < numSamples; ++i)
                scope.addSidechain (i, std::abs (sidechain[i]));
    }

    const Engine& getEngine() const noexcept { return engine; }
    const LiveScope& getScope() const noexcept { return scope; }
};

// Toda la señal de un escenario: estéreo de ruido, el kick del sidechain y sus notas MIDI.
struct ScenarioSignals
{
    std::vector<float> left, right, sidechain;
    std::vector<Note> notes;

    ScenarioSignals (double sampleRate, int totalSamples)
        : left (noise (static_cast<size_t> (totalSamples), 0.35f, 21u)), right (noise (static_cast<size_t> (totalSamples), 0.35f, 22u))
    {
        const auto onsets = dembowOnsets (sampleRate, totalSamples);
        sidechain = dryKicks (sampleRate, totalSamples, onsets);
        for (size_t i = 0; i < onsets.size(); ++i)
            notes.push_back ({ onsets[i], 0.5f + 0.5f * static_cast<float> (i % 3) / 2.0f });
    }
};

// Las notas que caen en [from, from + numSamples), con su muestra relativa al bloque.
std::span<const Trigger> notesInBlock (const std::vector<Note>& notes, int from, int numSamples, std::vector<Trigger>& out)
{
    out.clear();
    for (const auto& note : notes)
        if (note.sample >= from && note.sample < from + numSamples)
            out.push_back ({ note.sample - from, note.velocity });
    return out;
}

// --- Fase 9: distintos tempos y cambios de tempo en vivo ---------------------------------------------------------------

// Un host cuyo tempo cambia con el tiempo, como la automatización de tempo de FL: el tempo va en línea recta de 'from' a
// 'to' entre 'rampStart' y 'rampEnd' segundos (antes y después, constante). La posición es la integral exacta del tempo
// (lo que hace un host de verdad): al principio de cada bloque informa la posición y el tempo de ese instante, y Ebb
// extrapola dentro del bloque con ese tempo. Un salto de tempo es una rampa de duración 0.
struct TempoRamp
{
    double from = 120.0, to = 120.0;
    double rampStart = 0.0, rampEnd = 0.0;

    [[nodiscard]] double bpmAt (double seconds) const noexcept
    {
        if (seconds <= rampStart)
            return from;
        if (seconds >= rampEnd)
            return to;
        return from + (to - from) * (seconds - rampStart) / (rampEnd - rampStart);
    }

    // Negras desde 0 hasta 'seconds' (área bajo el tempo / 60).
    [[nodiscard]] double beatsAt (double seconds) const noexcept
    {
        const double before = std::min (seconds, rampStart);
        double beats = from * before;
        if (seconds > rampStart)
        {
            const double inRamp = std::min (seconds, rampEnd) - rampStart;
            beats += (from + 0.5 * (bpmAt (rampStart + inRamp) - from)) * inRamp;
        }
        if (seconds > rampEnd)
            beats += to * (seconds - rampEnd);
        return beats / 60.0;
    }

    // Primer instante (en muestras, fraccionario) en que la posición llega a 'beats' (búsqueda binaria: es creciente).
    [[nodiscard]] double sampleOf (double beats, double sampleRate) const noexcept
    {
        double low = 0.0, high = 1.0;
        while (beatsAt (high) < beats)
            high *= 2.0;
        for (int i = 0; i < 200; ++i)
        {
            const double mid = 0.5 * (low + high);
            (beatsAt (mid) < beats ? low : high) = mid;
        }
        return high * sampleRate;
    }
};

struct TempoRender
{
    std::vector<float> gain;
    std::uint32_t hits = 0; // curvas que empezaron, según el motor (lo que cuenta la GUI)
};

// Modo tempo (o MIDI con 'notes') con el host de 'ramp', bloques al azar de 1 a maxBlock. Señal constante 1: la salida
// es la ganancia.
TempoRender renderRamp (double sampleRate, const TempoRamp& ramp, TempoSettings settings, int totalSamples, int maxBlock,
                        const std::vector<Note>& notes = {})
{
    HostClock clock;
    clock.prepare (sampleRate);
    Engine engine;
    engine.prepare (sampleRate, false, settings);

    TempoRender out;
    out.gain.assign (static_cast<size_t> (totalSamples), 1.0f);
    std::vector<Trigger> triggers;
    BlockSizes sizes;
    for (int done = 0; done < totalSamples;)
    {
        const int numSamples = std::min (sizes.next (maxBlock), totalSamples - done);
        const double seconds = done / sampleRate;
        clock.beginBlock (playingAt (ramp.beatsAt (seconds), ramp.bpmAt (seconds)), numSamples);
        engine.setSettings (settings);
        float* channel = out.gain.data() + done;
        engine.process (&channel, 1, numSamples, clock, notesInBlock (notes, done, numSamples, triggers));
        out.hits = engine.hitCount();
        done += numSamples;
    }
    return out;
}

// Mayor salto de ganancia entre muestras seguidas, sin contar las muestras de 'skip' (los golpes: ahí la caída es a
// propósito) ni las 'guard' siguientes.
float maxStepExcept (const std::vector<float>& gain, const std::vector<size_t>& skip, size_t guard)
{
    float largest = 0.0f;
    for (size_t i = 1; i < gain.size(); ++i)
    {
        const bool nearHit = std::any_of (skip.begin(), skip.end(), [&] (size_t s) { return i >= s && i <= s + guard; });
        if (! nearHit)
            largest = std::max (largest, std::abs (gain[i] - gain[i - 1]));
    }
    return largest;
}

// Las muestras donde empieza cada golpe del modo tempo: la ganancia cae de golpe (profundidad 100 %, sin suavizado).
std::vector<size_t> dropsIn (const std::vector<float>& gain)
{
    std::vector<size_t> drops;
    for (size_t i = 1; i < gain.size(); ++i)
        if (gain[i - 1] - gain[i] > 0.5f)
            drops.push_back (i);
    return drops;
}

void testManyTempos()
{
    std::printf ("Tempos: de 60 a 300 BPM (y decimales), los golpes caen en la muestra exacta y la curva dura lo que debe\n");

    constexpr std::array<double, 10> tempos { 60.0, 70.0, 85.5, 93.0, 100.0, 128.0, 140.0, 174.0, 200.0, 300.0 };
    for (const double sampleRate : sampleRates)
        for (const double bpm : tempos)
        {
            // 1/4 con duración 50 %: golpe en cada negra, de vuelta a 1 a mitad de la negra.
            const TempoRamp steady { bpm, bpm };
            const int totalSamples = static_cast<int> (std::ceil (steady.sampleOf (8.0, sampleRate))) + 10; // 8 negras
            const auto render = renderRamp (sampleRate, steady, rawCurve (2, 0.5f), totalSamples, 4096);
            const auto drops = dropsIn (render.gain);

            bool onGrid = drops.size() == 8; // la del 0 no es una caída (empieza abajo)
            for (int beat = 1; beat <= 8 && onGrid; ++beat)
            {
                // La primera muestra cuya posición llega a la negra (con el margen de 1e-9 del redondeo del host).
                const auto expected = static_cast<size_t> (std::ceil (steady.sampleOf (static_cast<double> (beat) - 1.0e-9, sampleRate) - 1.0e-6));
                const auto drop = drops[static_cast<size_t> (beat - 1)];
                onGrid = drop == expected && render.gain[drop - 1] == 1.0f && render.gain[drop] < 0.02f;
            }
            if (! onGrid)
                std::printf ("  %.0f Hz, %.1f BPM: %zu caidas\n", sampleRate, bpm, drops.size());
            CHECK (onGrid);
            CHECK (render.hits == 9); // 0 y las 8 negras

            // En segundos, la vuelta dura 0.5 negras al tempo del host: a 60 BPM, 0.5 s; a 300 BPM, 0.1 s.
            const size_t firstDrop = drops.empty() ? 0 : drops.front();
            size_t back = firstDrop;
            while (back < render.gain.size() && render.gain[back] < 1.0f)
                ++back;
            const double backSeconds = static_cast<double> (back - firstDrop) / sampleRate;
            CHECK (std::abs (backSeconds - 30.0 / bpm) <= 2.0 / sampleRate);
        }

    // MIDI: el LARGO también va al tempo (1/4 con duración 50 % = media negra), a 70 y a 174 BPM.
    for (const double bpm : { 70.0, 174.0 })
    {
        constexpr double sampleRate = 48000.0;
        const int note = 1000;
        const auto render = renderRamp (sampleRate, { bpm, bpm }, midiCurve (2, 0.5f), 96000, 512, { { note } });
        size_t back = static_cast<size_t> (note);
        while (render.gain[back] < 1.0f)
            ++back;
        CHECK (render.gain[static_cast<size_t> (note)] < 0.02f);
        CHECK (std::abs (static_cast<double> (back - static_cast<size_t> (note)) / sampleRate - 30.0 / bpm) <= 2.0 / sampleRate);
    }
}

void testTempoAutomation()
{
    std::printf ("Tempo automatizado (rampas 90 -> 180 y 180 -> 70 BPM): golpes a tiempo, ni perdidos ni dobles, sin clics\n");

    // Rampas largas (un "speed up" de 8 compases) y cortas (en 1 s), subiendo y bajando, con bloques grandes: es lo peor
    // para Ebb, que dentro del bloque extrapola con el tempo del principio.
    const TempoRamp ramps[] {
        { 90.0, 180.0, 0.5, 14.5 },
        { 180.0, 70.0, 0.5, 14.5 },
        { 100.0, 200.0, 1.0, 2.0 },
        { 200.0, 60.0, 1.0, 2.0 },
    };
    for (const double sampleRate : sampleRates)
        for (const auto& ramp : ramps)
            for (const int maxBlock : { 64, 1024, 4096 })
            {
                const int totalSamples = static_cast<int> (16.0 * sampleRate);
                const auto settings = rawCurve (3, 0.6f); // 1/8: muchos golpes durante la rampa
                const auto render = renderRamp (sampleRate, ramp, settings, totalSamples, maxBlock);
                const auto drops = dropsIn (render.gain);

                // Cuántas corcheas pasan en total: los golpes de la curva tienen que ser exactamente esos (más el del 0).
                const double totalBeats = ramp.beatsAt (static_cast<double> (totalSamples - 1) / sampleRate);
                const auto eighths = static_cast<size_t> (std::floor (totalBeats / 0.5));
                const bool countOk = drops.size() == eighths && render.hits == eighths + 1;

                // Cada golpe, cerca del instante exacto de su corchea. Ebb solo sabe el tempo del principio de cada bloque:
                // con una rampa, la posición que extrapola se aleja de la real hasta el bloque siguiente. El error crece
                // con el cuadrado del bloque y con lo empinado de la rampa: con la de 8 compases, 0.3 ms como mucho; con
                // 200 -> 60 BPM en 1 s y bloques de 4096 a 44.1 kHz, 4.5 ms (medido). Con bloques de 64, 0.02 ms.
                double worstMs = 0.0;
                for (size_t k = 0; k < std::min (drops.size(), eighths); ++k)
                {
                    const double exact = ramp.sampleOf (0.5 * static_cast<double> (k + 1), sampleRate);
                    worstMs = std::max (worstMs, std::abs (static_cast<double> (drops[k]) - exact) / sampleRate * 1000.0);
                }
                const float step = maxStepExcept (render.gain, drops, 0);
                const double allowedMs = maxBlock <= 64 ? 0.05 : maxBlock <= 1024 ? 1.0 : 6.0;
                if (! countOk || worstMs > allowedMs || step >= 0.02f)
                    std::printf ("  %.0f Hz, %.0f -> %.0f BPM, bloques <= %d: %zu caidas, %u golpes, %zu corcheas; error %.3f ms; "
                                 "salto %.4f\n",
                                 sampleRate, ramp.from, ramp.to, maxBlock, drops.size(), static_cast<unsigned> (render.hits), eighths,
                                 worstMs, static_cast<double> (step));
                CHECK (countOk);
                CHECK (worstMs <= allowedMs);
                CHECK (step < 0.02f);
            }
}

void testTempoJumpsLive()
{
    std::printf ("Cambio de tempo de golpe sonando (140 -> 70 y 70 -> 140): la curva sigue en su sitio, sin clics\n");

    constexpr double sampleRate = 48000.0;
    for (const auto& [from, to] : { std::pair { 140.0, 70.0 }, std::pair { 70.0, 140.0 } })
        for (const auto mode : { TriggerMode::tempo, TriggerMode::midi })
        {
            // El cambio llega a mitad de una curva (0.3 negras después del golpe de la negra 2).
            const TempoRamp jump { from, from, 0.0, 0.0 };
            const double changeSeconds = jump.sampleOf (2.3, sampleRate) / sampleRate;
            const TempoRamp ramp { from, to, changeSeconds, changeSeconds };
            auto settings = mode == TriggerMode::tempo ? rawCurve (2, 0.9f) : midiCurve (2, 0.9f);

            std::vector<Note> notes;
            for (int beat = 0; beat < 8; ++beat)
                notes.push_back ({ static_cast<int> (std::ceil (ramp.sampleOf (static_cast<double> (beat), sampleRate))) });
            const auto render = renderRamp (sampleRate, ramp, settings, static_cast<int> (8.0 * sampleRate), 256,
                                            mode == TriggerMode::midi ? notes : std::vector<Note> {});
            const auto drops = dropsIn (render.gain);
            const float step = maxStepExcept (render.gain, drops, 0);
            if (step >= 0.01f)
                std::printf ("  %s %.0f -> %.0f BPM: salto %.4f\n", mode == TriggerMode::tempo ? "tempo" : "MIDI", from, to,
                             static_cast<double> (step));
            CHECK (step < 0.01f);
            // Después del cambio, los golpes siguen cayendo en las negras (al tempo nuevo).
            const auto expected = static_cast<size_t> (std::ceil (ramp.sampleOf (4.0 - 1.0e-9, sampleRate) - 1.0e-6));
            CHECK (std::find (drops.begin(), drops.end(), expected) != drops.end());
        }
}

// --- Huella del sonido (EbbTests --fingerprint <archivo>) -------------------------------------------------------------
//
// 7 variantes × 4 sample rates × 6 tamaños de bloque × mono/estéreo = 336 escenarios de 1.2 s. Las variantes de "cambios"
// tocan algo cada 50 ms: división, forma, profundidad, banda, cruce, mix, bypass, suavizado, duración, tempo, un salto de
// loop, parar y arrancar, lookahead, modo, velocidad, compás. De cada escenario se guarda un hash (FNV-1a de 64 bits) de
// los bytes de la salida, de lo que lee la GUI en cada bloque (ganancia mínima, golpes, posición de la curva) y, con el
// visor, de sus columnas. Dos versiones del motor suenan igual si los dos archivos son idénticos.

class Fnv1a
{
public:
    void add (const void* data, size_t size) noexcept
    {
        const auto* bytes = static_cast<const unsigned char*> (data);
        for (size_t i = 0; i < size; ++i)
            hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    template <typename T>
    void add (const T& value) noexcept { add (&value, sizeof (T)); }
    [[nodiscard]] std::uint64_t value() const noexcept { return hash; }

private:
    std::uint64_t hash = 14695981039346656037ull;
};

enum class Variant
{
    steadyTempo,
    steadyMidi,
    steadyAudio,
    lowBandCustom,
    changesTempo,
    changesMidi,
    changesAudio
};

constexpr std::array<const char*, 7> variantNames { "tempo", "midi", "audio", "graves", "cambios-tempo", "cambios-midi", "cambios-audio" };

// El cambio número 'k' de las variantes de "cambios" (se repiten en ciclo).
void applyChange (int k, ProcessorRig& rig, HostSim& host)
{
    auto& s = rig.settings;
    const auto nextMode = [&] { s.mode = triggerModeFromIndex ((static_cast<int> (s.mode) + 1) % 3); };
    switch (k % 24)
    {
        case 0:  s.division = 3; break;
        case 1:  rig.setCurve (curveShapes[4].curve); break;
        case 2:  s.depth = 0.4f; break;
        case 3:  s.band = DuckBand::low; break;
        case 4:  s.crossoverHz = 300.0f; break;
        case 5:  s.mix = 0.6f; break;
        case 6:  rig.bypassed = true; break;
        case 7:  rig.bypassed = false; break;
        case 8:  s.smoothingMs = 0.0f; break;
        case 9:  s.duration = 0.3f; break;
        case 10: host.bpm = 140.0; break;
        case 11: host.ppq = std::max (0.0, host.ppq - 1.5); break;
        case 12: host.playing = false; break;
        case 13: host.playing = true; break;
        case 14: s.band = DuckBand::high; break;
        case 15: rig.setLookaheadMs (5.0f); break;
        case 16: nextMode(); break;
        case 17: s.velocitySensitive = true; break;
        case 18: rig.setCurve (zigzagCurve()); break;
        case 19: s.division = 8; break;
        case 20: host.signature = { 7, 8 }; break;
        case 21: host.bpm = 93.7; s.crossoverHz = 2000.0f; break;
        case 22: nextMode(); break;
        default: rig.setLookaheadMs (0.0f); s.band = DuckBand::full; s.mix = 1.0f; s.depth = 0.9f; break;
    }
}

std::uint64_t fingerprint (Variant variant, double sampleRate, int blockSize, int numChannels)
{
    const int totalSamples = static_cast<int> (1.2 * sampleRate);
    const ScenarioSignals signals (sampleRate, totalSamples);
    auto left = signals.left, right = signals.right;

    TempoSettings settings; // los valores por defecto del plugin
    int lookahead = 0;
    bool liveView = false;
    switch (variant)
    {
        case Variant::steadyMidi:
        case Variant::changesMidi:   settings.mode = TriggerMode::midi; break;
        case Variant::steadyAudio:
            lookahead = static_cast<int> (std::lround (0.003 * sampleRate));
            liveView = true;
            [[fallthrough]];
        case Variant::changesAudio:  settings.mode = TriggerMode::audio; break;
        case Variant::lowBandCustom:
            settings.band = DuckBand::low;
            settings.mix = 0.7f;
            settings.smoothingMs = 5.0f;
            break;
        case Variant::changesTempo:  liveView = true; break;
        case Variant::steadyTempo:   break;
    }
    const bool changes = variant == Variant::changesTempo || variant == Variant::changesMidi || variant == Variant::changesAudio;

    auto rig = std::make_unique<ProcessorRig> (sampleRate, settings, lookahead, liveView);
    if (variant == Variant::lowBandCustom)
        rig->setCurve (zigzagCurve());
    HostSim host;
    host.sampleRate = sampleRate;

    Fnv1a hash;
    std::vector<Trigger> notes;
    const int changeEvery = static_cast<int> (0.05 * sampleRate);
    int nextChange = changeEvery, changeCount = 0;
    for (int done = 0; done < totalSamples;)
    {
        const int numSamples = std::min (blockSize, totalSamples - done);
        if (changes)
            for (; done >= nextChange; nextChange += changeEvery)
                applyChange (changeCount++, *rig, host);

        float* channels[] { left.data() + done, right.data() + done };
        rig->process (channels, numChannels, signals.sidechain.data() + done, numSamples, host.info(),
                      notesInBlock (signals.notes, done, numSamples, notes));
        hash.add (rig->getEngine().blockMinGain());
        hash.add (rig->getEngine().hitCount());
        hash.add (rig->getEngine().curvePosition());
        host.advance (numSamples);
        done += numSamples;
    }
    hash.add (left.data(), left.size() * sizeof (float));
    if (numChannels == 2)
        hash.add (right.data(), right.size() * sizeof (float));
    if (liveView)
        for (int c = 0; c < LiveScope::numColumns; ++c)
        {
            const auto column = rig->getScope().column (c);
            hash.add (column.input);
            hash.add (column.output);
            hash.add (column.sidechain);
            hash.add (column.gain);
            hash.add (column.hit);
        }
    return hash.value();
}

int writeFingerprints (const char* path)
{
    std::ofstream file (path);
    if (! file)
    {
        std::printf ("No se pudo escribir %s\n", path);
        return 1;
    }
    constexpr std::array<int, 6> blockSizes { 1, 7, 64, 256, 333, 4096 };
    int count = 0;
    for (size_t v = 0; v < variantNames.size(); ++v)
        for (const double sampleRate : sampleRates)
            for (const int blockSize : blockSizes)
                for (const int numChannels : { 1, 2 })
                {
                    const auto value = fingerprint (static_cast<Variant> (v), sampleRate, blockSize, numChannels);
                    char line[128];
                    std::snprintf (line, sizeof (line), "%-14s %6.0f Hz  bloque %4d  %d canal(es)  %016llx\n", variantNames[v],
                                   sampleRate, blockSize, numChannels, static_cast<unsigned long long> (value));
                    file << line;
                    ++count;
                }
    std::printf ("%d huellas escritas en %s\n", count, path);
    return 0;
}

// --- Tabla de CPU (EbbTests --cpu) --------------------------------------------------------------------------------------
//
// Cada escenario procesa 2 s (bloques de 256) con el plugin ya "caliente" y se mide 5 veces: se queda la más rápida (los
// demás programas del equipo solo pueden hacer una medida más LENTA). Resultado en "% de un núcleo": cuánto de un núcleo
// hace falta para procesar en tiempo real. Depende del equipo: sirve para comparar antes/después en la misma máquina.

// Como juce::ScopedNoDenormals en processBlock: FTZ + DAZ (los números diminutos se vuelven 0).
class ScopedFlushToZero
{
public:
    ScopedFlushToZero() : previous (_mm_getcsr()) { _mm_setcsr (previous | 0x8040); }
    ~ScopedFlushToZero() { _mm_setcsr (previous); }
    ScopedFlushToZero (const ScopedFlushToZero&) = delete;
    ScopedFlushToZero& operator= (const ScopedFlushToZero&) = delete;

private:
    unsigned int previous;
};

struct CpuScenario
{
    TempoSettings settings;
    int numChannels = 2;
    float lookaheadMs = 0.0f;
    bool liveView = false;
    bool bypassed = false;
    bool customCurve = false;
    bool movingCrossover = false; // el cruce cambia en cada bloque (automatización o una mano en la perilla)
    bool stopped = false;         // host parado
    bool sidechain = true;        // bus del sidechain conectado
};

double scenarioCpu (double sampleRate, const CpuScenario& scenario)
{
    const ScopedFlushToZero noDenormals;
    constexpr double seconds = 2.0;
    constexpr int blockSize = 256;
    const int totalSamples = static_cast<int> (seconds * sampleRate);
    const ScenarioSignals signals (sampleRate, totalSamples);

    auto rig = std::make_unique<ProcessorRig> (sampleRate, scenario.settings, 0, scenario.liveView);
    rig->setLookaheadMs (scenario.lookaheadMs);
    rig->bypassed = scenario.bypassed;
    if (scenario.customCurve)
        rig->setCurve (zigzagCurve());

    std::vector<Trigger> notes;
    notes.reserve (64);
    const auto runOnce = [&] {
        auto left = signals.left, right = signals.right;
        HostSim host;
        host.sampleRate = sampleRate;
        host.playing = ! scenario.stopped;
        const auto start = std::chrono::steady_clock::now();
        for (int done = 0, blockIndex = 0; done < totalSamples; ++blockIndex)
        {
            const int numSamples = std::min (blockSize, totalSamples - done);
            if (scenario.movingCrossover)
                rig->settings.crossoverHz = blockIndex % 2 == 0 ? 100.0f : 160.0f;
            float* channels[] { left.data() + done, right.data() + done };
            rig->process (channels, scenario.numChannels, scenario.sidechain ? signals.sidechain.data() + done : nullptr, numSamples,
                          host.info(), notesInBlock (signals.notes, done, numSamples, notes));
            host.advance (numSamples);
            done += numSamples;
        }
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count() / seconds;
    };
    runOnce(); // calentar
    double best = 1.0e9;
    for (int run = 0; run < 5; ++run)
        best = std::min (best, runOnce());
    return best;
}

void printCpuTable()
{
    std::printf ("Tabla de CPU de Ebb (ruido a -9 dBFS, kick de dembow en el sidechain, bloques de 256; mejor de 5)\n");
    const auto row = [] (const char* label, double sampleRate, const CpuScenario& scenario) {
        std::printf ("  %-72s %4.1f kHz %6.3f %%\n", label, sampleRate / 1000.0, 100.0 * scenarioCpu (sampleRate, scenario));
    };

    const CpuScenario init;
    row ("Init (tempo 1/4, Clasica, Todo; estereo)", 48000.0, init);
    auto mono = init;
    mono.numChannels = 1;
    row ("Init en mono", 48000.0, mono);
    auto stopped = init;
    stopped.stopped = true;
    row ("Init con el host parado", 48000.0, stopped);
    auto unplugged = init;
    unplugged.sidechain = false;
    row ("Init sin sidechain conectado", 48000.0, unplugged);
    auto fast = init;
    fast.settings.division = 8;
    row ("Tempo 1/16 T", 48000.0, fast);
    auto custom = init;
    custom.customCurve = true;
    row ("Curva propia de 32 puntos", 48000.0, custom);
    auto low = init;
    low.settings.band = DuckBand::low;
    row ("Graves (cruce 120 Hz)", 48000.0, low);
    auto moving = low;
    moving.movingCrossover = true;
    row ("Graves con el cruce en movimiento", 48000.0, moving);
    auto mix = init;
    mix.settings.mix = 0.6f;
    row ("Mix 60 %", 48000.0, mix);
    auto midi = init;
    midi.settings.mode = TriggerMode::midi;
    row ("MIDI (dembow)", 48000.0, midi);
    auto audio = init;
    audio.settings.mode = TriggerMode::audio;
    row ("Audio (dembow en el sidechain)", 48000.0, audio);
    auto lookahead = audio;
    lookahead.lookaheadMs = 10.0f;
    row ("Audio + lookahead 10 ms", 48000.0, lookahead);
    auto view = init;
    view.liveView = true;
    row ("Init con la GUI abierta (visor en vivo)", 48000.0, view);
    auto depthZero = init;
    depthZero.settings.depth = 0.0f;
    row ("Profundidad 0", 48000.0, depthZero);

    auto worst = lookahead;
    worst.settings.band = DuckBand::low;
    worst.movingCrossover = true;
    worst.settings.mix = 0.6f;
    worst.customCurve = true;
    worst.liveView = true;
    row ("Peor caso: audio + lookahead + graves moviendose + mix + 32 puntos + GUI", 48000.0, worst);
    row ("Peor caso a 96 kHz", 96000.0, worst);
    auto bypassed = worst;
    bypassed.bypassed = true;
    row ("Peor caso con BYPASS (el motor sigue calculando)", 48000.0, bypassed);
}


} // namespace

int main (int argc, char** argv)
{
    if (argc > 1 && std::strcmp (argv[1], "--cpu") == 0)
    {
        printCpuTable();
        return 0;
    }
    if (argc > 2 && std::strcmp (argv[1], "--fingerprint") == 0)
        return writeFingerprints (argv[2]);

    testTempoCurve();
    testCurveShapes();
    testHitsOnTheGrid();
    testNoClicksOnChanges();
    testRetriggerAndJumps();
    testCurveChangeAndHold();
    testStoppedAndFreeRunning();
    testEnginePassthrough();
    testTriggerHelpers();
    testMidiHits();
    testMidiRetriggerAndChanges();
    testMidiHoldAndTransport();
    testAudioHits();
    testLookahead();
    testBands();
    testMix();
    testBandChangesWhilePlaying();
    testLiveView();
    testManyTempos();
    testTempoAutomation();
    testTempoJumpsLive();

    if (failures == 0)
        std::printf ("\nTodos los tests de Ebb pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);
    return failures == 0 ? 0 : 1;
}
