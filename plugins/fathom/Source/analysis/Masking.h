#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

// Fase 14, sin JUCE (se prueba en tests/EngineTests.cpp): dónde "chocan" dos pistas, es decir, dónde las dos tienen su
// energía importante en la misma zona del oído y una tapa a la otra (enmascaramiento).
//
// La idea: el oído separa las frecuencias en "bandas críticas" (filtros de la cóclea). Dos sonidos dentro de la misma
// banda crítica compiten: el más fuerte tapa al más débil; en bandas distintas se oyen los dos. El ancho de esas bandas
// se mide como ERB (Glasberg y Moore, 1990): ERB(f) = 24.7 · (4.37 · f / 1000 + 1) Hz. Unos 35 Hz en 100 Hz (casi media
// octava: en graves todo se mezcla), 133 Hz en 1 kHz y 1.1 kHz en 10 kHz (1/6 de octava: en agudos el oído separa más).
//
// Las cuentas, con los espectros de las dos pistas ya promediados en el tiempo (un choque es algo que pasa durante la
// canción, no en un golpe):
//  1. Cada espectro se lleva a bandas críticas (lo más fuerte dentro de ±ERB/2): lo que el oído "junta".
//  2. Para cada pista, cuánto importa cada zona: su nivel menos el de su zona más fuerte (0 dB = su zona principal, −20
//     dB = algo secundario). Con la inclinación de 4.5 dB/oct, como se ve en el analizador: "fuerte" es fuerte para el
//     oído, no solo en graves.
//  3. Choque = las dos pistas importantes a la vez: 1 + min(importancia A, importancia B) / margen, entre 0 y 1. Con
//     margen 18 dB, choque 1 = las dos en su zona principal; 0.5 = la menos importante de las dos está 9 dB debajo de su
//     principal. Lo que está debajo del piso (silencio, ruido) no choca.
// Quién tapa a quién = la diferencia de nivel en esa zona (la inclinación se cancela al restar).
namespace fathom::analysis
{

inline double erbHz (double hz) noexcept { return 24.7 * (4.37 * hz / 1000.0 + 1.0); }

// Puntos repartidos en escala logarítmica de minHz a maxHz (como los del visor: uno cada 2 píxeles).
struct LogAxis
{
    double minHz = 20.0, maxHz = 20000.0;
    size_t points = 0;

    double hz (size_t i) const noexcept
    {
        return points < 2 ? minHz : minHz * std::pow (maxHz / minHz, static_cast<double> (i) / static_cast<double> (points - 1));
    }

    // La posición (con decimales) de una frecuencia.
    double position (double hzValue) const noexcept
    {
        return points < 2 ? 0.0 : std::log (hzValue / minHz) / std::log (maxHz / minHz) * static_cast<double> (points - 1);
    }
};

// Paso 1: lo más fuerte dentro de ±ERB/2 alrededor de cada punto. Lo que tapa dentro de una banda crítica es su
// componente más fuerte (un tono puro tapa igual aunque ocupe un solo bin; promediar lo diluiría en lo que lo rodea).
inline std::vector<float> erbSmooth (const std::vector<float>& db, const LogAxis& axis)
{
    const size_t count = db.size();
    std::vector<float> smoothed (count);
    if (count == 0 || axis.points != count)
        return smoothed;
    for (size_t i = 0; i < count; ++i)
    {
        const double hz = axis.hz (i), half = 0.5 * erbHz (hz);
        const double low = std::max (axis.position (std::max (hz - half, 1.0)), 0.0);
        const double high = std::min (axis.position (hz + half), static_cast<double> (count - 1));
        const auto first = std::min (static_cast<size_t> (std::ceil (low)), i);
        const auto last = std::max (static_cast<size_t> (std::floor (high)), i);
        smoothed[i] = *std::max_element (db.begin() + static_cast<std::ptrdiff_t> (first), db.begin() + static_cast<std::ptrdiff_t> (last) + 1);
    }
    return smoothed;
}

// Un espectro promediado en el tiempo: cada cuadro se acerca al nuevo con una constante de tiempo (en potencia, como un
// medidor RMS lento). Un golpe suelto apenas lo mueve; lo que suena todo el tiempo lo define.
struct SlowSpectrum
{
    std::vector<float> db; // vacío = sin datos todavía
    static constexpr double defaultSeconds = 1.5;

    void reset() { db.clear(); }

    void feed (const std::vector<float>& levelsDb, double elapsed, double tauSeconds = defaultSeconds)
    {
        if (db.size() != levelsDb.size())
        {
            db = levelsDb; // la primera vez (o con otro tamaño), tal cual
            return;
        }
        const double k = tauSeconds <= 0.0 ? 1.0 : 1.0 - std::exp (-std::max (elapsed, 0.0) / tauSeconds);
        for (size_t i = 0; i < db.size(); ++i)
        {
            const double now = std::pow (10.0, static_cast<double> (db[i]) * 0.1);
            const double target = std::pow (10.0, static_cast<double> (levelsDb[i]) * 0.1);
            db[i] = static_cast<float> (10.0 * std::log10 (std::max (now + (target - now) * k, 1.0e-30)));
        }
    }
};

// Cuánto choque hace falta para marcarlo, como "sensibilidad": el margen del paso 3.
struct MaskingSensitivity
{
    const char* name;
    float spanDb;
};
inline constexpr std::array<MaskingSensitivity, 3> maskingSensitivities { { { "Baja", 12.0f }, { "Media", 18.0f }, { "Alta", 24.0f } } };
inline constexpr float maskingFloorDb = -80.0f;     // con la inclinación: debajo de esto no hay nada que tapar
inline constexpr double maskingLowHz = 30.0;        // dónde se busca la zona principal de cada pista
inline constexpr double maskingHighHz = 16000.0;
inline constexpr float collisionThreshold = 0.5f;   // desde aquí una zona se nombra (se pinta desde antes, más tenue)

// Pasos 2 y 3, punto por punto: 0 = no chocan, 1 = las dos en su zona principal. 'a' y 'b' ya suavizados (paso 1).
inline std::vector<float> collisionAmount (const std::vector<float>& a, const std::vector<float>& b, const LogAxis& axis, float spanDb)
{
    const size_t count = a.size();
    std::vector<float> amount (count, 0.0f);
    if (count == 0 || b.size() != count || axis.points != count || spanDb <= 0.0f)
        return amount;
    const auto first = static_cast<size_t> (std::clamp (std::ceil (axis.position (maskingLowHz)), 0.0, static_cast<double> (count - 1)));
    const auto last = static_cast<size_t> (std::clamp (std::floor (axis.position (maskingHighHz)), 0.0, static_cast<double> (count - 1)));
    float maxA = maskingFloorDb, maxB = maskingFloorDb;
    for (size_t i = first; i <= last; ++i)
    {
        maxA = std::max (maxA, a[i]);
        maxB = std::max (maxB, b[i]);
    }
    for (size_t i = 0; i < count; ++i)
    {
        if (a[i] <= maskingFloorDb || b[i] <= maskingFloorDb)
            continue;
        const float importance = std::min (a[i] - maxA, b[i] - maxB); // ≤ 0
        amount[i] = std::clamp (1.0f + importance / spanDb, 0.0f, 1.0f);
    }
    return amount;
}

// Una zona de choque: un tramo seguido de puntos con choque ≥ el umbral.
struct Collision
{
    double lowHz = 0.0, highHz = 0.0;
    double centreHz = 0.0; // donde más chocan
    float amount = 0.0f;   // el choque ahí (0.5-1)
    float louderDb = 0.0f; // cuánto más fuerte está 'a' que 'b' ahí (negativo: 'b' tapa a 'a')
};

// Las zonas de izquierda a derecha.
inline std::vector<Collision> collisionZones (const std::vector<float>& amount, const std::vector<float>& a, const std::vector<float>& b,
                                              const LogAxis& axis, float threshold = collisionThreshold)
{
    std::vector<Collision> zones;
    const size_t count = amount.size();
    if (a.size() != count || b.size() != count || axis.points != count)
        return zones;
    size_t i = 0;
    while (i < count)
    {
        if (amount[i] < threshold)
        {
            ++i;
            continue;
        }
        // El centro: donde más chocan; si es una meseta (las dos en su zona principal), su punto medio.
        size_t end = i, best = i, bestLast = i;
        while (end + 1 < count && amount[end + 1] >= threshold)
        {
            ++end;
            if (amount[end] > amount[best] + 1.0e-3f)
                best = bestLast = end;
            else if (amount[end] >= amount[best] - 1.0e-3f)
                bestLast = end;
        }
        const size_t centre = (best + bestLast) / 2;
        zones.push_back ({ axis.hz (i), axis.hz (end), axis.hz (centre), amount[centre], a[centre] - b[centre] });
        i = end + 1;
    }
    return zones;
}

} // namespace fathom::analysis
