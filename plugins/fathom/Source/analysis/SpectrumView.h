#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

// Las cuentas del analizador profesional (Fase 5), sin JUCE (se prueban en tests/EngineTests.cpp): cómo se ajusta lo que
// se VE del espectro (inclinación, rango, resolución, velocidad, retención de picos) y dónde están sus picos. Nada de
// esto cambia el audio: el análisis corre en la GUI con las muestras que el audio deja en las tomas.
namespace fathom::analysis
{

// ---- Inclinación (tilt) -------------------------------------------------------------------------------------------------
// La música tiene mucha más energía en graves que en agudos: su espectro cae unos 3-6 dB por octava (el ruido rosa,
// "igual energía por octava", cae 3 dB/oct en un analizador por bins). Sin compensar, los agudos quedan pegados al piso
// y parece que "faltan". Sumar k·log2(f / 1 kHz) dB endereza esa caída: con 4.5 dB/oct una mezcla equilibrada se ve
// casi plana y un exceso en cualquier zona salta a la vista. 1 kHz no se mueve (el pivote). Es solo la vista.
inline constexpr std::array<float, 4> tilts { 0.0f, 3.0f, 4.5f, 6.0f }; // dB/oct
inline constexpr double tiltPivotHz = 1000.0;

inline float tiltDb (double hz, float dbPerOctave) noexcept
{
    return dbPerOctave == 0.0f ? 0.0f : dbPerOctave * static_cast<float> (std::log2 (hz / tiltPivotHz));
}

// ---- Rango ----------------------------------------------------------------------------------------------------------------
// Cuántos dB cubre la escala del analizador de arriba (0 dB) a abajo. Múltiplos de 8 para que las 8 divisiones de la
// cuadrícula caigan en números redondos (6, 9, 12 y 15 dB por línea).
inline constexpr std::array<float, 4> ranges { 48.0f, 72.0f, 96.0f, 120.0f };

// ---- Resolución -----------------------------------------------------------------------------------------------------------
// El tamaño de la FFT. Más puntos = bins más juntos (más detalle en graves: con 2048 a 44.1 kHz cada bin mide 21.5 Hz y
// dos notas graves vecinas caen en el mismo; con 16384, 2.7 Hz) pero la ventana dura más (46 ms → 372 ms): el espectro
// "promedia" más tiempo y reacciona tarde a los golpes. Ninguna es la correcta: alta para ver notas del 808, baja para
// ver los golpes de la percusión. A 88.2/96 kHz se duplica el tamaño: misma duración y mismos Hz por bin.
inline constexpr std::array<const char*, 4> resolutionNames { "Baja", "Media", "Alta", "M\xc3\xa1xima" };

inline int fftOrder (int resolution, double sampleRate) noexcept
{
    return 11 + std::clamp (resolution, 0, static_cast<int> (resolutionNames.size()) - 1) + (sampleRate > 66000.0 ? 1 : 0);
}

// ---- Velocidad (balística) ------------------------------------------------------------------------------------------------
// Cómo sigue lo dibujado al análisis de cada cuadro: sube hacia un nivel más alto con una constante de tiempo (0 =
// al instante) y baja a tantos dB por segundo. Lento = un promedio estable (el "balance tonal" de la mezcla, para
// compararla con una referencia); rápido = cada golpe y cada nota (para encontrar qué resuena y cuándo).
struct Speed
{
    const char* name;
    double riseSeconds;
    float fallDbPerSecond;
};
inline constexpr std::array<Speed, 5> speeds { { { "Muy lenta", 0.3, 6.0f },
                                                 { "Lenta", 0.1, 15.0f },
                                                 { "Media", 0.03, 40.0f }, // la de las Fases 1-4
                                                 { "R\xc3\xa1pida", 0.01, 90.0f },
                                                 { "Muy r\xc3\xa1pida", 0.0, 240.0f } } };

// Un punto dibujado después de 'elapsed' segundos, si el análisis nuevo dice 'level'.
inline float follow (float shown, float level, double elapsed, const Speed& speed) noexcept
{
    if (level > shown)
    {
        if (speed.riseSeconds <= 0.0)
            return level;
        const auto approach = static_cast<float> (1.0 - std::exp (-elapsed / speed.riseSeconds));
        return shown + (level - shown) * approach;
    }
    return std::max (level, shown - speed.fallDbPerSecond * static_cast<float> (elapsed));
}

// ---- Retención de picos ---------------------------------------------------------------------------------------------------
// Una segunda línea que guarda el máximo de cada frecuencia: muestra lo que pasó aunque ya no esté sonando (el golpe del
// kick, una nota del 808 que resuena más que las otras). "2 s": el máximo se queda 2 segundos y después cae despacio;
// "Siempre": hasta borrarlo.
enum class Hold
{
    off,
    seconds,
    forever,
};
inline constexpr std::array<const char*, 3> holdNames { "No", "2 s", "Siempre" };
inline constexpr double holdSeconds = 2.0;
inline constexpr float holdFallDbPerSecond = 20.0f;

struct HeldPoint
{
    float db = -200.0f;
    double age = 0.0; // segundos desde que se marcó el máximo
};

inline void hold (HeldPoint& point, float shown, double elapsed, Hold mode) noexcept
{
    if (mode == Hold::off || shown >= point.db)
    {
        point = { shown, 0.0 };
        return;
    }
    point.age += elapsed;
    if (mode == Hold::seconds && point.age > holdSeconds)
        point.db = std::max (shown, point.db - holdFallDbPerSecond * static_cast<float> (elapsed));
}

// ---- Canales (Fase 8) -------------------------------------------------------------------------------------------------------
// Qué parte de la señal estéreo mide el analizador. Mid = (L + R) / 2, lo de las Fases 1-7: lo que está en el centro (y
// en mono, todo). Side = (L − R) / 2: solo lo que es distinto entre los lados (el ancho). Mid + side = cada espectro en
// mid y además el side de la salida en otra línea, para ver cuánto ancho hay en cada zona (¿hay graves en el side?).
enum class Channels
{
    mid,
    side,
    midAndSide,
};
inline constexpr std::array<const char*, 3> channelsNames { "Mid (L+R)", "Side (L\xe2\x88\x92R)", "Mid + Side" };

// ---- Ajustes de la vista (se guardan con el proyecto) ---------------------------------------------------------------------
struct ViewSettings
{
    int tiltIndex = 2;       // 4.5 dB/oct
    int rangeIndex = 2;      // 96 dB (el de las Fases 1-4)
    int resolutionIndex = 1; // media: ~93 ms (la de las Fases 1-4)
    int speedIndex = 2;      // media
    int holdIndex = 0;       // sin retención
    int channelsIndex = 0;   // mid (el de las Fases 1-7)
    bool showInput = true;
    bool showOutput = true;
    bool showSidechain = false;

    float tilt() const noexcept { return tilts[static_cast<size_t> (std::clamp (tiltIndex, 0, static_cast<int> (tilts.size()) - 1))]; }
    float range() const noexcept { return ranges[static_cast<size_t> (std::clamp (rangeIndex, 0, static_cast<int> (ranges.size()) - 1))]; }
    const Speed& speed() const noexcept { return speeds[static_cast<size_t> (std::clamp (speedIndex, 0, static_cast<int> (speeds.size()) - 1))]; }
    Hold holdMode() const noexcept { return static_cast<Hold> (std::clamp (holdIndex, 0, static_cast<int> (holdNames.size()) - 1)); }
    Channels channels() const noexcept
    {
        return static_cast<Channels> (std::clamp (channelsIndex, 0, static_cast<int> (channelsNames.size()) - 1));
    }

    bool operator== (const ViewSettings&) const = default;
};

// ---- Picos del espectro ---------------------------------------------------------------------------------------------------
// Los picos que vale la pena "agarrar": puntos que son el máximo de su vecindario (±reach puntos) y sobresalen al menos
// minProminenceDb por encima del valle más alto de los dos lados (la "prominencia": un pico pequeño sobre una montaña no
// cuenta, uno que se despega de lo que lo rodea sí). Lo que queda por debajo de minLevelDb (el piso) no cuenta.
// Devuelve los índices de izquierda a derecha.
inline std::vector<size_t> findPeaks (const std::vector<float>& levels, size_t reach, float minProminenceDb, float minLevelDb)
{
    std::vector<size_t> peaks;
    const size_t count = levels.size();
    if (count < 3 || reach == 0)
        return peaks;
    for (size_t i = 1; i + 1 < count; ++i)
    {
        const float level = levels[i];
        // En una meseta, solo el primer punto (si no, una cima plana daría varios picos seguidos).
        if (level < minLevelDb || level <= levels[i - 1] || level < levels[i + 1])
            continue;
        const size_t first = i > reach ? i - reach : 0;
        const size_t last = std::min (count - 1, i + reach);
        float leftValley = level, rightValley = level;
        bool highest = true;
        for (size_t j = first; j <= last && highest; ++j)
        {
            highest = levels[j] <= level;
            if (j < i)
                leftValley = std::min (leftValley, levels[j]);
            else if (j > i)
                rightValley = std::min (rightValley, levels[j]);
        }
        if (highest && level - std::max (leftValley, rightValley) >= minProminenceDb)
            peaks.push_back (i);
    }
    return peaks;
}

// Uno de los picos: el más cercano a 'position' (en puntos), si está a menos de 'maxDistance'. Si no, −1.
inline int nearestPeak (const std::vector<size_t>& peaks, double position, double maxDistance) noexcept
{
    int best = -1;
    double bestDistance = maxDistance;
    for (size_t p = 0; p < peaks.size(); ++p)
        if (const double distance = std::abs (static_cast<double> (peaks[p]) - position); distance <= bestDistance)
        {
            best = static_cast<int> (p);
            bestDistance = distance;
        }
    return best;
}

} // namespace fathom::analysis
