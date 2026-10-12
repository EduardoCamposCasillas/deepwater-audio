// Tests de Ebb sin JUCE: el reloj musical (posición del host) y el motor de audio. Se lanzan con ctest. Si algo falla,
// imprime el archivo, la línea y la condición.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "deepwater/dsp/TransientDetector.h"
#include "dsp/CurveShapes.h"
#include "dsp/Engine.h"
#include "dsp/HostClock.h"
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

bool samePosition (MusicalPosition p, int bar, int beat, int sixteenth)
{
    return p.bar == bar && p.beat == beat && p.sixteenth == sixteenth;
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

void testMusicalPosition()
{
    std::printf ("Posicion musical: 4/4, 3/4, 6/8, 7/8, inicio de compas del host, redondeo y pre-roll\n");

    // 4/4: 9.5 negras = compás 3 (empieza en 8), tiempo 2, 3.ª semicorchea (media negra dentro del tiempo).
    CHECK (samePosition (musicalPosition (0.0, { 4, 4 }), 1, 1, 1));
    CHECK (samePosition (musicalPosition (9.5, { 4, 4 }), 3, 2, 3));
    CHECK (samePosition (musicalPosition (15.99, { 4, 4 }), 4, 4, 4));
    // 3/4: el compás dura 3 negras.
    CHECK (samePosition (musicalPosition (7.0, { 3, 4 }), 3, 2, 1));
    // 6/8: el compás dura 3 negras y cada tiempo es una corchea (2 semicorcheas).
    CHECK (samePosition (musicalPosition (4.75, { 6, 8 }), 2, 4, 2));
    // 7/8: 3.5 negras por compás.
    CHECK (samePosition (musicalPosition (3.5, { 7, 8 }), 2, 1, 1));
    CHECK (samePosition (musicalPosition (6.75, { 7, 8 }), 2, 7, 2));

    // Paso de FL (semicorchea dentro del compás): la caja del dembow cae en los pasos 4, 7, 12 y 15.
    CHECK (musicalPosition (0.75, { 4, 4 }).step == 4);
    CHECK (musicalPosition (1.5, { 4, 4 }).step == 7);
    CHECK (musicalPosition (2.75, { 4, 4 }).step == 12);
    CHECK (musicalPosition (3.5, { 4, 4 }).step == 15);
    CHECK (musicalPosition (6.75, { 7, 8 }).step == 14); // 7/8 = 14 pasos

    // Un host que entrega 3.9999999999 en vez de 4 (error de coma flotante) sigue estando en el compás 2.
    CHECK (samePosition (musicalPosition (3.9999999999, { 4, 4 }), 2, 1, 1));

    // Si el host dice dónde empezó el compás (p. ej. hubo un cambio de compás antes), se cuenta desde ahí.
    const auto fromHost = musicalPosition (11.25, { 3, 4 }, 10.0);
    CHECK (fromHost.beat == 2 && fromHost.sixteenth == 2);
    // Un inicio de compás incoherente (más de un compás atrás) se ignora.
    CHECK (samePosition (musicalPosition (9.5, { 4, 4 }, 2.0), 3, 2, 3));

    // Pre-roll (posición negativa, antes del compás 1) y compás inválido (denominador 3 → se usa 4/4).
    CHECK (samePosition (musicalPosition (-1.0, { 4, 4 }), 0, 4, 1));
    CHECK (samePosition (musicalPosition (9.5, { 4, 3 }), 3, 2, 3));
}

void testFreeRunningClock()
{
    std::printf ("Reloj libre (sin host): 120 BPM desde 0, continuo con cualquier tamano de bloque\n");

    for (const double sampleRate : sampleRates)
    {
        HostClock clock;
        clock.prepare (sampleRate);
        BlockSizes sizes;
        std::int64_t samples = 0;
        bool continuous = true;
        for (int block = 0; block < 2000; ++block)
        {
            const int numSamples = sizes.next (4096);
            clock.beginBlock ({}, numSamples);
            // La posición de este bloque = muestras transcurridas · negras por muestra (a 120 BPM, 2 negras/s).
            continuous = continuous && near (clock.blockStartPpq(), static_cast<double> (samples) * 2.0 / sampleRate, 1.0e-6);
            samples += numSamples;
        }
        CHECK (continuous);
        CHECK (clock.getSource() == ClockSource::freeRunning);
        CHECK (clock.isRunning());
        CHECK (clock.jumpCount() == 0);
        CHECK (near (clock.bpm(), HostClock::defaultBpm));
    }
}

void testFollowsHost()
{
    std::printf ("Sigue al host: extrapola dentro del bloque, sin saltos falsos con bloques y tempos cambiantes\n");

    for (const double sampleRate : sampleRates)
    {
        HostClock clock;
        clock.prepare (sampleRate);
        BlockSizes sizes;
        double ppq = 0.0;
        double bpm = 90.0;
        bool falseJump = false;
        bool extrapolates = true;
        for (int block = 0; block < 3000; ++block)
        {
            // Cambios de tempo entre bloques (automatización): el host integra con el tempo que informó.
            if (block % 100 == 0)
                bpm = 70.0 + static_cast<double> ((block / 100) % 7) * 15.0;
            const int numSamples = sizes.next (4096);
            clock.beginBlock (playingAt (ppq, bpm), numSamples);
            falseJump = falseJump || clock.jumpedThisBlock();

            const double perSample = bpm / (60.0 * sampleRate);
            extrapolates = extrapolates && near (clock.ppqAt (numSamples / 2), ppq + (numSamples / 2) * perSample);
            ppq += numSamples * perSample;
        }
        CHECK (! falseJump);
        CHECK (extrapolates);
        CHECK (clock.jumpCount() == 0);
        CHECK (clock.getSource() == ClockSource::host);
    }
}

void testDetectsJumps()
{
    std::printf ("Detecta saltos del host (loop, salto adelante) y el arranque, no como salto\n");

    HostClock clock;
    clock.prepare (48000.0);
    constexpr int block = 480; // a 120 BPM y 48 kHz = 0.02 negras
    constexpr double step = 0.02;

    // Parado en el compás 5: todas las muestras en el mismo sitio.
    TransportInfo stopped;
    stopped.hasHost = true;
    stopped.bpm = 120.0;
    stopped.ppqPosition = 16.0;
    clock.beginBlock (stopped, block);
    CHECK (clock.getSource() == ClockSource::stopped);
    CHECK (! clock.isRunning());
    CHECK (near (clock.ppqAt (0), 16.0) && near (clock.ppqAt (block - 1), 16.0));

    // Empieza a reproducir: es un arranque, no un salto.
    clock.beginBlock (playingAt (16.0, 120.0), block);
    CHECK (clock.startedThisBlock());
    CHECK (! clock.jumpedThisBlock());
    clock.beginBlock (playingAt (16.0 + step, 120.0), block);
    CHECK (! clock.startedThisBlock() && ! clock.jumpedThisBlock());

    // Loop de FL: vuelve 4 compases atrás.
    clock.beginBlock (playingAt (0.0, 120.0), block);
    CHECK (clock.jumpedThisBlock());
    CHECK (clock.jumpCount() == 1);
    clock.beginBlock (playingAt (step, 120.0), block);
    CHECK (! clock.jumpedThisBlock());

    // Salto adelante (clic en la línea de tiempo) y un error de redondeo mínimo que NO es un salto.
    clock.beginBlock (playingAt (64.0, 120.0), block);
    CHECK (clock.jumpedThisBlock() && clock.jumpCount() == 2);
    clock.beginBlock (playingAt (64.0 + step + 1.0e-4, 120.0), block);
    CHECK (! clock.jumpedThisBlock() && clock.jumpCount() == 2);

    // El host deja de informar posición: el reloj libre sigue desde donde iba, sin volver a 0.
    const double expected = 64.0 + 2.0 * step + 1.0e-4;
    clock.beginBlock ({}, block);
    CHECK (clock.getSource() == ClockSource::freeRunning);
    CHECK (near (clock.blockStartPpq(), expected, 1.0e-9));
}

void testTempoAndSignatureValidation()
{
    std::printf ("Tempo y compas: valores absurdos del host se descartan o se limitan\n");

    HostClock clock;
    clock.prepare (44100.0);

    auto info = playingAt (0.0, 140.0, { 7, 8 });
    clock.beginBlock (info, 64);
    CHECK (near (clock.bpm(), 140.0));
    CHECK (clock.timeSignature().numerator == 7 && clock.timeSignature().denominator == 8);

    info.bpm = 0.0;                    // sin tempo válido: se queda el anterior
    info.timeSignature = TimeSignature { 4, 3 }; // denominador imposible: se queda el anterior
    clock.beginBlock (info, 64);
    CHECK (near (clock.bpm(), 140.0));
    CHECK (clock.timeSignature().denominator == 8);

    info.bpm = 5000.0;
    clock.beginBlock (info, 64);
    CHECK (near (clock.bpm(), HostClock::maxBpm));
    info.bpm = std::nan ("");
    clock.beginBlock (info, 64);
    CHECK (near (clock.bpm(), HostClock::maxBpm));
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

} // namespace

int main()
{
    testMusicalPosition();
    testFreeRunningClock();
    testFollowsHost();
    testDetectsJumps();
    testTempoAndSignatureValidation();
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

    if (failures == 0)
        std::printf ("\nTodos los tests de Ebb pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);
    return failures == 0 ? 0 : 1;
}
