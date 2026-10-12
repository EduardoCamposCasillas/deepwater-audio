// Tests de Ebb sin JUCE: el reloj musical (posición del host) y el motor de audio. Se lanzan con ctest. Si algo falla,
// imprime el archivo, la línea y la condición.

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "dsp/Engine.h"
#include "dsp/HostClock.h"

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

void testEnginePassthrough()
{
    std::printf ("Motor: la senal pasa bit a bit, con y sin bypass, y al conmutar el bypass\n");

    for (const double sampleRate : sampleRates)
        for (const int channels : { 1, 2 })
        {
            Engine engine;
            engine.prepare (sampleRate, false);

            BlockSizes sizes;
            std::uint32_t noise = 1;
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

                if (block % 37 == 0)
                    engine.setBypassed (block % 74 == 0);

                std::array<float*, 2> pointers { left.data(), right.data() };
                engine.process (pointers.data(), channels, numSamples);
                identical = identical && left == originalLeft && right == originalRight;
            }
            CHECK (identical);
        }
}

} // namespace

int main()
{
    testMusicalPosition();
    testFreeRunningClock();
    testFollowsHost();
    testDetectsJumps();
    testTempoAndSignatureValidation();
    testEnginePassthrough();

    if (failures == 0)
        std::printf ("\nTodos los tests de Ebb pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);
    return failures == 0 ? 0 : 1;
}
