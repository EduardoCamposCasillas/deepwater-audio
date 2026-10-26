#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include "deepwater/dsp/Convolver.h"
#include "deepwater/dsp/DelayLine.h"
#include "deepwater/dsp/Smoothing.h"
#include "deepwater/dsp/TripleBuffer.h"
#include "dsp/PhaseDesign.h"

namespace fathom::dsp
{

// Los kernels de un convolucionador: tres lugares que se pasan entre el hilo de diseño y el de audio SIN locks. Cada
// lugar tiene un estado atómico (libre, escribiéndose, listo, en uso). El que diseña toma uno libre (compare-exchange:
// nunca dos a la vez), lo llena y lo publica como "listo" (si había otro listo sin usar, ese vuelve a libre); el audio
// toma el listo (un intercambio atómico: el kernel ya está escrito entero) y lo suelta cuando deja de sonar. Tres
// alcanzan: el que suena, el que lo reemplaza (durante un fundido o esperando) y el que se está diseñando.
class KernelStore
{
public:
    static constexpr int numSlots = 3;

    struct Info
    {
        std::uint64_t generation = 0;
        bool identity = false;
    };

    void prepare (int partitionSize, int maxPartitions, int maxPaths)
    {
        for (auto& kernel : kernels)
            kernel.prepare (partitionSize, maxPartitions, maxPaths);
        for (auto& state : states)
            state.store (free);
        readySlot.store (-1);
        published.store (0);
    }

    // El que diseña: un lugar libre para escribir, o −1.
    int acquire() noexcept
    {
        for (int s = 0; s < numSlots; ++s)
        {
            int expected = free;
            if (states[static_cast<size_t> (s)].compare_exchange_strong (expected, writing, std::memory_order_acq_rel))
                return s;
        }
        return -1;
    }

    void publish (int slot) noexcept
    {
        states[static_cast<size_t> (slot)].store (ready, std::memory_order_release);
        const int old = readySlot.exchange (slot, std::memory_order_acq_rel);
        if (old >= 0)
            states[static_cast<size_t> (old)].store (free, std::memory_order_release);
        published.store (infos[static_cast<size_t> (slot)].generation, std::memory_order_release);
    }

    // El audio: el último listo (o −1). Queda "en uso" hasta release().
    int take() noexcept
    {
        const int slot = readySlot.exchange (-1, std::memory_order_acq_rel);
        if (slot >= 0)
            states[static_cast<size_t> (slot)].store (inUse, std::memory_order_release);
        return slot;
    }

    void release (int slot) noexcept
    {
        if (slot >= 0)
            states[static_cast<size_t> (slot)].store (free, std::memory_order_release);
    }

    [[nodiscard]] bool hasReady() const noexcept { return readySlot.load (std::memory_order_acquire) >= 0; }
    [[nodiscard]] std::uint64_t publishedGeneration() const noexcept { return published.load (std::memory_order_acquire); }
    deepwater::dsp::ConvolutionKernel& kernel (int slot) noexcept { return kernels[static_cast<size_t> (slot)]; }
    Info& info (int slot) noexcept { return infos[static_cast<size_t> (slot)]; }

private:
    enum State
    {
        free,
        writing,
        ready,
        inUse,
    };

    std::array<deepwater::dsp::ConvolutionKernel, numSlots> kernels;
    std::array<Info, numSlots> infos {};
    std::array<std::atomic<int>, numSlots> states {};
    std::atomic<int> readySlot { -1 };
    std::atomic<std::uint64_t> published { 0 };
};

// La parte del motor que hace los modos de fase (ver PhaseDesign.h): después de las bandas IIR (o en vez de ellas) y
// antes de la salida. Entrega la señal procesada ("main") y la original retrasada lo mismo ("dry", para el bypass, el
// solo y la mezcla): con fase lineal, la latencia es la misma con o sin bypass.
//
//   cero latencia:  main = bandas IIR                      dry = entrada
//   natural:        main = FIR de corrección (bandas IIR)  dry = entrada retrasada lo mismo
//   lineal:         main = FIR de fase lineal (entrada)    dry = entrada retrasada lo mismo
//
// Los FIR se diseñan en un hilo aparte (diseñar uno grande cuesta milisegundos: en el audio sería un corte). El audio
// pide un diseño cuando cambian las bandas (a lo sumo uno a la vez por convolucionador) y, cuando llega, el
// convolucionador se funde del kernel viejo al nuevo en un bloque. Renderizando offline (o en los tests) no hay hilo:
// se diseña ahí mismo y el resultado no depende de cuánto tarde (el render es siempre igual).
//
// Cambiar de modo (o de calidad de la fase lineal) cambia la latencia: no hay forma de que el audio quede "pegado". La
// salida nueva arranca por su cuenta (se llena su convolucionador con la entrada, sin oírse) y, cuando está lista, se
// funde de la vieja a la nueva en 20 ms (en curva de S): un instante de las dos mezcladas, sin clic. El host recibe la latencia nueva.
//
// La memoria (los kernels de fase lineal, el diseñador: 15-35 MB según la frecuencia de muestreo) solo se reserva si
// hace falta: una etapa preparada "sin memoria" se queda en cero latencia y avisa (wantsMemory) si le piden otro modo;
// el motor le pide al hilo de mensajes una etapa nueva con memoria y la cambia por esta (Engine::providePhaseMemory).
class PhaseStage
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int maxChunk = 64; // el motor procesa por tramos de este tamaño
    static constexpr double fadeSeconds = 0.02;
    static constexpr double iirWarmupSeconds = 0.05; // las bandas IIR arrancan de cero: su transitorio se deja pasar
    static constexpr int numUnits = 3;               // los convolucionadores: natural y dos de fase lineal

    ~PhaseStage() { stopWorker(); }

    // Fuera del hilo de audio. Con 'withMemory', reserva todo para esta frecuencia de muestreo y arranca directo en ese
    // modo (con su filtro ya diseñado para 'bands'); sin memoria, arranca en cero latencia. 'realtime' = con hilo de
    // diseño (si no, se diseña en el audio).
    void prepare (double newSampleRate, PhaseMode mode, int quality, bool realtime, const DesignRequest::Bands& bands, bool withMemory = true)
    {
        stopWorker();
        sampleRate = newSampleRate;
        wantsThread = realtime;
        target = configFor (mode, quality, -1);
        current = {};
        hasNext = fading = false;
        iirRunning = true;
        iirRestart = false;
        iirWarmup = 0;
        memory = withMemory;
        memoryNeeded = false;
        if (! memory)
            return;
        designer.prepare (sampleRate);
        const int linearPartition = PhaseSizes::linearPartition (sampleRate);
        const int naturalPartition = PhaseSizes::naturalPartition (sampleRate);
        for (int u = 0; u < numUnits; ++u)
        {
            auto& unit = units[static_cast<size_t> (u)];
            const bool natural = u == 0;
            const int partition = natural ? naturalPartition : linearPartition;
            const int maxLength = natural ? PhaseSizes::naturalLength (sampleRate) : PhaseSizes::maxLinearLength (sampleRate);
            const int partitions = maxLength / partition + 1;
            unit.store.prepare (partition, partitions, PhaseDesigner::maxPaths);
            unit.convolver.prepare (partition, partitions, maxChannels, maxChannels);
            unit.currentSlot = unit.pendingSlot = -1;
            unit.requested = 0;
            unit.lastValid = false;
        }
        const int maxDelay = std::max (PhaseSizes::latency (PhaseMode::linear, numLinearQualities - 1, sampleRate),
                                       PhaseSizes::latency (PhaseMode::natural, 0, sampleRate));
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            dryDelay[static_cast<size_t> (ch)].prepare (maxDelay + maxChunk);
            mainDelay[static_cast<size_t> (ch)].prepare (maxDelay + maxChunk);
            sidechainDelay[static_cast<size_t> (ch)].prepare (maxDelay + maxChunk);
        }
        fade.setTime (fadeSeconds, sampleRate);
        fade.snap (0.0f);
        generation = 0;

        current = { target.mode, target.quality, target.mode == PhaseMode::zeroLatency ? -1 : target.mode == PhaseMode::natural ? 0 : 1 };
        iirRunning = usesIir (current);
        if (current.unit >= 0)
        {
            startUnit (current.unit);
            // El primer filtro, ya: arranca sonando bien (la entrada pasada es silencio, no hay nada que esperar).
            request (current, bands, {}, false, true);
            install (current.unit);
        }

        threaded = realtime;
        if (threaded)
            startWorker();
    }

    // El modo pedido (por los parámetros). Si es distinto del que suena, empieza la transición en el próximo bloque.
    void setTarget (PhaseMode mode, int quality) noexcept { target = configFor (mode, quality, -1); }

    // Con el hilo de diseño (tiempo real) o diseñando en el audio (offline). Sin hilo arrancado, siempre en el audio.
    void setRealtime (bool realtime) noexcept { wantsThread = realtime; }

    // La latencia del modo pedido (la que se informa al host).
    [[nodiscard]] int getLatencySamples() const noexcept { return PhaseSizes::latency (target.mode, target.quality, sampleRate); }

    // La del modo que suena ahora (Fase 11: con ella se alinea el sidechain de la etapa espectral con lo que sale de aquí).
    [[nodiscard]] int getAudibleLatency() const noexcept { return PhaseSizes::latency (current.mode, current.quality, sampleRate); }

    // ¿Se reservó la memoria de los modos de fase? Sin ella, la etapa se queda en cero latencia y wantsMemory() avisa
    // si le pidieron otro modo.
    [[nodiscard]] bool hasMemory() const noexcept { return memory; }
    [[nodiscard]] bool wantsMemory() const noexcept { return memoryNeeded; }

    // En cero latencia y sin transición: se la puede reemplazar por otra (la que trae la memoria) sin que se note.
    [[nodiscard]] bool isQuiet() const noexcept { return current.mode == PhaseMode::zeroLatency && ! hasNext; }

    // ¿Tienen que correr las bandas IIR en este tramo? (cero latencia y natural las usan).
    [[nodiscard]] bool needsIir() const noexcept { return usesIir (current) || (hasNext && usesIir (next)); }

    // Una vez: las bandas IIR vuelven a correr después de estar quietas (fase lineal): hay que reiniciarlas.
    bool takeIirRestart() noexcept
    {
        const bool restart = iirRestart;
        iirRestart = false;
        return restart;
    }

    // Con fase lineal los detectores del EQ dinámico escuchan la entrada retrasada como el centro del FIR (si no, la
    // ganancia se movería antes de que llegue el golpe que la causa).
    [[nodiscard]] bool alignedDetectors() const noexcept { return (hasNext ? next.mode : current.mode) == PhaseMode::linear; }

    // Una vez por bloque, antes de los tramos: la transición de modo y los diseños (pide los que hagan falta e instala
    // los que llegaron).
    void beginBlock (const DesignRequest::Bands& bands, const std::array<float, DesignRequest::numBands>& dynamicDb, bool mono) noexcept
    {
        threaded = wantsThread && workerRunning; // offline: se diseña en el audio (el hilo, si existe, queda quieto)
        if (! memory)
        {
            memoryNeeded = target.mode != PhaseMode::zeroLatency;
            return;
        }

        if (! hasNext)
        {
            if (! same (current, target))
                startNext();
        }
        else if (! fading)
        {
            if (same (target, current))
                cancelNext();
            else if (! same (target, next))
                startNext();
        }

        if (needsIir() && ! iirRunning)
        {
            iirRunning = true;
            iirRestart = true;
            iirWarmup = static_cast<int> (iirWarmupSeconds * sampleRate);
        }
        else if (! needsIir())
            iirRunning = false;

        for (const auto* config : { &current, hasNext ? &next : nullptr })
            if (config != nullptr && config->unit >= 0)
            {
                request (*config, bands, dynamicDb, mono, false);
                install (config->unit);
            }
    }

    // Un tramo (≤ maxChunk). 'dry' = la entrada original; 'io' = lo que dejaron las bandas IIR (si corrieron) y sale
    // con lo procesado ("main"). Después, dryOut() = la original retrasada como main. 'sidechain' solo hace falta con
    // los detectores alineados.
    void process (const float* const* dry, float* const* io, int numChannels, int count, const float* const* sidechain,
                  int sidechainChannels) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);
        lastIndex = count - 1;
        if (! hasNext && current.unit < 0)
        {
            // Cero latencia, sin transición: no hay nada que hacer (main = las bandas, dry = la entrada, bit a bit).
            for (int ch = 0; ch < numChannels; ++ch)
                dryPointers[static_cast<size_t> (ch)] = dry[ch];
            iirWarmup = std::max (0, iirWarmup - count);
            return;
        }

        // Lo que dejaron las bandas (la entrada del FIR natural), antes de escribir encima.
        for (int ch = 0; ch < numChannels; ++ch)
            std::copy_n (io[ch], count, iirOut[static_cast<size_t> (ch)].begin());

        const bool aligned = alignedDetectors();
        const int alignment = aligned ? PhaseSizes::linearLength ((hasNext ? next : current).quality, sampleRate) / 2 : 0;
        for (int i = 0; i < count; ++i)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& delay = dryDelay[static_cast<size_t> (ch)];
                delay.push (dry[ch][i]);
                mainDelay[static_cast<size_t> (ch)].push (iirOut[static_cast<size_t> (ch)][static_cast<size_t> (i)]);
                if (aligned)
                {
                    alignedIn[static_cast<size_t> (ch)][static_cast<size_t> (i)] = delay.read (alignment);
                    if (ch < sidechainChannels)
                    {
                        auto& scDelay = sidechainDelay[static_cast<size_t> (ch)];
                        scDelay.push (sidechain[ch][i]);
                        alignedSidechain[static_cast<size_t> (ch)][static_cast<size_t> (i)] = scDelay.read (alignment);
                    }
                }
            }

        // Los convolucionadores en uso. El natural filtra lo que dejaron las bandas; los de fase lineal, la entrada.
        std::array<bool, numUnits> running {};
        running[static_cast<size_t> (std::max (0, current.unit))] = current.unit >= 0;
        if (hasNext && next.unit >= 0)
            running[static_cast<size_t> (next.unit)] = true;
        for (int u = 0; u < numUnits; ++u)
        {
            if (! running[static_cast<size_t> (u)])
                continue;
            auto& unit = units[static_cast<size_t> (u)];
            const std::array<const float*, maxChannels> in { u == 0 ? iirOut[0].data() : dry[0],
                                                             numChannels > 1 ? (u == 0 ? iirOut[1].data() : dry[1]) : silence.data() };
            const std::array<float*, maxChannels> out { unit.output[0].data(), unit.output[1].data() };
            unit.convolver.process (in.data(), out.data(), count);
            // Si el fundido del kernel terminó, el viejo se suelta.
            if (unit.pendingSlot >= 0 && ! unit.convolver.hasPendingKernel())
            {
                unit.store.release (unit.currentSlot);
                unit.currentSlot = unit.pendingSlot;
                unit.pendingSlot = -1;
            }
        }

        // La salida de cada modo que suena y, en una transición, el fundido del viejo al nuevo.
        if (hasNext)
        {
            const bool ready = advanceWarmup (count);
            if (ready && ! fading)
            {
                fading = true;
                fade.snap (0.0f);
                fade.setTarget (1.0f);
            }
        }
        else
            iirWarmup = std::max (0, iirWarmup - count);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& main = mainOut[static_cast<size_t> (ch)];
            auto& original = dryOut[static_cast<size_t> (ch)];
            fill (current, ch, count, dry, main.data(), original.data());
            dryPointers[static_cast<size_t> (ch)] = original.data();
        }
        if (fading)
        {
            float t = fade.value;
            for (int i = 0; i < count; ++i)
            {
                // Curva en S (smoothstep): sale y llega con pendiente 0. Un fundido en línea recta entre dos señales
                // desfasadas deja un quiebre de pendiente al empezar y al terminar.
                t = fade.next();
                const float s = t * t * (3.0f - 2.0f * t);
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float nextMain = 0.0f, nextDry = 0.0f;
                    sample (next, ch, i, dry, nextMain, nextDry);
                    auto& main = mainOut[static_cast<size_t> (ch)][static_cast<size_t> (i)];
                    auto& original = dryOut[static_cast<size_t> (ch)][static_cast<size_t> (i)];
                    main += (nextMain - main) * s;
                    original += (nextDry - original) * s;
                }
            }
            if (t == 1.0f && fade.isSteady())
                finishTransition();
        }
        for (int ch = 0; ch < numChannels; ++ch)
            std::copy_n (mainOut[static_cast<size_t> (ch)].begin(), count, io[ch]);
    }

    // La original retrasada como la salida procesada (válida después de process(), para ese tramo).
    [[nodiscard]] const float* dryOutput (int channel) const noexcept { return dryPointers[static_cast<size_t> (channel)]; }
    // Con alignedDetectors(): la entrada y el sidechain retrasados como el centro del FIR (para ese tramo).
    [[nodiscard]] const float* alignedInput (int channel) const noexcept { return alignedIn[static_cast<size_t> (channel)].data(); }
    [[nodiscard]] const float* alignedSidechainInput (int channel) const noexcept
    {
        return alignedSidechain[static_cast<size_t> (channel)].data();
    }

    // El modo que suena ahora (en una transición, el viejo hasta que termina el fundido).
    [[nodiscard]] PhaseMode getCurrentMode() const noexcept { return current.mode; }
    [[nodiscard]] bool isSwitching() const noexcept { return hasNext; }

private:
    struct Config
    {
        PhaseMode mode = PhaseMode::zeroLatency;
        int quality = defaultLinearQuality;
        int unit = -1; // el convolucionador (−1 = ninguno)
        int warmup = 0;
    };

    struct Unit
    {
        KernelStore store;
        deepwater::dsp::PartitionedConvolver convolver;
        int currentSlot = -1, pendingSlot = -1;
        std::uint64_t requested = 0; // la generación del último pedido
        std::uint64_t minimum = 0;   // lo diseñado antes de (re)arrancar no sirve
        DesignRequest last {};       // el último pedido (para saber si cambió algo)
        bool lastValid = false;
        deepwater::dsp::TripleBuffer<DesignRequest> mailbox; // audio → hilo de diseño
        std::array<std::array<float, maxChunk>, maxChannels> output {};
    };

    static bool usesIir (const Config& c) noexcept { return c.mode != PhaseMode::linear; }

    static bool same (const Config& a, const Config& b) noexcept
    {
        return a.mode == b.mode && (a.mode != PhaseMode::linear || a.quality == b.quality);
    }

    static Config configFor (PhaseMode mode, int quality, int unit) noexcept
    {
        return { mode, std::clamp (quality, 0, numLinearQualities - 1), unit, 0 };
    }

    [[nodiscard]] int lengthOf (const Config& c) const noexcept
    {
        return c.mode == PhaseMode::natural ? PhaseSizes::naturalLength (sampleRate) : PhaseSizes::linearLength (c.quality, sampleRate);
    }

    // El modo nuevo empieza a sonar por su cuenta (todavía sin oírse).
    void startNext() noexcept
    {
        if (hasNext && next.unit >= 0)
            stopUnit (next.unit);
        next = target;
        next.unit = next.mode == PhaseMode::zeroLatency ? -1 : next.mode == PhaseMode::natural ? 0 : current.unit == 1 ? 2 : 1;
        // Su convolucionador tiene que haberse llenado con la entrada (el largo del filtro y un bloque).
        next.warmup = next.unit >= 0 ? lengthOf (next) + units[static_cast<size_t> (next.unit)].convolver.getLatency() : 0;
        if (next.unit >= 0)
            startUnit (next.unit);
        hasNext = true;
        fading = false;
    }

    void cancelNext() noexcept
    {
        if (next.unit >= 0)
            stopUnit (next.unit);
        hasNext = false;
    }

    void finishTransition() noexcept
    {
        if (current.unit >= 0 && current.unit != next.unit)
            stopUnit (current.unit);
        current = next;
        hasNext = fading = false;
    }

    // ¿El modo nuevo ya puede oírse? Las bandas IIR (si las usa) pasaron su transitorio, su convolucionador se llenó y
    // tiene su filtro.
    bool advanceWarmup (int count) noexcept
    {
        iirWarmup = std::max (0, iirWarmup - count);
        const bool iirReady = ! usesIir (next) || iirWarmup == 0;
        if (iirReady)
            next.warmup = std::max (0, next.warmup - count);
        const bool kernelReady = next.unit < 0 || units[static_cast<size_t> (next.unit)].currentSlot >= 0;
        return iirReady && next.warmup == 0 && kernelReady;
    }

    void startUnit (int u) noexcept
    {
        auto& unit = units[static_cast<size_t> (u)];
        stopUnit (u);
        unit.convolver.reset();
        unit.minimum = generation + 1;
        unit.lastValid = false;
    }

    void stopUnit (int u) noexcept
    {
        auto& unit = units[static_cast<size_t> (u)];
        unit.convolver.releaseKernels();
        unit.store.release (unit.currentSlot);
        unit.store.release (unit.pendingSlot);
        unit.currentSlot = unit.pendingSlot = -1;
        unit.store.release (unit.store.take()); // uno listo que ya no sirve
    }

    // Pide un diseño si cambió algo y no hay otro en camino. 'force' = sin comparar (el primero).
    void request (const Config& config, const DesignRequest::Bands& bands, const std::array<float, DesignRequest::numBands>& dynamicDb,
                  bool mono, bool force) noexcept
    {
        auto& unit = units[static_cast<size_t> (config.unit)];
        const bool inFlight = unit.store.publishedGeneration() < unit.requested;
        if (inFlight || unit.store.hasReady() || unit.convolver.hasPendingKernel())
            return;
        auto& wanted = scratchRequest;
        wanted.mode = config.mode == PhaseMode::natural ? PhaseMode::natural : PhaseMode::linear;
        wanted.quality = config.quality;
        wanted.mono = mono;
        wanted.bands = bands;
        wanted.dynamicDb = dynamicDb;
        if (! force && unit.lastValid && ! differs (wanted, unit.last))
            return;
        wanted.generation = ++generation;
        unit.last = wanted;
        unit.lastValid = true;
        unit.requested = wanted.generation;
        if (threaded)
            unit.mailbox.write (wanted);
        else
            designNow (unit, wanted);
    }

    // Sin hilo: el diseño en este mismo hilo. Si el hilo de diseño estaba ocupado (justo se pasó a offline), se espera
    // a que termine (offline no hay apuro).
    void designNow (Unit& unit, const DesignRequest& wanted) noexcept
    {
        while (designerBusy.exchange (true, std::memory_order_acquire))
            std::this_thread::yield();
        const int slot = unit.store.acquire();
        if (slot >= 0)
        {
            const auto result = designer.design (wanted, unit.store.kernel (slot));
            unit.store.info (slot) = { wanted.generation, result.identity };
            unit.store.publish (slot);
        }
        designerBusy.store (false, std::memory_order_release);
    }

    // Instala el último diseño que llegó: con fundido si ya había uno sonando.
    void install (int u) noexcept
    {
        auto& unit = units[static_cast<size_t> (u)];
        if (unit.convolver.hasPendingKernel())
            return;
        const int slot = unit.store.take();
        if (slot < 0)
            return;
        if (unit.store.info (slot).generation < unit.minimum)
        {
            unit.store.release (slot);
            return;
        }
        const bool crossfade = unit.currentSlot >= 0;
        unit.convolver.setKernel (&unit.store.kernel (slot), crossfade);
        if (crossfade)
            unit.pendingSlot = slot;
        else
            unit.currentSlot = slot;
    }

    // Si un pedido nuevo es distinto del último. La dinámica cambia la ganancia de a poco: menos de 0.05 dB no se
    // rediseña (salvo al llegar a 0 exacto: la banda vuelve a no hacer nada).
    static bool differs (const DesignRequest& a, const DesignRequest& b) noexcept
    {
        if (a.mode != b.mode || a.quality != b.quality || a.mono != b.mono)
            return true;
        for (size_t i = 0; i < a.bands.size(); ++i)
        {
            const auto& x = a.bands[i];
            const auto& y = b.bands[i];
            if (x.enabled != y.enabled)
                return true;
            if (! x.enabled)
                continue;
            if (x.frequency != y.frequency || x.gainDb != y.gainDb || x.q != y.q || x.shape != y.shape || x.slopeIndex != y.slopeIndex
                || x.analog != y.analog || x.placement != y.placement)
                return true;
            const float dx = a.dynamicDb[i], dy = b.dynamicDb[i];
            if (std::abs (dx - dy) >= 0.05f || ((dx == 0.0f) != (dy == 0.0f)))
                return true;
        }
        return false;
    }

    // main y dry de un modo en un tramo entero.
    void fill (const Config& config, int ch, int count, const float* const* dry, float* main, float* original) noexcept
    {
        for (int i = 0; i < count; ++i)
            sample (config, ch, i, dry, main[i], original[i]);
    }

    void sample (const Config& config, int ch, int i, const float* const* dry, float& main, float& original) noexcept
    {
        const auto c = static_cast<size_t> (ch), at = static_cast<size_t> (i);
        if (config.unit < 0)
        {
            main = iirOut[c][at];
            original = dry[ch][i];
            return;
        }
        auto& unit = units[static_cast<size_t> (config.unit)];
        // Las líneas de retardo ya tienen el tramo entero: la muestra i quedó 'lastIndex − i' atrás de la última.
        const int latency = PhaseSizes::latency (config.mode, config.quality, sampleRate);
        original = dryDelay[c].read (latency + lastIndex - i);
        // Con el filtro quieto en "no hacer nada" (un impulso), un retardo exacto en vez de la convolución (bit a bit).
        // Cuenta lo que se está OYENDO: la salida del convolucionador va un bloque atrás (el fundido hacia el impulso
        // todavía suena durante el bloque siguiente).
        const bool identity = unit.currentSlot >= 0 && unit.pendingSlot < 0 && unit.store.info (unit.currentSlot).identity
                              && unit.convolver.getAudibleKernel() == &unit.store.kernel (unit.currentSlot)
                              && ! unit.convolver.isAudibleFade();
        if (identity)
            main = config.unit == 0 ? mainDelay[c].read (latency + lastIndex - i) : original;
        else
            main = unit.output[c][at];
    }

    void startWorker()
    {
        quit.store (false);
        worker = std::make_unique<std::thread> ([this] { work(); });
        workerRunning = true;
    }

    void stopWorker()
    {
        if (worker == nullptr)
            return;
        quit.store (true);
        worker->join();
        worker.reset();
        workerRunning = false;
        threaded = false;
    }

    // El hilo de diseño: atiende el último pedido de cada convolucionador. Sin trabajo, duerme 1 ms.
    void work()
    {
        std::array<DesignRequest, numUnits> requests {};
        std::array<bool, numUnits> waiting {};
        while (! quit.load (std::memory_order_acquire))
        {
            bool worked = false;
            for (size_t u = 0; u < units.size(); ++u)
            {
                auto& unit = units[u];
                if (unit.mailbox.read (requests[u]))
                    waiting[u] = true;
                if (! waiting[u] || designerBusy.exchange (true, std::memory_order_acquire))
                    continue;
                const int slot = unit.store.acquire();
                if (slot >= 0)
                {
                    const auto result = designer.design (requests[u], unit.store.kernel (slot));
                    unit.store.info (slot) = { requests[u].generation, result.identity };
                    unit.store.publish (slot);
                    waiting[u] = false;
                    worked = true;
                }
                designerBusy.store (false, std::memory_order_release);
            }
            if (! worked)
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }

    double sampleRate = 44100.0;
    PhaseDesigner designer;
    std::array<Unit, numUnits> units;
    std::array<deepwater::dsp::DelayLine, maxChannels> dryDelay, mainDelay, sidechainDelay;
    Config current, next, target;
    bool hasNext = false, fading = false;
    deepwater::dsp::LinearRamp fade;
    bool iirRunning = true, iirRestart = false;
    int iirWarmup = 0;
    std::uint64_t generation = 0;
    DesignRequest scratchRequest {};

    std::array<std::array<float, maxChunk>, maxChannels> iirOut {}, mainOut {}, dryOut {}, alignedIn {}, alignedSidechain {};
    std::array<float, maxChunk> silence {};
    std::array<const float*, maxChannels> dryPointers {};
    int lastIndex = 0; // la última muestra del tramo (las líneas de retardo se leen desde ahí)

    std::unique_ptr<std::thread> worker;
    std::atomic<bool> quit { false }, designerBusy { false };
    bool workerRunning = false, threaded = false, wantsThread = false;
    bool memory = false, memoryNeeded = false;
};

} // namespace fathom::dsp
