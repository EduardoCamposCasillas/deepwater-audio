#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "dsp/Warp.h"
#include "dsp/Wavetable.h"
#include "deepwater/dsp/Simd.h"

namespace undertow::dsp
{

namespace simd = deepwater::dsp::simd;

// Oscilador wavetable sin aliasing audible, con unison. Sin dependencias de JUCE para poder probarlo aislado.
//
// Cuatro ideas:
//  1) Acumulador de fase (como en la Fase 1): la fase en [0, 1) multiplicada por el tamaño del frame
//     es la posición de lectura dentro del ciclo.
//  2) Mipmaps: según la frecuencia de la nota se lee la versión del ciclo con los armónicos justos
//     para que ninguno se refleje (alias) dentro de la banda audible.
//  3) Morphing: la posición (0..1) cae entre dos frames y se mezclan los dos.
//  4) Unison (Fase 6): hasta 16 copias de la misma onda, cada una con su fase, un poco desafinadas entre sí
//     y repartidas en el panorama estéreo. Comparten tabla, mipmap y frames: solo cambia la fase de cada una,
//     así que una copia extra cuesta un par de interpolaciones y no un oscilador entero.
//  5) Warp y FM (Fase 7): la fase se deforma (Warp.h) y/o se desplaza con otra señal (FM) antes de leer la
//     tabla. Eso va por un camino aparte (renderWarped); sin warp ni FM el oscilador es el de la Fase 6.
//  6) SIMD (Fase 11): las copias del unison se leen de 4 en 4 con instrucciones SSE2 (ver Simd.h). Cada copia
//     da exactamente el mismo valor que con el código escalar; solo cambia el orden de la suma final.
class WavetableOscillator
{
public:
    static constexpr int maxUnison = 16;

    // Anchura de la zona de fundido entre mipmaps: 1/6 de octava (2 semitonos) antes de cada límite.
    static constexpr double mipBlendOctaves = 1.0 / 6.0;
    static constexpr double mipBlendRatio = 1.122462048309373; // 2^(1/6): la misma franja como razón de frecuencias

    // Detune al 100 %: las copias de los extremos quedan a ±100 cents (±1 semitono) de la nota.
    static constexpr double maxDetuneCents = 100.0;

    // 'harmonicLimitHz': frecuencia máxima de los armónicos de la tabla. 0 = la regla normal (abajo). La voz
    // pasa otro límite cuando el oscilador corre sobremuestreado para FM, ring mod o warp (ver Voice).
    void setSampleRate (double newSampleRate, double harmonicLimitHz = 0.0) noexcept
    {
        sampleRate = newSampleRate;
        maxHarmonicFrequency = harmonicLimitHz > 0.0 ? harmonicLimitHz : defaultHarmonicLimit (sampleRate);
        readSpeedRelease = std::exp (-1.0 / (readSpeedReleaseSeconds * sampleRate));
        warpMipKey = -1.0;

        positionSmoothingCoef = smoothingCoefficient (positionSmoothingSeconds);
        unisonSmoothingCoef = smoothingCoefficient (unisonSmoothingSeconds);
        crossfadeLength = std::max (1, static_cast<int> (tableCrossfadeSeconds * sampleRate));
        updateIncrement();
    }

    void setFrequency (double newFrequencyHz) noexcept
    {
        baseFrequencyHz = newFrequencyHz;
        updateIncrement();
    }

    // Desplazamiento de tono en semitonos: afinación del oscilador (octava, semitonos, fine) más la modulación
    // (vibrato, caídas de pitch). La voz la llama en cada muestra: si el valor no cambió no se recalcula nada, y si
    // cambió, el recálculo espera a la próxima muestra (así un cambio de tono y uno de detune en la misma muestra
    // se calculan juntos, una sola vez).
    void setPitchOffset (float semitones) noexcept
    {
        if (semitones == pitchOffset)
            return;
        pitchOffset = semitones;
        incrementPending = true;
    }

    // Modulación de Position (fracción del recorrido, -1..1). Se suma DESPUÉS del suavizado de la perilla:
    // la perilla se mueve con inercia (sin zipper) y la modulación llega al instante (un LFO rápido o un
    // attack corto no se "emborronan").
    void setPositionModulation (float amount) noexcept { positionModulation = amount; }

    // Cambiar de tabla con la nota sonando haría un salto en la forma de onda (clic):
    // durante unos milisegundos se leen las dos y se hace un fundido cruzado.
    void setWavetable (const Wavetable* newTable) noexcept
    {
        if (newTable == table)
            return;

        if (table != nullptr && newTable != nullptr)
        {
            previousTable = table;
            crossfadeRemaining = crossfadeLength;
        }
        table = newTable;
    }

    void setPosition (float newPosition) noexcept { targetPosition = std::clamp (newPosition, 0.0f, 1.0f); }

    // Unison: cuántas copias suenan, cuánto se desafinan (0..1), cuánto se abren en estéreo (0..1) y el
    // paneo del oscilador entero (-1 izquierda .. +1 derecha). Se llama una vez por bloque.
    void setUnison (int voices, float newDetune, float width, float pan) noexcept
    {
        voices = std::clamp (voices, 1, maxUnison);
        targetDetune = std::clamp (newDetune, 0.0f, 1.0f);
        width = std::clamp (width, 0.0f, 1.0f);
        pan = std::clamp (pan, -1.0f, 1.0f);

        if (voices == numVoices && width == stereoWidth && pan == panPosition)
            return;

        const bool countChanged = voices != numVoices;
        numVoices = voices;
        stereoWidth = width;
        panPosition = pan;
        updateGainTargets();
        if (countChanged)
            updateIncrement(); // las posiciones de detune dependen del número de copias
    }

    // Modulación del detune desde la matriz (fracción del recorrido de la perilla), por muestra.
    void setDetuneModulation (float amount) noexcept { detuneModulation = amount; }

    // Límite normal de los armónicos de la tabla.
    // Un armónico por encima de Nyquist (sr/2) se refleja: si está en f, aparece en sr - f.
    // Si f < sr - 20 kHz, su reflejo cae POR ENCIMA de 20 kHz y no se oye. A 44.1/48 kHz eso deja
    // subir los armónicos hasta 24-28 kHz en lugar de 22-24 kHz: las notas agudas conservan más brillo.
    // A 88.2/96 kHz no hace falta: una octava por debajo de Nyquist ya cubre toda la banda audible,
    // así que ahí no se permite ningún reflejo.
    [[nodiscard]] static double defaultHarmonicLimit (double rate) noexcept
    {
        const double nyquist = 0.5 * rate;
        return nyquist >= 2.0 * audibleLimitHz ? nyquist : std::max (nyquist, rate - audibleLimitHz);
    }

    // --- Fase 7: warp y FM ---
    // Modo de warp y si la fase recibe FM. Son elecciones "de configuración": la voz las cambia con el sonido
    // en silencio (un fundido de 5 ms), porque pasar de un modo a otro cambia la onda de golpe.
    void setWarp (WarpMode mode, bool phaseModulated) noexcept
    {
        if (mode == warp.mode && phaseModulated == usesPhaseModulation)
            return;
        warp = Warp::make (mode, warp.amount);
        usesPhaseModulation = phaseModulated;
        phaseModulation = previousPhaseModulation = 0.0f;
        resetWarpState();
    }

    // Amount del warp (0..1, perilla suavizada + modulación). La voz lo llama en cada muestra.
    void setWarpAmount (float amount) noexcept
    {
        amount = std::clamp (amount, 0.0f, 1.0f);
        if (amount == warp.amount)
            return;
        warp = Warp::make (warp.mode, amount);
        warpMipKey = -1.0;
    }

    // FM (en realidad PM, modulación de FASE, como en el Yamaha DX7): desplazamiento de la posición de lectura,
    // en ciclos. La voz pasa aquí "profundidad × señal moduladora" en cada muestra.
    void setPhaseModulation (float cycles) noexcept { phaseModulation = cycles; }

    [[nodiscard]] const Warp& getWarp() const noexcept { return warp; }
    [[nodiscard]] bool usesWarpPath() const noexcept { return warp.mode != WarpMode::none || usesPhaseModulation; }

    // Para una nota que empieza desde silencio: sin arrastrar transiciones (morph, fundido de tabla, cambios
    // de ganancia del unison) que se quedaron a medias en la nota anterior.
    //
    // Fases: con una sola copia, fase 0 (cada nota arranca igual, con el mismo "golpe"). Con varias, cada una
    // empieza en una fase al azar: si todas salieran juntas, el principio de la nota sonaría a un pico fuerte
    // seguido de un barrido de "flanger" mientras se separan. 'seed' hace que cada nota tenga su reparto.
    void reset (std::uint32_t seed = 0) noexcept
    {
        for (size_t k = 0; k < phases.size(); ++k)
            phases[k] = numVoices == 1 && k == 0 ? 0.0 : randomPhase (seed, static_cast<std::uint32_t> (k));

        position = targetPosition;
        previousTable = nullptr;
        crossfadeRemaining = 0;
        detune = targetDetune;
        gains = targetGains;
        gainsSettled = true;
        updateActiveCount();
        updateIncrement();
        previousPhaseModulation = phaseModulation;
        resetWarpState();
    }

    // Salida mono: la mezcla (L + R) / 2 de lo que daría processStereo.
    [[nodiscard]] float processSample() noexcept
    {
        float left = 0.0f, right = 0.0f;
        if (usesWarpPath())
            renderWarped<false> (left, right);
        else
            render<false> (left, right);
        return left;
    }

    void processStereo (float& left, float& right) noexcept
    {
        left = right = 0.0f;
        if (usesWarpPath())
            renderWarped<true> (left, right);
        else
            render<true> (left, right);
    }

    // True si los canales izquierdo y derecho pueden ser distintos (unison abierto o paneo fuera del centro).
    // La voz lo usa para procesar el filtro en mono cuando no hace falta el estéreo.
    [[nodiscard]] bool isStereo() const noexcept { return stereoTarget || ! gainsSettled; }

    [[nodiscard]] int getMipLevel() const noexcept { return mip.level; }
    [[nodiscard]] float getMipBlend() const noexcept { return mip.blend; }
    [[nodiscard]] int getWarpMipLevel() const noexcept { return warpMip.level; } // lectura principal del warp
    [[nodiscard]] double getHarmonicLimit() const noexcept { return maxHarmonicFrequency; }
    [[nodiscard]] double getFrequency() const noexcept { return frequencyHz; } // la de la copia central
    [[nodiscard]] int getNumVoices() const noexcept { return numVoices; }
    [[nodiscard]] double getVoiceFrequency (int voice) const noexcept
    {
        return frequencyHz * ratios[static_cast<size_t> (voice)];
    }

private:
    // Punteros de lectura de un nivel de una tabla en la posición actual: se calculan una vez por muestra
    // y los comparten todas las copias del unison.
    struct FrameReader
    {
        const float* a = nullptr;
        const float* b = nullptr; // nullptr si la tabla tiene un solo frame
        float frameFraction = 0.0f;
        double size = 0.0;

        [[nodiscard]] float read (double phase) const noexcept
        {
            const double readPosition = phase * size;
            const int index = static_cast<int> (readPosition);
            const auto fraction = static_cast<float> (readPosition - index);

            const float first = interpolate (a + index, fraction);
            if (b == nullptr)
                return first;
            return first + (interpolate (b + index, fraction) - first) * frameFraction;
        }

        // Lo mismo para 4 copias a la vez (fases consecutivas en memoria).
        [[nodiscard]] simd::Float4 read4 (const double* phase) const noexcept
        {
            std::array<int, 4> index;
            const simd::Float4 fraction = simd::readPositions (phase, size, index.data());

            const simd::Float4 first = interpolate4 (a, index, fraction);
            if (b == nullptr)
                return first;
            return first + (interpolate4 (b, index, fraction) - first) * frameFraction;
        }

        // Pendiente de la onda en 'phase' (cuánto sube por ciclo). La usa polyBLAMP.
        [[nodiscard]] float slope (double phase) const noexcept
        {
            const double readPosition = phase * size;
            const int index = static_cast<int> (readPosition);
            const auto fraction = static_cast<float> (readPosition - index);

            float result = derivative (a + index, fraction);
            if (b != nullptr)
                result += (derivative (b + index, fraction) - result) * frameFraction;
            return result * static_cast<float> (size);
        }
    };

    // Nivel de mipmap y cuánto se mezcla con el siguiente (ver mipFor).
    struct Mip
    {
        int level = 0;
        float blend = 0.0f;

        bool operator== (const Mip&) const = default;
    };

    // Cerca del límite entre dos mipmaps se mezclan los dos niveles (ver mipFor).
    struct TableReader
    {
        FrameReader rich, poor;
        float mipBlend = 0.0f;

        [[nodiscard]] float read (double phase) const noexcept
        {
            const float value = rich.read (phase);
            if (mipBlend <= 0.0f)
                return value;
            return value + (poor.read (phase) - value) * mipBlend;
        }

        [[nodiscard]] simd::Float4 read4 (const double* phase) const noexcept
        {
            const simd::Float4 value = rich.read4 (phase);
            if (mipBlend <= 0.0f)
                return value;
            return value + (poor.read4 (phase) - value) * mipBlend;
        }

        [[nodiscard]] float slope (double phase) const noexcept
        {
            const float value = rich.slope (phase);
            if (mipBlend <= 0.0f)
                return value;
            return value + (poor.slope (phase) - value) * mipBlend;
        }
    };

    [[nodiscard]] static FrameReader makeFrameReader (const Wavetable& wavetable, int level, float normalisedPosition) noexcept
    {
        FrameReader reader;
        reader.size = Wavetable::levelSize (level);

        const int numFrames = wavetable.getNumFrames();
        if (numFrames == 1)
        {
            reader.a = wavetable.getFrame (level, 0);
            return reader;
        }

        const float framePosition = normalisedPosition * static_cast<float> (numFrames - 1);
        const int firstFrame = std::min (static_cast<int> (framePosition), numFrames - 2);
        reader.a = wavetable.getFrame (level, firstFrame);
        reader.b = wavetable.getFrame (level, firstFrame + 1);
        reader.frameFraction = framePosition - static_cast<float> (firstFrame);
        return reader;
    }

    [[nodiscard]] static TableReader makeTableReader (const Wavetable& wavetable, float framePosition, Mip level) noexcept
    {
        TableReader reader;
        reader.rich = makeFrameReader (wavetable, level.level, framePosition);
        reader.mipBlend = level.blend;
        if (level.blend > 0.0f)
            reader.poor = makeFrameReader (wavetable, level.level + 1, framePosition);
        return reader;
    }

    // Construir un TableReader (buscar los frames, sus punteros y la mezcla) costaba más que leer la tabla: ~20 ns
    // por muestra. Casi siempre se pide el mismo (posición quieta, mismo mipmap), así que se guarda el último y solo se
    // rehace si cambia algo de lo que depende. Con Position modulada se rehace en cada muestra, como antes.
    struct CachedReader
    {
        const Wavetable* table = nullptr;
        float position = -1.0f;
        Mip mip { -1, 0.0f };
        TableReader reader;

        [[nodiscard]] const TableReader& get (const Wavetable& wavetable, float framePosition, Mip level) noexcept
        {
            if (&wavetable != table || framePosition != position || level != mip)
            {
                table = &wavetable;
                position = framePosition;
                mip = level;
                reader = makeTableReader (wavetable, framePosition, level);
            }
            return reader;
        }
    };

    template <bool stereo>
    void render (float& left, float& right) noexcept
    {
        if (table == nullptr)
            return;

        // Position se suaviza por muestra: girar la perilla recorre los frames intermedios en vez de saltar.
        position += (targetPosition - position) * positionSmoothingCoef;
        const float framePosition = std::clamp (position + positionModulation, 0.0f, 1.0f);

        smoothUnison();

        const TableReader& current = readers.current.get (*table, framePosition, mip);
        const TableReader* previous = nullptr;
        float oldWeight = 0.0f;
        if (crossfadeRemaining > 0)
        {
            previous = &readers.previous.get (*previousTable, framePosition, mip);
            oldWeight = static_cast<float> (crossfadeRemaining) / static_cast<float> (crossfadeLength);
            if (--crossfadeRemaining == 0)
                previousTable = nullptr;
        }

        // Una sola copia (el caso más común: sub, bajos, leads sin unison): el camino escalar, que para una lectura
        // es más rápido que preparar un grupo SIMD de 4.
        if (activeCount == 1)
        {
            float value = current.read (phases[0]);
            if (oldWeight > 0.0f)
                value += (previous->read (phases[0]) - value) * oldWeight;

            if constexpr (stereo)
            {
                left += gains.left[0] * value;
                right += gains.right[0] * value;
            }
            else
            {
                left += gains.mono[0] * value;
            }

            phases[0] += increments[0];
            if (phases[0] >= 1.0)
                phases[0] -= 1.0;
            return;
        }

        // De 4 en 4 copias. Las del último grupo que no suenan (k >= activeCount) tienen ganancia 0 e incremento 0:
        // se leen (su fase es válida) pero no suman nada ni avanzan.
        simd::Float4 sumLeft, sumRight;
        for (int k = 0; k < activeCount; k += 4)
        {
            const auto v = static_cast<size_t> (k);
            simd::Float4 value = current.read4 (&phases[v]);
            if (oldWeight > 0.0f)
                value = value + (previous->read4 (&phases[v]) - value) * oldWeight;

            if constexpr (stereo)
            {
                sumLeft += simd::Float4::load (&gains.left[v]) * value;
                sumRight += simd::Float4::load (&gains.right[v]) * value;
            }
            else
            {
                sumLeft += simd::Float4::load (&gains.mono[v]) * value;
            }

            simd::advancePhases (&phases[v], &increments[v]);
        }
        left += sumLeft.sum();
        if constexpr (stereo)
            right += sumRight.sum();
    }

    // --- Camino de la Fase 7: warp y FM ------------------------------------------------------------------
    //
    // Por cada copia del unison y cada muestra:
    //   1) fase del maestro = fase de la copia + desplazamiento de la FM (la FM va primero: el warp deforma la
    //      fase ya modulada, así un Sync con FM es "FM de un oscilador sincronizado");
    //   2) el warp decide qué punto de la tabla se lee (o cómo se escalona);
    //   3) si entre la muestra anterior y esta hubo un SALTO (el reinicio del Sync, un escalón) o un QUIEBRE (un
    //      cambio brusco de pendiente: los bordes del PWM, la vuelta del Mirror), se corrige.
    //
    // Por qué hace falta: una onda que salta de golpe tiene infinitos armónicos que bajan solo 6 dB por octava, y
    // los que pasan de Nyquist se reflejan (se oyen como un "silbido" inarmónico). Un quiebre también, bajando
    // 12 dB por octava. Una onda limitada en banda no salta ni se quiebra: lo hace en una curva suave de unas
    // pocas muestras centrada en el instante EXACTO del salto (que cae entre dos muestras).
    //   - polyBLEP (salto): a las muestras de alrededor se les suma la diferencia entre esa curva y el salto seco.
    //   - polyBLAMP (quiebre): lo mismo con la integral de esa curva, escalada por el cambio de pendiente.
    // La curva sale de una B-spline cúbica (4 muestras: 2 antes y 2 después del salto). La versión clásica de 2
    // muestras (dos parábolas) es más simple pero deja pasar ~20 dB más de alias (medido en los tests).
    // Para poder corregir las muestras de ANTES, la salida de estos modos va dos muestras retrasada.
    template <bool stereo>
    void renderWarped (float& left, float& right) noexcept
    {
        if (table == nullptr)
            return;

        position += (targetPosition - position) * positionSmoothingCoef;
        const float framePosition = std::clamp (position + positionModulation, 0.0f, 1.0f);
        smoothUnison();

        // Velocidad real de lectura: la FM acelera y frena la fase (con un modulador rápido y profundo, una copia
        // puede ir mucho más rápido que la nota, o hacia atrás). El mipmap se elige para la copia más extrema.
        // Se guarda el pico (y se suelta en ~20 ms): si el mipmap siguiera a la velocidad muestra a muestra, el
        // brillo de la onda cambiaría al ritmo del modulador, y ese cambio también crea armónicos que se reflejan.
        if (warpStateFresh)
            previousPhaseModulation = phaseModulation; // la FM recién empezada no es un "salto" de velocidad
        const double modulationStep = static_cast<double> (phaseModulation) - previousPhaseModulation;
        previousPhaseModulation = phaseModulation;
        const double fastestIncrement = std::max (std::abs (phaseIncrement * highestRatio + modulationStep),
                                                  std::abs (phaseIncrement * lowestRatio + modulationStep));
        readSpeedPeak = std::max (fastestIncrement, readSpeedPeak * readSpeedRelease);
        updateWarpMips (readSpeedPeak * sampleRate);

        const bool mirror = warp.mode == WarpMode::mirror;
        const TableReader& current = readers.current.get (*table, framePosition, warpMip);
        const TableReader& currentFast = mirror ? readers.currentFast.get (*table, framePosition, warpFastMip) : current;
        const TableReader* previous = &current;
        const TableReader* previousFast = &current;
        float oldWeight = 0.0f;
        if (crossfadeRemaining > 0)
        {
            previous = &readers.previous.get (*previousTable, framePosition, warpMip);
            if (mirror)
                previousFast = &readers.previousFast.get (*previousTable, framePosition, warpFastMip);
            oldWeight = static_cast<float> (crossfadeRemaining) / static_cast<float> (crossfadeLength);
            if (--crossfadeRemaining == 0)
                previousTable = nullptr;
        }

        // Lectura y pendiente en una sola fase, con el fundido entre tablas si lo hay. Solo para lo que no es "una
        // lectura por copia": los valores fijos de esta muestra (principio y final del ciclo) y la pendiente del Bitcrush.
        const auto mix = [oldWeight] (float now, float before) noexcept { return now + (before - now) * oldWeight; };
        const auto read = [&] (double phase) noexcept {
            const float value = current.read (phase);
            return oldWeight > 0.0f ? mix (value, previous->read (phase)) : value;
        };
        const auto slope = [&] (double phase) noexcept {
            const float value = current.slope (phase);
            return oldWeight > 0.0f ? mix (value, previous->slope (phase)) : value;
        };
        const auto slopeFast = [&] (double phase) noexcept {
            const float value = currentFast.slope (phase);
            return oldWeight > 0.0f ? mix (value, previousFast->slope (phase)) : value;
        };

        // --- 1) Qué fase lee cada copia (escalar: cada modo deforma la fase a su manera) ---
        // 'mainPhases' es la lectura principal; 'extraPhases', la segunda que necesitan Mirror (la lectura rápida, en
        // espejo) y Quantize (la onda sin escalonar, para mezclarla con el primer 10 % de la perilla).
        const bool needsMain = ! (mirror && warp.amount >= 1.0f);
        const bool needsExtra = mirror || (warp.mode == WarpMode::quantize && warp.wet < 1.0f);
        const int paddedCount = (activeCount + 3) & ~3; // múltiplo de 4: los grupos SIMD van completos
        std::array<double, maxUnison> masters, mainPhases, extraPhases;
        for (int k = 0; k < paddedCount; ++k)
        {
            const auto v = static_cast<size_t> (k);
            if (k >= activeCount)
            {
                mainPhases[v] = extraPhases[v] = 0.0; // relleno: una fase válida que no suma nada
                continue;
            }

            const double master = wrapPhase (phases[v] + static_cast<double> (phaseModulation));
            masters[v] = master;
            switch (warp.mode)
            {
                case WarpMode::mirror:
                    mainPhases[v] = master;
                    extraPhases[v] = mirrorPhase (master);
                    break;
                case WarpMode::quantize:
                    mainPhases[v] = quantizedPhase (warp, master);
                    extraPhases[v] = master;
                    break;
                case WarpMode::bitcrush:
                    mainPhases[v] = master;
                    break;
                case WarpMode::none:
                case WarpMode::sync:
                case WarpMode::bendPlus:
                case WarpMode::bendMinus:
                case WarpMode::pwm:
                    mainPhases[v] = warpPhase (warp, master);
                    break;
            }
        }

        // --- 2) Las lecturas de la tabla, de 4 en 4 copias (SIMD) ---
        const auto readCopies = [&] (const TableReader& now, const TableReader* before, const std::array<double, maxUnison>& at,
                                     std::array<float, maxUnison>& out) noexcept {
            if (activeCount == 1)
            {
                out[0] = now.read (at[0]);
                if (oldWeight > 0.0f)
                    out[0] = mix (out[0], before->read (at[0]));
                return;
            }
            for (int k = 0; k < activeCount; k += 4)
            {
                const auto v = static_cast<size_t> (k);
                simd::Float4 value = now.read4 (&at[v]);
                if (oldWeight > 0.0f)
                    value = value + (before->read4 (&at[v]) - value) * oldWeight;
                value.store (&out[v]);
            }
        };
        std::array<float, maxUnison> mainValues, extraValues;
        if (needsMain)
            readCopies (current, previous, mainPhases, mainValues);
        if (needsExtra)
            readCopies (mirror ? currentFast : current, mirror ? previousFast : previous, extraPhases, extraValues);

        // Bend y la FM sola no tienen saltos ni quiebres: la suma es la del camino normal, de 4 en 4.
        if (! warp.needsCorrection())
        {
            warpStateFresh = false;
            if (activeCount == 1)
            {
                if constexpr (stereo)
                {
                    left += gains.left[0] * mainValues[0];
                    right += gains.right[0] * mainValues[0];
                }
                else
                {
                    left += gains.mono[0] * mainValues[0];
                }
                phases[0] += increments[0];
                if (phases[0] >= 1.0)
                    phases[0] -= 1.0;
                return;
            }

            simd::Float4 sumLeft, sumRight;
            for (int k = 0; k < activeCount; k += 4)
            {
                const auto v = static_cast<size_t> (k);
                const auto value = simd::Float4::load (&mainValues[v]);
                if constexpr (stereo)
                {
                    sumLeft += simd::Float4::load (&gains.left[v]) * value;
                    sumRight += simd::Float4::load (&gains.right[v]) * value;
                }
                else
                {
                    sumLeft += simd::Float4::load (&gains.mono[v]) * value;
                }
                simd::advancePhases (&phases[v], &increments[v]);
            }
            left += sumLeft.sum();
            if constexpr (stereo)
                right += sumRight.sum();
            return;
        }

        // --- 3) Cada copia: su valor y la corrección de sus saltos y quiebres (escalar) ---
        const bool corrected = warp.needsCorrection() && ! warpStateFresh;
        Correction left4, right4; // las correcciones de esta muestra, ya con la ganancia de cada canal
        float sumLeft = 0.0f, sumRight = 0.0f;

        // Lo que no depende de la copia se lee una vez por muestra, no una vez por copia: la pendiente al principio
        // de la onda (PWM, Mirror) y, solo cuando alguna copia da la vuelta, el final y el principio del ciclo del
        // esclavo (Sync).
        float startSlope = 0.0f;
        if (corrected && warp.mode == WarpMode::pwm)
            startSlope = slope (0.0);
        else if (corrected && mirror)
            startSlope = slopeFast (0.0);

        bool syncValuesReady = false;
        float syncEnd = 0.0f, syncStart = 0.0f, syncSlopeChange = 0.0f;
        const auto readSyncValues = [&] () noexcept {
            if (syncValuesReady)
                return;
            const double endPhase = wrapPhase (warp.syncRatio);
            syncEnd = read (endPhase);
            syncStart = read (0.0);
            syncSlopeChange = slope (0.0) - slope (endPhase);
            syncValuesReady = true;
        };

        // Suma una copia (con su corrección) a los dos canales y avanza su fase.
        const auto finishCopy = [&] (size_t v, float value, const Correction& correction) noexcept {
            value += correction.now;
            if constexpr (stereo)
            {
                sumLeft += gains.left[v] * value;
                sumRight += gains.right[v] * value;
                if (correction.used)
                {
                    left4.accumulate (correction, gains.left[v]);
                    right4.accumulate (correction, gains.right[v]);
                }
            }
            else
            {
                sumLeft += gains.mono[v] * value;
                if (correction.used)
                    left4.accumulate (correction, gains.mono[v]);
            }

            phases[v] += increments[v];
            if (phases[v] >= 1.0)
                phases[v] -= 1.0;
        };

        for (int k = 0; k < activeCount; ++k)
        {
            const auto v = static_cast<size_t> (k);
            const double master = masters[v];

            // El valor "ingenuo" (como warpedValue). Quantize y Bitcrush lo calculan abajo, junto con sus escalones.
            float value = 0.0f;
            if (mirror)
                value = warp.amount >= 1.0f ? extraValues[v] : mainValues[v] + warp.amount * (extraValues[v] - mainValues[v]);
            else if (warp.mode == WarpMode::sync || warp.mode == WarpMode::pwm)
                value = mainValues[v];

            // Cuánto avanzó el maestro desde la muestra anterior (con FM puede ser negativo: la fase retrocede) y si
            // en ese tramo cruzó el final del ciclo.
            double step = master - previousMaster[v];
            if (step < -0.5)
                step += 1.0;
            else if (step > 0.5)
                step -= 1.0;
            const double before = previousMaster[v];
            previousMaster[v] = master;
            const bool wrappedForward = step > 0.0 && before + step >= 1.0;
            const bool wrappedBackward = step < 0.0 && before + step < 0.0;
            // Fracción del tramo (0..1) en la que el maestro pasa por 'edge', según el sentido del movimiento.
            const auto crossing = [before, step] (double edge) noexcept { return (edge - before) / step; };

            // Corrección de un salto 'jump' y/o un cambio de pendiente 'bend' (por muestra) en el instante 'at'.
            Correction correction;
            const auto correct = [&correction] (float jump, float bend, double at) noexcept {
                correction.add (jump, bend, static_cast<float> (std::clamp (at, 0.0, 1.0)));
            };

            switch (warp.mode)
            {
                case WarpMode::sync:
                    if (corrected && (wrappedForward || wrappedBackward))
                    {
                        // Al completar el ciclo, el esclavo pasa de su final (fase = ratio) a su principio (fase 0):
                        // salta de valor y de pendiente. (Con un ratio entero no hay salto: el esclavo también
                        // estaba terminando un ciclo.) La pendiente por muestra es la de la tabla × la velocidad.
                        readSyncValues();
                        const auto speed = static_cast<float> (warp.syncRatio * step);
                        const float slopeChange = syncSlopeChange * speed;
                        if (wrappedForward)
                            correct (syncStart - syncEnd, slopeChange, crossing (1.0));
                        else
                            correct (syncEnd - syncStart, -slopeChange, crossing (0.0));
                    }
                    break;

                case WarpMode::pwm:
                    if (corrected)
                    {
                        // Quiebres al entrar en la zona comprimida (principio del ciclo) y al salir (en 'pulseWidth').
                        const double width = warp.pulseWidth;
                        const auto inside = static_cast<float> (startSlope * step / width); // pendiente dentro
                        if (step > 0.0)
                        {
                            if (before < width && before + step >= width)
                                correct (0.0f, -inside, crossing (width));
                            if (wrappedForward)
                                correct (0.0f, inside, crossing (1.0));
                        }
                        else if (step < 0.0)
                        {
                            if (before >= width && before + step < width)
                                correct (0.0f, inside, crossing (width));
                            if (wrappedBackward)
                                correct (0.0f, -inside, crossing (0.0));
                        }
                    }
                    break;

                case WarpMode::mirror:
                    if (corrected && warp.amount > 0.0f)
                    {
                        // La lectura en espejo da la vuelta en la mitad del ciclo (pico) y al final (valle): su
                        // pendiente pasa de +2 a −2 veces la de la tabla y al revés.
                        const auto turn = static_cast<float> (4.0 * warp.amount * startSlope * std::abs (step));
                        const bool crossedMiddle = step > 0.0 ? before < 0.5 && before + step >= 0.5
                                                              : before >= 0.5 && before + step < 0.5;
                        if (crossedMiddle)
                            correct (0.0f, -turn, crossing (0.5));
                        if (wrappedForward || wrappedBackward)
                            correct (0.0f, turn, crossing (wrappedForward ? 1.0 : 0.0));
                    }
                    break;

                case WarpMode::quantize:
                {
                    const float stepped = mainValues[v];
                    value = stepped;
                    if (warp.wet < 1.0f)
                    {
                        const float original = extraValues[v];
                        value = original + warp.wet * (stepped - original);
                    }
                    if (corrected && stepped != previousStep[v])
                    {
                        // El borde del primer escalón que cruzó la fase. (Si el cambio vino de mover la perilla y no
                        // de avanzar la fase, 'at' se queda en 1: el salto se centra en esta muestra.)
                        double at = 1.0;
                        if (step > 0.0)
                            at = crossing (std::min ((std::floor (before * warp.steps) + 1.0) / warp.steps, 1.0));
                        else if (step < 0.0)
                            at = crossing (std::floor (before * warp.steps) / warp.steps);
                        correct (warp.wet * (stepped - previousStep[v]), 0.0f, at);
                    }
                    previousStep[v] = stepped;
                    break;
                }

                case WarpMode::bitcrush:
                {
                    const float original = mainValues[v];
                    const float crushed = crush (warp, original);
                    value = original + warp.wet * (crushed - original);
                    // Pendiente de la onda por muestra: sirve para saber CUÁNDO cruzó cada umbral (ver abajo).
                    const auto originalSlope = static_cast<float> (slope (master) * step);
                    if (corrected && crushed != previousStep[v])
                    {
                        // En una subida rápida (el salto de una sierra) la onda cruza varios escalones entre dos muestras.
                        // Cada uno se corrige en SU instante: cuando la onda pasa por el umbral entre ese escalón y el
                        // siguiente. Más de 16 escalones se agrupan: ahí la onda ya es casi vertical.
                        const float jump = crushed - previousStep[v];
                        const int parts = std::clamp (static_cast<int> (std::lround (std::abs (jump) * warp.crushScale)), 1, 16);
                        const float part = jump / static_cast<float> (parts);
                        const Hermite curve { previousOriginal[v], previousSlope[v], original, originalSlope };
                        for (int p = 0; p < parts; ++p)
                            correct (warp.wet * part, 0.0f, curve.crossing (previousStep[v] + (static_cast<float> (p) + 0.5f) * part));
                    }
                    previousStep[v] = crushed;
                    previousOriginal[v] = original;
                    previousSlope[v] = originalSlope;
                    break;
                }

                case WarpMode::none:
                case WarpMode::bendPlus:
                case WarpMode::bendMinus:
                    break;
            }
            finishCopy (v, value, correction);
        }
        warpStateFresh = false;

        // Salida retrasada dos muestras. Sale la de hace dos (ya con todas sus correcciones); las otras avanzan.
        if constexpr (! stereo)
        {
            // En mono los dos canales llevan lo mismo (y si antes era estéreo, se mezclan sin salto).
            for (size_t i = 0; i < delayLeft.size(); ++i)
                delayLeft[i] = delayRight[i] = 0.5f * (delayLeft[i] + delayRight[i]);
            carryLeft = carryRight = 0.5f * (carryLeft + carryRight);
            right4 = left4;
            sumRight = sumLeft;
        }
        left += delayLeft[0] + left4.twoBefore;
        right += delayRight[0] + right4.twoBefore;
        delayLeft = { delayLeft[1] + left4.before, sumLeft + carryLeft };
        delayRight = { delayRight[1] + right4.before, sumRight + carryRight };
        carryLeft = left4.after;
        carryRight = right4.after;
    }

    // La onda entre dos muestras, como una cúbica que pasa por los dos valores con sus pendientes (Hermite).
    // Estimar el cruce de un umbral con una recta fallaría cerca del salto de una sierra, donde la onda limitada en
    // banda "ondula" (Gibbs) más rápido de lo que dos puntos pueden describir.
    struct Hermite
    {
        float start, startSlope, end, endSlope;

        [[nodiscard]] float at (float t) const noexcept
        {
            const float t2 = t * t, t3 = t2 * t;
            return (2.0f * t3 - 3.0f * t2 + 1.0f) * start + (t3 - 2.0f * t2 + t) * startSlope
                   + (-2.0f * t3 + 3.0f * t2) * end + (t3 - t2) * endSlope;
        }

        [[nodiscard]] float derivative (float t) const noexcept
        {
            const float t2 = t * t;
            return (6.0f * t2 - 6.0f * t) * start + (3.0f * t2 - 4.0f * t + 1.0f) * startSlope
                   + (-6.0f * t2 + 6.0f * t) * end + (3.0f * t2 - 2.0f * t) * endSlope;
        }

        // Instante (0..1) en que la curva pasa por 'level' (que está entre 'start' y 'end'). Newton desde la estimación
        // lineal, pero sin salir del intervalo en el que se sabe que está el cruce: si un paso de Newton se sale
        // (cerca de una ondulación la pendiente engaña), se parte el intervalo por la mitad.
        [[nodiscard]] double crossing (float level) const noexcept
        {
            const float startSide = start - level;
            if (startSide == 0.0f)
                return 0.0;
            const float travel = end - start;
            float low = 0.0f, high = 1.0f;
            float t = travel != 0.0f ? std::clamp ((level - start) / travel, 0.0f, 1.0f) : 1.0f;
            for (int i = 0; i < 8; ++i)
            {
                const float error = at (t) - level;
                if (error == 0.0f)
                    break;
                ((error < 0.0f) == (startSide < 0.0f) ? low : high) = t;
                const float d = derivative (t);
                const float next = d != 0.0f ? t - error / d : -1.0f;
                t = next > low && next < high ? next : 0.5f * (low + high);
            }
            return t;
        }
    };

    // Corrección polyBLEP + polyBLAMP de 4 muestras alrededor de un salto (o quiebre) que ocurre en la fracción 'a'
    // del camino entre la muestra anterior y esta. Son los "residuos" de la B-spline cúbica: la diferencia entre
    // el salto (o la rampa) suavizado y el seco, evaluada en las 4 muestras (2 antes y 2 después del instante).
    struct Correction
    {
        float twoBefore = 0.0f, before = 0.0f, now = 0.0f, after = 0.0f;
        bool used = false; // casi todas las muestras no tienen ningún salto: no hay nada que acumular

        void add (float jump, float bend, float a) noexcept
        {
            used = true;
            const float b = 1.0f - a;
            const float a2 = a * a, a3 = a2 * a, a4 = a3 * a, a5 = a4 * a;
            const float b2 = b * b, b3 = b2 * b, b4 = b3 * b, b5 = b4 * b;
            constexpr float rampCentre = 7.0f / 30.0f; // cuánto se "redondea" una rampa justo en el quiebre

            // Salto (integral de la B-spline menos el escalón) y quiebre (integral de lo anterior), en cada muestra.
            twoBefore += jump * b4 / 24.0f + bend * b5 / 120.0f;
            before += jump * (0.5f + (-4.0f * a + 2.0f * a3 - 0.75f * a4) / 6.0f)
                      + bend * (-0.5f * a + (2.0f * a2 - 0.5f * a4 + 0.15f * a5) / 6.0f + rampCentre);
            now += jump * (-0.5f + (4.0f * b - 2.0f * b3 + 0.75f * b4) / 6.0f)
                   + bend * (-0.5f * b + (2.0f * b2 - 0.5f * b4 + 0.15f * b5) / 6.0f + rampCentre);
            after += -jump * a4 / 24.0f + bend * a5 / 120.0f;
        }

        // Suma las correcciones de una copia del unison con su ganancia en este canal.
        void accumulate (const Correction& copy, float gain) noexcept
        {
            twoBefore += gain * copy.twoBefore;
            before += gain * copy.before;
            after += gain * copy.after;
        }
    };

    // Mipmaps del warp: la lectura principal, para la zona más rápida del warp; la del Mirror, al doble.
    // Solo se recalculan si la velocidad (FM, tono, amount) cambió más de un 0.1 % (0.0014 octavas: el fundido entre
    // mipmaps avanza menos de un 1 % por paso, inaudible). Con FM el pico se suelta poco a poco en cada muestra.
    void updateWarpMips (double readHz) noexcept
    {
        const double key = readHz * warp.maxSpeed;
        if (warpMipKey >= 0.0 && std::abs (key - warpMipKey) <= 0.001 * key)
            return;
        warpMipKey = key;
        warpMip = mipFor (key);
        if (warp.mode == WarpMode::mirror)
            warpFastMip = mipFor (readHz * Warp::mirrorSpeed);
    }

    void resetWarpState() noexcept
    {
        warpStateFresh = true;
        delayLeft = delayRight = {};
        carryLeft = carryRight = 0.0f;
        warpMipKey = -1.0;
        readSpeedPeak = 0.0;
    }

    // Interpolación cúbica de Catmull-Rom con 4 muestras (p[-1], p[0], p[1], p[2]).
    // La lineal (2 muestras) es más barata pero apaga los armónicos altos y deja "imágenes" espurias;
    // la cúbica pasa por las muestras y además respeta la pendiente de la onda en cada punto.
    [[nodiscard]] static float interpolate (const float* p, float t) noexcept
    {
        const float c1 = 0.5f * (p[1] - p[-1]);
        const float c2 = p[-1] - 2.5f * p[0] + 2.0f * p[1] - 0.5f * p[2];
        const float c3 = 0.5f * (p[2] - p[-1]) + 1.5f * (p[0] - p[1]);
        return ((c3 * t + c2) * t + c1) * t + p[0];
    }

    // Catmull-Rom de 4 copias a la vez: se cargan las 4 muestras vecinas de cada copia (una fila por copia), se
    // transpone (columnas = "la muestra −1 de las 4 copias", etc.) y el polinomio se evalúa como en interpolate,
    // con las mismas operaciones en el mismo orden: el resultado de cada copia es idéntico bit a bit.
    [[nodiscard]] static simd::Float4 interpolate4 (const float* frame, const std::array<int, 4>& index, simd::Float4 t) noexcept
    {
        auto pm1 = simd::Float4::load (frame + index[0] - 1);
        auto p0 = simd::Float4::load (frame + index[1] - 1);
        auto p1 = simd::Float4::load (frame + index[2] - 1);
        auto p2 = simd::Float4::load (frame + index[3] - 1);
        simd::transpose (pm1, p0, p1, p2);

        const simd::Float4 c1 = simd::Float4 (0.5f) * (p1 - pm1);
        const simd::Float4 c2 = pm1 - simd::Float4 (2.5f) * p0 + simd::Float4 (2.0f) * p1 - simd::Float4 (0.5f) * p2;
        const simd::Float4 c3 = simd::Float4 (0.5f) * (p2 - pm1) + simd::Float4 (1.5f) * (p0 - p1);
        return ((c3 * t + c2) * t + c1) * t + p0;
    }

    // Derivada del mismo polinomio (en unidades por muestra de la tabla).
    [[nodiscard]] static float derivative (const float* p, float t) noexcept
    {
        const float c1 = 0.5f * (p[1] - p[-1]);
        const float c2 = p[-1] - 2.5f * p[0] + 2.0f * p[1] - 0.5f * p[2];
        const float c3 = 0.5f * (p[2] - p[-1]) + 1.5f * (p[0] - p[1]);
        return (3.0f * c3 * t + 2.0f * c2) * t + c1;
    }

    // Posición de la copia k en el abanico del unison: de -1 (la más grave) a +1 (la más aguda), repartidas
    // a distancias iguales. Con una sola copia, 0 (sin detune).
    [[nodiscard]] float spreadPosition (int k) const noexcept
    {
        if (numVoices == 1)
            return 0.0f;
        return -1.0f + 2.0f * static_cast<float> (k) / static_cast<float> (numVoices - 1);
    }

    void smoothUnison() noexcept
    {
        // Detune: perilla suavizada + modulación. Solo se recalculan las razones de frecuencia si cambió.
        if (detune != targetDetune)
        {
            detune += (targetDetune - detune) * unisonSmoothingCoef;
            if (std::abs (targetDetune - detune) < 1.0e-5f)
                detune = targetDetune;
        }
        if (std::clamp (detune + detuneModulation, 0.0f, 1.0f) != appliedDetune)
            incrementPending = true;
        if (incrementPending)
            updateIncrement();

        // Ganancias de cada copia: se deslizan 5 ms hacia su objetivo. Así cambiar el número de copias, el
        // ancho o el paneo con la nota sonando no produce saltos de nivel (clics) en ningún canal.
        if (gainsSettled)
            return;

        gainsSettled = true;
        for (size_t k = 0; k < maxUnison; ++k)
        {
            float& gLeft = gains.left[k];
            float& gRight = gains.right[k];
            const float tLeft = targetGains.left[k], tRight = targetGains.right[k];
            gLeft += (tLeft - gLeft) * unisonSmoothingCoef;
            gRight += (tRight - gRight) * unisonSmoothingCoef;
            if (std::abs (tLeft - gLeft) < 1.0e-6f && std::abs (tRight - gRight) < 1.0e-6f)
            {
                gLeft = tLeft;
                gRight = tRight;
            }
            else
            {
                gainsSettled = false;
            }
            gains.mono[k] = 0.5f * (gLeft + gRight);
        }
        updateActiveCount();
    }

    // Nivel y paneo de cada copia.
    //
    // Nivel: N copias con fases al azar no se suman en amplitud sino en potencia, que crece como N. Cada copia
    // lleva 1/√N para que la potencia total (lo que el oído percibe como volumen) no cambie al subir el unison.
    //
    // Paneo: las copias se agrupan en parejas simétricas (la más grave con la más aguda, etc.) y cada pareja se
    // abre a los dos lados, alternando qué lado recibe la grave. Así cada canal tiene copias agudas y graves;
    // si el paneo siguiera al detune, un lado sonaría desafinado hacia abajo y el otro hacia arriba.
    //
    // Ley de paneo "equal power": L = cos θ, R = sin θ, con L² + R² constante. Escalada por √2 para que en el
    // centro L = R = 1: un sonido sin paneo ni unison suena exactamente igual que en la Fase 5.
    void updateGainTargets() noexcept
    {
        const float level = 1.0f / std::sqrt (static_cast<float> (numVoices));
        stereoTarget = false;

        for (int k = 0; k < maxUnison; ++k)
        {
            const auto v = static_cast<size_t> (k);
            float& tLeft = targetGains.left[v];
            float& tRight = targetGains.right[v];
            if (k >= numVoices)
            {
                tLeft = tRight = targetGains.mono[v] = 0.0f;
                continue;
            }

            const int pair = std::min (k, numVoices - 1 - k);
            const float side = pair % 2 == 0 ? 1.0f : -1.0f;
            const float voicePan = std::clamp (panPosition + stereoWidth * side * spreadPosition (k), -1.0f, 1.0f);
            const float angle = (voicePan + 1.0f) * 0.25f * std::numbers::pi_v<float>;
            tLeft = level * std::numbers::sqrt2_v<float> * std::cos (angle);
            tRight = level * std::numbers::sqrt2_v<float> * std::sin (angle);
            if (voicePan == 0.0f)
                tLeft = tRight = level; // exacto en el centro (cos(π/4)·√2 no da 1 exacto en float)
            targetGains.mono[v] = 0.5f * (tLeft + tRight);
            stereoTarget = stereoTarget || tLeft != tRight;
        }
        gainsSettled = false;
    }

    // Solo se procesan las copias que suenan o que todavía se están apagando.
    void updateActiveCount() noexcept
    {
        activeCount = numVoices;
        for (int k = maxUnison - 1; k >= numVoices; --k)
        {
            const auto v = static_cast<size_t> (k);
            if (gains.left[v] != 0.0f || gains.right[v] != 0.0f)
            {
                activeCount = k + 1;
                break;
            }
        }
        updateCopyIncrements();
    }

    // Avance de fase de cada copia por muestra. Las que no suenan, 0: el bucle SIMD procesa grupos de 4 completos
    // y así las del final del último grupo no se mueven (igual que si no se hubieran procesado).
    void updateCopyIncrements() noexcept
    {
        for (size_t k = 0; k < maxUnison; ++k)
            increments[k] = static_cast<int> (k) < activeCount ? phaseIncrement * ratios[k] : 0.0;
    }

    // Fase inicial al azar (0..1) a partir de (semilla, copia): repetible y sin estado.
    [[nodiscard]] static double randomPhase (std::uint32_t seed, std::uint32_t k) noexcept
    {
        std::uint64_t x = (static_cast<std::uint64_t> (seed) << 32) ^ (k * 0x9E3779B97F4A7C15ull);
        x ^= x >> 30;
        x *= 0xBF58476D1CE4E5B9ull;
        x ^= x >> 27;
        x *= 0x94D049BB133111EBull;
        x ^= x >> 31;
        return static_cast<double> (x >> 11) * 0x1.0p-53;
    }

    [[nodiscard]] float smoothingCoefficient (double seconds) const noexcept
    {
        return static_cast<float> (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
    }

    void updateIncrement() noexcept
    {
        incrementPending = false;
        frequencyHz = pitchOffset == 0.0f ? baseFrequencyHz
                                          : baseFrequencyHz * std::exp2 (static_cast<double> (pitchOffset) / 12.0);

        // Con Octave +4, Semi +12 y la modulación de pitch al máximo, una nota aguda pediría cientos de kHz.
        // Un incremento de fase >= 1 (más de un ciclo por muestra) haría que la fase se saliera de [0, 1) y la
        // lectura de la tabla, de la memoria. Por encima de 0.45·sr no queda nada audible que generar.
        frequencyHz = std::min (frequencyHz, maxFrequencyRatio * sampleRate);
        phaseIncrement = sampleRate > 0.0 ? frequencyHz / sampleRate : 0.0;

        // Detune: curva cuadrática (25 % → ±6 cents, 50 % → ±25 cents, 100 % → ±100 cents). La mitad baja de la
        // perilla queda para el "chorus" sutil, donde el oído distingue diferencias de un par de cents.
        // Las copias están a distancias iguales EN CENTS, así que sus razones de frecuencia forman una
        // progresión geométrica: bastan dos exp2 en vez de una por copia.
        // Las razones solo dependen del detune y del número de copias: con un vibrato (solo cambia el tono) no se tocan.
        const float newDetune = std::clamp (detune + detuneModulation, 0.0f, 1.0f);
        if (newDetune != appliedDetune || numVoices != ratiosVoices)
        {
            appliedDetune = newDetune;
            ratiosVoices = numVoices;
            const double spreadSemitones = maxDetuneCents / 100.0 * appliedDetune * appliedDetune;
            highestRatio = lowestRatio = 1.0;
            if (numVoices == 1 || spreadSemitones == 0.0)
            {
                ratios.fill (1.0);
            }
            else
            {
                const double lowest = std::exp2 (-spreadSemitones / 12.0);
                const double step = std::exp2 (2.0 * spreadSemitones / 12.0 / (numVoices - 1));
                double ratio = lowest;
                for (int k = 0; k < numVoices; ++k, ratio *= step)
                    ratios[static_cast<size_t> (k)] = ratio;
                highestRatio = 1.0 / lowest;
                lowestRatio = lowest;
            }
        }

        // Con unison el mipmap se elige para la copia MÁS AGUDA: ninguna copia produce alias (las graves pierden,
        // como mucho, algo de brillo por encima de ~20 kHz). Solo se recalcula cuando cambia el tono o el detune.
        mip = mipFor (frequencyHz * highestRatio);
        warpMipKey = -1.0;
        updateCopyIncrements();
    }

    // Nivel de mipmap para leer la tabla a 'highestHz' (la frecuencia de la lectura más rápida): el primero (el
    // más rico) cuyo armónico más alto no supera el límite.
    // Ejemplo a 48 kHz (límite 28 kHz): A5 = 440 Hz admite 63 armónicos -> nivel 5 (32 armónicos).
    [[nodiscard]] Mip mipFor (double highestHz) const noexcept
    {
        const double allowedHarmonics = maxHarmonicFrequency / std::max (highestHz, 1.0e-3);
        Mip result;
        while (result.level < Wavetable::numLevels - 1 && Wavetable::maxHarmonicsAtLevel (result.level) > allowedHarmonics)
            ++result.level;

        // Fundido entre mipmaps. Al subir el tono y cruzar un límite, el nivel siguiente tiene la mitad de
        // armónicos: con un cambio seco, la octava más aguda del sonido desaparecería de golpe (un salto de
        // brillo audible con vibrato o pitch bend). Por eso, en la franja de mipBlendOctaves antes del límite
        // se mezcla con el nivel siguiente de 0 % a 100 %: al llegar al límite ya suena solo el nivel siguiente
        // y el cambio es continuo. Se mezcla hacia el nivel MÁS POBRE (nunca hacia uno que se reflejaría),
        // así que no añade aliasing: el precio es un poco menos de brillo dentro de esa franja.
        // (Fuera de esa franja la mezcla es 0 y no hace falta el log2: con un vibrato esto se calcula en cada muestra.)
        const double headroom = allowedHarmonics / Wavetable::maxHarmonicsAtLevel (result.level);
        if (result.level < Wavetable::numLevels - 1 && headroom < mipBlendRatio)
        {
            const double headroomOctaves = std::log2 (headroom);
            result.blend = static_cast<float> (std::clamp (1.0 - headroomOctaves / mipBlendOctaves, 0.0, 1.0));
        }
        return result;
    }

    // Ganancias de las copias, una fila por canal ("estructura de arrays"): así las de 4 copias seguidas se cargan
    // en un registro SIMD con una sola instrucción. Por defecto solo suena la primera copia, en el centro.
    struct UnisonGains
    {
        std::array<float, maxUnison> left { 1.0f }, right { 1.0f }, mono { 1.0f };
    };

    static constexpr double audibleLimitHz = 20000.0;
    // Tope de la copia central: con el detune máximo (+1 semitono) la más aguda llega a 0.477·sr (< 1 ciclo/muestra).
    static constexpr double maxFrequencyRatio = 0.45;
    static constexpr double positionSmoothingSeconds = 0.01;
    static constexpr double unisonSmoothingSeconds = 0.005;
    static constexpr double tableCrossfadeSeconds = 0.005;
    static constexpr double readSpeedReleaseSeconds = 0.02;

    const Wavetable* table = nullptr;
    const Wavetable* previousTable = nullptr;
    struct
    {
        CachedReader current, currentFast, previous, previousFast; // "fast" = la lectura rápida del Mirror
    } readers;
    int crossfadeRemaining = 0;
    int crossfadeLength = 1;

    double sampleRate = 44100.0;
    double baseFrequencyHz = 440.0; // la de la nota
    double frequencyHz = 440.0;     // la que suena en la copia central: nota + afinación + modulación
    double maxHarmonicFrequency = 22050.0;
    double phaseIncrement = 0.0;
    float pitchOffset = 0.0f; // semitonos
    Mip mip;
    double highestRatio = 1.0, lowestRatio = 1.0; // copias más aguda y más grave del unison

    float targetPosition = 0.0f;
    float position = 0.0f;
    float positionModulation = 0.0f;
    float positionSmoothingCoef = 1.0f;

    // Unison. Con los valores por defecto (1 copia, centro) el oscilador suena exactamente como en la Fase 5.
    int numVoices = 1;
    int activeCount = 1;
    float targetDetune = 0.0f, detune = 0.0f, detuneModulation = 0.0f, appliedDetune = 0.0f;
    int ratiosVoices = 1;          // número de copias con el que se calcularon 'ratios'
    bool incrementPending = false; // el tono o el detune cambiaron: recalcular antes de la próxima muestra
    float stereoWidth = 0.0f;
    float panPosition = 0.0f;
    float unisonSmoothingCoef = 1.0f;
    bool stereoTarget = false;
    bool gainsSettled = true;
    std::array<double, maxUnison> phases {};  // double: con float la fase acumula error audible en notas graves
    std::array<double, maxUnison> ratios = [] {
        std::array<double, maxUnison> r {};
        r.fill (1.0);
        return r;
    }();
    std::array<double, maxUnison> increments {}; // phaseIncrement × ratios (0 en las copias que no suenan)
    UnisonGains gains, targetGains;

    // Fase 7: warp y FM. Sin warp ni FM no se usa nada de esto (el camino normal es el de la Fase 6).
    Warp warp;
    bool usesPhaseModulation = false;
    float phaseModulation = 0.0f;         // ciclos
    float previousPhaseModulation = 0.0f; // para medir cuánto acelera o frena la FM la lectura
    Mip warpMip, warpFastMip;
    double warpMipKey = -1.0;             // velocidad para la que se calcularon (−1 = recalcular)
    double readSpeedPeak = 0.0;           // ciclos por muestra de la lectura más rápida reciente
    double readSpeedRelease = 0.0;        // cuánto se suelta el pico por muestra (~20 ms)
    bool warpStateFresh = true;           // sin muestra anterior con la que comparar: no hay salto que corregir
    // Modos con saltos o quiebres: las dos muestras retrasadas (la más antigua primero) y la corrección que ya le
    // toca a la muestra siguiente.
    std::array<float, 2> delayLeft {}, delayRight {};
    float carryLeft = 0.0f, carryRight = 0.0f;
    std::array<double, maxUnison> previousMaster {};
    std::array<float, maxUnison> previousStep {};     // Quantize/Bitcrush: el escalón de la muestra anterior
    std::array<float, maxUnison> previousOriginal {}; // Bitcrush: la onda sin escalonar de la muestra anterior
    std::array<float, maxUnison> previousSlope {};    // Bitcrush: y su pendiente (por muestra)
};

} // namespace undertow::dsp
