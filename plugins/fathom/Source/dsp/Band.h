#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "deepwater/dsp/EqFilter.h"
#include "deepwater/dsp/MatchedEq.h"
#include "deepwater/dsp/Smoothing.h"

namespace fathom::dsp
{

// Las formas de una banda. El orden es el del parámetro "Shape" (se guarda como índice en los proyectos): agregar al
// final, nunca reordenar.
enum class Shape
{
    bell,
    lowShelf,
    highShelf,
    tiltShelf,
    flatTilt,
    lowCut,
    highCut,
    notch,
    bandPass,
    allPass,
};
inline constexpr int numShapes = 10;

// Pendientes de los cortes en dB/oct (6 por polo). También es el orden del parámetro "Slope".
inline constexpr std::array<int, 9> slopes { 6, 12, 18, 24, 30, 36, 48, 72, 96 };

// Dónde actúa una banda en una señal estéreo (Fase 8). El orden es el del parámetro "Stereo" (se guarda el índice:
// agregar al final, nunca reordenar).
//   - estéreo: los dos canales igual (lo de las Fases 1-7);
//   - izquierda / derecha: un solo canal;
//   - mid: lo que está en el CENTRO, M = (L + R) / 2 (voz, kick, bajo: lo que suena igual en los dos lados);
//   - side: lo que es DISTINTO entre los lados, S = (L − R) / 2 (el ancho: reverbs, dobles, pads abiertos).
// Como L = M + S y R = M − S, cambiar M cambia los dos lados igual y cambiar S los cambia al revés.
enum class Placement
{
    stereo,
    left,
    right,
    mid,
    side,
};
inline constexpr int numPlacements = 5;

// Qué ajustes usa cada forma (los demás no cambian el sonido; la GUI los atenúa).
constexpr bool usesGain (Shape shape) noexcept
{
    return shape == Shape::bell || shape == Shape::lowShelf || shape == Shape::highShelf || shape == Shape::tiltShelf
           || shape == Shape::flatTilt;
}
constexpr bool usesSlope (Shape shape) noexcept { return shape == Shape::lowCut || shape == Shape::highCut; }
constexpr bool usesQ (Shape shape, int slopeIndex) noexcept
{
    return shape != Shape::flatTilt && ! (usesSlope (shape) && slopes[static_cast<size_t> (slopeIndex)] == 6);
}

// EQ dinámico (Fase 9): la ganancia de la banda se mueve sola según cuánto suena su zona (ver Dynamics.h).
struct DynamicSettings
{
    bool enabled = false;
    float rangeDb = -6.0f;      // hasta dónde se mueve la ganancia con la zona fuerte: − corta, + sube (sobre la ganancia)
    float thresholdDb = -20.0f; // desde qué nivel de la zona (pico, dBFS) empieza a actuar
    float attackMs = 10.0f;
    float releaseMs = 150.0f;
    bool autoThreshold = false; // el umbral sigue al nivel típico de la zona (no usa thresholdDb)
    bool external = false;      // el detector escucha el sidechain en vez de la señal de la banda
    bool spectral = false;      // Fase 11: actúa por frecuencia (bin de la FFT) en vez de mover la banda entera

    static constexpr float minRangeDb = -30.0f;
    static constexpr float maxRangeDb = 30.0f;
    static constexpr float minThresholdDb = -60.0f;
    static constexpr float maxThresholdDb = 6.0f;
    static constexpr float minAttackMs = 0.5f;
    static constexpr float maxAttackMs = 300.0f;
    static constexpr float minReleaseMs = 5.0f;
    static constexpr float maxReleaseMs = 3000.0f;
};

// Solo las formas con ganancia pueden ser dinámicas (lo que se mueve es la ganancia).
constexpr bool canBeDynamic (Shape shape) noexcept { return usesGain (shape); }

// Dinámica espectral (Fase 11, SpectralDynamics.h): las formas que marcan una zona con ganancia. Los tilts inclinan
// todo el espectro (una mitad sube lo que la otra baja): "cortar solo donde sobra" no tiene una zona que mirar.
constexpr bool canBeSpectral (Shape shape) noexcept
{
    return shape == Shape::bell || shape == Shape::lowShelf || shape == Shape::highShelf;
}

// Los ajustes de una banda tal como llegan de los parámetros.
struct BandSettings
{
    bool enabled = false;
    float frequency = 1000.0f; // Hz
    float gainDb = 0.0f;
    float q = 1.0f;
    Shape shape = Shape::bell;
    int slopeIndex = 1; // 12 dB/oct
    // Respuesta analógica (Fase 7): la magnitud del prototipo analógico también cerca de Nyquist (MatchedEq.h). Si es
    // false, la bilineal clásica (la de las Fases 2-6, con la campana "aplastada" arriba). Es un ajuste global del EQ:
    // el procesador lo pone igual en las 24 bandas.
    bool analog = true;
    Placement placement = Placement::stereo; // Fase 8
    DynamicSettings dynamics;                // Fase 9

    static constexpr float minFrequency = 10.0f;
    static constexpr float maxFrequency = 30000.0f; // encima de Nyquist el filtro se limita a 0.49·fs
    static constexpr float minGainDb = -30.0f;
    static constexpr float maxGainDb = 30.0f;
    static constexpr float minQ = 0.025f;
    static constexpr float maxQ = 40.0f;
    // Flat tilt: la ganancia se lee como pendiente, 10 dB = 1 dB/oct (de 20 Hz a 20 kHz hay casi 10 octavas: la
    // ganancia es, más o menos, cuánto cambia de un extremo al otro).
    static constexpr float flatTiltDbPerOctavePerDb = 0.1f;
};

// Fase 11: una banda cuya dinámica es espectral (encendida, dinámica, espectral y con una forma que lo admite). Su
// detector de la Fase 9 queda quieto: la dinámica la hace la etapa espectral.
constexpr bool isSpectral (const BandSettings& s) noexcept
{
    return s.enabled && s.dynamics.enabled && s.dynamics.spectral && canBeSpectral (s.shape);
}

// Una banda del EQ, estéreo, con los parámetros suavizados.
//
// Mover un filtro de golpe es un clic: los coeficientes saltan y el estado del filtro (lo que "recuerda" de las
// muestras anteriores) de pronto no corresponde a la nueva respuesta. Por eso no se suavizan los coeficientes sino los
// PARÁMETROS, cada uno en el dominio en que se oye: la frecuencia en octavas (log2 Hz: de 100 a 200 Hz tarda lo mismo
// que de 5 a 10 kHz), la ganancia en dB y el Q en log (de 0.5 a 1 lo mismo que de 4 a 8). Quietos, no se recalcula nada.
//
// Mientras alguna rampa se mueve (Fase 16), el diseño se calcula a "ritmo de control": cada 'controlInterval' muestras
// se avanzan las rampas ese tramo, se diseña el punto donde terminan y en el medio se interpolan los coeficientes del
// SVF (g, k y la mezcla) desde los de ahora (EqCascade::blend). Diseñar (tan, exp2, el matched) cuesta unas 16 veces
// más que interpolar: con 24 bandas automatizadas, recalcular en cada muestra se llevaba un tercio de un núcleo. El
// SVF es estable con cualquier g > 0 y k > 0, y entre dos valores positivos la recta también lo es; el tramo dura
// 0.36 ms a 44.1 kHz, muy por debajo de la rampa de 20 ms: lo que cambia es el camino, no a dónde llega (el último
// tramo termina justo en el destino, con los mismos coeficientes que la curva de la GUI).
//
// Encender, apagar o cambiar de forma o de pendiente no se puede suavizar así (un corte no "se convierte" en un shelf
// ni 4 etapas en 2). Para eso la banda tiene un fundido 'mix': salida = entrada + mix·(filtrada − entrada), que es
// interpolar la respuesta entre "nada" (1) y la banda (H). Apagar = mix baja a 0 en 20 ms; cambiar de forma = baja a 0,
// se cambia la forma con el filtro en limpio y vuelve a subir. Con mix en 0 y apagada, la banda no procesa nada.
//
// Pasar de la respuesta bilineal a la analógica (o al revés) tampoco es una rampa de un parámetro: son dos diseños
// distintos de la MISMA forma. Ahí no hace falta silenciar la banda (las dos respuestas se parecen; cambian arriba):
// durante 20 ms se calculan las dos y se interpolan sus coeficientes (EqCascade::blend), que el SVF sigue sin clic.
//
// Mid y side (Fase 8) no convierten la señal ida y vuelta: la banda filtra M (o S) y le suma a L y a R solo lo que
// CAMBIÓ, d = filtrada − M (L += d, R += d; en side, L += d, R −= d). Es la misma cuenta (L' = M' + S = L + d) pero si
// la banda no cambia nada, d = 0 exacto y los canales pasan bit a bit. Cambiar de canal (estéreo → mid) es como
// cambiar de forma: el filtro pasa a ver otra señal, así que se funde a 0, se cambia en limpio y vuelve.
//
// La ganancia dinámica (Fase 9) llega desde afuera (Dynamics.h) cada 'dynamicInterval' muestras y se suma a la ganancia
// de la banda. Recalcular el diseño en cada muestra costaría mucho con 24 bandas dinámicas; en cambio, cuando llega un
// valor nuevo se calculan los coeficientes de ese punto y durante las 'dynamicInterval' muestras siguientes se interpolan
// desde los de ahora (como al pasar de bilineal a analógica): el SVF sigue la rampa sin escalones. Si a la vez se mueve
// otro ajuste, la dinámica entra en los tramos de control con lo demás.
class Band
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double rampSeconds = 0.02;
    static constexpr int dynamicInterval = 16; // muestras entre dos valores de la ganancia dinámica (0.36 ms a 44.1 kHz)
    static constexpr int controlInterval = 16; // muestras entre dos diseños mientras se mueven las rampas (Fase 16)

    void prepare (double newSampleRate, const BandSettings& settings) noexcept
    {
        sampleRate = newSampleRate;
        flatTilt.prepare (sampleRate);
        for (auto* ramp : { &octaves, &gainDb, &logQ, &mix, &analogMix })
            ramp->setTime (rampSeconds, sampleRate);
        dynamicDb.setTime (dynamicInterval / sampleRate, sampleRate);
        dynamicDb.snap (0.0f);
        pending = settings;
        mix.snap (settings.enabled ? 1.0f : 0.0f);
        startShape();
    }

    void setSettings (const BandSettings& settings) noexcept
    {
        pending = settings;
        if (isIdle())
        {
            // Apagada y en silencio: no hay nada que suavizar. Si se enciende, arranca desde cero con el fundido.
            if (settings.enabled)
                startShape();
            else
                return;
        }
        const auto targets = targetsFor (settings);
        octaves.setTarget (targets[0]);
        gainDb.setTarget (targets[1]);
        logQ.setTarget (targets[2]);
        analogMix.setTarget (settings.analog ? 1.0f : 0.0f);
        mix.setTarget (settings.enabled && ! needsSwitch() ? 1.0f : 0.0f);
    }

    // True si la banda no hace nada (apagada y con el fundido terminado): el motor se la salta.
    [[nodiscard]] bool isIdle() const noexcept { return mix.value == 0.0f && mix.isSteady() && ! pending.enabled; }

    // Una vez por muestra, antes de process(): avanza las rampas.
    void advance() noexcept
    {
        // El fundido de salida terminó y hay una forma o una pendiente nueva esperando: se cambia ahora, en silencio.
        if (mix.value == 0.0f && mix.isSteady() && needsSwitch())
        {
            startShape();
            mix.setTarget (pending.enabled ? 1.0f : 0.0f);
        }
        mix.next();
        // A mitad de un tramo de control: interpolar hacia su final (en el último paso, el destino exacto).
        if (segmentPosition < segmentLength)
        {
            ++segmentPosition;
            cascade = segmentPosition == segmentLength
                          ? segmentTo
                          : deepwater::dsp::EqCascade::blend (segmentFrom, segmentTo,
                                                              static_cast<float> (segmentPosition) / static_cast<float> (segmentLength));
            return;
        }
        if (staticSteady())
        {
            if (dynamicDb.isSteady())
                return;
            dynamicDb.next();
            // Solo se mueve la ganancia dinámica: interpolar entre los dos extremos (en el último paso, el destino exacto).
            if (blendReady)
                cascade = dynamicDb.isSteady() ? blendTo
                                               : deepwater::dsp::EqCascade::blend (blendFrom, blendTo,
                                                                                    1.0f - static_cast<float> (dynamicDb.remaining)
                                                                                               / static_cast<float> (dynamicDb.rampSamples));
            else
                updateCoefficients();
            return;
        }
        // Un tramo nuevo: las rampas avanzan hasta su final de una vez (o hasta que la última llega, así el último tramo
        // termina justo en el destino) y se diseña ese punto.
        blendReady = false;
        const int length = std::min (controlInterval, std::max ({ octaves.remaining, gainDb.remaining, logQ.remaining, analogMix.remaining }));
        for (int i = 0; i < length; ++i)
            for (auto* ramp : { &octaves, &gainDb, &logQ, &analogMix, &dynamicDb })
                ramp->next();
        segmentFrom = cascade;
        segmentTo = design (shape, slopeIndex, octaves.value, gainDb.value + dynamicDb.value, logQ.value, analogMix.value);
        segmentLength = length;
        segmentPosition = 1;
        cascade = length == 1 ? segmentTo : deepwater::dsp::EqCascade::blend (segmentFrom, segmentTo, 1.0f / static_cast<float> (length));
    }

    // La ganancia dinámica en dB (Fase 9), sumada a la de la banda. Llega cada 'dynamicInterval' muestras y se alcanza
    // en otras tantas (ver la clase).
    void setDynamicGain (float db) noexcept
    {
        if (db == dynamicDb.target)
            return;
        dynamicDb.setTarget (db);
        // A mitad de un tramo de control los coeficientes de ahora no son el punto de partida de la rampa: la dinámica
        // entra en el tramo siguiente (o, si ya no hay otro, se recalcula en cada muestra hasta llegar).
        blendReady = staticSteady() && segmentPosition == segmentLength;
        if (blendReady)
        {
            blendFrom = cascade;
            blendTo = design (shape, slopeIndex, octaves.value, gainDb.value + db, logQ.value, analogMix.value);
        }
    }

    // Una muestra estéreo en el sitio, en el canal donde actúa la banda.
    void processStereo (float& left, float& right) noexcept
    {
        switch (placement)
        {
            case Placement::stereo: processPair (left, right); return;
            case Placement::left: left = process (0, left); return;
            case Placement::right: right = process (1, right); return;
            case Placement::mid:
            {
                const float mid = 0.5f * (left + right);
                const float change = process (0, mid) - mid;
                left += change;
                right += change;
                return;
            }
            case Placement::side:
            {
                const float side = 0.5f * (left - right);
                const float change = process (0, side) - side;
                left += change;
                right -= change;
                return;
            }
        }
    }

    // En mono L = R = M y S = 0: estéreo, izquierda, derecha y mid procesan el canal; side no tiene nada que tocar.
    void processMono (float& x) noexcept
    {
        if (placement != Placement::side)
            x = process (0, x);
    }

    // Solo de banda (Fase 8): lo que deja pasar el filtro SOLO en su canal, de vuelta en L/R (lo demás, silencio).
    // Izquierda = la zona en L y R callado; mid = la zona del centro en los dos lados; side = la del side (L = z, R = −z).
    [[nodiscard]] std::array<float, 2> zoneStereo (float left, float right) noexcept
    {
        switch (placement)
        {
            case Placement::stereo: processPair (left, right); return { left, right };
            case Placement::left: return { process (0, left), 0.0f };
            case Placement::right: return { 0.0f, process (1, right) };
            case Placement::mid:
            {
                const float zone = process (0, 0.5f * (left + right));
                return { zone, zone };
            }
            case Placement::side:
            {
                const float zone = process (0, 0.5f * (left - right));
                return { zone, -zone };
            }
        }
        return { left, right };
    }

    [[nodiscard]] float zoneMono (float x) noexcept { return placement == Placement::side ? 0.0f : process (0, x); }

    [[nodiscard]] float process (int channel, float x) noexcept
    {
        return mixed (x, filter.process (channel, x, cascade));
    }

    // Los dos canales a la vez (Fase 16: SIMD; el mismo resultado que dos process()).
    void processPair (float& left, float& right) noexcept
    {
        float wetLeft = left, wetRight = right;
        filter.processPair (wetLeft, wetRight, cascade);
        left = mixed (left, wetLeft);
        right = mixed (right, wetRight);
    }

    // La respuesta de una banda con estos ajustes, ya quieta (apagada = nada). La GUI la usa para dibujar la curva: son
    // los MISMOS números con los que termina sonando el audio.
    // 'dynamicDb' = la ganancia dinámica que se le suma (la curva que se mueve en vivo en el visor).
    static deepwater::dsp::EqCascade coefficientsFor (const BandSettings& settings, double sampleRate, float dynamicDb = 0.0f) noexcept
    {
        if (! settings.enabled)
            return {};
        deepwater::dsp::FlatTiltDesign design;
        if (settings.shape == Shape::flatTilt)
            design.prepare (sampleRate);
        const auto targets = targetsFor (settings);
        return makeCascade (settings.shape, settings.slopeIndex, targets[0], clampGain (targets[1] + dynamicDb), targets[2], sampleRate,
                            design, settings.analog);
    }

    // Fase 10: el prototipo analógico de la banda (lo que imita el modo de fase natural, en magnitud y fase), con los
    // mismos números que el diseño digital (y la frecuencia limitada a 0.49·fs como en él). El flat tilt no tiene uno:
    // es una escalera de pares digitales (nullopt = no se corrige).
    static std::optional<deepwater::dsp::AnalogCascade> analogPrototype (const BandSettings& settings, double sampleRate,
                                                                          float dynamicDb = 0.0f) noexcept
    {
        using deepwater::dsp::AnalogBiquad;
        using deepwater::dsp::AnalogCascade;
        if (! settings.enabled || settings.shape == Shape::flatTilt)
            return std::nullopt;
        const auto targets = targetsFor (settings);
        const double hz = std::min (std::exp2 (static_cast<double> (targets[0])), 0.49 * sampleRate);
        const double q = std::exp2 (static_cast<double> (targets[2]));
        const auto gain = static_cast<double> (clampGain (targets[1] + dynamicDb));
        const int order = slopes[static_cast<size_t> (clampSlope (settings.slopeIndex))] / 6;
        AnalogCascade cascade;
        cascade.numStages = 1;
        auto& stage = cascade.stages[0];
        switch (settings.shape)
        {
            case Shape::lowCut: return AnalogCascade::butterworth (hz, order, q, true);
            case Shape::highCut: return AnalogCascade::butterworth (hz, order, q, false);
            case Shape::bell: stage = AnalogBiquad::bell (hz, q, gain); break;
            case Shape::lowShelf: stage = AnalogBiquad::lowShelf (hz, q, gain); break;
            case Shape::highShelf: stage = AnalogBiquad::highShelf (hz, q, gain); break;
            case Shape::tiltShelf: stage = AnalogBiquad::tiltShelf (hz, q, gain); break;
            case Shape::notch: stage = AnalogBiquad::notch (hz, q); break;
            case Shape::bandPass: stage = AnalogBiquad::bandPass (hz, q); break;
            case Shape::allPass: stage = AnalogBiquad::allPass (hz, q); break;
            case Shape::flatTilt: return std::nullopt;
        }
        return cascade;
    }

    [[nodiscard]] const deepwater::dsp::EqCascade& getCascade() const noexcept { return cascade; }
    [[nodiscard]] Shape getShape() const noexcept { return shape; }
    [[nodiscard]] Placement getPlacement() const noexcept { return placement; }

private:
    // Fuera del fundido no se mezcla: en 1 sale la banda tal cual (una campana en 0 dB = la entrada bit a bit).
    [[nodiscard]] float mixed (float x, float wet) const noexcept
    {
        const float m = mix.value;
        return m == 1.0f ? wet : m == 0.0f ? x : x + (wet - x) * m;
    }

    // {octavas, dB, log2 Q}.
    static std::array<float, 3> targetsFor (const BandSettings& s) noexcept
    {
        const float hz = std::clamp (s.frequency, BandSettings::minFrequency, BandSettings::maxFrequency);
        const float db = std::clamp (s.gainDb, BandSettings::minGainDb, BandSettings::maxGainDb);
        const float q = std::clamp (s.q, BandSettings::minQ, BandSettings::maxQ);
        return { std::log2 (hz), db, std::log2 (q) };
    }

    static float clampGain (float db) noexcept { return std::clamp (db, BandSettings::minGainDb, BandSettings::maxGainDb); }

    static int clampSlope (int slopeIndex) noexcept { return std::clamp (slopeIndex, 0, static_cast<int> (slopes.size()) - 1); }

    // 'analog' elige el diseño: MatchedEq (magnitud analógica) o la bilineal de EqCoefficients/EqCascade. El flat tilt
    // es igual en los dos (su escalera de pares ya se reparte hasta 0.4·fs).
    static deepwater::dsp::EqCascade makeCascade (Shape shape, int slopeIndex, float octave, float db, float logQ, double sampleRate,
                                                  const deepwater::dsp::FlatTiltDesign& design, bool analog) noexcept
    {
        using deepwater::dsp::EqCascade;
        const double hz = std::exp2 (static_cast<double> (octave));
        const double q = std::exp2 (static_cast<double> (logQ));
        const auto gain = static_cast<double> (db);
        const int order = slopes[static_cast<size_t> (clampSlope (slopeIndex))] / 6;
        switch (shape)
        {
            case Shape::flatTilt: return design.make (hz, gain * static_cast<double> (BandSettings::flatTiltDbPerOctavePerDb));
            case Shape::lowCut:
                return analog ? deepwater::dsp::MatchedEq::butterworthHighPass (hz, order, q, sampleRate)
                              : EqCascade::butterworthHighPass (hz, order, q, sampleRate);
            case Shape::highCut:
                return analog ? deepwater::dsp::MatchedEq::butterworthLowPass (hz, order, q, sampleRate)
                              : EqCascade::butterworthLowPass (hz, order, q, sampleRate);
            case Shape::bell:
            case Shape::lowShelf:
            case Shape::highShelf:
            case Shape::tiltShelf:
            case Shape::notch:
            case Shape::bandPass:
            case Shape::allPass: break;
        }
        return EqCascade::single (analog ? single<deepwater::dsp::MatchedEq> (shape, hz, q, gain, sampleRate)
                                         : single<deepwater::dsp::EqCoefficients> (shape, hz, q, gain, sampleRate));
    }

    // Las formas de una sola etapa, con cualquiera de los dos diseños (tienen la misma interfaz).
    template <typename Design>
    static deepwater::dsp::EqCoefficients single (Shape shape, double hz, double q, double gain, double sampleRate) noexcept
    {
        switch (shape)
        {
            case Shape::lowShelf: return Design::lowShelf (hz, q, gain, sampleRate);
            case Shape::highShelf: return Design::highShelf (hz, q, gain, sampleRate);
            case Shape::tiltShelf: return Design::tiltShelf (hz, q, gain, sampleRate);
            case Shape::notch: return Design::notch (hz, q, sampleRate);
            case Shape::bandPass: return Design::bandPassFilter (hz, q, sampleRate);
            case Shape::allPass: return Design::allPass (hz, q, sampleRate);
            case Shape::bell:
            case Shape::flatTilt:
            case Shape::lowCut:
            case Shape::highCut: break;
        }
        return Design::bell (hz, q, gain, sampleRate);
    }

    // ¿La forma (o la pendiente de un corte, o el canal) pedida es distinta de la que suena?
    [[nodiscard]] bool needsSwitch() const noexcept
    {
        return pending.shape != shape || (usesSlope (shape) && clampSlope (pending.slopeIndex) != slopeIndex)
               || pending.placement != placement;
    }

    // Empieza a sonar la forma pedida, con los ajustes ya en su sitio (sin rampas) y el filtro en limpio. Solo se llama
    // con mix en 0: no se oye.
    void startShape() noexcept
    {
        shape = pending.shape;
        slopeIndex = clampSlope (pending.slopeIndex);
        placement = pending.placement;
        const auto targets = targetsFor (pending);
        octaves.snap (targets[0]);
        gainDb.snap (targets[1]);
        logQ.snap (targets[2]);
        analogMix.snap (pending.analog ? 1.0f : 0.0f);
        dynamicDb.snap (dynamicDb.target);
        blendReady = false;
        segmentPosition = segmentLength = 0;
        filter.reset();
        updateCoefficients();
    }

    [[nodiscard]] bool staticSteady() const noexcept
    {
        return octaves.isSteady() && gainDb.isSteady() && logQ.isSteady() && analogMix.isSteady();
    }

    // El diseño en un punto; 'analogAmount' entre 0 y 1 = los dos diseños con los coeficientes interpolados.
    [[nodiscard]] deepwater::dsp::EqCascade design (Shape s, int slope, float octave, float db, float q, float analogAmount) const noexcept
    {
        db = clampGain (db);
        if (analogAmount == 0.0f || analogAmount == 1.0f)
            return makeCascade (s, slope, octave, db, q, sampleRate, flatTilt, analogAmount == 1.0f);
        return deepwater::dsp::EqCascade::blend (makeCascade (s, slope, octave, db, q, sampleRate, flatTilt, false),
                                                 makeCascade (s, slope, octave, db, q, sampleRate, flatTilt, true), analogAmount);
    }

    void updateCoefficients() noexcept
    {
        cascade = design (shape, slopeIndex, octaves.value, gainDb.value + dynamicDb.value, logQ.value, analogMix.value);
    }

    double sampleRate = 44100.0;
    BandSettings pending;           // lo último que pidieron los parámetros
    Shape shape = Shape::bell;      // la forma que suena ahora
    int slopeIndex = 1;
    Placement placement = Placement::stereo; // el canal donde suena ahora
    deepwater::dsp::LinearRamp octaves, gainDb, logQ, mix;
    deepwater::dsp::LinearRamp analogMix; // 0 = bilineal, 1 = analógica (entre medio, los coeficientes interpolados)
    deepwater::dsp::LinearRamp dynamicDb; // Fase 9: la ganancia dinámica (rampa de 'dynamicInterval' muestras)
    deepwater::dsp::EqCascade blendFrom, blendTo; // los extremos de esa rampa, si nada más se mueve
    bool blendReady = false;
    deepwater::dsp::EqCascade segmentFrom, segmentTo; // Fase 16: el tramo de control en curso
    int segmentLength = 0, segmentPosition = 0;
    deepwater::dsp::FlatTiltDesign flatTilt;
    deepwater::dsp::EqCascade cascade;
    deepwater::dsp::StereoEqCascadeFilter filter; // L y R (mono, mid y side usan el canal 0)
};

} // namespace fathom::dsp
