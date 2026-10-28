#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

#include "deepwater/dsp/DelayLine.h"
#include "deepwater/dsp/RealFft.h"
#include "deepwater/dsp/Smoothing.h"
#include "deepwater/dsp/TripleBuffer.h"
#include "dsp/Band.h"
#include "dsp/Dynamics.h"
#include "dsp/PhaseDesign.h"

namespace fathom::dsp
{

// --- Dinámica espectral (Fase 11) -------------------------------------------------------------------------------------
//
// El EQ dinámico de la Fase 9 mueve la banda ENTERA: si en la zona de una campana en 3 kHz aparece una resonancia en
// 3.4 kHz, la campana baja toda su zona, también lo que estaba bien. Una banda espectral, en cambio, decide frecuencia
// por frecuencia: solo baja donde hay exceso. Sirve para lo que cambia con las notas (la resonancia que aparece en una
// sola nota del 808, el armónico que silba en una vocal de la voz con autotune): la campana marca DÓNDE mirar y cuánto
// puede cortar; el exceso de cada frecuencia, cuánto corta ahí.
//
// Cómo se hace: STFT (transformada de Fourier de tiempo corto). La señal se corta en tramos ("frames") de N muestras
// que se solapan un 75 % (uno nuevo cada N/4 = el "hop"), cada uno con una ventana de Hann, y a cada tramo una FFT: N/2
// + 1 "bins", cada uno una franja de fs/N Hz (21.5 Hz con 2048 muestras a 44.1 kHz). En cada bin:
//   1. el nivel: 4·|X|/N es la amplitud de un seno que cae en ese bin (la ventana suma N/2 y un seno reparte su
//      amplitud entre la frecuencia positiva y la negativa): el mismo "pico de un seno" que mide la Fase 9;
//   2. la curva de ganancia de la Fase 9 (umbral, rodilla de 6 dB, 1 dB por dB hasta el rango) y el ataque y la
//      liberación sobre esa ganancia, pero de cada bin por separado y una vez por tramo;
//   3. cuánto se aplica en ese bin: lo que la banda movería ahí con todo el rango, Δ(f) = |H con ganancia + rango| −
//      |H con ganancia| en dB, escalado por lo que pide el bin (ganancia del bin / rango). En el centro de una campana
//      Δ = el rango entero; en los flancos, menos; lejos, nada. Con todos los bins a fondo, la banda es exactamente la
//      dinámica de la Fase 9 con todo el rango.
// Después se multiplica cada bin por su ganancia, FFT inversa, otra vez la ventana de Hann y se suman los tramos
// solapados ("overlap-add"): con Hann en la entrada y en la salida y un hop de N/4, las ventanas al cuadrado suman 1.5
// en todas partes, así que sin cambios la suma devuelve la señal (dividida por 1.5). La segunda ventana suaviza el borde
// de cada tramo: un cambio de ganancia entre dos tramos se funde a lo largo de toda la ventana, sin clic.
//
// El precio: latencia. Un tramo necesita N muestras antes de poder salir, y (Fase 16) un hop más para calcularlo: la
// etapa retrasa N + hop (58 ms con 2048 a 44.1 kHz) y lo informa al host. El hop extra es para repartir el trabajo:
// todo el cálculo de un tramo depende de la entrada recién llegada, así que sin él había que hacerlo de golpe en el
// borde, y con bloques del host chicos un callback de cada hop/bloque se llevaba varias veces su tiempo (el host corta).
// Ahora el tramo que se completa se calcula por pasos (FFT de cada canal, cada banda, las ganancias, la inversa y la
// suma de cada canal) a lo largo del hop siguiente, en proporción a las muestras que llegan; sus muestras se leen de la
// historia (más atrás lo que ya pasó desde el borde) y su suma cae hop muestras más adelante, justo cuando se lee. Y la resolución: un bin mide 21.5 Hz, la ventana de Hann reparte un seno en ~4 bins (86 Hz),
// así que en graves (bajo ~150 Hz) separa mal dos armónicos cercanos; y la ganancia cambia como mucho cada hop (11.6 ms):
// es lenta para un transitorio (para eso, la dinámica normal). Con 88.2/96 kHz todo es el doble de muestras (el mismo
// tiempo y los mismos Hz).
//
// Varias bandas espectrales comparten la STFT: cada una suma sus dB a los bins de su canal (L, R, o los de mid y side,
// que se cambian a M/S en el espectro, se aplican y se vuelven a L/R). La parte estática de cada banda (su ganancia)
// sigue en las bandas IIR (o en el FIR de fase lineal); aquí solo va lo dinámico. El detector escucha lo que sale de las
// bandas, sin el efecto de la propia banda (se le resta su curva estática, así lee "lo que llega" como en la Fase 9), o el
// sidechain (retrasado como lo que sale de los modos de fase, para que los dos tramos sean del mismo instante).
//
// Bit a bit: mientras ningún bin cambia nada (todas las ganancias en 0 dB exactos), la salida es la entrada retrasada N
// tal cual, no la suma de tramos (que se le parece hasta el redondeo). La etapa lleva la cuenta de cuándo fue el último
// tramo que cambió algo: una muestra "limpia" es la que ningún tramo con cambios tocó.
//
// Encender la primera banda espectral (o apagar la última) cambia la latencia: como en los modos de fase, se funde de la
// salida sin retraso a la retrasada en 20 ms (curva en S) y recién entonces actúan las bandas; al apagarse, las bandas
// sueltan, se espera a que no quede ningún tramo con cambios y se funde de vuelta.

struct SpectralSizes
{
    static int frameLength (double sampleRate) noexcept { return 2048 * PhaseSizes::rateFactor (sampleRate); }
    static int hop (double sampleRate) noexcept { return frameLength (sampleRate) / 4; }
    static int latency (double sampleRate) noexcept { return frameLength (sampleRate) + hop (sampleRate); }
};

class SpectralStage
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int numBands = DesignRequest::numBands;
    static constexpr int maxChunk = 64; // el motor procesa por tramos de este tamaño
    static constexpr double fadeSeconds = 0.02;
    using Bands = DesignRequest::Bands;

    // Para la GUI: lo que cada banda espectral mueve ahora en una grilla fija (log, 20 Hz-20 kHz), la que se dibuja
    // sobre su curva. En cada punto, el bin que más se mueve de los que caen ahí (que una resonancia angosta en agudos,
    // donde un punto abarca varios bins, no desaparezca del dibujo).
    static constexpr int gridPoints = 256;
    static constexpr double gridMinHz = 20.0, gridMaxHz = 20000.0;
    static double gridHz (int i) noexcept { return gridMinHz * std::pow (gridMaxHz / gridMinHz, i / (gridPoints - 1.0)); }

    struct Snapshot
    {
        std::uint32_t frame = 0;
        std::array<std::array<float, gridPoints>, numBands> db {};
    };

    // Para el medidor de la banda: el bin que más se mueve (dB, con su signo), el nivel del bin más fuerte en el centro de
    // la zona y el umbral que se usa (con el automático, el que calculó).
    struct Readout
    {
        float gainDb = 0.0f, levelDb = BandDynamics::floorDb, thresholdDb = 0.0f;
    };

    // Fuera del hilo de audio: reserva todo. Si alguna banda ya es espectral (un proyecto que se abre, un render) arranca
    // directo retrasado y sin fundidos (lo de antes es silencio: no hay nada que esperar).
    void prepare (double newSampleRate, const Bands& bands)
    {
        sampleRate = newSampleRate;
        length = SpectralSizes::frameLength (sampleRate);
        hop = SpectralSizes::hop (sampleRate);
        bins = length / 2 + 1;
        delay = SpectralSizes::latency (sampleRate);
        frameRate = sampleRate / hop;
        binHz = sampleRate / length;
        fft.prepare (length);
        window.resize (static_cast<size_t> (length));
        // Hann "periódica" (divide por N, no por N − 1): es la que suma exacto al solaparse cada N/4.
        for (int n = 0; n < length; ++n)
            window[static_cast<size_t> (n)] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * n / length));
        maxSidechainDelay = PhaseSizes::latency (PhaseMode::linear, numLinearQualities - 1, sampleRate);
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            const auto c = static_cast<size_t> (ch);
            mainHistory[c].prepare (delay + 8);
            dryHistory[c].prepare (delay + 8);
            sidechainHistory[c].prepare (maxSidechainDelay + delay + 8);
            ola[c].assign (static_cast<size_t> (delay), 0.0f);
            re[c].assign (static_cast<size_t> (bins), 0.0f);
            im[c].assign (static_cast<size_t> (bins), 0.0f);
            keyRe[c].assign (static_cast<size_t> (bins), 0.0f);
            keyIm[c].assign (static_cast<size_t> (bins), 0.0f);
            dryOut[c].assign (static_cast<size_t> (maxChunk), 0.0f);
        }
        frame.assign (static_cast<size_t> (length), 0.0f);
        for (auto& group : groupDb)
            group.assign (static_cast<size_t> (bins), 0.0f);
        for (auto* all : { &levelDb, &rawPower })
            for (auto& levels : *all)
                levels.assign (static_cast<size_t> (bins), 0.0f);
        bandDb.assign (static_cast<size_t> (bins), 0.0f);
        prepareGrid();

        delayFade.setTime (fadeSeconds, sampleRate);
        hopCounter = 0;
        olaIndex = 0;
        work = {};
        sinceChange = std::numeric_limits<int>::max();
        snapshot = {};
        publishedSomething = false;
        settings = bands;
        mono = false;
        wanted = false;
        for (size_t b = 0; b < states.size(); ++b)
        {
            auto& state = states[b];
            for (auto* v : { &state.weight, &state.staticDb, &state.staticInverse, &state.gain })
                v->assign (static_cast<size_t> (bins), 0.0f);
            for (auto* ramp : { &state.amount, &state.autoMix })
                ramp->setTime (fadeSeconds, frameRate);
            state.keyValid = false;
            want[b] = isSpectral (bands[b]);
            wanted = wanted || want[b];
            restartBand (state);
            state.amount.snap (want[b] ? 1.0f : 0.0f);
            state.running = want[b];
        }
        running = wanted;
        sinceStart = wanted ? std::numeric_limits<int>::max() : 0;
        delayFade.snap (wanted ? 1.0f : 0.0f);
    }

    // Una vez por bloque: los ajustes con que suenan las bandas (con la escala de ganancia). Si aparece la primera banda
    // espectral, arranca (sin oírse todavía: primero se funde a la salida retrasada).
    void beginBlock (const Bands& bands, bool isMono) noexcept
    {
        settings = bands;
        mono = isMono;
        wanted = false;
        for (size_t b = 0; b < bands.size(); ++b)
        {
            want[b] = isSpectral (bands[b]) && ! (mono && bands[b].placement == Placement::side); // en mono el side no existe
            wanted = wanted || want[b];
        }
        if (wanted && ! running)
            start();
    }

    // La latencia que agrega (la del estado pedido: la que se informa al host).
    [[nodiscard]] int getLatencySamples() const noexcept { return wanted ? SpectralSizes::latency (sampleRate) : 0; }
    [[nodiscard]] bool isRunning() const noexcept { return running; }

    // Un tramo del motor (≤ PhaseStage::maxChunk), en el sitio. 'main' = lo que sale de las bandas y de los modos de
    // fase; 'dry' = la original retrasada como main. 'sidechainDelay' = la latencia de los modos de fase que suena (el
    // sidechain se retrasa eso para quedar alineado con main). Después, dryOutput() = la original retrasada como main.
    void process (float* const* main, const float* const* dry, int numChannels, int count, const float* const* sidechain,
                  int sidechainChannels, int sidechainDelay) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);
        alignment = std::clamp (sidechainDelay, 0, maxSidechainDelay);
        if (! running)
        {
            // Quieta: solo se guarda la historia (al arrancar, lo retrasado ya tiene lo de antes) y todo pasa tal cual.
            for (int i = 0; i < count; ++i)
                pushHistory (main, dry, numChannels, sidechain, sidechainChannels, i);
            for (int ch = 0; ch < numChannels; ++ch)
                dryPointers[static_cast<size_t> (ch)] = dry[ch];
            return;
        }

        // El fundido de la latencia: hacia la salida retrasada si se pide; de vuelta, cuando ninguna banda actúa y no
        // queda ningún tramo con cambios por salir.
        bool anyAmount = false;
        for (const auto& state : states)
            anyAmount = anyAmount || state.amount.value > 0.0f || state.amount.target > 0.0f;
        if (wanted)
            delayFade.setTarget (1.0f);
        else if (! anyAmount && isClean())
            delayFade.setTarget (0.0f);

        for (int i = 0; i < count; ++i)
        {
            pushHistory (main, dry, numChannels, sidechain, sidechainChannels, i);
            if (sinceChange < std::numeric_limits<int>::max())
                ++sinceChange;
            if (sinceStart < std::numeric_limits<int>::max())
                ++sinceStart;
            const bool clean = isClean();
            const float t = delayFade.next();
            const float s = t * t * (3.0f - 2.0f * t); // curva en S, como en los modos de fase
            const auto at = static_cast<size_t> (olaIndex);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto c = static_cast<size_t> (ch);
                // La suma de tramos de la muestra de hace N + hop ya está completa (todos los tramos que la tocan se
                // calcularon).
                const float processed = ola[c][at];
                ola[c][at] = 0.0f;
                const float delayed = clean ? mainHistory[c].read (delay) : processed;
                const float delayedDry = dryHistory[c].read (delay);
                float& sample = main[ch][i];
                const float original = dry[ch][i];
                sample = s == 1.0f ? delayed : s == 0.0f ? sample : sample + (delayed - sample) * s;
                dryOut[c][static_cast<size_t> (i)] = s == 1.0f ? delayedDry : s == 0.0f ? original : original + (delayedDry - original) * s;
            }
            olaIndex = olaIndex + 1 == delay ? 0 : olaIndex + 1;
            if (++hopCounter == hop)
            {
                // El tramo anterior termina (lo que le falte) y empieza el que se acaba de completar.
                hopCounter = 0;
                frameAge = hop;
                advanceFrame (std::numeric_limits<int>::max());
                beginFrame (numChannels);
            }
        }
        // Lo que le toca al tramo en curso por las muestras que llegaron.
        if (work.active)
        {
            frameAge = hopCounter;
            advanceFrame ((totalSteps() * hopCounter + hop - 1) / hop);
        }
        for (int ch = 0; ch < numChannels; ++ch)
            dryPointers[static_cast<size_t> (ch)] = dryOut[static_cast<size_t> (ch)].data();

        // De vuelta sin retraso del todo: se detiene (sin tramos que calcular).
        if (! wanted && delayFade.value == 0.0f && delayFade.isSteady())
        {
            running = false;
            work = {};
        }
    }

    [[nodiscard]] const float* dryOutput (int channel) const noexcept { return dryPointers[static_cast<size_t> (channel)]; }

    // Hilo de audio: lo que hace una banda ahora (para el medidor).
    [[nodiscard]] const Readout& getReadout (int band) const noexcept { return states[static_cast<size_t> (band)].readout; }

    // Hilo de la GUI (un solo lector): lo último que movió cada banda. Devuelve true si llegó algo nuevo.
    bool readSnapshot (Snapshot& out) noexcept { return mailbox.read (out); }

private:
    // Lo que se le suma a cada canal en el espectro: L, R, mid y side (en mono, todo va a L).
    enum Group
    {
        groupLeft,
        groupRight,
        groupMid,
        groupSide,
        numGroups,
    };

    struct BandState
    {
        bool running = false;
        deepwater::dsp::LinearRamp amount;  // 0-1: cuánto actúa (encender o apagar la parte espectral sin saltos)
        deepwater::dsp::LinearRamp autoMix; // 0 = umbral manual, 1 = automático
        float average = 0.0f;               // umbral automático: el nivel típico de la zona (dB)
        bool averageValid = false;
        // Δ(f) / rango por bin (0 lejos, 1 donde la banda mueve todo el rango), la curva estática (dB) y la ganancia de
        // cada bin (dB, entre 0 y el rango), con el ataque y la liberación.
        std::vector<float> weight, staticDb, staticInverse, gain; // staticInverse = 1 / la potencia de la curva estática
        int first = 1, last = 0; // los bins donde el peso no es ~0
        bool keyValid = false;
        BandSettings key; // los ajustes con que se calcularon los pesos
        Readout readout;
    };

    void start() noexcept
    {
        running = true;
        sinceStart = 0;
        sinceChange = std::numeric_limits<int>::max();
        hopCounter = 0;
        work = {};
        for (auto& buffer : ola)
            std::fill (buffer.begin(), buffer.end(), 0.0f);
        delayFade.snap (0.0f);
        delayFade.setTarget (1.0f);
    }

    // Una muestra "limpia" a la salida: ningún tramo con cambios la tocó (el último empezó hace más de N + hop muestras).
    [[nodiscard]] bool isClean() const noexcept { return sinceChange > delay; }

    void pushHistory (const float* const* main, const float* const* dry, int numChannels, const float* const* sidechain,
                      int sidechainChannels, int i) noexcept
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            mainHistory[static_cast<size_t> (ch)].push (main[ch][i]);
            dryHistory[static_cast<size_t> (ch)].push (dry[ch][i]);
        }
        // Sin sidechain, silencio; uno mono va a los dos lados.
        const float keyLeft = sidechainChannels > 0 ? sidechain[0][i] : 0.0f;
        const float keyRight = sidechainChannels > 1 ? sidechain[1][i] : keyLeft;
        sidechainHistory[0].push (keyLeft);
        sidechainHistory[1].push (keyRight);
    }

    static void restartBand (BandState& state) noexcept
    {
        std::fill (state.gain.begin(), state.gain.end(), 0.0f);
        state.averageValid = false;
        state.readout = {};
    }

    // Los pesos de una banda (cuánto mueve cada bin con todo el rango) y su curva estática, si cambió algo.
    void updateWeights (BandState& state, const BandSettings& s) noexcept
    {
        const auto& k = state.key;
        if (state.keyValid && k.frequency == s.frequency && k.gainDb == s.gainDb && k.q == s.q && k.shape == s.shape && k.analog == s.analog
            && k.dynamics.rangeDb == s.dynamics.rangeDb)
            return;
        state.keyValid = true;
        state.key = s;
        const float range = std::clamp (s.dynamics.rangeDb, DynamicSettings::minRangeDb, DynamicSettings::maxRangeDb);
        auto active = s;
        active.enabled = true;
        const auto resting = Band::coefficientsFor (active, sampleRate);
        const auto moved = Band::coefficientsFor (active, sampleRate, range);
        int first = bins, last = -1;
        for (int bin = 0; bin < bins; ++bin)
        {
            const auto at = static_cast<size_t> (bin);
            const double hz = bin * binHz;
            const auto still = static_cast<float> (resting.magnitudeDb (hz, sampleRate));
            float w = 0.0f;
            if (range != 0.0f)
                w = std::clamp ((static_cast<float> (moved.magnitudeDb (hz, sampleRate)) - still) / range, 0.0f, 1.0f);
            if (w < 1.0e-3f)
                w = 0.0f;
            state.weight[at] = w;
            state.staticDb[at] = still;
            state.staticInverse[at] = std::pow (10.0f, -0.1f * still);
            if (w > 0.0f)
            {
                first = std::min (first, bin);
                last = bin;
            }
        }
        // Los bins que salieron de la zona olvidan su ganancia (si la zona vuelve, arrancan de 0 dB).
        for (int bin = 0; bin < bins; ++bin)
            if (bin < first || bin > last)
                state.gain[static_cast<size_t> (bin)] = 0.0f;
        state.first = first;
        state.last = last;
    }

    // El nivel (dB, "pico de un seno") de cada bin de lo que escucha el detector: la señal ('key' = false) o el
    // sidechain, en el canal de la banda. Se calcula una vez por tramo para cada combinación que se use.
    size_t levelsFor (bool key, Placement placement) noexcept
    {
        // En mono hay un solo canal (y el sidechain se escucha en su mid, como en la Fase 9).
        const int channel = mono ? (key ? static_cast<int> (Placement::mid) : 0) : static_cast<int> (placement);
        const auto index = static_cast<size_t> ((key ? numPlacements : 0) + channel);
        auto& levels = levelDb[index];
        if (levelReady[index])
            return index;
        levelReady[index] = true;
        if (key && ! keyReady)
        {
            keyReady = true;
            for (int ch = 0; ch < maxChannels; ++ch)
            {
                const auto c = static_cast<size_t> (ch);
                for (int n = 0; n < length; ++n)
                    frame[static_cast<size_t> (n)] = sidechainHistory[c].read (alignment + frameAge + length - 1 - n) * window[static_cast<size_t> (n)];
                fft.forward (frame.data(), keyRe[c].data(), keyIm[c].data());
            }
        }
        const auto& lr = key ? keyRe : re;
        const auto& li = key ? keyIm : im;
        const bool stereoSource = key || ! mono;
        // (4·|X|/N)²: la amplitud de un seno en su bin, al cuadrado.
        const float norm = 16.0f / (static_cast<float> (length) * static_cast<float> (length));
        for (int bin = 0; bin < bins; ++bin)
        {
            const auto at = static_cast<size_t> (bin);
            const float lRe = lr[0][at], lIm = li[0][at];
            const float rRe = stereoSource ? lr[1][at] : lRe, rIm = stereoSource ? li[1][at] : lIm;
            float power = 0.0f;
            switch (static_cast<Placement> (channel))
            {
                case Placement::stereo: power = std::max (lRe * lRe + lIm * lIm, rRe * rRe + rIm * rIm); break;
                case Placement::left: power = lRe * lRe + lIm * lIm; break;
                case Placement::right: power = rRe * rRe + rIm * rIm; break;
                case Placement::mid:
                {
                    const float mRe = 0.5f * (lRe + rRe), mIm = 0.5f * (lIm + rIm);
                    power = mRe * mRe + mIm * mIm;
                    break;
                }
                case Placement::side:
                {
                    const float sRe = 0.5f * (lRe - rRe), sIm = 0.5f * (lIm - rIm);
                    power = sRe * sRe + sIm * sIm;
                    break;
                }
            }
            power *= norm;
            rawPower[index][at] = power;
            levels[at] = power > 1.0e-12f ? std::max (BandDynamics::floorDb, 10.0f * std::log10 (power)) : BandDynamics::floorDb;
        }
        // La ventana de Hann reparte un seno en 3-4 bins (el del centro y sus vecinos, 6 dB más abajo). Si cada bin
        // decidiera solo, el centro bajaría todo y los costados casi nada: el tono bajaría menos de lo que pide su nivel.
        // Cada bin mira también a sus dos vecinos (el máximo de los tres): todo el seno recibe la misma ganancia.
        float previous = BandDynamics::floorDb;
        for (int bin = 0; bin < bins; ++bin)
        {
            const auto at = static_cast<size_t> (bin);
            const float current = levels[at];
            const float following = bin + 1 < bins ? levels[at + 1] : BandDynamics::floorDb;
            levels[at] = std::max ({ previous, current, following });
            previous = current;
        }
        return index;
    }

    static Group groupFor (Placement placement, bool isMono) noexcept
    {
        if (isMono)
            return groupLeft;
        switch (placement)
        {
            case Placement::left: return groupLeft;
            case Placement::right: return groupRight;
            case Placement::mid: return groupMid;
            case Placement::side: return groupSide;
            case Placement::stereo: break;
        }
        return groupLeft; // estéreo: L y R (se suma a los dos)
    }

    // Una banda en un tramo: la ganancia de cada bin de su zona, sumada a su grupo. Devuelve true si cambió algo.
    bool processBand (size_t b, bool allowed) noexcept
    {
        auto& state = states[b];
        const auto& s = settings[b];
        state.amount.setTarget (want[b] && allowed ? 1.0f : 0.0f);
        if (! state.running)
        {
            if (state.amount.target == 0.0f)
                return false;
            state.running = true;
            restartBand (state);
        }
        const float amount = state.amount.next();
        const auto& d = s.dynamics;
        const bool active = want[b]; // apagándose: los bins sueltan con su liberación mientras 'amount' baja
        if (active)
            updateWeights (state, s);
        const float range = std::clamp (d.rangeDb, DynamicSettings::minRangeDb, DynamicSettings::maxRangeDb);
        const auto coefficient = [this] (float ms) { return static_cast<float> (1.0 - std::exp (-hop / (static_cast<double> (ms) * 0.001 * sampleRate))); };
        const float attack = coefficient (std::clamp (d.attackMs, DynamicSettings::minAttackMs, DynamicSettings::maxAttackMs));
        const float release = coefficient (std::clamp (d.releaseMs, DynamicSettings::minReleaseMs, DynamicSettings::maxReleaseMs));
        const bool key = d.external;
        const auto index = levelsFor (key, s.placement);
        const float* levels = levelDb[index].data();
        const float* raw = rawPower[index].data();
        const auto compensated = [&] (size_t at) { return key ? levels[at] : levels[at] - state.staticDb[at]; };

        // Umbral automático: el nivel típico de la zona entera (su energía pesada por la zona, como el RMS de la Fase 9;
        // un seno reparte la suya en 1.5 bins de ancho con Hann) + media rodilla. Solo los bins que, ellos solos, suenan
        // más fuerte que toda la zona de costumbre pasan. (Con la potencia de cada bin sin mirar a los vecinos.)
        double zonePower = 0.0;
        for (int bin = state.first; bin <= state.last; ++bin)
        {
            const auto at = static_cast<size_t> (bin);
            const double w = state.weight[at];
            zonePower += w * w * static_cast<double> (raw[at]) * (key ? 1.0 : static_cast<double> (state.staticInverse[at]));
        }
        const float zoneDb = zonePower > 1.0e-12 ? static_cast<float> (10.0 * std::log10 (zonePower / 1.5)) : BandDynamics::floorDb;
        if (zoneDb > BandDynamics::silenceDb)
        {
            const auto averageCoef = static_cast<float> (1.0 - std::exp (-1.0 / (BandDynamics::averageSeconds * frameRate)));
            state.average = state.averageValid ? state.average + (zoneDb - state.average) * averageCoef : zoneDb;
            state.averageValid = true;
        }
        state.autoMix.setTarget (d.autoThreshold ? 1.0f : 0.0f);
        const float automatic = state.autoMix.next();
        const float manual = std::clamp (d.thresholdDb, DynamicSettings::minThresholdDb, DynamicSettings::maxThresholdDb);
        const float typical = state.averageValid ? state.average + BandDynamics::autoMarginDb : manual;
        const float threshold = manual + (typical - manual) * automatic;

        auto& group = groupDb[static_cast<size_t> (groupFor (s.placement, mono))];
        const bool both = ! mono && s.placement == Placement::stereo;
        bool changed = false;
        float deepest = 0.0f, loudest = BandDynamics::floorDb, loudestAnywhere = BandDynamics::floorDb;
        std::fill (bandDb.begin(), bandDb.end(), 0.0f);
        for (int bin = state.first; bin <= state.last; ++bin)
        {
            const auto at = static_cast<size_t> (bin);
            const float level = compensated (at);
            const float target = active ? BandDynamics::gainChangeDb (level, threshold, range) : 0.0f;
            float& g = state.gain[at];
            g += (target - g) * (std::abs (target) > std::abs (g) ? attack : release);
            if (std::abs (target - g) < 1.0e-3f) // como en la Fase 9: a 0.001 dB se copia (vuelve a 0 exacto)
                g = target;
            const float w = state.weight[at];
            const float db = amount * w * g;
            if (db != 0.0f)
            {
                changed = true;
                bandDb[at] = db;
                group[at] += db;
                if (both)
                    groupDb[groupRight][at] += db;
                if (std::abs (db) > std::abs (deepest))
                    deepest = db;
            }
            loudestAnywhere = std::max (loudestAnywhere, level);
            if (w >= 0.5f)
                loudest = std::max (loudest, level);
        }
        state.readout = { deepest, loudest > BandDynamics::floorDb ? loudest : loudestAnywhere, threshold };
        if (changed)
        {
            groupUsed[static_cast<size_t> (groupFor (s.placement, mono))] = true;
            if (both)
                groupUsed[groupRight] = true;
        }
        toGrid (snapshot.db[b], state.first, state.last);

        // Terminó de apagarse: suelta todo.
        if (amount == 0.0f && state.amount.isSteady() && ! want[b])
        {
            state.running = false;
            restartBand (state);
        }
        return changed;
    }

    // Un tramo (cada hop), por pasos a lo largo del hop siguiente: análisis de cada canal, cada banda, las ganancias, la
    // inversa y la suma de cada canal, y el cierre. Lee la historia 'frameAge' muestras más atrás (lo que pasó desde que
    // el tramo se completó).
    struct FrameWork
    {
        bool active = false;
        int channels = 1, step = 0, olaStart = 0;
        bool allowed = false, changed = false, anyBand = false;
    };

    [[nodiscard]] int totalSteps() const noexcept { return 2 * work.channels + numBands + 2; }

    void beginFrame (int numChannels) noexcept
    {
        work.active = true;
        work.channels = numChannels;
        work.step = 0;
        // Su primera muestra se lee dentro de N + hop − N = hop muestras.
        work.olaStart = olaIndex + hop >= delay ? olaIndex + hop - delay : olaIndex + hop;
        // Las bandas actúan cuando lo retrasado ya suena del todo y la suma de tramos ya tiene todos los suyos.
        work.allowed = wanted && delayFade.value == 1.0f && delayFade.isSteady() && sinceStart >= delay + hop;
        work.changed = work.anyBand = false;
        levelReady = {};
        keyReady = false;
        groupUsed = {};
    }

    // Hace los pasos del tramo en curso hasta 'target' (o todos).
    void advanceFrame (int target) noexcept
    {
        const int channels = work.channels;
        const int bandsStart = channels, gainsStep = channels + numBands, outputStart = gainsStep + 1;
        const int lastStep = outputStart + channels;
        while (work.active && work.step < std::min (target, lastStep + 1))
        {
            const int step = work.step++;
            if (step < bandsStart)
            {
                const auto c = static_cast<size_t> (step);
                for (int n = 0; n < length; ++n)
                    frame[static_cast<size_t> (n)] = mainHistory[c].read (frameAge + length - 1 - n) * window[static_cast<size_t> (n)];
                fft.forward (frame.data(), re[c].data(), im[c].data());
            }
            else if (step < gainsStep)
            {
                const auto b = static_cast<size_t> (step - bandsStart);
                if (states[b].running || want[b])
                {
                    work.anyBand = true;
                    work.changed = processBand (b, work.allowed) || work.changed;
                }
                else
                    std::fill (snapshot.db[b].begin(), snapshot.db[b].end(), 0.0f);
            }
            else if (step == gainsStep)
            {
                if (work.changed)
                    applyGains (channels);
            }
            else if (step < lastStep)
                outputChannel (step - outputStart);
            else
                finishFrame();
        }
    }

    // La salida de un canal del tramo a la suma: sin cambios, la entrada con las dos ventanas (lo mismo que daría la
    // FFT inversa, sin hacerla); con cambios, la inversa del espectro con las ganancias.
    void outputChannel (int ch) noexcept
    {
        constexpr float olaScale = 1.0f / 1.5f; // las ventanas al cuadrado solapadas cada N/4 suman 1.5
        const auto c = static_cast<size_t> (ch);
        if (work.changed)
            fft.inverse (re[c].data(), im[c].data(), frame.data());
        else
            for (int n = 0; n < length; ++n)
                frame[static_cast<size_t> (n)] = mainHistory[c].read (frameAge + length - 1 - n) * window[static_cast<size_t> (n)];
        auto& buffer = ola[c];
        for (int n = 0; n < length; ++n)
        {
            int at = work.olaStart + n;
            if (at >= delay)
                at -= delay;
            buffer[static_cast<size_t> (at)] += frame[static_cast<size_t> (n)] * window[static_cast<size_t> (n)] * olaScale;
        }
    }

    void finishFrame() noexcept
    {
        work.active = false;
        // Sus muestras salen desde dentro de hop: se cuenta desde que el tramo se completó (frameAge atrás).
        if (work.changed)
            sinceChange = std::min (sinceChange, frameAge);
        for (size_t g = 0; g < groupDb.size(); ++g)
            if (groupUsed[g])
                std::fill (groupDb[g].begin(), groupDb[g].end(), 0.0f);

        // A la GUI: mientras alguna banda actúa (y una vez más al terminar, para que vea el 0).
        if (work.anyBand || publishedSomething)
        {
            ++snapshot.frame;
            mailbox.write (snapshot);
            publishedSomething = work.anyBand;
        }
    }

    // Las ganancias de los grupos sobre el espectro de los canales. Con mid o side: L y R pasan a M/S (con lo de L/R ya
    // aplicado), se aplica y vuelven.
    void applyGains (int numChannels) noexcept
    {
        const bool stereo = numChannels == 2;
        const bool midSide = stereo && (groupUsed[groupMid] || groupUsed[groupSide]);
        for (int bin = 0; bin < bins; ++bin)
        {
            const auto at = static_cast<size_t> (bin);
            const float gl = groupUsed[groupLeft] ? toGain (groupDb[groupLeft][at]) : 1.0f;
            re[0][at] *= gl;
            im[0][at] *= gl;
            if (! stereo)
                continue;
            const float gr = groupUsed[groupRight] ? toGain (groupDb[groupRight][at]) : 1.0f;
            re[1][at] *= gr;
            im[1][at] *= gr;
            if (! midSide)
                continue;
            const float gm = groupUsed[groupMid] ? toGain (groupDb[groupMid][at]) : 1.0f;
            const float gs = groupUsed[groupSide] ? toGain (groupDb[groupSide][at]) : 1.0f;
            const float mRe = 0.5f * (re[0][at] + re[1][at]) * gm, mIm = 0.5f * (im[0][at] + im[1][at]) * gm;
            const float sRe = 0.5f * (re[0][at] - re[1][at]) * gs, sIm = 0.5f * (im[0][at] - im[1][at]) * gs;
            re[0][at] = mRe + sRe;
            im[0][at] = mIm + sIm;
            re[1][at] = mRe - sRe;
            im[1][at] = mIm - sIm;
        }
    }

    static float toGain (float db) noexcept { return db == 0.0f ? 1.0f : std::pow (10.0f, 0.05f * db); }

    // Qué bins caen en cada punto de la grilla de la GUI: un rango (se toma el que más se mueve) o, en graves, donde un
    // punto cae entre dos bins, la interpolación entre ellos.
    struct GridPoint
    {
        int from = 0, to = 0; // los bins [from, to], o los dos entre los que se interpola con 't'
        float t = 0.0f;
        bool interpolate = false;
    };

    void prepareGrid() noexcept
    {
        const double ratio = std::pow (gridMaxHz / gridMinHz, 1.0 / (gridPoints - 1.0));
        for (int i = 0; i < gridPoints; ++i)
        {
            const double hz = gridHz (i);
            auto& point = grid[static_cast<size_t> (i)];
            point.from = static_cast<int> (std::ceil (hz / std::sqrt (ratio) / binHz));
            point.to = std::min (bins - 1, static_cast<int> (std::floor (hz * std::sqrt (ratio) / binHz)));
            if (point.from > point.to)
            {
                const double position = std::min (hz / binHz, static_cast<double> (bins - 1));
                point.to = static_cast<int> (position);
                point.from = std::min (point.to + 1, bins - 1);
                point.t = static_cast<float> (position - point.to);
                point.interpolate = true;
            }
        }
    }

    void toGrid (std::array<float, gridPoints>& out, int first, int last) const noexcept
    {
        for (size_t i = 0; i < grid.size(); ++i)
        {
            const auto& point = grid[i];
            if (point.interpolate)
            {
                const float a = bandDb[static_cast<size_t> (point.to)], b = bandDb[static_cast<size_t> (point.from)];
                out[i] = a + (b - a) * point.t;
                continue;
            }
            float extreme = 0.0f;
            for (int bin = std::max (point.from, first); bin <= std::min (point.to, last); ++bin)
                if (std::abs (bandDb[static_cast<size_t> (bin)]) > std::abs (extreme))
                    extreme = bandDb[static_cast<size_t> (bin)];
            out[i] = extreme;
        }
    }

    double sampleRate = 44100.0, frameRate = 86.0, binHz = 21.5;
    int length = 2048, hop = 512, bins = 1025;
    deepwater::dsp::RealFft<float> fft;
    std::vector<float> window, frame;
    std::array<deepwater::dsp::DelayLine, maxChannels> mainHistory, dryHistory, sidechainHistory;
    std::array<std::vector<float>, maxChannels> ola, re, im, keyRe, keyIm, dryOut;
    std::array<const float*, maxChannels> dryPointers {};
    int hopCounter = 0, olaIndex = 0, alignment = 0, maxSidechainDelay = 0;
    int delay = 2560;  // la latencia: N + hop (también el largo de la suma de tramos)
    int frameAge = 0;  // muestras desde que se completó el tramo en curso
    FrameWork work;
    int sinceChange = 0, sinceStart = 0; // muestras desde el último tramo con cambios y desde que arrancó
    bool running = false, wanted = false, mono = false;
    deepwater::dsp::LinearRamp delayFade; // 0 = sin retraso (la etapa no está), 1 = retrasada N

    Bands settings {};
    std::array<bool, numBands> want {};
    std::array<BandState, numBands> states;
    std::array<std::vector<float>, numGroups> groupDb;
    std::array<bool, numGroups> groupUsed {};
    std::array<std::vector<float>, 2 * numPlacements> levelDb, rawPower; // señal y sidechain, por canal: dB con los vecinos y potencia sola
    std::array<bool, 2 * numPlacements> levelReady {};
    bool keyReady = false;
    std::vector<float> bandDb;

    std::array<GridPoint, gridPoints> grid {};
    Snapshot snapshot;
    bool publishedSomething = false;
    deepwater::dsp::TripleBuffer<Snapshot> mailbox;
};

} // namespace fathom::dsp
