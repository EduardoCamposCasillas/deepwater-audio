#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

#include "deepwater/dsp/Smoothing.h"
#include "dsp/Band.h"
#include "dsp/Solo.h"

namespace fathom::dsp
{

// EQ dinámico (Fase 9): una banda que solo actúa cuando su zona suena fuerte. Un EQ normal corta 4 dB en 3 kHz SIEMPRE,
// aunque la voz solo se ponga chillona en las notas altas; uno dinámico corta solo en esas notas y deja el resto intacto.
// Es como un compresor que mira (y toca) una sola franja del espectro.
//
// Cada banda dinámica tiene su detector, en cuatro pasos:
//   1. la zona: la misma que se escucha en SOLO (Solo.h: un pasa banda con la frecuencia y el Q de una campana, un pasa
//      bajos para un shelf de graves...) aplicada a la señal que llega a la banda, o al sidechain. Los tilts no marcan
//      zona: su detector escucha todo su canal;
//   2. el nivel: RMS de la zona con una ventana corta (un filtro de 1 polo sobre x²), en dB y +3 dB para que un seno
//      mida su pico (como un medidor RMS AES17: −6 dBFS de pico se lee −6). La ventana depende de la zona: unos
//      2 ciclos de su frecuencia más grave (1 a 20 ms): lo justo para que el nivel no ondule con cada ciclo;
//   3. la curva de ganancia: por cada dB que el nivel pasa del umbral, la ganancia se mueve 1 dB hacia el rango (un
//      rango negativo corta, uno positivo sube), hasta llegar a él. Con rango −6 la zona queda "pegada" al umbral
//      hasta que hace falta cortar más de 6 dB: es la idea del de-esser. Alrededor del umbral la curva se curva en
//      6 dB (rodilla suave, Giannoulis et al. 2012): la banda entra de a poco y no "salta" al cruzarlo;
//   4. ataque y liberación sobre esa GANANCIA, en dB (el detector "en el dominio log" del mismo paper): un filtro de
//      1 polo que usa el ataque cuando la banda tiene que actuar más y la liberación cuando vuelve. Así el ataque es
//      siempre el tiempo en que la banda llega al 63 % de su cambio, esté el umbral 3 o 30 dB debajo de la señal. (Se
//      probó primero el EnvelopeFollower de la librería, que suaviza el nivel en lineal: con el umbral muy abajo el
//      nivel lo cruza casi al instante y una banda con 10 ms de ataque cortaba 12 dB en menos de 1 ms: un clic.)
// El resultado se suma a la ganancia de la banda (Band::setDynamicGain): la ganancia propia es donde descansa y el rango,
// cuánto se aleja de ahí con la zona fuerte.
//
// Umbral automático: el umbral pasa a estar 3 dB (media rodilla) sobre el nivel TÍPICO de la zona (el promedio en dB del
// nivel en ~1.5 s, sin contar los silencios). Lo sostenido queda justo debajo de la rodilla (no se toca) y solo actúan
// los momentos en que la zona suena más fuerte que de costumbre, aunque la pista entera suba o baje; el rango decide
// cuánto.
//
// Todo lo que no es audio corre cada Band::dynamicInterval muestras (el "control rate"): el nivel sigue cada muestra,
// pero la cuenta en dB y el valor que va a la banda, no. Rango, umbral, encendido y umbral automático se suavizan en
// 20 ms a ese ritmo: moverlos sonando no hace clic.
class BandDynamics
{
public:
    static constexpr int controlInterval = Band::dynamicInterval;
    static constexpr float kneeDb = 6.0f;
    static constexpr float autoMarginDb = 0.5f * kneeDb;
    static constexpr double averageSeconds = 1.5;
    static constexpr float silenceDb = -70.0f; // debajo de esto no cuenta para el promedio del umbral automático
    static constexpr float floorDb = -120.0f;
    static constexpr double minWindowSeconds = 0.001, maxWindowSeconds = 0.02;

    // Cuánto se mueve la ganancia (dB) con la zona en 'levelDb': 0 hasta la rodilla, después 1 dB por dB, hasta el rango.
    static float gainChangeDb (float levelDb, float thresholdDb, float rangeDb) noexcept
    {
        const float over = levelDb - thresholdDb;
        float amount = 0.0f;
        if (over >= 0.5f * kneeDb)
            amount = over;
        else if (over > -0.5f * kneeDb)
            amount = (over + 0.5f * kneeDb) * (over + 0.5f * kneeDb) / (2.0f * kneeDb);
        return rangeDb < 0.0f ? -std::min (amount, -rangeDb) : std::min (amount, rangeDb);
    }

    // La ventana del RMS: dos ciclos de lo más grave de la zona. Una campana ancha (Q < 1) llega más abajo que su
    // centro; un shelf de agudos, a su frecuencia; un shelf de graves y los tilts tienen de todo abajo: la más larga.
    static double windowSeconds (const BandSettings& s) noexcept
    {
        const double hz = std::max (static_cast<double> (s.frequency), 1.0);
        switch (s.shape)
        {
            case Shape::bell: return std::clamp (2.0 / (hz * std::min (1.0, static_cast<double> (s.q))), minWindowSeconds, maxWindowSeconds);
            case Shape::highShelf: return std::clamp (2.0 / hz, minWindowSeconds, maxWindowSeconds);
            case Shape::lowShelf:
            case Shape::tiltShelf:
            case Shape::flatTilt:
            case Shape::lowCut:
            case Shape::highCut:
            case Shape::notch:
            case Shape::bandPass:
            case Shape::allPass: break;
        }
        return maxWindowSeconds;
    }

    void prepare (double newSampleRate, const BandSettings& settings) noexcept
    {
        sampleRate = newSampleRate;
        const double controlRate = sampleRate / controlInterval;
        for (auto* ramp : { &range, &threshold, &autoMix, &fade })
            ramp->setTime (Band::rampSeconds, controlRate);
        averageCoef = static_cast<float> (coefficient (averageSeconds, controlRate));
        zoneReady = false;
        apply (settings);
        range.snap (range.target);
        threshold.snap (threshold.target);
        autoMix.snap (autoMix.target);
        fade.snap (fade.target);
        restart();
    }

    void setSettings (const BandSettings& settings) noexcept
    {
        const bool wasRunning = isRunning();
        apply (settings);
        if (! wasRunning && active)
            restart(); // estaba quieto: arranca sin el nivel de hace rato
    }

    // True mientras el detector tiene algo que hacer (encendido, o soltando la ganancia al apagarse).
    [[nodiscard]] bool isRunning() const noexcept { return active || fade.value > 0.0f || gainDb != 0.0f; }
    [[nodiscard]] bool listensToSidechain() const noexcept { return external; }

    // La banda se apagó del todo (el motor ya no la procesa): sin nada que soltar.
    void stop() noexcept
    {
        fade.snap (0.0f);
        smoothedDb = gainDb = 0.0f;
    }

    // Una muestra de lo que escucha el detector (la señal de la banda o el sidechain). Devuelve true cada controlInterval
    // muestras, con una ganancia nueva en getGainDb(). En estéreo cuenta el lado más fuerte (los dos canales se tocan
    // igual: la imagen no se corre).
    bool feedStereo (float left, float right) noexcept
    {
        float square = 0.0f;
        if (zoneReady)
        {
            zone.advance();
            const auto z = zone.zoneStereo (left, right);
            square = std::max (z[0] * z[0], z[1] * z[1]);
        }
        else
        {
            const float mid = 0.5f * (left + right), side = 0.5f * (left - right);
            switch (placement)
            {
                case Placement::stereo: square = std::max (left * left, right * right); break;
                case Placement::left: square = left * left; break;
                case Placement::right: square = right * right; break;
                case Placement::mid: square = mid * mid; break;
                case Placement::side: square = side * side; break;
            }
        }
        return detect (square);
    }

    bool feedMono (float x) noexcept
    {
        float square = 0.0f;
        if (zoneReady)
        {
            zone.advance();
            const float z = zone.zoneMono (x);
            square = z * z;
        }
        else if (placement != Placement::side)
            square = x * x;
        return detect (square);
    }

    // Para la banda y la GUI: la ganancia dinámica (dB), el nivel de la zona y el umbral que se está usando.
    [[nodiscard]] float getGainDb() const noexcept { return gainDb; }
    [[nodiscard]] float getLevelDb() const noexcept { return levelDb; }
    [[nodiscard]] float getThresholdDb() const noexcept { return effectiveThreshold; }

private:
    static double coefficient (double seconds, double rate) noexcept
    {
        return seconds > 0.0 ? 1.0 - std::exp (-1.0 / (seconds * rate)) : 1.0;
    }

    void apply (const BandSettings& s) noexcept
    {
        const auto& d = s.dynamics;
        active = s.enabled && d.enabled && canBeDynamic (s.shape) && ! isSpectral (s); // la espectral es de la Fase 11
        external = d.external;
        placement = s.placement;
        fade.setTarget (active ? 1.0f : 0.0f);
        range.setTarget (std::clamp (d.rangeDb, DynamicSettings::minRangeDb, DynamicSettings::maxRangeDb));
        threshold.setTarget (std::clamp (d.thresholdDb, DynamicSettings::minThresholdDb, DynamicSettings::maxThresholdDb));
        autoMix.setTarget (d.autoThreshold ? 1.0f : 0.0f);
        const double controlRate = sampleRate / controlInterval;
        attackCoef = static_cast<float> (coefficient (std::clamp (d.attackMs, DynamicSettings::minAttackMs, DynamicSettings::maxAttackMs) * 0.001, controlRate));
        releaseCoef = static_cast<float> (coefficient (std::clamp (d.releaseMs, DynamicSettings::minReleaseMs, DynamicSettings::maxReleaseMs) * 0.001, controlRate));
        windowCoef = coefficient (windowSeconds (s), sampleRate);

        // La zona del detector = la del solo (con su canal). Si cambia su clase de filtro, la banda de la zona se funde
        // sola (como cualquier banda); si aparece o desaparece (un tilt), se prepara de cero.
        if (const auto zoneSettings = soloZoneFor (s))
        {
            if (zoneReady)
                zone.setSettings (*zoneSettings);
            else
                zone.prepare (sampleRate, *zoneSettings);
            zoneReady = true;
        }
        else
            zoneReady = false;
    }

    void restart() noexcept
    {
        meanSquare = 0.0;
        counter = 0;
        averageValid = false;
        levelDb = floorDb;
        smoothedDb = gainDb = 0.0f;
    }

    bool detect (float square) noexcept
    {
        // En double: con 20 ms a 96 kHz el coeficiente es ~5·10⁻⁴ y en float el paso se perdería en las colas.
        meanSquare += (static_cast<double> (square) - meanSquare) * windowCoef;
        if (++counter < controlInterval)
            return false;
        counter = 0;

        // ×2 = +3 dB: el RMS de un seno es su pico / √2.
        const double power = 2.0 * meanSquare;
        levelDb = power > 1.0e-12 ? std::max (floorDb, static_cast<float> (10.0 * std::log10 (power))) : floorDb;
        if (levelDb > silenceDb)
        {
            average = averageValid ? average + (levelDb - average) * averageCoef : levelDb;
            averageValid = true;
        }
        const float manual = threshold.next();
        const float automatic = autoMix.next();
        const float typical = averageValid ? average + autoMarginDb : manual; // sin señal todavía no hay "nivel típico"
        effectiveThreshold = manual + (typical - manual) * automatic;

        const float target = gainChangeDb (levelDb, effectiveThreshold, range.next());
        smoothedDb += (target - smoothedDb) * (std::abs (target) > std::abs (smoothedDb) ? attackCoef : releaseCoef);
        if (std::abs (target - smoothedDb) < 1.0e-3f) // el filtro de 1 polo nunca llega: a 0.001 dB, se copia
            smoothedDb = target;
        const float amount = fade.next();
        gainDb = amount == 1.0f ? smoothedDb : smoothedDb * amount;
        return true;
    }

    double sampleRate = 44100.0;
    bool active = false, external = false;
    Placement placement = Placement::stereo;
    Band zone; // el filtro de la zona (con sus rampas: mover la frecuencia de la banda también mueve la zona sin saltos)
    bool zoneReady = false;
    double windowCoef = 1.0, meanSquare = 0.0;
    float attackCoef = 1.0f, releaseCoef = 1.0f;
    deepwater::dsp::LinearRamp range, threshold, autoMix, fade; // avanzan una vez por controlInterval
    float averageCoef = 0.0f, average = 0.0f;
    bool averageValid = false;
    int counter = 0;
    float levelDb = floorDb, effectiveThreshold = 0.0f, smoothedDb = 0.0f, gainDb = 0.0f;
};

} // namespace fathom::dsp
