#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace ebb::dsp
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

// Reloj musical del plugin. Se resincroniza con el host al principio de CADA bloque (no lleva un contador propio que
// pueda desviarse) y dentro del bloque extrapola: cada muestra avanza bpm / (60 · fs) negras. Así un loop de FL, un
// salto en la línea de tiempo o un cambio de tempo se corrigen solos en el bloque siguiente.
//
// También detecta los SALTOS: si el host sigue reproduciendo pero la posición no es la que esperábamos (la anterior +
// lo que avanzó el bloque), hubo un loop o un salto. En la Fase 2 eso servirá para no arrastrar una curva a medias.
class HostClock
{
public:
    static constexpr double defaultBpm = 120.0;
    static constexpr double minBpm = 20.0;
    static constexpr double maxBpm = 999.0;
    // Diferencia mínima para considerar que hubo un salto. Con automatización de tempo el host integra el tempo dentro
    // del bloque y nuestra predicción (tempo constante) se desvía milésimas de negra; un loop salta compases enteros.
    static constexpr double jumpToleranceBeats = 1.0 / 64.0;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        currentBpm = defaultBpm;
        signature = {};
        source = ClockSource::freeRunning;
        startPpq = 0.0;
        freeRunPpq = 0.0;
        expectedNextPpq.reset();
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
        const double blockBeats = numSamples * beatsPerSample;

        if (info.hasHost && info.ppqPosition && std::isfinite (*info.ppqPosition))
        {
            startPpq = *info.ppqPosition;
            barStart = info.ppqLastBarStart;
            if (info.isPlaying)
            {
                source = ClockSource::host;
                started = ! wasPlaying;
                if (wasPlaying && expectedNextPpq && std::abs (startPpq - *expectedNextPpq) > jumpToleranceBeats)
                {
                    jumped = true;
                    ++jumps;
                }
                expectedNextPpq = startPpq + blockBeats;
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

    // Posición en negras de la muestra 'offset' del bloque actual. Parado, todas las muestras están en el mismo sitio.
    [[nodiscard]] double ppqAt (int offset) const noexcept
    {
        return source == ClockSource::stopped ? startPpq : startPpq + offset * beatsPerSample;
    }

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

    [[nodiscard]] MusicalPosition positionAt (int offset) const noexcept
    {
        return musicalPosition (ppqAt (offset), signature, barStart);
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
    std::optional<double> barStart;
    bool looping = false;
    bool jumped = false;
    bool started = false;
    std::uint32_t jumps = 0;
};

} // namespace ebb::dsp
