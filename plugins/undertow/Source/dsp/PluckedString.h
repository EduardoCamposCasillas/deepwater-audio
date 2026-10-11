#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

#include "deepwater/dsp/DelayLine.h"

namespace undertow::dsp
{

// Fase 13: con qué se puntea la cuerda. El orden se guarda en los proyectos (índice): solo añadir al final.
enum class StringExcitation
{
    whiteNoise,    // ruido blanco: todos los armónicos con la misma fuerza (el sonido de la Fase 12)
    filteredNoise, // ruido por el paso bajo de Pick Tone: una púa más blanda, o un dedo
    impulse,       // un solo golpe (1 muestra) por el mismo paso bajo: siempre igual, sin el "grano" del ruido
};

// Cómo es el punteo. Se lee solo al puntear: moverlo con la cuerda sonando cambia la nota siguiente, no esta.
struct PluckShape
{
    StringExcitation excitation = StringExcitation::whiteNoise;
    float pickPosition = 0.0f;  // 0..0.5 del largo de la cuerda (0 = sin peine; 0.5 = en el centro)
    float pickToneHz = 2500.0f; // corte del paso bajo de la excitación (White Noise no lo usa)

    bool operator== (const PluckShape&) const = default;
};

// Fase 12: cuerda pulsada (Karplus-Strong, con las extensiones de Jaffe & Smith). Sin dependencias de JUCE.
//
// La cuerda es una línea de retardo que se realimenta: lo que sale por el final vuelve a entrar por el principio
// después de pasar por un filtro de lazo. Al puntear, un golpe de ruido de un período entra en la línea; como se repite
// cada vuelta, se oye como un tono. En cada vuelta el filtro (paso bajo de 1 polo) quita un poco de agudos y la ganancia
// un poco de todo: los armónicos altos mueren primero, como en una cuerda real.
//
//   excitación ──(+)──► línea de retardo (Lagrange) ──► filtro de 1 polo ──► × g ──┐
//                 ▲                                                               │
//                 └───────────────────────────────────────────────────────────────┘
//
// Afinación exacta: una vuelta completa debe durar fs/f0 muestras. La vuelta es la línea (parte entera + fracción con
// Lagrange) más el retardo de fase del filtro A ESA frecuencia (que cambia con el Damping) más la muestra de la
// realimentación. Se calculan los dos retardos exactos en f0 y la línea se acorta lo que haga falta.
//
// Afinar la fase en f0 no basta cuando se pierde mucho por vuelta (notas graves con Decay corto o Damping alto): un modo
// que se apaga deprisa "resbala" hacia donde el filtro deja pasar más, y queda un poco bajo. Con G = ganancia de la
// vuelta, τg = su retardo de grupo y m = d ln|G| / dω en f0, el modo cae en Δω = −ln|G|·m / (τg² + m²) (desarrollo de
// primer orden del polo alrededor de f0): se afina el lazo para f0 − Δω. La misma cuenta da la caída real por muestra,
// ln|G|·τg / (τg² + m²), que es la que se ajusta al T60 (con un filtro fuerte, una vuelta "dura" τg, no el período).
//
// Decay = T60 del fundamental en segundos, igual en todas las notas: una nota aguda da más vueltas por segundo, así que
// pierde menos en cada una (g = 10^(−3 / (T60 · f0)) por vuelta, compensando lo que ya quita el filtro en f0).
// Damping = cuánto más rápido mueren los agudos: el T60 a 2.5 kHz es Decay × 0.003^damping (de igual que el fundamental
// con 0 % a 300 veces más corto con 100 %). El filtro se diseña para cumplir esos dos tiempos.
// Límite: un paso bajo deja pasar la continua más que el fundamental, así que la ganancia que compensa el filtro en f0
// también la sube. Para que la continua caiga como mucho 4 veces más lento que el fundamental, en notas agudas con
// Decay largo el Damping alto se ablanda solo (de todas formas allí los armónicos son pocos y mueren deprisa).
class PluckedString
{
public:
    static constexpr double minFrequency = 8.0;    // Hz: la línea se reserva para esta nota (MIDI 0 = 8.2 Hz)
    static constexpr double minPeriodSamples = 8.0; // tope agudo: fs/8 (5.5 kHz a 44.1 kHz)
    static constexpr float minDecaySeconds = 0.05f;
    static constexpr float maxDecaySeconds = 30.0f;
    static constexpr float maxPickPosition = 0.5f;
    static constexpr float minPickToneHz = 200.0f;
    static constexpr float maxPickToneHz = 20000.0f;

    // Se llama fuera del hilo de audio: reserva la línea y el buffer de la excitación para la nota más grave posible.
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        maxPeriod = static_cast<int> (std::ceil (sampleRate / minFrequency)) + 8;
        line.prepare (maxPeriod + 8);
        excitation.assign (static_cast<size_t> (maxPeriod + maxPeriod / 2 + 8), 0.0f); // + el peine (medio período)
        scratch.assign (static_cast<size_t> (maxPeriod), 0.0f);
        smoothingCoef = static_cast<float> (1.0 - std::exp (-1.0 / (smoothingSeconds * sampleRate)));
        muteCoef = static_cast<float> (1.0 - std::exp (-1.0 / (muteSeconds * sampleRate)));
        reset();
    }

    // Cuerda quieta (nota nueva desde silencio). No reserva nada: solo borra.
    void reset() noexcept
    {
        line.reset();
        filterState = 0.0f;
        excitationLength = excitationIndex = 0;
        mute = targetMute = 0.0f;
        silent = true;
        snapParameters();
    }

    // La longitud de la línea sigue a la frecuencia en cada muestra (un glissando no salta); el filtro y la ganancia se
    // recalculan a ritmo de control.
    void setFrequency (double hz) noexcept
    {
        if (hz != frequency)
        {
            frequency = hz;
            dirty = lengthDirty = true;
        }
    }

    // 'modulation' (Fase 13, la matriz): 1 = toda la perilla hacia arriba en su escala logarítmica (×600).
    void setDecay (float seconds, float modulation = 0.0f) noexcept
    {
        const float logSeconds = std::log (std::clamp (seconds, minDecaySeconds, maxDecaySeconds));
        targetLogDecay = modulation == 0.0f ? logSeconds
                                            : std::clamp (logSeconds + modulation * logDecayRange, logMinDecay, logMaxDecay);
    }

    void setDamping (float amount) noexcept { targetDamping = std::clamp (amount, 0.0f, 1.0f); }

    // Decay y Damping saltan a su valor (sin el deslizamiento de 10 ms): al empezar una nota desde silencio.
    void snapParameters() noexcept
    {
        logDecay = targetLogDecay;
        damping = targetDamping;
        dirty = true;
        updateCoefficients();
        jumpToTargets();
    }

    // Puntear: un período de excitación entra en la línea (se SUMA a lo que ya vibra: redisparar no hace clic).
    // Se le quita la media: la continua no tiene "armónicos" que el filtro pueda apagar y se quedaría circulando.
    void pluck (std::uint32_t seed, const PluckShape& shape = {}) noexcept
    {
        targetMute = 0.0f; // si se estaba apagando, la nota nueva la vuelve a dejar libre
        if (silent)
        {
            // No suena nada: no hay nada que suavizar (y en silencio los deslizamientos no avanzan).
            logDecay = targetLogDecay;
            damping = targetDamping;
            mute = targetMute;
        }
        updateCoefficients();
        if (silent)
            jumpToTargets();
        const int length = std::clamp (static_cast<int> (std::lround (period)), 1, maxPeriod);
        excitationLength = makeExcitation (seed, shape, length);
        excitationIndex = 0;
        silent = false;
        silentCount = 0;
        quietPeak = 0.0f;
    }

    // Note-off: como apoyar la palma. El Decay baja hacia 0.1 s y el Damping sube hacia el 85 % en ~15 ms: la cuerda se
    // apaga rápido pero sin corte (un corte seco sería un clic).
    void release() noexcept { targetMute = 1.0f; }

    [[nodiscard]] float processSample() noexcept
    {
        if (silent)
            return 0.0f;

        // Decay (en escala logarítmica), Damping y el apagado se deslizan; los coeficientes se recalculan cada
        // 'controlInterval' muestras como mucho (cuestan unas 60 funciones trigonométricas/exponenciales).
        if (logDecay != targetLogDecay || damping != targetDamping || mute != targetMute)
        {
            glide (logDecay, targetLogDecay, smoothingCoef);
            glide (damping, targetDamping, smoothingCoef);
            glide (mute, targetMute, muteCoef);
            dirty = true;
        }
        if (dirty && --samplesUntilUpdate <= 0)
            updateCoefficients();
        else if (lengthDirty)
            updateLength();

        // Ganancia y polo van en rampa lineal hasta el valor del último cálculo: un escalón cada 32 muestras se oiría.
        if (rampSamples > 0)
        {
            loopGain += gainStep;
            feedforward += feedforwardStep;
            if (--rampSamples == 0)
                jumpToTargets();
        }

        const float delayed = line.readLagrange (taps);
        filterState += feedforward * (delayed - filterState); // y = (1 − p)·x + p·y₋₁
        float y = loopGain * filterState;
        if (excitationIndex < excitationLength)
            y += excitation[static_cast<size_t> (excitationIndex++)];
        line.push (y);

        // Una vuelta entera por debajo de −100 dB: la cuerda ya no se oye y deja de calcularse hasta el próximo punteo.
        quietPeak = std::max (quietPeak, std::abs (y));
        if (++silentCount >= static_cast<int> (period) + 4)
        {
            silent = quietPeak < silenceThreshold && excitationIndex >= excitationLength;
            silentCount = 0;
            quietPeak = 0.0f;
        }
        return y;
    }

    // Para los tests.
    [[nodiscard]] double getPeriod() const noexcept { return period; }
    [[nodiscard]] double getLoopGain() const noexcept { return targetLoopGain; }
    [[nodiscard]] double getPole() const noexcept { return 1.0 - targetFeedforward; }
    [[nodiscard]] bool isSilent() const noexcept { return silent; }
    [[nodiscard]] std::vector<float> getExcitation() const // el último punteo (reserva memoria: solo tests)
    {
        return { excitation.begin(), excitation.begin() + excitationLength };
    }

    // Polo p de un paso bajo de 1 polo con ganancia 1 en continua, H = (1 − p) / (1 − p·z⁻¹), tal que |H(ω)|² = r.
    // De (1 − p)² = r·(1 − 2p·cos ω + p²) sale una cuadrática cuyas raíces multiplican 1: la buena es la menor,
    // p = a / (b + √(b² − a²)) con a = 1 − r, b = 1 − r·cos ω (esta forma no pierde precisión en graves).
    // 'oneMinusCos' = 1 − cos ω, calculado aparte (= 2·sin²(ω/2)) por la misma razón.
    [[nodiscard]] static double poleForSquaredGain (double r, double oneMinusCos) noexcept
    {
        if (r >= 1.0)
            return 0.0;
        r = std::max (r, 0.0);
        const double a = 1.0 - r;
        const double b = a + r * oneMinusCos;
        return a / (b + std::sqrt (std::max (b * b - a * a, 0.0)));
    }

private:
    // Fase 13: escribe la excitación en 'excitation' y devuelve cuántas muestras dura. Pasos:
    //   1. la fuente, de un período: ruido blanco (el de la Fase 12, la misma secuencia) o un golpe de 1 muestra;
    //   2. Pick Tone: paso bajo de 1 polo (solo Filtered Noise e Impulse), circular: el período se repite en la cuerda;
    //   3. quitar la media;
    //   4. Pick Position: el peine x[n] − w·x[n − D], que alarga la excitación D muestras;
    //   5. ajustar el nivel.
    int makeExcitation (std::uint32_t seed, const PluckShape& shape, int length) noexcept
    {
        const auto n = static_cast<size_t> (length);
        float* source = scratch.data();
        float* result = excitation.data();

        if (shape.excitation == StringExcitation::impulse)
        {
            std::fill (source, source + n, 0.0f);
            source[0] = 1.0f;
        }
        else
        {
            std::uint32_t state = seed * 2654435761u + 0x9E3779B9u;
            if (state == 0)
                state = 1;
            for (size_t i = 0; i < n; ++i)
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                source[i] = static_cast<float> (state >> 8) * (2.0f / 16777216.0f) - 1.0f; // uniforme en [-1, 1)
            }
        }

        if (shape.excitation != StringExcitation::whiteNoise)
            lowPassCircular (source, n, std::clamp (shape.pickToneHz, minPickToneHz, maxPickToneHz));

        double sum = 0.0;
        for (size_t i = 0; i < n; ++i)
            sum += source[i];
        const auto mean = static_cast<float> (sum / length);
        for (size_t i = 0; i < n; ++i)
            source[i] -= mean;

        // Pick Position (Jaffe & Smith): puntear a una fracción β del largo manda la deformación hacia los dos extremos;
        // la parte que va hacia el puente vuelve invertida β períodos después. Restar la señal retrasada D = β·período
        // anula los armónicos múltiplos de 1/β: en el centro (β = 1/2) todos los pares, el sonido "hueco"; cerca del
        // puente casi ninguno (brillante y nasal). El peso w sube de 0 a 1 en el primer 5 % para que 0 % no salte.
        // El peine es LINEAL (la excitación dura N + D muestras): su espectro es exactamente X(f)·(1 − w·e^(−j2πfD)) y,
        // con D fraccionario sobre el período real, los huecos caen justo en los armónicos. (Con D redondeado, o dando
        // la vuelta dentro de las N muestras enteras del buffer, se quedaban en −20 dB.)
        const float beta = std::clamp (shape.pickPosition, 0.0f, maxPickPosition);
        const float weight = std::min (beta / fullCombPosition, 1.0f);
        size_t total = n;
        float combEnergy = 1.0f; // energía que añade el peine, por unidad de energía del ruido blanco
        if (weight > 0.0f)
            combEnergy = applyComb (source, result, n, static_cast<double> (beta) * period, weight, total);
        else
            std::copy (source, source + n, result);

        // Nivel. Ruido blanco: el de la Fase 12; con el peine se divide por la raíz de la energía que añade (≈ 1 + w²: dos
        // copias del ruido sin relación entre sí). Filtered Noise e Impulse: la energía se lleva exactamente a la del ruido
        // blanco (período / 3): cambiar el tipo de excitación o el Pick Tone cambia el color, no el volumen.
        float gain = 1.0f;
        if (shape.excitation == StringExcitation::whiteNoise)
        {
            if (weight > 0.0f)
                gain = 1.0f / std::sqrt (combEnergy);
        }
        else
        {
            double energy = 0.0;
            for (size_t i = 0; i < total; ++i)
                energy += static_cast<double> (result[i]) * result[i];
            if (energy > 0.0)
                gain = static_cast<float> (std::sqrt (static_cast<double> (length) / (3.0 * energy)));
        }

        // Un golpe concentra la energía en pocas muestras: con el nivel del ruido, en una nota grave y brillante el pico
        // pasaría de 20. Se limita a 4 (el ruido llega a 1; en la salida de la voz, × 0.25 × Level: 0.8 como mucho): un
        // Impulse brillante en notas graves suena algo más suave que el ruido.
        float peak = 0.0f;
        for (size_t i = 0; i < total; ++i)
            peak = std::max (peak, std::abs (result[i]));
        gain = std::min (gain, maxExcitationPeak / std::max (peak, 1.0e-20f));
        if (gain != 1.0f)
            for (size_t i = 0; i < total; ++i)
                result[i] *= gain;
        return static_cast<int> (total);
    }

    // El peine: result[i] = source[i] − w·source(i − D), con source = 0 fuera de sus n muestras. D fraccionario con
    // Lagrange de 3.er orden (muestras j − 1 … j + 2; las fórmulas de deepwater::dsp::LagrangeTaps escritas por
    // posición): la fracción es la misma en todas las muestras, así que los pesos se calculan una vez.
    // 'total' = n + D + 2 (hasta donde llega la copia retrasada). Devuelve la energía de la salida para ruido blanco de
    // energía 1: 1 + w²·Σℓ² − 2w·(el peso que cae sobre la propia muestra, si D < 2). La interpolación suaviza un poco la
    // copia (Σℓ² < 1): compensar con 1 + w² dejaba el ruido con peine hasta 0.7 dB más suave.
    float applyComb (const float* source, float* result, size_t n, double delay, float weight, size_t& total) const noexcept
    {
        const double whole = std::floor (-delay);
        const auto f = static_cast<float> (-delay - whole);
        const std::array<float, 4> lagrange { -f * (f - 1.0f) * (f - 2.0f) / 6.0f, (f + 1.0f) * (f - 1.0f) * (f - 2.0f) / 2.0f,
                                              -(f + 1.0f) * f * (f - 2.0f) / 2.0f, (f + 1.0f) * f * (f - 1.0f) / 6.0f };
        const auto shift = static_cast<long long> (whole) - 1; // primera de las 4 muestras = i + shift
        const auto size = static_cast<long long> (n);

        total = std::min (excitation.size(), n + static_cast<size_t> (std::ceil (delay)) + 2);
        for (size_t i = 0; i < total; ++i)
        {
            const auto base = static_cast<long long> (i) + shift;
            float delayed = 0.0f;
            if (base >= 0 && base + 3 < size)
            {
                const float* x = source + base;
                delayed = lagrange[0] * x[0] + lagrange[1] * x[1] + lagrange[2] * x[2] + lagrange[3] * x[3];
            }
            else
            {
                for (long long k = 0; k < 4; ++k)
                    if (base + k >= 0 && base + k < size)
                        delayed += lagrange[static_cast<size_t> (k)] * source[base + k];
            }
            result[i] = (i < n ? source[i] : 0.0f) - weight * delayed;
        }

        double energy = 1.0;
        for (size_t k = 0; k < lagrange.size(); ++k)
        {
            energy += static_cast<double> (weight) * weight * lagrange[k] * lagrange[k];
            if (shift + static_cast<long long> (k) == 0) // la toma cae sobre la propia muestra
                energy -= 2.0 * weight * lagrange[k];
        }
        return static_cast<float> (energy);
    }

    // Paso bajo TPT de 1 polo (el de deepwater::dsp::OnePole) sobre una señal periódica, sin transitorio de arranque:
    // empieza con el estado que tendría tras infinitos períodos. Desde 0, una vuelta deja el estado en S; sin entrada,
    // una vuelta lo multiplica por aᴺ (a = 1 − 2G es el polo). El estado periódico cumple s = aᴺ·s + S → s = S / (1 − aᴺ).
    // Una pasada para medir S y otra para filtrar.
    void lowPassCircular (float* signal, size_t n, float cutoffHz) const noexcept
    {
        const double cutoff = std::min (static_cast<double> (cutoffHz), 0.49 * sampleRate);
        const auto g = static_cast<float> (std::tan (std::numbers::pi * cutoff / sampleRate));
        const float G = g / (1.0f + g);

        float state = 0.0f;
        for (size_t i = 0; i < n; ++i)
            state += 2.0f * G * (signal[i] - state);
        const double turn = std::pow (1.0 - 2.0 * static_cast<double> (G), static_cast<double> (n));
        state = static_cast<float> (state / (1.0 - turn));

        for (size_t i = 0; i < n; ++i)
        {
            const float v = (signal[i] - state) * G;
            const float low = v + state;
            state = low + v;
            signal[i] = low;
        }
    }

    static void glide (float& value, float target, float coef) noexcept
    {
        if (value == target)
            return;
        value += (target - value) * coef;
        if (std::abs (target - value) < 1.0e-5f)
            value = target;
    }

    [[nodiscard]] double clampedFrequency() const noexcept
    {
        return std::clamp (frequency, minFrequency, sampleRate / minPeriodSamples);
    }

    // Solo la longitud de la línea, con el filtro y la corrección de afinación del último cálculo completo.
    void updateLength() noexcept
    {
        lengthDirty = false;
        period = sampleRate / clampedFrequency();
        taps = deepwater::dsp::LagrangeTaps::forDelay (period * periodScale - delayOffset);
    }

    void updateCoefficients() noexcept
    {
        dirty = lengthDirty = false;
        samplesUntilUpdate = controlInterval;
        constexpr double pi = std::numbers::pi;
        using Complex = std::complex<double>;

        const double hz = clampedFrequency();
        period = sampleRate / hz;
        const double omega0 = 2.0 * pi * hz / sampleRate;
        const auto oneMinusCosOf = [] (double w) { return 2.0 * std::sin (0.5 * w) * std::sin (0.5 * w); };

        // Apagado (note-off): el Decay se acerca a 0.1 s (si era más largo) y el Damping al 85 % (si era menor).
        const double decay = std::exp (logDecay + mute * std::min (0.0, std::log (mutedDecaySeconds) - logDecay));
        const double damp = damping + mute * std::max (0.0f, mutedDamping - damping);

        // Ganancia por vuelta que necesita el fundamental para caer 60 dB en 'decay' segundos (f0 vueltas por segundo).
        const double passGain = std::pow (10.0, -3.0 / (decay * hz));

        // Filtro: el T60 en la frecuencia de referencia (o en el 2.º armónico, si la nota está por encima) debe ser
        // decay × ratio. El cociente de ganancias por vuelta entre esa frecuencia y el fundamental fija el polo.
        const double ratio = std::pow (minDampingRatio, damp);
        const double referenceHz = std::min (std::max (dampingReferenceHz, 2.0 * hz), 0.45 * sampleRate);
        const double referenceGain = std::pow (10.0, -3.0 / (ratio * decay * hz));
        const double referenceRatio = referenceGain / passGain;
        double pole = poleForSquaredGain (referenceRatio * referenceRatio, oneMinusCosOf (2.0 * pi * referenceHz / sampleRate));

        // Límites del polo:
        // 1) El filtro tiene su máximo en continua: la ganancia que compensa lo que quita en f0 también se aplica a la
        //    continua (y a lo que haya muy por debajo del fundamental). Si el filtro quitara demasiado, la continua no se
        //    apagaría nunca (o crecería). Se limita a caer como mucho 4 veces más lento que el fundamental:
        //    ganancia en continua ≤ passGain^(1/4), así que |H(f0)| ≥ passGain / dcLimit.
        //    Sin este límite, al soltar la tecla (filtro muy fuerte compensado) quedaba un resto de continua.
        const double dcLimit = std::min (maxDcGain, std::pow (passGain, 1.0 / maxDcDecayRatio));
        pole = std::min (pole, poleForSquaredGain ((passGain / dcLimit) * (passGain / dcLimit), oneMinusCosOf (omega0)));
        // 2) Su retardo (como mucho p / (1 − p) muestras, en continua) debe caber en la vuelta con margen para Lagrange.
        pole = std::min (pole, (period - 4.0) / (period - 3.0));

        // Dos pasadas: la 1.ª en f0 calcula cuánto resbalará el modo; la 2.ª afina el lazo para f0 − Δω.
        // Si la ganancia que hace falta pasa del límite (Lagrange también quita un poco en f0 y el límite 1 no lo
        // contaba), se ablanda el filtro lo justo y se repiten las dos: así el Decay se cumple también en las notas
        // más agudas.
        double omega = omega0;
        double designPeriod = period, request = 0.0, logLoss = 0.0, filterGain = 1.0;
        for (int pass = 0; pass < 4; ++pass)
        {
            if (pass == 2)
            {
                const double needed = std::exp (logLoss) / (dcLimit * std::abs (taps.response (omega)));
                if (std::exp (logLoss) / filterGain <= dcLimit || pole == 0.0)
                    break;
                pole = std::min (pole, poleForSquaredGain (needed * needed, oneMinusCosOf (omega)));
                omega = omega0;
            }

            // Respuesta del filtro: H = (1 − p) / (1 − p·e^(−jω)), con 1 − p·cos ω = (1 − p) + p·(1 − cos ω) (precisa
            // con p cerca de 1).
            const Complex denominator ((1.0 - pole) + pole * oneMinusCosOf (omega), pole * std::sin (omega));
            const double filterDelay = std::arg (denominator) / omega;
            filterGain = (1.0 - pole) / std::abs (denominator);

            // La vuelta: 1 (realimentación) + línea + filtro = el período. Lagrange se aparta un poco del retardo pedido
            // (en graves nada; en C7 a 44.1 kHz, milésimas de muestra): se mide y se corrige.
            designPeriod = 2.0 * pi / omega;
            const double lineDelay = designPeriod - 1.0 - filterDelay;
            taps = deepwater::dsp::LagrangeTaps::forDelay (lineDelay);
            request = lineDelay - (taps.phaseDelay (omega) - lineDelay);
            taps = deepwater::dsp::LagrangeTaps::forDelay (request);

            // d ln G / dω = m − j·τg, sumando la de cada pieza: retardo entero (−j·N), Lagrange (L' / L) y el filtro
            // (−j·p·e^(−jω) / (1 − p·e^(−jω))).
            const Complex lagrange = taps.response (omega);
            Complex lagrangeSlope {};
            for (size_t k = 0; k < taps.weights.size(); ++k)
                lagrangeSlope += Complex (0.0, -static_cast<double> (k)) * static_cast<double> (taps.weights[k])
                                 * std::polar (1.0, -omega * static_cast<double> (k));
            const Complex pullBack = pole * std::polar (1.0, -omega);
            const Complex slope = Complex (0.0, -1.0 - taps.first) + lagrangeSlope / lagrange
                                  - Complex (0.0, 1.0) * pullBack / (1.0 - pullBack);
            const double m = slope.real();
            const double groupDelay = -slope.imag();
            const double norm = groupDelay * groupDelay + m * m;

            // Pérdida por vuelta (ln|G|) para que el modo caiga 60 dB en 'decay' segundos, y cuánto resbala.
            logLoss = -3.0 * std::numbers::ln10 * norm / (groupDelay * decay * sampleRate);
            filterGain *= std::abs (lagrange);
            if (pass % 2 == 0)
                omega = omega0 + logLoss * m / norm;
        }

        periodScale = designPeriod / period;
        delayOffset = designPeriod - request;
        targetLoopGain = static_cast<float> (std::min (std::exp (logLoss) / filterGain, dcLimit));
        targetFeedforward = static_cast<float> (1.0 - pole);
        rampSamples = controlInterval;
        gainStep = (targetLoopGain - loopGain) / static_cast<float> (controlInterval);
        feedforwardStep = (targetFeedforward - feedforward) / static_cast<float> (controlInterval);
    }

    void jumpToTargets() noexcept
    {
        loopGain = targetLoopGain;
        feedforward = targetFeedforward;
        rampSamples = 0;
    }

    static constexpr double smoothingSeconds = 0.01;
    static constexpr double muteSeconds = 0.015;
    static constexpr double mutedDecaySeconds = 0.1;
    static constexpr float mutedDamping = 0.85f;
    static constexpr double dampingReferenceHz = 2500.0;
    static constexpr double minDampingRatio = 0.003;   // T60 a 2.5 kHz con Damping 100 % = Decay × 0.003
    static constexpr double maxDcGain = 1.0 - 1.0e-5;  // la continua nunca se queda circulando para siempre
    static constexpr double maxDcDecayRatio = 4.0;     // ... y cae como mucho 4 veces más lento que el fundamental
    static constexpr float silenceThreshold = 1.0e-5f; // −100 dB
    static constexpr float fullCombPosition = 0.05f;   // desde aquí el peine de Pick Position es completo (w = 1)
    static constexpr float maxExcitationPeak = 4.0f;
    static inline const float logMinDecay = std::log (minDecaySeconds);
    static inline const float logMaxDecay = std::log (maxDecaySeconds);
    static inline const float logDecayRange = logMaxDecay - logMinDecay;
    static constexpr int controlInterval = 32; // 0.7 ms a 44.1 kHz: la longitud de la línea sí sigue al tono en cada muestra

    deepwater::dsp::DelayLine line;
    std::vector<float> excitation;
    std::vector<float> scratch; // Fase 13: la fuente de la excitación antes del peine
    deepwater::dsp::LagrangeTaps taps;
    double periodScale = 1.0; // período de diseño / período de la nota (la corrección del "resbalón" del modo)
    double delayOffset = 1.0; // período de diseño − retardo de la línea (realimentación + filtro + corrección de Lagrange)
    double sampleRate = 44100.0;
    double frequency = 440.0;
    double period = 100.0;
    int maxPeriod = 8;
    float loopGain = 0.0f, targetLoopGain = 0.0f, gainStep = 0.0f;
    float feedforward = 1.0f, targetFeedforward = 1.0f, feedforwardStep = 0.0f;
    int rampSamples = 0;
    float filterState = 0.0f;
    float logDecay = 0.0f, targetLogDecay = 0.0f;
    float damping = 0.5f, targetDamping = 0.5f;
    float mute = 0.0f, targetMute = 0.0f;
    float smoothingCoef = 1.0f, muteCoef = 1.0f;
    int excitationLength = 0, excitationIndex = 0;
    int samplesUntilUpdate = 0;
    int silentCount = 0;
    float quietPeak = 0.0f;
    bool dirty = true;
    bool lengthDirty = false;
    bool silent = true;
};

} // namespace undertow::dsp
