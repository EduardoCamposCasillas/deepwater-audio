#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

#include "deepwater/dsp/DcBlocker.h"
#include "deepwater/dsp/DelayLine.h"
#include "deepwater/dsp/OnePole.h"

namespace undertow::dsp
{

// Fase 13: con qué se puntea la cuerda. El orden se guarda en los proyectos (índice): solo añadir al final.
enum class StringExcitation
{
    whiteNoise,    // ruido blanco: todos los armónicos con la misma fuerza (el sonido de la Fase 12)
    filteredNoise, // ruido por el paso bajo de Pick Tone: una púa más blanda, o un dedo
    impulse,       // un solo golpe (1 muestra) por el mismo paso bajo: siempre igual, sin el "grano" del ruido
    sources,       // Fase 16: sin punteo; las fuentes de la voz entran sin parar (la cuerda como resonador, ver resonate)
};

// Cómo es el punteo. Se lee solo al puntear: moverlo con la cuerda sonando cambia la nota siguiente, no esta.
// Fase 16: con 'sources' no hay punteo; Pick Position y Pick Tone dan forma a la entrada continua y se leen siempre.
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
//
// Fase 15: rigidez (Stiffness). Una cuerda rígida resiste doblarse; esa fuerza pesa más en las ondas cortas, que viajan
// más rápido: los parciales quedan por encima de los armónicos, fₙ = n·f0·√(1 + B·n²) / √(1 + B) (B = inharmonicidad;
// dividir por √(1 + B) deja el 1.er parcial en la nota). En el lazo: una cascada de M all-pass de 1.er orden iguales,
// A(z) = (a + z⁻¹) / (1 + a·z⁻¹) con a < 0, justo después de la línea. Ganancia 1 en todas las frecuencias (no toca
// el Decay del fundamental) y retardo que BAJA con la frecuencia: los agudos dan la vuelta antes y suben. La línea se
// acorta lo que el all-pass retrasa en f0 (el fundamental sigue afinado) y 'a' se elige para que el 8.º parcial caiga
// donde dice la fórmula (Newton sobre la fase del lazo; la cascada de all-pass iguales es la idea de Van Duyne & Smith).
//
//   línea ──► all-pass × M ──► filtro de 1 polo ──► × g ──► (+ excitación) ──► línea
//
// Con Stiffness 0 el all-pass no existe: el mismo cálculo que en la Fase 14, bit a bit.
//
// 'a' se elige cuando la cuerda empieza a vibrar desde el silencio y no cambia hasta que vuelve a quedar en silencio
// (la rigidez es del material: un redisparo sobre una cuerda que suena la conserva). Probado y descartado: moverla con la
// cuerda sonando. El all-pass guarda energía según su retardo de grupo: al cambiar 'a' el nivel sube o baja 2-7 dB, y con
// muchos cambios (S&H a Stiffness y a Pitch, o redisparos con otra rigidez) esos saltos se suman como un paseo al azar: una
// cuerda de Decay largo llegó a crecer ×10 000. Con 'a' fijo el all-pass es lineal e invariante y la cuerda es tan
// estable como sin él. Una excepción: si la matriz sube tanto la nota que el retardo del all-pass ya no cabe en la vuelta,
// 'a' se acerca a 0 lo justo (como una cuerda que se tensa: B baja con la tensión), solo en esa dirección. Aunque la nota
// vuelva a bajar, 'a' no vuelve: el nivel cambia una vez como mucho y no hay paseo al azar.
//
// Fase 16: la cuerda como resonador. En vez de un golpe al empezar, una señal entra en el lazo en cada muestra (las
// fuentes de la voz: osciladores, sub, ruido). El lazo es un filtro peine realimentado: H = 1 / (1 − G·z⁻ᴺ). Donde
// la vuelta suma una fase de 2π·k (los armónicos de la cuerda) cada vuelta refuerza a la anterior y la ganancia es
// 1 / (1 − |G|); entre armónicos las vueltas se cancelan, 1 / (1 + |G|). Con un Decay largo |G| roza 1: los picos son
// altos y estrechos (la cuerda "elige" sus armónicos de lo que le llega) y tardan en llenarse y en vaciarse.
//
//   entrada ──► filtro DC ──► Pick Tone (paso bajo) ──► Pick Position (peine) ──► × (1 − |G|) ──(+)──► lazo
//
// Nivel: la entrada se multiplica por 1 − |G| (|G| = la ganancia real de la vuelta en el fundamental). Así un tono
// afinado con la cuerda sale con su mismo nivel en el fundamental, con cualquier Decay, y nunca más fuerte; lo
// desafinado sale más bajo (ese es el efecto). Probado y descartado: normalizar la potencia del ruido blanco, que
// deja el ruido igual de fuerte pero sube un tono afinado hasta +40 dB con un Decay largo.
// El filtro DC (5 Hz) protege la continua: el filtro de lazo la deja pasar más que al fundamental (ver arriba) y
// con esta normalización saldría hasta ×4.
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
    static constexpr double maxInharmonicity = 0.05; // B con Stiffness 100 %: campana o barra de metal
    static constexpr int maxDispersionSections = 8;

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
        inputLine.prepare (maxPeriod / 2 + 8); // Fase 16: el peine de la entrada (como mucho medio período)
        inputDcBlocker.prepare (inputDcCutoffHz, sampleRate);
        reset();
    }

    // Cuerda quieta (nota nueva desde silencio). No reserva nada: solo borra.
    void reset() noexcept
    {
        line.reset();
        inputLine.reset();
        inputDcBlocker.reset();
        inputTone.reset();
        inputPickPosition = targetInputPickPosition;
        inputLogTone = targetInputLogTone;
        inputToneDirty = inputCombDirty = true;
        filterState = 0.0f;
        excitationLength = excitationIndex = 0;
        mute = targetMute = 0.0f;
        silent = true;
        clearDispersion();
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

    // Fase 15: 0..1. Se lee al puntear una cuerda en silencio (ver arriba): con la cuerda sonando, la nueva rigidez
    // espera a que vuelva a empezar desde el silencio.
    void setStiffness (float amount) noexcept { targetStiffness = std::clamp (amount, 0.0f, 1.0f); }

    // B (inharmonicidad) de una posición de la perilla: B = 0.05·s⁴. La 4.ª potencia reparte la perilla por lo que se
    // oye: 25 % ≈ 2·10⁻⁴ (cuerda grave de piano), 50 % ≈ 3·10⁻³ (piano agudo), 75 % ≈ 0.016, 100 % = 0.05 (campana).
    [[nodiscard]] static double inharmonicityFor (float stiffness) noexcept
    {
        const double s = std::clamp (static_cast<double> (stiffness), 0.0, 1.0);
        return maxInharmonicity * s * s * s * s;
    }

    // Parcial n de una cuerda rígida, en múltiplos de la nota (el 1.º es la nota).
    [[nodiscard]] static double stiffPartialRatio (double n, double inharmonicity) noexcept
    {
        return n * std::sqrt ((1.0 + inharmonicity * n * n) / (1.0 + inharmonicity));
    }

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
        const bool fromSilence = silent;
        if (fromSilence)
            wakeUp();
        updateCoefficients();
        if (fromSilence)
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

    // Fase 16: deshace la palma (una nota nueva en modo resonador, que no puntea).
    void letRing() noexcept { targetMute = 0.0f; }

    // Fase 16: Pick Position y Pick Tone de la entrada del resonador. Se deslizan 10 ms (mover el peine de golpe
    // haría clic); con la matriz se pueden mover con la nota sonando.
    void setResonatorShape (const PluckShape& shape) noexcept
    {
        targetInputPickPosition = std::clamp (shape.pickPosition, 0.0f, maxPickPosition);
        targetInputLogTone = std::log (std::clamp (shape.pickToneHz, minPickToneHz, maxPickToneHz));
    }

    // La cuerda sin entrada: lo que ya vibra (un punteo) sigue y se apaga solo.
    [[nodiscard]] float processSample() noexcept { return tick<false> (0.0f); }

    // Fase 16: una muestra con 'input' entrando en el lazo (ver el comentario de la clase). Una cuerda en silencio
    // empieza a vibrar cuando llega algo por encima de −100 dB, y vuelve al silencio cuando la entrada y la cola callan.
    [[nodiscard]] float resonate (float input) noexcept
    {
        const float shaped = shapeInput (input);
        if (silent)
        {
            if (std::abs (shaped) < silenceThreshold)
                return 0.0f;
            wakeUp();
            updateCoefficients();
            jumpToTargets();
            silent = false;
            silentCount = 0;
            quietPeak = 0.0f;
        }
        return tick<true> (shaped);
    }

    // Para los tests.
    [[nodiscard]] double getPeriod() const noexcept { return period; }
    [[nodiscard]] double getLoopGain() const noexcept { return targetLoopGain; }
    [[nodiscard]] double getPole() const noexcept { return 1.0 - targetFeedforward; }
    [[nodiscard]] double getInputGain() const noexcept { return targetInputGain; }
    [[nodiscard]] bool isSilent() const noexcept { return silent; }
    [[nodiscard]] int getDispersionSections() const noexcept { return sections; }
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
    // Empezar a vibrar desde el silencio: no hay nada que suavizar (y en silencio los deslizamientos no avanzan).
    void wakeUp() noexcept
    {
        logDecay = targetLogDecay;
        damping = targetDamping;
        mute = targetMute;
        // La rigidez de esta vibración: el all-pass existe solo si hace falta (sin él, el cálculo de la Fase 14).
        clearDispersion();
        if (targetStiffness > 0.0f)
        {
            stiffness = targetStiffness;
            sections = sectionsForPeriod (sampleRate / clampedFrequency());
            designPending = true; // updateCoefficients elige 'a' (necesita el filtro de lazo)
        }
    }

    // Fase 16: la entrada del resonador antes del lazo: filtro DC → Pick Tone → Pick Position (el peine de la
    // Fase 13, aquí en tiempo real sobre una línea propia). Sin el factor 1 − |G|, que va en tick con su rampa.
    [[nodiscard]] float shapeInput (float x) noexcept
    {
        if (inputLogTone != targetInputLogTone)
        {
            glide (inputLogTone, targetInputLogTone, smoothingCoef);
            inputToneDirty = true;
        }
        if (inputPickPosition != targetInputPickPosition)
        {
            glide (inputPickPosition, targetInputPickPosition, smoothingCoef);
            inputCombDirty = true;
        }
        if (inputToneDirty) // tan(): solo cuando Pick Tone se mueve
        {
            inputToneDirty = false;
            inputToneCoef = deepwater::dsp::OnePole::coefficientForCutoff (std::exp (static_cast<double> (inputLogTone)),
                                                                           sampleRate);
        }
        if (inputCombDirty) // también con cada cambio del período (un vibrato)
            updateInputComb();

        x = inputTone.lowPass (inputDcBlocker.process (x), inputToneCoef);
        inputLine.push (x);
        if (inputCombWeight > 0.0f)
            x = (x - inputCombWeight * inputLine.readLagrange (inputCombTaps)) * inputCombNorm;
        return x;
    }

    // El peine de la entrada, como en el punteo: D = β·período, peso que sube de 0 a 1 en el primer 5 %, y nivel
    // dividido por √(1 + w²) (la energía que suma para una entrada de ruido).
    void updateInputComb() noexcept
    {
        inputCombDirty = false;
        inputCombWeight = std::min (inputPickPosition / fullCombPosition, 1.0f);
        if (inputCombWeight > 0.0f)
        {
            inputCombTaps = deepwater::dsp::LagrangeTaps::forDelay (static_cast<double> (inputPickPosition) * period);
            inputCombNorm = 1.0f / std::sqrt (1.0f + inputCombWeight * inputCombWeight);
        }
    }

    // Una muestra del lazo. 'withInput' (Fase 16) suma la entrada del resonador; sin ella, el cálculo de siempre.
    template <bool withInput>
    [[nodiscard]] float tick (float input) noexcept
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
            if constexpr (withInput)
                inputGain += inputGainStep;
            if (--rampSamples == 0)
                jumpToTargets();
        }
        // Fase 15: 'a' solo se mueve cuando la nota sube y el all-pass no cabe; entonces va en rampa con la línea.
        if (dispersionRamp > 0)
        {
            dispersion = --dispersionRamp == 0 ? targetDispersion : dispersion + dispersionStep;
            lengthDirty = true;
        }

        float delayed = line.readLagrange (taps);
        if (sections > 0)
            delayed = disperse (delayed);
        filterState += feedforward * (delayed - filterState); // y = (1 − p)·x + p·y₋₁
        float y = loopGain * filterState;
        if (excitationIndex < excitationLength)
            y += excitation[static_cast<size_t> (excitationIndex++)];
        if constexpr (withInput)
            y += inputGain * input;
        line.push (y);

        // Una vuelta entera por debajo de −100 dB: la cuerda ya no se oye y deja de calcularse hasta el próximo punteo.
        quietPeak = std::max (quietPeak, std::abs (y));
        if (++silentCount >= static_cast<int> (period) + 4)
        {
            silent = quietPeak < silenceThreshold && excitationIndex >= excitationLength;
            if (silent)
                clearDispersion(); // la próxima vibración elige su rigidez (con 0, sin all-pass)
            silentCount = 0;
            quietPeak = 0.0f;
        }
        return y;
    }

    // Fase 13: escribe la excitación en 'excitation' y devuelve cuántas muestras dura. Pasos:
    //   1. la fuente, de un período: ruido blanco (el de la Fase 12, la misma secuencia) o un golpe de 1 muestra;
    //   2. Pick Tone: paso bajo de 1 polo (solo Filtered Noise e Impulse), circular: el período se repite en la cuerda;
    //   3. quitar la media;
    //   4. Pick Position: el peine x[n] − w·x[n − D], que alarga la excitación D muestras;
    //   5. ajustar el nivel.
    // (Con 'sources' la voz no puntea; si alguien lo hace, sale como Filtered Noise.)
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
        // Fase 15: con el all-pass, la vuelta deja sitio al menos a sus M muestras (su retardo con a = 0).
        const double shortest = sections > 0 ? static_cast<double> (sections) + minPeriodBeyondSections : 0.0;
        return std::clamp (frequency, minFrequency, sampleRate / std::max (minPeriodSamples, shortest));
    }

    // Solo la longitud de la línea, con el filtro y la corrección de afinación del último cálculo completo.
    void updateLength() noexcept
    {
        lengthDirty = false;
        period = sampleRate / clampedFrequency();
        inputCombDirty = true;
        if (sections == 0)
        {
            taps = deepwater::dsp::LagrangeTaps::forDelay (period * periodScale - delayOffset);
            return;
        }
        // Con el all-pass, su retardo en f0 cambia con el tono: entre dos cálculos completos se sigue con su pendiente
        // (para un vibrato, exacto de sobra; el cálculo siguiente, a 32 muestras como mucho, lo pone en su sitio).
        // Mientras 'a' va en rampa, exacto con la 'a' de esta muestra.
        const double designPeriod = period * periodScale;
        const double omega = 2.0 * std::numbers::pi / designPeriod;
        const double allpass = dispersionRamp > 0 ? allpassDelayAt (omega, dispersion)
                                                  : allpassDelayNow + allpassDelaySlope * (omega - allpassDelayOmega);
        taps = deepwater::dsp::LagrangeTaps::forDelay (designPeriod - baseOffset - allpass);
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
        // ... junto con el all-pass de la rigidez (Fase 15), si está: lo que retrasa en f0 (o M, su mínimo con a = 0, si
        // todavía hay que elegir 'a'). Lo que deja el filtro es lo más que el all-pass puede retrasar, sin que la línea
        // baje de un cuarto de la vuelta: los agudos casi no se retrasan en el all-pass, así que para ellos la vuelta es
        // la línea. Con 3 muestras de línea (una nota grave muy rígida subida con la matriz) la excitación, que dura un
        // período de la nota original, daba cientos de vueltas mientras entraba y se acumulaba ×20.
        if (sections > 0)
        {
            const double longest = std::min (period - 4.0, (1.0 - minLineFraction) * period - 1.0);
            const double reserve = designPending ? static_cast<double> (sections)
                                                 : std::min (allpassDelayAt (omega0, targetDispersion), longest);
            pole = std::min (pole, (period - 4.0 - reserve) / (period - 3.0 - reserve));
            const Complex atFundamental ((1.0 - pole) + pole * oneMinusCosOf (omega0), pole * std::sin (omega0));
            const double room = longest - std::arg (atFundamental) / omega0;
            const double lowest = dispersionLimit (omega0, room);
            if (designPending)
            {
                designPending = false;
                dispersion = targetDispersion
                    = static_cast<float> (designDispersion (omega0, inharmonicityFor (stiffness), lowest));
            }
            else if (targetDispersion < lowest)
            {
                // La nota subió y no cabe: 'a' hacia 0 lo justo, en rampa (ver arriba: no vuelve).
                targetDispersion = static_cast<float> (lowest);
                dispersionStep = (targetDispersion - dispersion) / static_cast<float> (controlInterval);
                dispersionRamp = controlInterval;
            }
        }

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
            double lineDelay = designPeriod - 1.0 - filterDelay;
            if (sections > 0)
                lineDelay -= allpassDelayAt (omega, targetDispersion);
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
            Complex slope = Complex (0.0, -1.0 - taps.first) + lagrangeSlope / lagrange
                            - Complex (0.0, 1.0) * pullBack / (1.0 - pullBack);
            if (sections > 0) // el all-pass solo retrasa (|A| = 1): suma su retardo de grupo
                slope -= Complex (0.0, static_cast<double> (sections) * allpassGroupDelay (omega, targetDispersion));
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
        if (sections > 0)
        {
            // Para updateLength: el resto de la vuelta (realimentación, filtro, corrección de Lagrange), el retardo del
            // all-pass aquí y su pendiente con ω, d(φ/ω)/dω = (retardo de grupo − de fase) / ω.
            allpassDelayOmega = omega;
            allpassDelayNow = allpassDelayAt (omega, targetDispersion);
            baseOffset = delayOffset - allpassDelayNow;
            allpassDelaySlope = (static_cast<double> (sections) * allpassGroupDelay (omega, targetDispersion) - allpassDelayNow)
                                / omega;
            if (dispersionRamp > 0) // la línea empieza donde está 'a' ahora
                taps = deepwater::dsp::LagrangeTaps::forDelay (designPeriod - baseOffset - allpassDelayAt (omega, dispersion));
        }
        targetLoopGain = static_cast<float> (std::min (std::exp (logLoss) / filterGain, dcLimit));
        targetFeedforward = static_cast<float> (1.0 - pole);
        rampSamples = controlInterval;
        gainStep = (targetLoopGain - loopGain) / static_cast<float> (controlInterval);
        feedforwardStep = (targetFeedforward - feedforward) / static_cast<float> (controlInterval);

        // Fase 16: la entrada del resonador × (1 − |G|), con |G| la ganancia real de la vuelta en el modo (la del
        // lazo × la del filtro y Lagrange): así el pico del fundamental vale 1.
        targetInputGain = static_cast<float> (std::max (1.0 - static_cast<double> (targetLoopGain) * filterGain, 0.0));
        inputGainStep = (targetInputGain - inputGain) / static_cast<float> (controlInterval);
        inputCombDirty = true;
    }

    void jumpToTargets() noexcept
    {
        loopGain = targetLoopGain;
        feedforward = targetFeedforward;
        inputGain = targetInputGain;
        rampSamples = 0;
    }

    // --- Fase 15: el all-pass de la rigidez ---

    // Cuántas secciones: 8 (con más, la curva sigue mejor a la fórmula), pero no más de la mitad de lo que deja la
    // vuelta (período − 4): en notas muy agudas hay pocos parciales y bastan menos.
    [[nodiscard]] static int sectionsForPeriod (double samples) noexcept
    {
        return std::clamp (static_cast<int> ((samples - 4.0) / 2.0), 1, maxDispersionSections);
    }

    void clearDispersion() noexcept
    {
        sections = 0;
        dispersion = targetDispersion = dispersionStep = 0.0f;
        dispersionRamp = 0;
        dispersionState.fill (0.0f);
        baseOffset = allpassDelayNow = allpassDelaySlope = allpassDelayOmega = 0.0;
        designPending = false;
    }

    // Cada sección: y = a·x + s;  s = x − a·y  (forma transpuesta: 2 multiplicaciones y 1 estado por sección).
    [[nodiscard]] float disperse (float x) noexcept
    {
        const float a = dispersion;
        for (int k = 0; k < sections; ++k)
        {
            float& state = dispersionState[static_cast<size_t> (k)];
            const float y = a * x + state;
            state = x - a * y;
            x = y;
        }
        return x;
    }

    // Fase (retraso, en radianes) de una sección a ω: ω − 2·atan(a·sin ω / (1 + a·cos ω)). Con a = 0, ω (1 muestra).
    [[nodiscard]] static double allpassPhase (double omega, double a) noexcept
    {
        return omega - 2.0 * std::atan2 (a * std::sin (omega), 1.0 + a * std::cos (omega));
    }

    // Retardo de grupo de una sección: (1 − a²) / (1 + 2a·cos ω + a²). Con a < 0, mayor en graves que en agudos.
    [[nodiscard]] static double allpassGroupDelay (double omega, double a) noexcept
    {
        return (1.0 - a * a) / (1.0 + 2.0 * a * std::cos (omega) + a * a);
    }

    // Retardo de las M secciones a ω (en muestras) con el coeficiente a.
    [[nodiscard]] double allpassDelayAt (double omega, double a) const noexcept
    {
        return static_cast<double> (sections) * allpassPhase (omega, a) / omega;
    }

    // El 'a' más negativo con el que el all-pass retrasa en f0 como mucho 'room' muestras. Retardo por sección
    // d = room / M: φ(ω1) = d·ω1 despejada da a = sin θ / sin (ω1 − θ), con θ = ω1·(1 − d) / 2.
    [[nodiscard]] double dispersionLimit (double omega1, double room) const noexcept
    {
        const double perSection = std::max (room / static_cast<double> (sections), 1.0);
        const double theta = 0.5 * omega1 * (1.0 - perSection);
        return std::max (std::sin (theta) / std::sin (omega1 - theta), minDispersion);
    }

    // El coeficiente a para la inharmonicidad B. La vuelta (realimentación + línea + all-pass; el filtro de lazo no
    // cuenta: su pequeña dispersión ya estaba en la Fase 14) debe tener fase 2π en el 1.er parcial (la línea se ajusta
    // a eso) y 2π·n en el parcial n de referencia. Despejando la línea de la 1.ª condición, la 2.ª es una ecuación en a:
    //   f(a) = (ωr/ω1)·(2π − M·φ(ω1)) + M·φ(ωr) − 2π·n = 0.
    // Newton desde la mitad del intervalo donde f cambia de signo, sin salir de él (si un paso se sale, bisección). El
    // de referencia es el 8.º parcial, o el punto de fs/4 si el 8.º cae más arriba (cerca de Nyquist el all-pass digital
    // se aparta del analógico). 'lowest' = el 'a' más negativo que cabe: si para la B pedida haría falta más, se queda ahí (la rigidez
    // se satura en las notas más agudas).
    [[nodiscard]] double designDispersion (double omega1, double inharmonicity, double lowest) const noexcept
    {
        constexpr double twoPi = 2.0 * std::numbers::pi;
        const double m = static_cast<double> (sections);
        const double ratio = std::min (stiffPartialRatio (referencePartial, inharmonicity),
                                       referenceLimit * std::numbers::pi / omega1);
        const double omegaR = ratio * omega1;
        // El parcial (fraccionario) que cae en ωr: n²·(1 + B·n²) = r²·(1 + B), resuelta en la forma sin restas.
        const double r2 = ratio * ratio * (1.0 + inharmonicity);
        const double n = std::sqrt (2.0 * r2 / (1.0 + std::sqrt (1.0 + 4.0 * inharmonicity * r2)));
        const auto f = [&] (double a) {
            return ratio * (twoPi - m * allpassPhase (omega1, a)) + m * allpassPhase (omegaR, a) - twoPi * n;
        };
        const auto phaseSlope = [] (double a, double w) { // dφ/da
            return -2.0 * std::sin (w) / (1.0 + 2.0 * a * std::cos (w) + a * a);
        };

        double low = lowest, high = 0.0;
        if (f (low) >= 0.0)
            return low; // ni con todo el retardo disponible llega: saturada
        if (f (high) <= 0.0)
            return 0.0;
        double a = 0.5 * low;
        for (int iteration = 0; iteration < 60; ++iteration)
        {
            const double value = f (a);
            if (value > 0.0)
                high = a;
            else
                low = a;
            const double derivative = m * (phaseSlope (a, omegaR) - ratio * phaseSlope (a, omega1));
            double next = derivative != 0.0 ? a - value / derivative : 0.5 * (low + high);
            if (! (next > low && next < high))
                next = 0.5 * (low + high);
            if (std::abs (next - a) < 1.0e-12)
                return next;
            a = next;
        }
        return a;
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
    static constexpr double referencePartial = 8.0;    // Fase 15: el parcial que el all-pass pone en su sitio
    static constexpr double referenceLimit = 0.5;      // ... salvo que pase de fs/4 (fracción de Nyquist)
    static constexpr double minDispersion = -0.999;    // a: más cerca de −1 el all-pass sería casi un integrador
    static constexpr double minLineFraction = 0.25;    // con el all-pass, la línea es al menos 1/4 de la vuelta
    static constexpr double minPeriodBeyondSections = 5.0; // y la vuelta, al menos M + 5 (con a = 0 retrasa M)
    static constexpr float maxExcitationPeak = 4.0f;
    static inline const float logMinDecay = std::log (minDecaySeconds);
    static inline const float logMaxDecay = std::log (maxDecaySeconds);
    static inline const float logDecayRange = logMaxDecay - logMinDecay;
    static inline const float logMaxPickTone = std::log (maxPickToneHz);
    static constexpr double inputDcCutoffHz = 5.0; // Fase 16: por debajo de la nota más grave (8 Hz)
    static constexpr int controlInterval = 32; // 0.7 ms a 44.1 kHz: la longitud de la línea sí sigue al tono en cada muestra

    deepwater::dsp::DelayLine line;
    // Fase 16: la entrada del resonador.
    deepwater::dsp::DelayLine inputLine;
    deepwater::dsp::DcBlocker inputDcBlocker;
    deepwater::dsp::OnePole inputTone;
    deepwater::dsp::LagrangeTaps inputCombTaps;
    float inputToneCoef = 1.0f, inputCombWeight = 0.0f, inputCombNorm = 1.0f;
    float inputPickPosition = 0.0f, targetInputPickPosition = 0.0f;
    float inputLogTone = logMaxPickTone, targetInputLogTone = logMaxPickTone;
    float inputGain = 0.0f, targetInputGain = 0.0f, inputGainStep = 0.0f;
    bool inputToneDirty = true, inputCombDirty = true;
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
    // Fase 15: el all-pass de la rigidez (0 secciones = no existe). 'dispersion' = a, fijo durante la vibración.
    float stiffness = 0.0f, targetStiffness = 0.0f; // la de esta vibración / la de la perilla + la matriz
    std::array<float, maxDispersionSections> dispersionState {};
    int sections = 0;
    float dispersion = 0.0f, targetDispersion = 0.0f, dispersionStep = 0.0f;
    int dispersionRamp = 0;
    bool designPending = false;
    double baseOffset = 0.0;       // delayOffset sin el all-pass
    double allpassDelayNow = 0.0, allpassDelayOmega = 0.0, allpassDelaySlope = 0.0; // su retardo en f0 y cómo sigue al tono
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
