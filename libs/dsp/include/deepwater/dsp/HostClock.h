#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace deepwater::dsp
{

struct TimeSignature
{
    int numerator = 4;
    int denominator = 4;

    // Duración de un compás y de un tiempo en negras. 4/4: 4 y 1; 6/8: 3 y 0.5; 7/8: 3.5 y 0.5.
    [[nodiscard]] double quartersPerBar() const noexcept { return numerator * quartersPerBeat(); }
    [[nodiscard]] double quartersPerBeat() const noexcept { return 4.0 / denominator; }

    [[nodiscard]] bool isValid() const noexcept
    {
        const bool powerOfTwo = denominator > 0 && (denominator & (denominator - 1)) == 0;
        return numerator >= 1 && numerator <= 64 && powerOfTwo && denominator <= 64;
    }
};

// Lo que informa el host al principio de un bloque (el PlayHead de JUCE traducido, sin JUCE). Cada dato es opcional:
// no todos los hosts informan todo, y el Standalone no informa nada.
struct TransportInfo
{
    bool hasHost = false;      // hay PlayHead y respondió en este bloque
    bool isPlaying = false;
    bool isLooping = false;
    std::optional<double> bpm;
    std::optional<TimeSignature> timeSignature;
    std::optional<double> ppqPosition;      // posición en negras de la primera muestra del bloque
    std::optional<double> ppqLastBarStart;  // dónde empezó el compás actual (respeta los cambios de compás del host)
};

// De dónde sale la posición del bloque actual.
enum class ClockSource : std::uint8_t
{
    host,        // el host está reproduciendo: su posición manda
    stopped,     // el host está parado: la posición queda quieta donde él diga
    freeRunning  // no hay host (Standalone) o no informa posición: reloj propio desde 0 al tempo conocido (o 120)
};

// Posición musical como la muestra FL: compás, tiempo y semicorchea, contando desde 1.
struct MusicalPosition
{
    int bar = 1;
    int beat = 1;
    int sixteenth = 1;
    int step = 1; // semicorchea dentro del compás, como el contador Bar:Step de FL (1–16 en 4/4)
};

// Convierte una posición en negras a compás.tiempo.semicorchea. Si el host dice dónde empezó el compás se usa eso
// (así un cambio de compás a mitad de canción no descuadra la cuenta); si no, se asume el mismo compás desde el inicio.
[[nodiscard]] inline MusicalPosition musicalPosition (double ppq, TimeSignature signature,
                                                      std::optional<double> lastBarStart = std::nullopt) noexcept
{
    if (! signature.isValid())
        signature = {};

    const double perBar = signature.quartersPerBar();
    const double perBeat = signature.quartersPerBeat();
    // Margen para errores de redondeo del host: un "inicio de compás" 1e-9 negras por delante sigue valiendo.
    constexpr double epsilon = 1.0e-6;

    double barStart = std::floor (ppq / perBar + epsilon) * perBar;
    if (lastBarStart && *lastBarStart <= ppq + epsilon && ppq - *lastBarStart < perBar + epsilon)
        barStart = *lastBarStart;

    const double inBar = std::max (0.0, ppq - barStart);
    const int beat = std::clamp (static_cast<int> (std::floor (inBar / perBeat + epsilon)), 0, signature.numerator - 1);
    const double inBeat = std::max (0.0, inBar - beat * perBeat);
    // Semicorcheas por tiempo: 4 en x/4, 2 en x/8, 1 en x/16 (y en x/32 un tiempo es más corto que una semicorchea).
    const int sixteenthsPerBeat = std::max (1, 16 / signature.denominator);
    const int sixteenth = std::clamp (static_cast<int> (std::floor (inBeat / 0.25 + epsilon)), 0, sixteenthsPerBeat - 1);

    const int stepsPerBar = std::max (1, static_cast<int> (std::ceil (perBar / 0.25 - epsilon)));
    const int step = std::clamp (static_cast<int> (std::floor (inBar / 0.25 + epsilon)), 0, stepsPerBar - 1);

    return { static_cast<int> (std::lround (barStart / perBar)) + 1, beat + 1, sixteenth + 1, step + 1 };
}

// Reloj musical de un plugin. Nació en Ebb y pasó a la librería con Tidechart, que también sigue al host (sus
// progresiones cambian de acorde en su compás). Se resincroniza con el host al principio de CADA bloque (no lleva un
// contador propio que pueda desviarse) y dentro del bloque extrapola: cada muestra avanza bpm / (60 · fs) negras. Así
// un loop de FL, un salto en la línea de tiempo o un cambio de tempo se corrigen solos en el bloque siguiente.
//
// También detecta los SALTOS: si el host sigue reproduciendo pero la posición no es la que esperábamos (la anterior +
// lo que avanzó el bloque), hubo un loop o un salto: Ebb no arrastra una curva a medias y Tidechart apaga lo que suena.
//
// Tempo automatizado (Fase 9 de Ebb): el host informa el tempo del PRINCIPIO del bloque, pero si el tempo sube o baja
// dentro del bloque, su posición real avanza un poco más o un poco menos que la extrapolada. Al bloque siguiente la
// diferencia es pequeña (milésimas de negra; crece con el cuadrado del tamaño del bloque) pero corregirla de golpe
// hacía saltar la curva de Ebb (un clic) y, si el tempo bajaba y la posición tenía que volver atrás justo después de un
// golpe, ¡el golpe sonaba dos veces! Ahora una diferencia menor que un salto no se corrige de golpe: el reloj sigue desde donde iba y la
// recupera avanzando un poco más rápido o más lento (hasta un 10 %) durante unas pocas muestras. La posición nunca
// retrocede ni salta; con tempo constante la diferencia es 0 y todo queda igual que antes.
class HostClock
{
public:
    static constexpr double defaultBpm = 120.0;
    static constexpr double minBpm = 20.0;
    static constexpr double maxBpm = 999.0;
    // Diferencia mínima para considerar que hubo un salto. Con automatización de tempo el host integra el tempo dentro
    // del bloque y nuestra predicción (tempo constante) se desvía milésimas de negra; un loop salta compases enteros.
    static constexpr double jumpToleranceBeats = 1.0 / 64.0;
    // A qué velocidad se recupera la diferencia con el host (fracción del tempo): 0.1 = el reloj avanza entre el
    // 90 % y el 110 % del tempo hasta alcanzarlo. Un cambio de velocidad tan corto y tan pequeño no se oye.
    static constexpr double catchUpRate = 0.1;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        currentBpm = defaultBpm;
        signature = {};
        source = ClockSource::freeRunning;
        startPpq = 0.0;
        freeRunPpq = 0.0;
        expectedNextPpq.reset();
        offset = 0.0;
        barStart.reset();
        looping = false;
        jumped = false;
        started = false;
        jumps = 0;
        updateRate();
    }

    // Una vez por bloque, antes de procesar sus muestras.
    void beginBlock (const TransportInfo& info, int numSamples) noexcept
    {
        if (info.bpm && std::isfinite (*info.bpm) && *info.bpm > 0.0)
            currentBpm = std::clamp (*info.bpm, minBpm, maxBpm);
        if (info.timeSignature && info.timeSignature->isValid())
            signature = *info.timeSignature;
        updateRate();

        const bool wasPlaying = source == ClockSource::host;
        jumped = false;
        started = false;
        looping = info.hasHost && info.isLooping;
        offset = 0.0;
        const double blockBeats = numSamples * beatsPerSample;

        if (info.hasHost && info.ppqPosition && std::isfinite (*info.ppqPosition))
        {
            startPpq = *info.ppqPosition;
            barStart = info.ppqLastBarStart;
            if (info.isPlaying)
            {
                source = ClockSource::host;
                started = ! wasPlaying;
                // expectedNextPpq = dónde iba a estar nuestro reloj (con la diferencia que aún arrastraba).
                if (wasPlaying && expectedNextPpq)
                {
                    const double behindUs = *expectedNextPpq - startPpq;
                    if (std::abs (behindUs) > jumpToleranceBeats)
                    {
                        jumped = true;
                        ++jumps;
                    }
                    else
                    {
                        offset = behindUs; // se sigue desde donde íbamos y se recupera poco a poco
                    }
                }
                expectedNextPpq = ppqAt (numSamples);
            }
            else
            {
                source = ClockSource::stopped;
                expectedNextPpq.reset();
            }
            // Si el host deja de informar, el reloj libre sigue desde donde se quedó y no vuelve a 0.
            freeRunPpq = source == ClockSource::host ? startPpq + blockBeats : startPpq;
            return;
        }

        source = ClockSource::freeRunning;
        startPpq = freeRunPpq;
        freeRunPpq += blockBeats;
        barStart.reset();
        expectedNextPpq.reset();
    }

    // Posición en negras de la muestra 'sample' del bloque actual. Parado, todas las muestras están en el mismo sitio.
    // Con una diferencia pendiente con el host (tempo automatizado), esta se achica catchUpRate · tempo por muestra.
    [[nodiscard]] double ppqAt (int sample) const noexcept
    {
        if (source == ClockSource::stopped)
            return startPpq;
        const double position = startPpq + sample * beatsPerSample;
        if (offset == 0.0)
            return position;
        const double recovered = catchUpRate * beatsPerSample * sample;
        return position + (offset > 0.0 ? std::max (0.0, offset - recovered) : std::min (0.0, offset + recovered));
    }

    // Lo más que puede avanzar la posición de una muestra a la siguiente (recuperando una diferencia con el host).
    [[nodiscard]] double maxBeatsPerSample() const noexcept { return beatsPerSample * (1.0 + catchUpRate); }

    [[nodiscard]] double blockStartPpq() const noexcept { return startPpq; }
    [[nodiscard]] double bpm() const noexcept { return currentBpm; }
    [[nodiscard]] double getBeatsPerSample() const noexcept { return beatsPerSample; }
    [[nodiscard]] TimeSignature timeSignature() const noexcept { return signature; }
    [[nodiscard]] std::optional<double> lastBarStart() const noexcept { return barStart; }

    // Dónde empieza la cuadrícula de compases en este bloque: el inicio de compás del host si es coherente con la
    // posición (respeta los cambios de compás); si no, el 0, asumiendo el mismo compás desde el principio.
    [[nodiscard]] double barOrigin() const noexcept
    {
        constexpr double epsilon = 1.0e-6;
        if (barStart && *barStart <= startPpq + epsilon && startPpq - *barStart < signature.quartersPerBar() + epsilon)
            return *barStart;
        return 0.0;
    }
    [[nodiscard]] ClockSource getSource() const noexcept { return source; }
    [[nodiscard]] bool isRunning() const noexcept { return source != ClockSource::stopped; }
    [[nodiscard]] bool isLooping() const noexcept { return looping; }

    // Lo que pasó en ESTE bloque: el host empezó a reproducir, o saltó estando en reproducción.
    [[nodiscard]] bool startedThisBlock() const noexcept { return started; }
    [[nodiscard]] bool jumpedThisBlock() const noexcept { return jumped; }
    [[nodiscard]] std::uint32_t jumpCount() const noexcept { return jumps; }

    [[nodiscard]] MusicalPosition positionAt (int sample) const noexcept
    {
        return musicalPosition (ppqAt (sample), signature, barStart);
    }

private:
    void updateRate() noexcept { beatsPerSample = currentBpm / (60.0 * sampleRate); }

    double sampleRate = 44100.0;
    double currentBpm = defaultBpm;
    double beatsPerSample = defaultBpm / (60.0 * 44100.0);
    TimeSignature signature;
    ClockSource source = ClockSource::freeRunning;
    double startPpq = 0.0;
    double freeRunPpq = 0.0;
    std::optional<double> expectedNextPpq;
    double offset = 0.0; // nuestro reloj − el del host al principio del bloque (tempo automatizado)
    std::optional<double> barStart;
    bool looping = false;
    bool jumped = false;
    bool started = false;
    std::uint32_t jumps = 0;
};

} // namespace deepwater::dsp
