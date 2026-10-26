#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

#include "deepwater/dsp/Convolver.h"
#include "deepwater/dsp/RealFft.h"
#include "dsp/Band.h"

namespace fathom::dsp
{

// --- Modos de fase (Fase 10) ------------------------------------------------------------------------------------------
//
// Un EQ cambia el volumen de cada frecuencia (la magnitud, lo que dibuja la curva) y, de paso, su RETARDO (la fase).
// Un filtro IIR como los de las bandas (el SVF) es "de fase mínima": responde al instante (latencia 0) pero retrasa
// distinto cada frecuencia alrededor de lo que toca. Casi nunca se oye; importa cuando la misma señal se suma con otra
// (capas en paralelo, el bus de una batería con micrófonos) o en los graves de un kick, donde el retardo desparejo
// "ablanda" el golpe.
//
// - Cero latencia: las bandas tal cual (lo de las Fases 1-9).
// - Fase lineal: un filtro FIR SIMÉTRICO con la misma magnitud. Simétrico = todas las frecuencias se retrasan lo
//   mismo (la mitad del largo del filtro): la forma de onda no se deforma, solo llega más tarde, y el host compensa ese
//   retardo (latencia informada). El precio: latencia, CPU y "pre-eco" (pre-ringing): como el filtro es simétrico, lo
//   que una campana angosta o un corte empinado hace "sonar" después del golpe también suena ANTES. Y la resolución en
//   graves la da el largo: un filtro de N muestras no distingue detalles más finos que ~fs/N Hz (de ahí las calidades).
// - Natural: la magnitud Y la fase de un EQ analógico (el prototipo de cada banda, MatchedEq.h). El IIR "matched" ya
//   tiene la magnitud analógica casi hasta Nyquist, pero su fase se aparta arriba; se le agrega después un FIR CORTO
//   de corrección C = H_analógico / H_IIR. Lejos de Nyquist C ≈ 1 (no hace nada); cerca, corrige el resto de la
//   magnitud y la fase. Como C es suave, alcanza un FIR corto: poca latencia y casi sin pre-eco (la respuesta sigue
//   siendo la causal de un analógico).
//
// Cómo se diseña un FIR con una respuesta dada (muestreo en frecuencia con ventana): se evalúa la respuesta deseada en
// una grilla de 2N frecuencias, una FFT inversa da su respuesta al impulso (de fase cero: centrada en 0 y "dada la
// vuelta"), se la centra en N/2, se toman N muestras y se multiplican por una ventana de Hann (que baja suave a 0 en
// los bordes: cortar de golpe dejaría ondulaciones en la magnitud). Lineal: la respuesta deseada es real (|H|, fase 0).
// Natural: compleja (C).
//
// Con bandas en un solo canal (Fase 8), como en el auto gain, la respuesta es una matriz 2×2 sobre (L, R) en cada
// frecuencia: cuatro FIR (L→L, R→L, L→R, R→R). Si todo es estéreo, uno solo para los dos canales; con L/R, dos.

enum class PhaseMode
{
    zeroLatency,
    natural,
    linear,
};
inline constexpr int numPhaseModes = 3;
inline constexpr int numLinearQualities = 5; // baja, media, alta, muy alta, máxima
inline constexpr int defaultLinearQuality = 1;

// Largos y latencias. Todo escala con la frecuencia de muestreo (a 88.2/96 kHz el doble de muestras: el mismo tiempo
// y la misma resolución en Hz).
struct PhaseSizes
{
    static int rateFactor (double sampleRate) noexcept { return sampleRate > 100000.0 ? 4 : sampleRate > 50000.0 ? 2 : 1; }

    // Fase lineal: 2048 a 32768 muestras a 44.1/48 kHz (de 46 ms a 0.74 s de filtro).
    static int linearLength (int quality, double sampleRate) noexcept
    {
        return (2048 << std::clamp (quality, 0, numLinearQualities - 1)) * rateFactor (sampleRate);
    }
    static int linearPartition (double sampleRate) noexcept { return 512 * rateFactor (sampleRate); }
    static int maxLinearLength (double sampleRate) noexcept { return linearLength (numLinearQualities - 1, sampleRate); }

    // Natural: el FIR de corrección.
    static int naturalLength (double sampleRate) noexcept { return 256 * rateFactor (sampleRate); }
    static int naturalPartition (double sampleRate) noexcept { return 32 * rateFactor (sampleRate); }

    // El retardo total: la mitad del filtro (su centro) + un bloque de la convolución.
    static int latency (PhaseMode mode, int quality, double sampleRate) noexcept
    {
        switch (mode)
        {
            case PhaseMode::natural: return naturalLength (sampleRate) / 2 + naturalPartition (sampleRate);
            case PhaseMode::linear: return linearLength (quality, sampleRate) / 2 + linearPartition (sampleRate);
            case PhaseMode::zeroLatency: break;
        }
        return 0;
    }
};

// Lo que se pide diseñar: los ajustes con que suenan las bandas (con la escala de ganancia) y lo que la dinámica les
// suma ahora. Se copia entre hilos con memcpy (TripleBuffer).
struct DesignRequest
{
    static constexpr int numBands = 24;
    using Bands = std::array<BandSettings, numBands>;

    std::uint64_t generation = 0;
    PhaseMode mode = PhaseMode::linear; // natural o lineal
    int quality = defaultLinearQuality;
    bool mono = false;
    Bands bands {};
    std::array<float, numBands> dynamicDb {};
};

// ¿Una banda deja la señal intacta? (apagada, o con ganancia y en 0 dB). Las demás formas siempre cambian algo.
inline bool isIdentityBand (const BandSettings& band, float dynamicDb) noexcept
{
    if (! band.enabled)
        return true;
    if (! usesGain (band.shape))
        return false;
    return std::clamp (band.gainDb + dynamicDb, BandSettings::minGainDb, BandSettings::maxGainDb) == 0.0f;
}

// Calcula los FIR de un pedido. Toda la memoria se reserva en prepare(); design() no reserva nada (puede correr en el
// hilo de diseño o, renderizando offline, en el de audio). La GUI tiene el suyo para dibujar la curva real.
class PhaseDesigner
{
public:
    static constexpr int maxPaths = 4;

    // Un FIR de entrada → salida (0 = L, 1 = R).
    struct Path
    {
        int input = 0, output = 0, impulse = 0;
    };

    struct Result
    {
        bool identity = true; // todas las bandas intactas: el filtro es un impulso (un retardo puro)
        int length = 0;       // muestras de cada FIR (su centro, length / 2, es el retardo)
        int numPaths = 0;
        std::array<Path, maxPaths> paths {};
    };

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        maxLength = std::max (PhaseSizes::maxLinearLength (sampleRate), PhaseSizes::naturalLength (sampleRate));
        const auto maxBins = static_cast<size_t> (maxLength + 1); // grilla de 2N puntos: N + 1 bins
        for (auto& v : linearCache)
            v.assign (maxBins, 1.0f);
        for (auto& v : naturalCache)
            v.assign (static_cast<size_t> (PhaseSizes::naturalLength (sampleRate) + 1), std::complex<float> { 1.0f });
        linearKeys = {};
        naturalKeys = {};
        for (auto* v : { &responseRe, &responseIm })
            for (auto& path : *v)
                path.assign (maxBins, 0.0);
        for (auto& v : impulses)
            v.assign (static_cast<size_t> (maxLength), 0.0f);
        circular.assign (static_cast<size_t> (2 * maxLength), 0.0);
        hz.assign (maxBins, 0.0);
        for (int q = 0; q < numLinearQualities; ++q)
            prepareSize (PhaseSizes::linearLength (q, sampleRate));
        prepareSize (PhaseSizes::naturalLength (sampleRate));
        analysisRe.assign (maxBins, 0.0);
        analysisIm.assign (maxBins, 0.0);
        analysisDb.assign (maxBins, 0.0f);
        analysisLength = 0;
    }

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }

    // Diseña los FIR del pedido (quedan en impulse()).
    Result designImpulses (const DesignRequest& request) noexcept
    {
        Result result;
        const bool natural = request.mode == PhaseMode::natural;
        const int length = natural ? PhaseSizes::naturalLength (sampleRate) : PhaseSizes::linearLength (request.quality, sampleRate);
        result.length = length;
        const int bins = length + 1;
        const double binHz = sampleRate / (2.0 * length);
        for (int k = 0; k < bins; ++k)
            hz[static_cast<size_t> (k)] = k * binHz;

        // Qué bandas cambian algo, y si alguna actúa en un solo canal (en mono, las side no hacen nada).
        std::array<bool, DesignRequest::numBands> active {};
        bool split = false;
        for (size_t b = 0; b < active.size(); ++b)
        {
            const auto& band = request.bands[b];
            active[b] = ! isIdentityBand (band, request.dynamicDb[b]) && ! (request.mono && band.placement == Placement::side);
            result.identity = result.identity && ! active[b];
            split = split || (active[b] && band.placement != Placement::stereo);
        }
        split = split && ! request.mono;

        if (result.identity)
        {
            // Un impulso en el centro: el FIR solo retrasa (L → L y R → R).
            std::fill_n (impulses[0].begin(), length, 0.0f);
            impulses[0][static_cast<size_t> (length / 2)] = 1.0f;
            addPath (result, 0, 0, 0);
            if (! request.mono)
                addPath (result, 1, 1, 0);
            return result;
        }

        for (size_t b = 0; b < active.size(); ++b)
            if (active[b])
                natural ? updateNatural (b, request, bins) : updateLinear (b, request, bins);

        // Las respuestas de cada camino, bin a bin: el producto de las bandas (o de sus matrices).
        //   camino 0 = L → L (o el único), 1 = R → R, 2 = R → L, 3 = L → R.
        bool cross = false;
        for (size_t b = 0; b < active.size(); ++b)
            if (active[b] && split)
                cross = cross || request.bands[b].placement == Placement::mid || request.bands[b].placement == Placement::side;
        for (int k = 0; k < bins; ++k)
        {
            const auto at = static_cast<size_t> (k);
            Matrix total;
            for (size_t b = 0; b < active.size(); ++b)
            {
                if (! active[b])
                    continue;
                const Complex h = natural ? Complex (naturalCache[b][at]) : Complex (linearCache[b][at]);
                total = split ? matrixFor (request.bands[b].placement, h) * total : total.times (h);
            }
            const std::array<Complex, maxPaths> entries { total.a, total.d, total.b, total.c };
            for (size_t p = 0; p < entries.size(); ++p)
            {
                responseRe[p][at] = entries[p].real();
                responseIm[p][at] = entries[p].imag();
            }
        }

        const int distinct = ! split ? 1 : cross ? 4 : 2;
        for (int p = 0; p < distinct; ++p)
            toImpulse (p, length);
        addPath (result, 0, 0, 0);
        if (request.mono)
            return result;
        addPath (result, 1, 1, split ? 1 : 0);
        if (cross)
        {
            addPath (result, 1, 0, 2);
            addPath (result, 0, 1, 3);
        }
        return result;
    }

    // Diseña y deja el kernel listo para el convolucionador (con su partición).
    Result design (const DesignRequest& request, deepwater::dsp::ConvolutionKernel& kernel) noexcept
    {
        const auto result = designImpulses (request);
        kernel.clear();
        for (int p = 0; p < result.numPaths; ++p)
        {
            const auto& path = result.paths[static_cast<size_t> (p)];
            kernel.addPath (path.input, path.output, impulses[static_cast<size_t> (path.impulse)].data(), result.length);
        }
        return result;
    }

    [[nodiscard]] const float* impulse (int index) const noexcept { return impulses[static_cast<size_t> (index)].data(); }

    // Para la GUI: la magnitud real (en dB) de un FIR del último diseño, en cualquier frecuencia (interpolada entre los
    // bins de una FFT de 2N puntos; el FIR tiene N muestras, así que su respuesta no tiene detalles más finos).
    void analyse (int impulseIndex, int length) noexcept
    {
        analysisLength = length;
        auto& fft = ffts[static_cast<size_t> (sizeIndex (2 * length))];
        const auto& h = impulses[static_cast<size_t> (impulseIndex)];
        std::fill (circular.begin(), circular.begin() + 2 * length, 0.0);
        for (int n = 0; n < length; ++n)
            circular[static_cast<size_t> (n)] = h[static_cast<size_t> (n)];
        fft.forward (circular.data(), analysisRe.data(), analysisIm.data());
        for (int k = 0; k <= length; ++k)
        {
            const double magnitude = std::hypot (analysisRe[static_cast<size_t> (k)], analysisIm[static_cast<size_t> (k)]);
            analysisDb[static_cast<size_t> (k)] = static_cast<float> (20.0 * std::log10 (std::max (magnitude, 1.0e-12)));
        }
    }

    [[nodiscard]] float analysedDb (double frequency) const noexcept
    {
        if (analysisLength == 0)
            return 0.0f;
        const double position = std::clamp (frequency / (sampleRate / (2.0 * analysisLength)), 0.0, static_cast<double> (analysisLength));
        const auto below = static_cast<size_t> (position);
        const auto above = std::min (below + 1, static_cast<size_t> (analysisLength));
        const auto t = static_cast<float> (position - static_cast<double> (below));
        return analysisDb[below] + (analysisDb[above] - analysisDb[below]) * t;
    }

    // Natural: la corrección de una banda en una frecuencia, H_analógico / H_IIR (sin la parte que no se corrige).
    // Donde el IIR ya atenúa mucho (|H| bajo −40 dB: el fondo de un corte o de un notch) no se corrige nada: dividir por
    // casi 0 daría números enormes por algo que igual no se oye. Y se limita a ±12 dB.
    static std::complex<double> correction (std::complex<double> analog, std::complex<double> digital) noexcept
    {
        constexpr double floor = 0.01; // −40 dB
        const double power = std::norm (digital);
        const double weight = power / (power + floor * floor);
        std::complex<double> ratio = power > 0.0 ? analog / digital : std::complex<double> { 1.0 };
        ratio = weight * ratio + (1.0 - weight);
        const double size = std::abs (ratio);
        if (size > 4.0)
            ratio *= 4.0 / size;
        else if (size < 0.25 && size > 0.0)
            ratio *= 0.25 / size;
        return ratio;
    }

private:
    using Complex = std::complex<double>;

    // (L, R) de entrada → salida: [[a, b], [c, d]] (como en AutoGain.h).
    struct Matrix
    {
        Complex a { 1.0 }, b {}, c {}, d { 1.0 };

        Matrix operator* (const Matrix& m) const noexcept
        {
            return { a * m.a + b * m.c, a * m.b + b * m.d, c * m.a + d * m.c, c * m.b + d * m.d };
        }
        [[nodiscard]] Matrix times (Complex h) const noexcept { return { a * h, b, c, d * h }; }
    };

    static Matrix matrixFor (Placement placement, Complex h) noexcept
    {
        const Complex half = 0.5 * (h - 1.0);
        switch (placement)
        {
            case Placement::stereo: return { h, {}, {}, h };
            case Placement::left: return { h, {}, {}, 1.0 };
            case Placement::right: return { 1.0, {}, {}, h };
            case Placement::mid: return { 1.0 + half, half, half, 1.0 + half };
            case Placement::side: return { 1.0 + half, -half, -half, 1.0 + half };
        }
        return {};
    }

    // Lo que cambia la respuesta de una banda: si es lo mismo que la última vez, no se recalcula (arrastrar un nodo
    // cuesta una banda, no 24).
    struct CacheKey
    {
        bool valid = false;
        int bins = 0;
        BandSettings band;
        float dynamicDb = 0.0f;

        [[nodiscard]] bool matches (const BandSettings& s, float dyn, int n) const noexcept
        {
            return valid && bins == n && dynamicDb == dyn && band.frequency == s.frequency && band.gainDb == s.gainDb && band.q == s.q
                   && band.shape == s.shape && band.slopeIndex == s.slopeIndex && band.analog == s.analog;
        }
    };

    void updateLinear (size_t b, const DesignRequest& request, int bins) noexcept
    {
        const auto& band = request.bands[b];
        if (linearKeys[b].matches (band, request.dynamicDb[b], bins))
            return;
        linearKeys[b] = { true, bins, band, request.dynamicDb[b] };
        const auto cascade = Band::coefficientsFor (band, sampleRate, request.dynamicDb[b]);
        auto& cache = linearCache[b];
        for (int k = 0; k < bins; ++k)
            cache[static_cast<size_t> (k)] = static_cast<float> (std::abs (cascade.response (hz[static_cast<size_t> (k)], sampleRate)));
    }

    void updateNatural (size_t b, const DesignRequest& request, int bins) noexcept
    {
        auto band = request.bands[b];
        band.analog = true; // la parte IIR del modo natural es siempre la matched
        if (naturalKeys[b].matches (band, request.dynamicDb[b], bins))
            return;
        naturalKeys[b] = { true, bins, band, request.dynamicDb[b] };
        const auto cascade = Band::coefficientsFor (band, sampleRate, request.dynamicDb[b]);
        const auto prototype = Band::analogPrototype (band, sampleRate, request.dynamicDb[b]);
        auto& cache = naturalCache[b];
        for (int k = 0; k < bins; ++k)
        {
            const double f = hz[static_cast<size_t> (k)];
            cache[static_cast<size_t> (k)] = prototype ? std::complex<float> (correction (prototype->response (f), cascade.response (f, sampleRate)))
                                                       : std::complex<float> { 1.0f };
        }
    }

    // Respuesta de un camino (bins de la grilla de 2N) → FIR de N muestras centrado en N/2, con ventana de Hann.
    void toImpulse (int path, int length) noexcept
    {
        const int size = 2 * length;
        auto& fft = ffts[static_cast<size_t> (sizeIndex (size))];
        fft.inverse (responseRe[static_cast<size_t> (path)].data(), responseIm[static_cast<size_t> (path)].data(), circular.data());
        auto& h = impulses[static_cast<size_t> (path)];
        const auto& window = windows[static_cast<size_t> (sizeIndex (length))];
        for (int n = 0; n < length; ++n)
            h[static_cast<size_t> (n)] = static_cast<float> (circular[static_cast<size_t> ((n - length / 2 + size) % size)]) * window[static_cast<size_t> (n)];
    }

    static void addPath (Result& result, int input, int output, int impulse) noexcept
    {
        result.paths[static_cast<size_t> (result.numPaths++)] = { input, output, impulse };
    }

    static int sizeIndex (int size) noexcept
    {
        int index = 0;
        while ((1 << index) < size)
            ++index;
        return index;
    }

    // Las FFT de la grilla (2N) y la ventana de cada largo N que se usa.
    void prepareSize (int length)
    {
        ffts[static_cast<size_t> (sizeIndex (2 * length))].prepare (2 * length);
        auto& window = windows[static_cast<size_t> (sizeIndex (length))];
        window.resize (static_cast<size_t> (length));
        for (int n = 0; n < length; ++n)
            window[static_cast<size_t> (n)] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * n / length));
    }

    double sampleRate = 44100.0;
    int maxLength = 0;
    std::array<std::vector<float>, DesignRequest::numBands> linearCache;                // |H| de cada banda por bin
    std::array<std::vector<std::complex<float>>, DesignRequest::numBands> naturalCache; // la corrección de cada banda
    std::array<CacheKey, DesignRequest::numBands> linearKeys {}, naturalKeys {};
    std::array<std::vector<double>, maxPaths> responseRe, responseIm;
    std::array<std::vector<float>, maxPaths> impulses;
    std::vector<double> circular, hz;
    std::array<deepwater::dsp::RealFft<double>, 20> ffts;
    std::array<std::vector<float>, 20> windows;
    std::vector<double> analysisRe, analysisIm;
    std::vector<float> analysisDb;
    int analysisLength = 0;
};

} // namespace fathom::dsp
