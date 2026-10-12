// Tests de la librería DSP compartida (deepwater::dsp). No dependen de JUCE: se compilan como un ejecutable normal y
// se lanzan con ctest. Si algo falla, imprime el archivo, la línea y la condición.
//
// Los de la FFT y el oversampling halfband vienen de los tests de Undertow (Fases 3, 7 y 8): se movieron con las piezas.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numbers>
#include <string_view>
#include <thread>
#include <vector>

#include "deepwater/dsp/AnalyzerTap.h"
#include "deepwater/dsp/BreakpointCurve.h"
#include "deepwater/dsp/Crossover.h"
#include "deepwater/dsp/DcBlocker.h"
#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/DelayLine.h"
#include "deepwater/dsp/EnvelopeFollower.h"
#include "deepwater/dsp/EqFilter.h"
#include "deepwater/dsp/FastMath.h"
#include "deepwater/dsp/Fft.h"
#include "deepwater/dsp/Halfband.h"
#include "deepwater/dsp/LevelMeter.h"
#include "deepwater/dsp/Lfo.h"
#include "deepwater/dsp/OnePole.h"
#include "deepwater/dsp/Oversampler.h"
#include "deepwater/dsp/Pitch.h"
#include "deepwater/dsp/Simd.h"
#include "deepwater/dsp/Smoothing.h"
#include "deepwater/dsp/SpectrumAnalyzer.h"
#include "deepwater/dsp/Svf.h"
#include "deepwater/dsp/TransientDetector.h"
#include "deepwater/dsp/TripleBuffer.h"

namespace
{
int failures = 0;

#define CHECK(condition)                                                                   \
    do                                                                                     \
    {                                                                                      \
        if (! (condition))                                                                 \
        {                                                                                  \
            std::printf ("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #condition);           \
            ++failures;                                                                    \
        }                                                                                  \
    } while (false)

namespace dw = deepwater::dsp;
using Complex = std::complex<double>;

constexpr double pi = std::numbers::pi;
constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };

double toDb (double gain)
{
    return 20.0 * std::log10 (std::max (gain, 1.0e-15));
}

// Amplitud de un seno de frecuencia 'hz' dentro de 'signal' (desde 'start'), por correlación con ventana de Hann.
double toneAmplitude (const std::vector<float>& signal, double hz, double sampleRate, size_t start)
{
    const size_t count = signal.size() - start;
    Complex correlation {};
    double windowSum = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        const double window = 0.5 - 0.5 * std::cos (2.0 * pi * static_cast<double> (i) / static_cast<double> (count - 1));
        correlation += window * static_cast<double> (signal[start + i])
                       * std::polar (1.0, -2.0 * pi * hz * static_cast<double> (start + i) / sampleRate);
        windowSum += window;
    }
    return 2.0 * std::abs (correlation) / windowSum;
}

std::vector<float> sineSignal (double hz, double sampleRate, size_t numSamples)
{
    std::vector<float> signal (numSamples);
    for (size_t i = 0; i < numSamples; ++i)
        signal[i] = static_cast<float> (std::sin (2.0 * pi * hz * static_cast<double> (i) / sampleRate));
    return signal;
}

// --- FFT -------------------------------------------------------------------------------------------------------------

void testFftRoundTrip()
{
    std::printf ("FFT: una senoidal cae en su bin y la inversa recupera la señal\n");

    constexpr size_t n = 1024;
    std::vector<Complex> data (n);
    for (size_t i = 0; i < n; ++i)
    {
        const auto x = static_cast<double> (i);
        data[i] = std::cos (2.0 * pi * 5.0 * x / n) + 0.25 * std::sin (0.37 * x * x);
    }
    const auto original = data;

    dw::fft (data, false);
    // cos(2π·5·n/N) aporta N/2 al bin 5; el resto de la señal es "ruido" de fondo, mucho menor.
    CHECK (std::abs (data[5]) > 0.4 * n);

    dw::fft (data, true);
    double maxError = 0.0;
    for (size_t i = 0; i < n; ++i)
        maxError = std::max (maxError, std::abs (data[i] - original[i]));
    CHECK (maxError < 1.0e-12);
}

// --- Oversampling halfband ------------------------------------------------------------------------------------------

void testHalfbandDecimator()
{
    std::printf ("Decimador halfband (2x → 1x): plano hasta 20 kHz; rechaza lo que se reflejaría en lo audible\n");

    for (const double sr : { 44100.0, 48000.0 })
    {
        // Ganancia para un seno de frecuencia 'hz' a la frecuencia sobremuestreada (2·sr). En la banda de paso se mide
        // la amplitud exacta (correlación con ventana de Hann); en la de rechazo basta el RMS de lo que sale.
        const auto gainDb = [sr] (double hz) {
            dw::HalfbandDecimator decimator;
            decimator.prepare (sr);
            const double rate = 2.0 * sr;
            constexpr int settle = 2000;
            constexpr int count = 40000;
            double sum = 0.0, windowSum = 0.0;
            Complex correlation {};
            for (int i = 0; i < settle + count; ++i)
            {
                const auto first = static_cast<float> (std::sin (2.0 * pi * hz * (2.0 * i) / rate));
                const auto second = static_cast<float> (std::sin (2.0 * pi * hz * (2.0 * i + 1.0) / rate));
                const double y = decimator.process (first, second);
                if (i < settle)
                    continue;
                const double window = 0.5 - 0.5 * std::cos (2.0 * pi * (i - settle) / (count - 1.0));
                correlation += window * y * std::polar (1.0, -2.0 * pi * hz * i / sr);
                windowSum += window;
                sum += y * y;
            }
            return hz < 0.5 * sr ? toDb (2.0 * std::abs (correlation) / windowSum) : toDb (std::sqrt (2.0 * sum / count));
        };

        double ripple = 0.0;
        for (const double hz : { 50.0, 1000.0, 5000.0, 10000.0, 15000.0, 19000.0, 20000.0 })
            ripple = std::max (ripple, std::abs (gainDb (hz)));

        // Todo lo que está entre sr − 20 kHz y sr se reflejaría por debajo de 20 kHz al volver a 1x.
        double rejection = -300.0;
        for (double hz = sr - 20000.0; hz < sr; hz += 250.0)
            rejection = std::max (rejection, gainDb (hz));

        dw::HalfbandDecimator decimator;
        decimator.prepare (sr);
        std::printf ("  %4.1f kHz: %3d taps, retardo %.1f muestras; banda de paso ±%.5f dB; rechazo %.1f dB\n", sr / 1000.0,
                     decimator.getNumTaps(), decimator.getLatency(), ripple, rejection);
        CHECK (ripple < 0.001);
        CHECK (rejection < -95.0);
    }
}

void testHalfbandInterpolator()
{
    std::printf ("Interpolador halfband (1x → 2x): plano hasta 20 kHz; la imagen espejo a −100 dB\n");

    for (const double sr : sampleRates)
    {
        double ripple = 0.0, worstImage = -300.0;
        for (const double hz : { 50.0, 1000.0, 5000.0, 10000.0, 15000.0, 19000.0, 20000.0 })
        {
            dw::HalfbandInterpolator interpolator;
            interpolator.prepare (sr);
            constexpr size_t n = 65536;
            std::vector<float> up (2 * n);
            for (size_t i = 0; i < n; ++i)
            {
                const auto x = static_cast<float> (std::sin (2.0 * pi * hz * static_cast<double> (i) / sr));
                interpolator.process (x, up[2 * i], up[2 * i + 1]);
            }
            ripple = std::max (ripple, std::abs (toDb (toneAmplitude (up, hz, 2.0 * sr, 1000))));
            // Rellenar con ceros crea una copia del espectro en sr − f: el filtro debe borrarla.
            worstImage = std::max (worstImage, toDb (toneAmplitude (up, sr - hz, 2.0 * sr, 1000)));
        }
        dw::HalfbandInterpolator interpolator;
        interpolator.prepare (sr);
        std::printf ("  %4.1f kHz: retardo %.1f muestras; banda de paso ±%.5f dB; imagen %.1f dB\n", sr / 1000.0,
                     interpolator.getLatency(), ripple, worstImage);
        CHECK (ripple < 0.001);
        CHECK (worstImage < -95.0);
    }
}

void testOversamplerLatency()
{
    std::printf ("Oversampler: sin curva, subir y bajar devuelve la señal retrasada exactamente getLatency() muestras\n");

    for (const double sr : sampleRates)
    {
        std::printf ("  %4.1f kHz, latencia por factor:", sr / 1000.0);
        for (int stages = 0; stages <= dw::Oversampler::maxStages; ++stages)
        {
            dw::Oversampler oversampler;
            oversampler.prepare (sr, stages);
            const int latency = oversampler.getLatency();
            std::printf ("  %dx = %d", 1 << stages, latency);

            // Un seno de 1 kHz: medio sample de desalineación daría un error de ~0.07; aquí debe quedar en el rizado
            // de los filtros (8 filtros de ±0.001 dB como mucho).
            const auto input = sineSignal (1000.0, sr, 8192);
            float worst = 0.0f;
            for (size_t i = 0; i < input.size(); ++i)
            {
                const float y = oversampler.process (input[i], [] (float x) { return x; });
                if (i >= 1000)
                    worst = std::max (worst, std::abs (y - input[i - static_cast<size_t> (latency)]));
            }
            CHECK (worst < 2.0e-3f);
            CHECK (latency == oversampler.getLatency (stages));
            CHECK (latency < 100); // a 44.1 kHz y ×16 son unas 80 muestras (1.8 ms)
        }
        std::printf ("\n");
    }
}

// Fase 10 de Riptide: las versiones en bloque hacen las mismas cuentas en el mismo orden que las de muestra a muestra.
void testBlockProcessingIsBitExact()
{
    std::printf ("Halfband y oversampler en bloque: idénticos bit a bit a muestra a muestra (bloques de 1 a 32)\n");

    // Ruido con tamaños de bloque irregulares: cruza muchas veces la vuelta de los buffers circulares.
    std::vector<float> input (6000);
    std::uint32_t seed = 0x2468ACE1u;
    for (auto& x : input)
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        x = 1.5f * (static_cast<float> (seed) / 4294967295.0f * 2.0f - 1.0f);
    }
    const auto nextBlock = [] (size_t& position, size_t total) {
        const size_t size = std::min<size_t> (1 + (position * 7 + 3) % 32, total - position);
        position += size;
        return size;
    };

    for (const double sr : sampleRates)
    {
        // Etapas sueltas (la frecuencia más baja y la más alta de un ×16).
        for (const double rate : { sr, 8.0 * sr })
        {
            dw::HalfbandInterpolator upSample, upBlock;
            dw::HalfbandDecimator downSample, downBlock;
            upSample.prepare (rate);
            upBlock.prepare (rate);
            downSample.prepare (rate);
            downBlock.prepare (rate);
            std::vector<float> upExpected (2 * input.size()), upActual (2 * input.size());
            std::vector<float> downExpected (input.size() / 2), downActual (input.size() / 2);
            for (size_t i = 0; i < input.size(); ++i)
                upSample.process (input[i], upExpected[2 * i], upExpected[2 * i + 1]);
            for (size_t i = 0; i < input.size() / 2; ++i)
                downExpected[i] = downSample.process (input[2 * i], input[2 * i + 1]);
            for (size_t position = 0; position < input.size();)
            {
                const size_t start = position;
                const size_t size = nextBlock (position, input.size());
                upBlock.processBlock (input.data() + start, upActual.data() + 2 * start, static_cast<int> (size));
            }
            for (size_t position = 0; position < input.size() / 2;)
            {
                const size_t start = position;
                const size_t size = nextBlock (position, input.size() / 2);
                downBlock.processBlock (input.data() + 2 * start, downActual.data() + start, static_cast<int> (size));
            }
            CHECK (upActual == upExpected);
            CHECK (downActual == downExpected);
        }

        for (int stages = 0; stages <= dw::Oversampler::maxStages; ++stages)
        {
            dw::Oversampler perSample, perBlock;
            perSample.prepare (sr, stages);
            perBlock.prepare (sr, stages);
            const auto curve = [] (float x) { return x / std::sqrt (1.0f + x * x); };
            std::vector<float> expected (input.size()), actual (input.size());
            for (size_t i = 0; i < input.size(); ++i)
                expected[i] = perSample.process (input[i], curve);
            for (size_t position = 0; position < input.size();)
            {
                const size_t start = position;
                const size_t size = nextBlock (position, input.size());
                perBlock.process (input.data() + start, actual.data() + start, static_cast<int> (size), curve);
            }
            CHECK (actual == expected);
        }
    }
}

void testOversamplerReducesAliasing()
{
    std::printf ("Oversampler: con una curva fuerte (tanh de un seno de 5 kHz) el aliasing audible baja con el factor\n");

    // Seno exactamente en el bin 863 (primo) de una FFT de 8192: sus armónicos caen en múltiplos de 863 y cualquier
    // otra cosa por debajo de 20 kHz es aliasing (un armónico que se pasó de Nyquist y se reflejó).
    constexpr double sr = 48000.0;
    constexpr size_t n = 8192;
    constexpr size_t bin = 863;
    const double hz = sr * static_cast<double> (bin) / static_cast<double> (n);

    double previous = 0.0;
    for (int stages = 0; stages <= dw::Oversampler::maxStages; ++stages)
    {
        dw::Oversampler oversampler;
        oversampler.prepare (sr, stages);
        std::vector<Complex> spectrum (n);
        constexpr size_t settle = 1000;
        for (size_t i = 0; i < settle + n; ++i)
        {
            const auto x = 4.0f * static_cast<float> (std::sin (2.0 * pi * hz * static_cast<double> (i) / sr));
            const float y = oversampler.process (x, [] (float v) { return dw::fastTanh (v); });
            if (i >= settle)
                spectrum[i - settle] = y;
        }
        dw::fft (spectrum, false);

        double harmonic = 0.0, alias = 0.0;
        for (size_t k = 1; static_cast<double> (k) * sr / static_cast<double> (n) <= 20000.0; ++k)
            (k % bin == 0 ? harmonic : alias) += std::norm (spectrum[k]);
        const double aliasDb = 10.0 * std::log10 (alias / harmonic + 1.0e-30);
        std::printf ("  %2dx: aliasing audible %6.1f dB respecto a los armónicos\n", 1 << stages, aliasDb);

        if (stages == 0)
            CHECK (aliasDb > -40.0); // sin oversampling se oye claramente
        else
            CHECK (aliasDb < previous - 6.0 || aliasDb < -120.0); // −120 dB o menos ya es el ruido del float
        if (stages >= 2)
            CHECK (aliasDb < -60.0);
        previous = aliasDb;
    }
}

void testDcBlocker()
{
    std::printf ("Filtro DC: quita un desplazamiento constante y deja pasar lo audible intacto\n");

    for (const double sr : sampleRates)
    {
        dw::DcBlocker blocker;
        blocker.prepare (5.0, sr);
        // DC de 0.5 + seno de 1 kHz: tras 1 s la DC tiene que haber desaparecido y el seno seguir igual.
        const auto sine = sineSignal (1000.0, sr, static_cast<size_t> (2.0 * sr));
        std::vector<float> output (sine.size());
        for (size_t i = 0; i < sine.size(); ++i)
            output[i] = blocker.process (0.5f + 0.25f * sine[i]);

        const size_t start = static_cast<size_t> (sr);
        double mean = 0.0;
        for (size_t i = start; i < output.size(); ++i)
            mean += output[i];
        mean /= static_cast<double> (output.size() - start);
        CHECK (std::abs (mean) < 1.0e-4);
        CHECK (std::abs (toDb (toneAmplitude (output, 1000.0, sr, start) / 0.25)) < 0.01);
        // En el corte, −3 dB (como cualquier filtro de 1 polo).
        dw::DcBlocker atCutoff;
        atCutoff.prepare (5.0, sr);
        const auto slow = sineSignal (5.0, sr, static_cast<size_t> (4.0 * sr));
        std::vector<float> slowOut (slow.size());
        for (size_t i = 0; i < slow.size(); ++i)
            slowOut[i] = atCutoff.process (slow[i]);
        CHECK (std::abs (toDb (toneAmplitude (slowOut, 5.0, sr, static_cast<size_t> (2.0 * sr))) + 3.01) < 0.1);
    }
}

// --- Filtro de 1 polo y tilt ---------------------------------------------------------------------------------------

// Mide la ganancia (dB) de un filtro de un canal a una frecuencia, con un seno a régimen.
template <typename Filter>
double measureDb (Filter&& filter, double hz, double sampleRate)
{
    const auto input = sineSignal (hz, sampleRate, 32768);
    std::vector<float> output (input.size());
    for (size_t i = 0; i < input.size(); ++i)
        output[i] = filter (input[i]);
    return toDb (toneAmplitude (output, hz, sampleRate, 8192));
}

void testOnePole()
{
    std::printf ("1 polo: low-pass + high-pass = la entrada, −3 dB en el corte y 6 dB/oct\n");

    for (const double sr : sampleRates)
    {
        const float coefficient = dw::OnePole::coefficientForCutoff (1000.0, sr);
        dw::OnePole a, b;
        float worst = 0.0f;
        for (size_t i = 0; i < 2000; ++i)
        {
            const float x = std::sin (0.37f * static_cast<float> (i)) + 0.3f * std::sin (0.05f * static_cast<float> (i));
            worst = std::max (worst, std::abs (a.lowPass (x, coefficient) + b.highPass (x, coefficient) - x));
        }
        CHECK (worst < 1.0e-6f);

        const auto lowPassAt = [&] (double hz)
        {
            dw::OnePole filter;
            return measureDb ([&] (float x) { return filter.lowPass (x, coefficient); }, hz, sr);
        };
        const auto highPassAt = [&] (double hz)
        {
            dw::OnePole filter;
            return measureDb ([&] (float x) { return filter.highPass (x, coefficient); }, hz, sr);
        };
        CHECK (std::abs (lowPassAt (1000.0) + 3.01) < 0.02);
        CHECK (std::abs (highPassAt (1000.0) + 3.01) < 0.02);
        CHECK (std::abs (lowPassAt (8000.0) - lowPassAt (4000.0) + 6.0) < 1.5); // ~6 dB/oct (la bilineal cae más cerca de Nyquist)
        CHECK (std::abs (highPassAt (62.5) + 24.1) < 0.1);                      // 4 octavas debajo: −24 dB
    }
}

void testTiltFilter()
{
    std::printf ("Tilt: ±dB en agudos, el contrario en graves, 0 dB en el pivote; plano = bit a bit\n");

    for (const double sr : sampleRates)
        for (const float tiltDb : { -6.0f, -2.5f, 3.0f, 6.0f })
        {
            const auto c = dw::TiltFilter::Coefficients::forTilt (1000.0, tiltDb, sr);
            const auto at = [&] (double hz)
            {
                dw::TiltFilter tilt;
                return measureDb ([&] (float x) { return tilt.process (x, c); }, hz, sr);
            };
            CHECK (std::abs (at (1000.0)) < 0.05);          // pivote
            CHECK (std::abs (at (20.0) + tiltDb) < 0.1);     // graves: −tilt
            CHECK (std::abs (at (16000.0) - tiltDb) < 0.35); // agudos: +tilt (la bilineal comprime la subida cerca de Nyquist)
            CHECK (std::abs (at (5000.0)) < std::abs (tiltDb)); // la transición es gradual
        }

    // Con 0 dB la señal sale intacta, bit a bit.
    const auto flat = dw::TiltFilter::Coefficients::forTilt (1000.0, 0.0f, 48000.0);
    CHECK (flat.isFlat());
    dw::TiltFilter tilt;
    bool exact = true;
    for (size_t i = 0; i < 1000; ++i)
    {
        const float x = std::sin (0.1f * static_cast<float> (i));
        exact = exact && tilt.process (x, flat) == x;
    }
    CHECK (exact);
}

// --- SVF TPT/ZDF -----------------------------------------------------------------------------------------------------

// Respuesta teórica de una etapa: el prototipo analógico de 2 polos evaluado en Ω = tan(π·f/fs) / tan(π·fc/fs)
// (TPT equivale a la transformada bilineal).
double svfTheory (int output, double frequency, double cutoff, double q, double sampleRate)
{
    const Complex s { 0.0, std::tan (pi * frequency / sampleRate) / std::tan (pi * cutoff / sampleRate) };
    const Complex denominator = s * s + s / q + 1.0;
    const Complex numerator = output == 0 ? Complex { 1.0 } : (output == 1 ? s : s * s);
    return std::abs (numerator / denominator);
}

void testSvfMatchesTheory()
{
    std::printf ("SVF: low-pass, band-pass y high-pass medidos = teoría (bilineal) a 4 sample rates\n");

    double worst = 0.0;
    for (const double sr : sampleRates)
        for (const double cutoff : { 200.0, 1000.0, 8000.0 })
            for (const double q : { 0.70710678, 3.0 })
                for (const double ratio : { 0.25, 1.0, 4.0 })
                {
                    const double hz = cutoff * ratio;
                    const auto coefficients = dw::SvfStage::Coefficients::forCutoff (cutoff, q, sr);
                    const auto input = sineSignal (hz, sr, 32768);
                    std::array<std::vector<float>, 3> outputs;
                    for (auto& o : outputs)
                        o.resize (input.size());
                    dw::SvfStage stage;
                    for (size_t i = 0; i < input.size(); ++i)
                    {
                        const auto out = stage.process (input[i], coefficients);
                        outputs[0][i] = out.lowPass;
                        outputs[1][i] = out.bandPass;
                        outputs[2][i] = out.highPass;
                    }
                    for (int o = 0; o < 3; ++o)
                    {
                        const double expected = toDb (svfTheory (o, hz, cutoff, q, sr));
                        if (expected < -60.0)
                            continue; // tan atenuado que la medida es ruido de redondeo
                        const double measured = toDb (toneAmplitude (outputs[static_cast<size_t> (o)], hz, sr, 8192));
                        worst = std::max (worst, std::abs (measured - expected));
                    }
                }
    std::printf ("  peor diferencia: %.5f dB\n", worst);
    CHECK (worst < 0.01);

    // Butterworth: −3.01 dB exactos en el corte (low-pass y high-pass).
    const double atCutoff = toDb (svfTheory (0, 1000.0, 1000.0, 0.70710678, 48000.0));
    CHECK (std::abs (atCutoff + 3.0103) < 0.001);
}

// --- Crossover Linkwitz-Riley ----------------------------------------------------------------------------------------

void testLinkwitzRileySplit()
{
    std::printf ("Crossover LR4: -6 dB en el corte, 24 dB/oct, low + high = allpass (suma plana)\n");

    for (const double sr : sampleRates)
    {
        const double crossoverHz = 1000.0;
        const auto c = dw::LinkwitzRiley::forFrequency (crossoverHz, sr);

        // Respuesta de cada salida a un seno.
        const auto measure = [&] (double hz, double& low, double& high, double& sum)
        {
            const auto input = sineSignal (hz, sr, 32768);
            std::vector<float> lows (input.size()), highs (input.size()), sums (input.size());
            dw::LinkwitzRileySplit split;
            for (size_t i = 0; i < input.size(); ++i)
            {
                const auto bands = split.process (input[i], c);
                lows[i] = bands.low;
                highs[i] = bands.high;
                sums[i] = bands.low + bands.high;
            }
            low = toDb (toneAmplitude (lows, hz, sr, 8192));
            high = toDb (toneAmplitude (highs, hz, sr, 8192));
            sum = toDb (toneAmplitude (sums, hz, sr, 8192));
        };

        double low = 0.0, high = 0.0, sum = 0.0;
        measure (crossoverHz, low, high, sum);
        CHECK (std::abs (low + 6.02) < 0.02 && std::abs (high + 6.02) < 0.02); // cada una a −6 dB en el corte
        for (const double hz : { 30.0, 250.0, 700.0, 1000.0, 1400.0, 4000.0, 15000.0 })
        {
            measure (hz, low, high, sum);
            CHECK (std::abs (sum) < 0.001); // la suma es plana en cualquier frecuencia
        }
        // Una octava lejos del corte: Butterworth² = −24.6 dB; dos octavas: −48.2 dB (24 dB/oct).
        measure (500.0, low, high, sum);
        CHECK (std::abs (high + 24.6) < 0.2);
        measure (250.0, low, high, sum);
        CHECK (std::abs (high + 48.2) < 0.3);
        measure (2000.0, low, high, sum);
        CHECK (std::abs (low + 24.6) < 0.6); // la bilineal hace caer el LP algo más rápido hacia Nyquist

        // La suma es, muestra a muestra, el allpass con los mismos coeficientes.
        dw::LinkwitzRileySplit split;
        dw::LinkwitzRileyAllPass allPass;
        float worst = 0.0f;
        for (size_t i = 0; i < 20000; ++i)
        {
            const float x = static_cast<float> (std::sin (0.37 * static_cast<double> (i)) * std::sin (0.0021 * static_cast<double> (i)));
            const auto bands = split.process (x, c);
            worst = std::max (worst, std::abs (bands.low + bands.high - allPass.process (x, c)));
        }
        CHECK (worst < 1.0e-5f);
    }
}

void testMultibandSplitterSumsFlat()
{
    std::printf ("Divisor multibanda (1-4 bandas): la suma es plana y sigue a la cadena de allpasses; cada banda aisla su zona\n");

    using Splitter = dw::MultibandSplitter<4>;
    const std::array<double, 3> crossoverHz { 120.0, 1000.0, 5000.0 };
    for (const double sr : sampleRates)
    {
        Splitter::CrossoverCoefficients c {};
        for (size_t i = 0; i < c.size(); ++i)
            c[i] = dw::LinkwitzRiley::forFrequency (crossoverHz[i], sr);

        for (int numBands = 1; numBands <= 4; ++numBands)
        {
            // Muestra a muestra: Σ bandas = AP₁·AP₂·... (y con 1 banda, la entrada exacta).
            Splitter splitter;
            std::array<dw::LinkwitzRileyAllPass, 3> chain;
            float worst = 0.0f;
            for (size_t i = 0; i < 30000; ++i)
            {
                const float x = static_cast<float> (std::sin (0.91 * static_cast<double> (i)) * std::sin (0.0013 * static_cast<double> (i)));
                std::array<float, 4> bands {};
                splitter.process (x, c, numBands, bands.data());
                float sum = 0.0f;
                for (int b = 0; b < numBands; ++b)
                    sum += bands[static_cast<size_t> (b)];
                float reference = x;
                for (int k = 0; k < numBands - 1; ++k)
                    reference = chain[static_cast<size_t> (k)].process (reference, c[static_cast<size_t> (k)]);
                worst = std::max (worst, std::abs (sum - reference));
                if (numBands == 1)
                    CHECK (bands[0] == x);
            }
            CHECK (worst < 2.0e-5f);

            // Magnitud de la suma y aislamiento: un seno en el centro (geométrico) de cada banda sale casi todo por ella.
            for (int target = 0; target < numBands; ++target)
            {
                const double low = target == 0 ? 30.0 : crossoverHz[static_cast<size_t> (target - 1)];
                const double high = target == numBands - 1 ? 16000.0 : crossoverHz[static_cast<size_t> (target)];
                const double hz = std::sqrt (low * high);
                const auto input = sineSignal (hz, sr, 32768);
                std::array<std::vector<float>, 4> bandSignals;
                std::vector<float> sums (input.size());
                for (auto& band : bandSignals)
                    band.resize (input.size());
                Splitter s;
                for (size_t i = 0; i < input.size(); ++i)
                {
                    std::array<float, 4> bands {};
                    s.process (input[i], c, numBands, bands.data());
                    for (int b = 0; b < numBands; ++b)
                    {
                        bandSignals[static_cast<size_t> (b)][i] = bands[static_cast<size_t> (b)];
                        sums[i] += bands[static_cast<size_t> (b)];
                    }
                }
                CHECK (std::abs (toDb (toneAmplitude (sums, hz, sr, 8192))) < 0.001);
                CHECK (toDb (toneAmplitude (bandSignals[static_cast<size_t> (target)], hz, sr, 8192)) > -1.0);
                for (int b = 0; b < numBands; ++b)
                    if (b != target)
                        CHECK (toDb (toneAmplitude (bandSignals[static_cast<size_t> (b)], hz, sr, 8192)) < -12.0);
            }
        }
    }
}

void testSvfIsStableWhenModulated()
{
    std::printf ("SVF: estable con el cutoff saltando en cada muestra y Q alto\n");

    dw::SvfStage stage;
    unsigned seed = 12345;
    float peak = 0.0f;
    for (int i = 0; i < 200000; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const double cutoff = 20.0 * std::pow (1000.0, static_cast<double> (seed >> 8) / 16777216.0); // 20 Hz–20 kHz
        const auto c = dw::SvfStage::Coefficients::forCutoff (cutoff, 20.0, 48000.0);
        const float x = (i / 50) % 2 == 0 ? 0.5f : -0.5f;
        const auto out = stage.process (x, c);
        peak = std::max ({ peak, std::abs (out.lowPass), std::abs (out.bandPass), std::abs (out.highPass) });
    }
    CHECK (std::isfinite (peak));
    CHECK (peak < 100.0f);

    // Cutoff en Nyquist o por encima: forCutoff lo limita (tan(π/2) sería infinito).
    const auto top = dw::SvfStage::Coefficients::forCutoff (30000.0, 0.7, 48000.0);
    CHECK (std::isfinite (top.g) && std::isfinite (top.a1));
}

// --- Filtros de EQ (EqFilter.h) --------------------------------------------------------------------------------------

// Prototipo analógico de la campana (RBJ): (s² + (A/Q)·s + 1) / (s² + s/(A·Q) + 1), con s = j·f/f0.
double analogBellDb (double hz, double centre, double q, double gainDb)
{
    const double a = std::pow (10.0, gainDb / 40.0);
    const Complex s { 0.0, hz / centre };
    return toDb (std::abs ((s * s + (a / q) * s + 1.0) / (s * s + s / (a * q) + 1.0)));
}

void testEqBellMatchesTheory()
{
    std::printf ("EQ campana: medida = response() a 4 sample rates; centro = ganancia pedida; graves = analógico\n");

    double worstMeasured = 0.0, worstCentre = 0.0, worstAnalog = 0.0;
    for (const double sr : sampleRates)
        for (const double centre : { 60.0, 1000.0, 9000.0 })
            for (const double q : { 0.3, 1.0, 8.0 })
                for (const double gainDb : { -18.0, 6.0, 24.0 })
                {
                    const auto c = dw::EqCoefficients::bell (centre, q, gainDb, sr);
                    worstCentre = std::max (worstCentre, std::abs (c.magnitudeDb (centre, sr) - gainDb));
                    for (const double ratio : { 0.3, 0.8, 1.0, 1.25, 3.0 })
                    {
                        const double hz = centre * ratio;
                        if (hz >= 0.45 * sr)
                            continue;
                        // Se mide después de que la resonancia se asienta: el transitorio decae con τ = 2/(k·ω0), y una
                        // campana angosta y alta en graves (Q 8, +24 dB, 60 Hz) tarda casi 0.2 s en cada τ.
                        const double tau = 2.0 / (static_cast<double> (c.svf.k) * 2.0 * pi * centre);
                        const auto settle = std::max<size_t> (16384, static_cast<size_t> (10.0 * tau * sr));
                        const auto input = sineSignal (hz, sr, settle + 16384);
                        std::vector<float> output (input.size());
                        dw::EqFilter filter;
                        for (size_t i = 0; i < input.size(); ++i)
                            output[i] = filter.process (input[i], c);
                        const double measured = toDb (toneAmplitude (output, hz, sr, settle));
                        worstMeasured = std::max (worstMeasured, std::abs (measured - c.magnitudeDb (hz, sr)));
                        // En graves (lejos de Nyquist) la bilineal casi no deforma: digital ≈ analógico.
                        if (centre <= 1000.0 && hz <= 2000.0)
                            worstAnalog = std::max (worstAnalog, std::abs (c.magnitudeDb (hz, sr) - analogBellDb (hz, centre, q, gainDb)));
                    }
                }
    std::printf ("  peor diferencia: medida %.5f dB, centro %.5f dB, vs. analógico (graves) %.4f dB\n", worstMeasured,
                 worstCentre, worstAnalog);
    CHECK (worstMeasured < 0.01);
    CHECK (worstCentre < 0.001);
    CHECK (worstAnalog < 0.1);

    // La campana es simétrica en octavas alrededor del centro (en graves, donde la bilineal no deforma).
    const auto c = dw::EqCoefficients::bell (200.0, 1.0, 9.0, 96000.0);
    CHECK (std::abs (c.magnitudeDb (100.0, 96000.0) - c.magnitudeDb (400.0, 96000.0)) < 0.01);

    // Q en la definición del Cookbook: el ancho de banda en octavas BW cumple 1/Q = 2·sinh(ln2/2·BW), medido entre los
    // puntos donde la campana vale la MITAD de su ganancia en dB. Q = 1.4142 ↔ 1 octava.
    const double q = std::sqrt (2.0);
    const auto wide = dw::EqCoefficients::bell (1000.0, q, 12.0, 192000.0);
    CHECK (std::abs (wide.magnitudeDb (1000.0 / std::sqrt (2.0), 192000.0) - 6.0) < 0.05);
    CHECK (std::abs (wide.magnitudeDb (1000.0 * std::sqrt (2.0), 192000.0) - 6.0) < 0.05);
}

void testEqBellIdentityAndInverse()
{
    std::printf ("EQ campana: 0 dB = bit a bit; +x dB seguida de -x dB = señal original\n");

    const auto input = sineSignal (440.0, 48000.0, 8192);
    const auto flat = dw::EqCoefficients::bell (1000.0, 2.0, 0.0, 48000.0);
    CHECK (flat.dry == 1.0f && flat.bandPass == 0.0f && flat.lowPass == 0.0f);
    dw::EqFilter filter;
    bool exact = true;
    for (const float x : input)
        exact = exact && filter.process (x, flat) == x;
    CHECK (exact);

    // La respuesta de +x y la de −x se anulan en todas las frecuencias...
    double worst = 0.0;
    for (const double sr : sampleRates)
        for (const double hz : { 50.0, 700.0, 3000.0, 15000.0 })
        {
            const auto up = dw::EqCoefficients::bell (2000.0, 0.7, 15.0, sr);
            const auto down = dw::EqCoefficients::bell (2000.0, 0.7, -15.0, sr);
            worst = std::max (worst, std::abs (up.magnitudeDb (hz, sr) + down.magnitudeDb (hz, sr)));
        }
    CHECK (worst < 0.001);

    // ...y en el audio: la cascada devuelve el seno con su amplitud original.
    const auto up = dw::EqCoefficients::bell (500.0, 3.0, 12.0, 48000.0);
    const auto down = dw::EqCoefficients::bell (500.0, 3.0, -12.0, 48000.0);
    dw::EqFilter first, second;
    const auto tone = sineSignal (500.0, 48000.0, 32768);
    std::vector<float> output (tone.size());
    for (size_t i = 0; i < tone.size(); ++i)
        output[i] = second.process (first.process (tone[i], up), down);
    CHECK (std::abs (toDb (toneAmplitude (output, 500.0, 48000.0, 16384))) < 0.01);
}

void testEqBellIsStableWhenModulated()
{
    std::printf ("EQ campana: estable con frecuencia, Q y ganancia saltando en cada muestra (extremos y Nyquist)\n");

    for (const double sr : sampleRates)
    {
        dw::EqFilter filter;
        unsigned seed = 777;
        auto random = [&seed] {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<double> (seed >> 8) / 16777216.0;
        };
        float peak = 0.0f;
        for (int i = 0; i < 100000; ++i)
        {
            const double hz = 10.0 * std::pow (3000.0, random());    // 10 Hz–30 kHz (encima de Nyquist se limita)
            const double q = 0.025 * std::pow (1600.0, random());    // 0.025–40
            const double gainDb = -30.0 + 60.0 * random();
            const auto c = dw::EqCoefficients::bell (hz, q, gainDb, sr);
            const float x = (i / 37) % 2 == 0 ? 0.5f : -0.5f;
            peak = std::max (peak, std::abs (filter.process (x, c)));
        }
        CHECK (std::isfinite (peak));
        CHECK (peak < 1000.0f);

        // Respuesta en y por encima de Nyquist: finita.
        const auto top = dw::EqCoefficients::bell (30000.0, 40.0, 30.0, sr);
        CHECK (std::isfinite (top.magnitudeDb (0.4999 * sr, sr)) && std::isfinite (top.magnitudeDb (sr, sr)));
    }
}

// --- Suavizado ------------------------------------------------------------------------------------------------------

void testSmoothedParameter()
{
    std::printf ("SmoothedParameter: tras una constante de tiempo llega al 63 %%; después converge al objetivo\n");

    for (const double sr : sampleRates)
    {
        dw::SmoothedParameter p;
        p.setTime (0.010, sr);
        const int tau = static_cast<int> (std::lround (0.010 * sr));
        for (int i = 0; i < tau; ++i)
            p.approach (1.0f);
        CHECK (std::abs (p.value - (1.0f - std::exp (-1.0f))) < 0.002f);

        for (int i = 0; i < 20 * tau; ++i)
            p.approach (1.0f);
        CHECK (std::abs (p.value - 1.0f) < 1.0e-4f);
    }

    // Hacia 0 sí termina exactamente en el objetivo y deja de avisar de cambios.
    dw::SmoothedParameter p;
    p.setTime (0.010, 48000.0);
    p.value = 1.0f;
    int steps = 0;
    while (p.approach (0.0f) && steps < 100000)
        ++steps;
    CHECK (p.value == 0.0f);
    CHECK (steps < 20 * 480);
}

void testLinearRamp()
{
    std::printf ("LinearRamp: llega exactamente al objetivo en el tiempo pedido, sin saltos al cambiarlo a mitad\n");

    for (const double sr : sampleRates)
    {
        dw::LinearRamp ramp;
        ramp.setTime (0.020, sr);
        ramp.snap (1.0f);
        CHECK (ramp.isSteady() && ramp.next() == 1.0f);

        const float goal = dw::dbToGain (-7.3f);
        ramp.setTarget (goal);
        const int samples = static_cast<int> (std::lround (0.020 * sr));
        for (int i = 0; i < samples - 1; ++i)
            ramp.next();
        CHECK (! ramp.isSteady());
        CHECK (ramp.next() == goal); // exacto, no "casi"
        CHECK (ramp.isSteady() && ramp.next() == goal);

        // Cambiar el objetivo a mitad de rampa: sigue desde donde estaba, con pasos acotados.
        float maxStep = 0.0f;
        const auto advance = [&] (int count) {
            for (int i = 0; i < count; ++i)
            {
                const float previous = ramp.value;
                maxStep = std::max (maxStep, std::abs (ramp.next() - previous));
            }
        };
        ramp.setTarget (2.0f);
        advance (samples / 2);
        ramp.setTarget (0.0f);
        advance (2 * samples);
        CHECK (ramp.value == 0.0f);
        CHECK (maxStep <= 2.0f / static_cast<float> (samples) * 1.001f);
    }
}

void testEffectSwitch()
{
    std::printf ("EffectSwitch: fundido lineal del tiempo pedido; apagado y quieto = silencio\n");

    dw::EffectSwitch fade;
    fade.setTime (0.010, 48000.0);
    fade.snap (false);
    CHECK (fade.isSilent (false));
    int samples = 0;
    while (fade.next (true) < 1.0f && samples < 10000)
        ++samples;
    CHECK (std::abs ((samples + 1) - 480) <= 1);
    CHECK (fade.isSteady (true));
    CHECK (! fade.isSilent (false)); // acaba de apagarse: todavía tiene que bajar
}

// --- Decibelios y medidores ----------------------------------------------------------------------------------------

void testDecibels()
{
    std::printf ("Decibelios: 0 dB = ×1 exacto, ±6.02 dB = ×2 / ×0.5, ida y vuelta, silencio\n");
    CHECK (dw::dbToGain (0.0f) == 1.0f);
    CHECK (std::abs (dw::dbToGain (6.0206f) - 2.0f) < 1.0e-4f);
    CHECK (std::abs (dw::dbToGain (-6.0206f) - 0.5f) < 1.0e-5f);
    for (float db = -60.0f; db <= 24.0f; db += 0.7f)
        CHECK (std::abs (dw::gainToDb (dw::dbToGain (db)) - db) < 1.0e-4f);
    CHECK (dw::gainToDb (0.0f) == dw::silenceDb);
    CHECK (dw::gainToDb (1.0e-9f) == dw::silenceDb);
}

void testLevelMeter()
{
    std::printf ("LevelMeter: RMS de un seno = pico − 3.01 dB, integración de 300 ms, pico exacto entre lecturas\n");

    for (const double sr : sampleRates)
    {
        dw::LevelMeter meter;
        meter.prepare (sr);

        // Seno de 1 kHz a −6 dBFS durante 2 s (casi 7 constantes de tiempo): RMS = pico / raíz(2).
        const float amplitude = dw::dbToGain (-6.0f);
        const int total = static_cast<int> (2.0 * sr);
        for (int i = 0; i < total; ++i)
            meter.push (amplitude * static_cast<float> (std::sin (2.0 * pi * 1000.0 * i / sr)));
        CHECK (std::abs (dw::gainToDb (meter.rms()) - (-6.0f - 3.0103f)) < 0.02f);
        CHECK (std::abs (meter.takePeak() - amplitude) < 1.0e-3f);
        CHECK (meter.takePeak() == 0.0f); // leer reinicia el pico

        // Escalón de silencio a 1.0: tras 300 ms la potencia media llega al 63 %.
        meter.reset();
        const int tau = static_cast<int> (std::lround (dw::LevelMeter::rmsSeconds * sr));
        for (int i = 0; i < tau; ++i)
            meter.push (1.0f);
        CHECK (std::abs (meter.rms() * meter.rms() - (1.0f - std::exp (-1.0f))) < 0.002f);

        // Un solo pico entre miles de muestras bajas no se pierde.
        meter.reset();
        for (int i = 0; i < 4096; ++i)
            meter.push (i == 1234 ? -0.9f : 0.01f);
        CHECK (meter.takePeak() == 0.9f);
    }
}

void testPeakDisplay()
{
    std::printf ("PeakDisplay: sube al instante, cae 20 dB/s, marca sostenida 1.5 s, clip latcheado\n");

    dw::PeakDisplay display;
    display.update (dw::dbToGain (-6.0f), 1.0 / 30.0);
    CHECK (std::abs (display.levelDb + 6.0f) < 1.0e-4f);
    CHECK (std::abs (display.holdDb + 6.0f) < 1.0e-4f);

    // Silencio durante 1 s: la barra baja 20 dB; la marca sigue en -6 hasta los 1.5 s.
    for (int frame = 0; frame < 30; ++frame)
        display.update (0.0f, 1.0 / 30.0);
    CHECK (std::abs (display.levelDb + 26.0f) < 0.01f);
    CHECK (std::abs (display.holdDb + 6.0f) < 1.0e-4f);
    for (int frame = 0; frame < 30; ++frame)
        display.update (0.0f, 1.0 / 30.0);
    CHECK (display.holdDb < -6.0f && display.holdDb >= display.levelDb);
    CHECK (! display.clipped);

    // 0 dBFS exacto no es clip; por encima sí, y queda encendido hasta reiniciarlo.
    display.update (1.0f, 1.0 / 30.0);
    CHECK (! display.clipped);
    display.update (1.01f, 1.0 / 30.0);
    display.update (0.0f, 10.0);
    CHECK (display.clipped);
    CHECK (display.levelDb == dw::silenceDb);
    display.resetClip();
    CHECK (! display.clipped);
}

// --- Línea de retardo -----------------------------------------------------------------------------------------------

void testDelayLine()
{
    std::printf ("DelayLine: retardo entero exacto, lineal y cúbico fraccionarios\n");

    dw::DelayLine line;
    line.prepare (100);
    CHECK (line.getMaxDelay() >= 100);

    line.push (1.0f);
    for (int i = 0; i < 37; ++i)
        line.push (0.0f);
    CHECK (line.read (37) == 1.0f);
    CHECK (line.read (36) == 0.0f && line.read (38) == 0.0f);
    CHECK (std::abs (line.readLinear (36.5f) - 0.5f) < 1.0e-6f);

    // Un seno lento leído 10.3 muestras atrás con Catmull-Rom = el seno de hace 10.3 muestras.
    line.reset();
    constexpr double w = 2.0 * pi * 300.0 / 48000.0;
    double worst = 0.0;
    for (int n = 0; n < 2000; ++n)
    {
        line.push (static_cast<float> (std::sin (w * n)));
        if (n > 20)
            worst = std::max (worst, std::abs (line.readCubic (10.3f) - std::sin (w * (n - 10.3))));
    }
    CHECK (worst < 1.0e-4);
}

void testLagrangeDelay()
{
    std::printf ("Lagrange 3.er orden: cúbicas exactas, retardo exacto en graves, ganancia <= 1, fase medida = teoría\n");

    // Un polinomio de grado 3 se reproduce exactamente (salvo el redondeo del float).
    dw::DelayLine line;
    line.prepare (64);
    const auto cubic = [] (double t) { return 1.0e-4 * (0.5 * t * t * t - 3.0 * t * t + 2.0 * t + 7.0); };
    for (int n = 0; n < 40; ++n)
        line.push (static_cast<float> (cubic (n)));
    double worstCubic = 0.0;
    for (const double delay : { 1.0, 1.25, 2.5, 7.75, 13.999, 20.0 })
        worstCubic = std::max (worstCubic, std::abs (line.readLagrange (dw::LagrangeTaps::forDelay (delay)) - cubic (39.0 - delay)));
    CHECK (worstCubic < 2.0e-6); // valores de hasta 2.5: el límite es la resolución del float

    // Retardo entero: pesos 0, 1, 0, 0 (la lectura entera, bit a bit).
    const auto whole = dw::LagrangeTaps::forDelay (9.0);
    CHECK (whole.first == 8 && whole.weights[1] == 1.0f && whole.weights[0] == 0.0f && whole.weights[2] == 0.0f
           && whole.weights[3] == 0.0f);
    CHECK (line.readLagrange (whole) == line.read (9));

    // Fracción en [1, 2): ganancia <= 1 en todo el espectro y retardo de fase exacto en graves.
    double worstGain = 0.0, worstLowDelay = 0.0;
    for (int f = 0; f <= 100; ++f)
    {
        const double delay = 30.0 + f / 100.0;
        const auto taps = dw::LagrangeTaps::forDelay (delay);
        CHECK (taps.fraction >= 1.0 && taps.fraction < 2.0);
        for (int k = 1; k <= 200; ++k)
            worstGain = std::max (worstGain, taps.magnitude (pi * k / 200.0));
        worstLowDelay = std::max (worstLowDelay, std::abs (taps.phaseDelay (1.0e-3) - delay));
    }
    std::printf ("  ganancia máxima %.6f, error del retardo a w = 0.001: %.2e muestras\n", worstGain, worstLowDelay);
    CHECK (worstGain <= 1.0 + 1.0e-6);
    CHECK (worstLowDelay < 1.0e-5);

    // Un seno de 3 kHz a 44.1 kHz leído con retardo 12.5: el retardo medido coincide con phaseDelay().
    constexpr double w = 2.0 * pi * 3000.0 / 44100.0;
    const auto taps = dw::LagrangeTaps::forDelay (12.5);
    line.reset();
    Complex in {}, out {};
    for (int n = 0; n < 100 + 29 * 147; ++n) // 147 muestras = 10 ciclos exactos: sin fuga de la frecuencia negativa
    {
        const double x = std::sin (w * n);
        line.push (static_cast<float> (x));
        if (n >= 100)
        {
            in += x * std::polar (1.0, -w * n);
            out += static_cast<double> (line.readLagrange (taps)) * std::polar (1.0, -w * n);
        }
    }
    // La fase solo dice el retardo módulo un período (14.7 muestras): se lleva al período de la teoría.
    const double period = 2.0 * pi / w;
    double measuredDelay = -std::arg (out / in) / w;
    measuredDelay += period * std::round ((taps.phaseDelay (w) - measuredDelay) / period);
    std::printf ("  retardo a 3 kHz: medido %.5f, teoría %.5f muestras\n", measuredDelay, taps.phaseDelay (w));
    CHECK (std::abs (measuredDelay - taps.phaseDelay (w)) < 1.0e-3);
    CHECK (std::abs (std::abs (out / in) - taps.magnitude (w)) < 1.0e-3);
}

// --- Utilidades -----------------------------------------------------------------------------------------------------

void testFastTanh()
{
    std::printf ("fastTanh: error < 1e-6 frente a std::tanh, impar y limitada a ±1\n");

    double worst = 0.0;
    for (int i = -120000; i <= 120000; ++i)
    {
        const float x = static_cast<float> (i) * 1.0e-4f;
        worst = std::max (worst, static_cast<double> (std::abs (dw::fastTanh (x) - std::tanh (x))));
        if (dw::fastTanh (-x) != -dw::fastTanh (x))
            ++failures;
    }
    std::printf ("  error máximo: %.2e\n", worst);
    CHECK (worst < 1.0e-6);
    CHECK (std::abs (dw::fastTanh (1.0e6f)) <= 1.0f);
}

void testSimdMatchesScalar()
{
    std::printf ("SIMD: Float4, transpose, readPositions y advancePhases = la cuenta escalar, bit a bit\n");

    const std::array<float, 4> a { 1.5f, -2.25f, 3.0e-3f, 7.0f };
    const std::array<float, 4> b { 0.5f, 4.0f, -1.0f, 1.0e5f };
    std::array<float, 4> sum {}, product {};
    (dw::simd::Float4::load (a.data()) + dw::simd::Float4::load (b.data())).store (sum.data());
    (dw::simd::Float4::load (a.data()) * dw::simd::Float4::load (b.data())).store (product.data());
    for (size_t i = 0; i < 4; ++i)
        CHECK (sum[i] == a[i] + b[i] && product[i] == a[i] * b[i]);
    CHECK (dw::simd::Float4::load (a.data()).sum() == (a[0] + a[2]) + (a[1] + a[3]));

    std::array<std::array<float, 4>, 4> m {};
    std::array<dw::simd::Float4, 4> rows;
    for (size_t r = 0; r < 4; ++r)
    {
        for (size_t c = 0; c < 4; ++c)
            m[r][c] = static_cast<float> (10 * r + c);
        rows[r] = dw::simd::Float4::load (m[r].data());
    }
    dw::simd::transpose (rows[0], rows[1], rows[2], rows[3]);
    for (size_t c = 0; c < 4; ++c)
    {
        std::array<float, 4> column {};
        rows[c].store (column.data());
        for (size_t r = 0; r < 4; ++r)
            CHECK (column[r] == m[r][c]);
    }

    double phases[4] = { 0.0, 0.123456789, 0.5, 0.999999 };
    const double increments[4] = { 0.01, 0.3, 0.75, 2.0e-6 };
    int index[4] {};
    float fraction[4] {};
    dw::simd::readPositions (phases, 2048.0, index).store (fraction);
    for (size_t i = 0; i < 4; ++i)
    {
        const double position = phases[i] * 2048.0;
        CHECK (index[i] == static_cast<int> (position));
        CHECK (fraction[i] == static_cast<float> (position - static_cast<int> (position)));
    }
    double expected[4];
    for (size_t i = 0; i < 4; ++i)
        expected[i] = phases[i] + increments[i] >= 1.0 ? phases[i] + increments[i] - 1.0 : phases[i] + increments[i];
    dw::simd::advancePhases (phases, increments);
    CHECK (std::memcmp (phases, expected, sizeof (phases)) == 0);
}

void testMidiNoteToHz()
{
    std::printf ("midiNoteToHz: La 440 (nota 69), octavas exactas\n");
    CHECK (dw::midiNoteToHz (69) == 440.0);
    CHECK (dw::midiNoteToHz (81) == 880.0);
    CHECK (std::abs (dw::midiNoteToHz (60) - 261.6255653) < 1.0e-6);
}

// --- Modulación: seguidor de envolvente y LFO ----------------------------------------------------------------------

double gainToDbD (double gain)
{
    return 20.0 * std::log10 (gain);
}

void testEnvelopeFollower()
{
    std::printf ("EnvelopeFollower: 63 %% en un ataque, cae 8.7 dB por cada relajación, sigue el pico de un seno\n");

    for (const double sr : sampleRates)
    {
        dw::EnvelopeFollower follower;
        follower.prepare (sr);
        follower.setTimes (0.010, 0.200);

        // Escalón de 0 a 1: tras τ de ataque vale 1 − 1/e.
        const int attackSamples = static_cast<int> (std::lround (0.010 * sr));
        for (int i = 0; i < attackSamples; ++i)
            follower.process (1.0);
        CHECK (std::abs (follower.getValue() - (1.0 - std::exp (-1.0))) < 0.002);

        // Asentado en 1 y soltado: cada τ de relajación cae 1/e = 8.69 dB (una recta en dB). El ataque solo retrasa un
        // poco el arranque de la caída (el primer τ cae 8.2 dB).
        for (int i = 0; i < static_cast<int> (sr); ++i)
            follower.process (1.0);
        const int releaseSamples = static_cast<int> (std::lround (0.200 * sr));
        std::array<double, 4> releaseDb {};
        for (size_t k = 1; k < releaseDb.size(); ++k)
        {
            for (int i = 0; i < releaseSamples; ++i)
                follower.process (0.0);
            releaseDb[k] = gainToDbD (follower.getValue());
        }
        CHECK (std::abs (releaseDb[1] + 8.24) < 0.05);
        CHECK (std::abs (releaseDb[2] - releaseDb[1] + 8.686) < 0.02);
        CHECK (std::abs (releaseDb[3] - releaseDb[2] + 8.686) < 0.02);

        // Un pico nuevo, aunque sea de una sola muestra, entra entero en la primera etapa: con ataque 0 la envolvente
        // lo marca al instante.
        follower.setTimes (0.0, 0.200);
        follower.process (0.9);
        CHECK (follower.getValue() == 0.9);
        follower.setTimes (0.010, 0.200);

        // Un seno de 100 Hz con ataque rápido y relajación larga: la envolvente queda en su pico, con poca ondulación.
        follower.setTimes (0.0005, 0.300);
        follower.reset();
        double low = 1.0, high = 0.0;
        for (int i = 0; i < static_cast<int> (sr); ++i)
        {
            const double value = follower.process (std::abs (0.5 * std::sin (2.0 * pi * 100.0 * i / sr)));
            if (i > static_cast<int> (sr) / 2)
            {
                low = std::min (low, value);
                high = std::max (high, value);
            }
        }
        // El ataque no es instantáneo: en la cima del seno se queda un poco por debajo (−0.06 dB). Entre dos picos de |x|
        // (5 ms) la relajación de 300 ms la deja caer ~0.05 dB.
        CHECK (high <= 0.5 && gainToDbD (high / 0.5) > -0.1);
        CHECK (gainToDbD (high / low) < 0.1);
    }

    // Tiempo 0 = sigue al instante.
    dw::EnvelopeFollower instant;
    instant.prepare (48000.0);
    instant.setTimes (0.0, 0.0);
    CHECK (instant.process (0.7) == 0.7 && instant.process (0.2) == 0.2);
}

// Índices de las muestras en que el detector dispara.
std::vector<size_t> detectTransients (dw::TransientDetector& detector, const std::vector<float>& signal)
{
    std::vector<size_t> hits;
    for (size_t i = 0; i < signal.size(); ++i)
        if (detector.process (std::abs (signal[i])))
            hits.push_back (i);
    return hits;
}

void testTransientDetector()
{
    std::printf ("TransientDetector: un golpe por kick en su muestra exacta, sin redisparos en la cola ni en un grave sostenido\n");

    for (const double sr : sampleRates)
    {
        // Kick sintético: un coseno de 55 Hz (arranca en su pico: el golpe empieza en la primera muestra) que se apaga
        // con τ = 60 ms. Patrón de dembow desplazado a 95 BPM: pasos 1, 7, 11 y 15 (0, 1.5, 2.5 y 3.5 negras).
        const double beat = sr * 60.0 / 95.0;
        std::vector<float> kicks (static_cast<size_t> (5.0 * beat), 0.0f);
        std::vector<size_t> onsets;
        for (const double at : { 0.0, 1.5, 2.5, 3.5 })
        {
            const auto onset = static_cast<size_t> (std::lround (0.2 * sr + at * beat));
            onsets.push_back (onset);
            for (size_t i = onset; i < kicks.size(); ++i)
            {
                const double t = static_cast<double> (i - onset) / sr;
                kicks[i] += static_cast<float> (0.9 * std::cos (2.0 * pi * 55.0 * t) * std::exp (-t / 0.06));
            }
        }
        dw::TransientDetector detector;
        detector.prepare (sr);
        detector.setThresholdDb (-18.0f);
        detector.setHoldSeconds (0.03);
        CHECK (detectTransients (detector, kicks) == onsets);

        // Un grave sostenido (30 Hz, el que más ondula la envolvente) por encima del umbral: un solo golpe.
        detector.reset();
        auto sub = sineSignal (30.0, sr, static_cast<size_t> (sr));
        for (auto& sample : sub)
            sample *= 0.5f;
        CHECK (detectTransients (detector, sub).size() == 1);

        // Por debajo del umbral, nunca.
        detector.reset();
        detector.setThresholdDb (-3.0f);
        CHECK (detectTransients (detector, sub).empty());
    }

    // Retención: dos clics a 100 ms. La envolvente baja de 0 dB a −12 dB (umbral − histéresis) en 69 ms, así que con
    // 20 ms de retención los dos disparan y con 150 ms el segundo se ignora.
    dw::TransientDetector detector;
    detector.prepare (48000.0);
    detector.setThresholdDb (-6.0f);
    std::vector<float> clicks (9600, 0.0f);
    clicks[100] = clicks[100 + 4800] = 1.0f;
    detector.setHoldSeconds (0.02);
    CHECK ((detectTransients (detector, clicks) == std::vector<size_t> { 100, 4900 }));
    detector.reset();
    detector.setHoldSeconds (0.15);
    CHECK ((detectTransients (detector, clicks) == std::vector<size_t> { 100 }));

    // Histéresis: a los 40 ms la envolvente solo bajó a −7 dB (no a −12): aun sin retención, el segundo clic no dispara.
    detector.reset();
    detector.setHoldSeconds (0.0);
    std::vector<float> close (4800, 0.0f);
    close[0] = close[1920] = 1.0f;
    CHECK ((detectTransients (detector, close) == std::vector<size_t> { 0 }));
}

void testBreakpointCurve()
{
    std::printf ("Curva por puntos: tension, evaluacion, edicion sin desordenar y saneado\n");

    // El segmento doblado une siempre sus extremos; tensión 0 = recta; > 0 arranca lenta; < 0 arranca rápida.
    bool endpoints = true;
    for (const float tension : { -1.0f, -0.3f, 0.0f, 0.2f, 1.0f })
        endpoints = endpoints && std::abs (dw::BreakpointCurve::bend (0.0f, tension)) < 1.0e-6f
                    && std::abs (dw::BreakpointCurve::bend (1.0f, tension) - 1.0f) < 1.0e-6f;
    CHECK (endpoints);
    CHECK (dw::BreakpointCurve::bend (0.3f, 0.0f) == 0.3f);
    CHECK (dw::BreakpointCurve::bend (0.5f, 0.5f) < 0.5f && dw::BreakpointCurve::bend (0.5f, -0.5f) > 0.5f);
    CHECK (dw::BreakpointCurve::bend (0.5f, 1.0f) < 0.02f);                         // tensión máxima: 1/(e^4 + 1)
    CHECK (std::abs (dw::BreakpointCurve::bend (0.5f, 1.0e-6f) - 0.5f) < 1.0e-5f); // casi 0: sin saltos cerca de la recta
    bool monotonic = true;
    for (int i = 1; i <= 1000; ++i)
        monotonic = monotonic && dw::BreakpointCurve::bend (static_cast<float> (i) / 1000.0f, 0.7f)
                                     >= dw::BreakpointCurve::bend (static_cast<float> (i - 1) / 1000.0f, 0.7f);
    CHECK (monotonic);

    // Evaluación: pasa por cada punto, interpola entre ellos y fuera de [0, 1] da el extremo.
    const dw::BreakpointCurve curve { { 0.0f, 0.2f }, { 0.25f, 1.0f, 0.6f }, { 0.75f, 0.0f }, { 1.0f, 0.5f } };
    CHECK (curve.size() == 4);
    CHECK (curve.valueAt (0.0f) == 0.2f && curve.valueAt (0.25f) == 1.0f && curve.valueAt (0.75f) == 0.0f && curve.valueAt (1.0f) == 0.5f);
    CHECK (std::abs (curve.valueAt (0.125f) - 0.6f) < 1.0e-6f); // recta: a mitad, la media
    CHECK (std::abs (curve.valueAt (0.5f) - (1.0f - dw::BreakpointCurve::bend (0.5f, 0.6f))) < 1.0e-6f);
    CHECK (curve.valueAt (-3.0f) == 0.2f && curve.valueAt (7.0f) == 0.5f);

    // Escalón vertical (dos puntos en la misma x): a la izquierda el valor de antes, en el punto el de después.
    const dw::BreakpointCurve step { { 0.0f, 0.0f }, { 0.5f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 1.0f } };
    CHECK (step.valueAt (0.4999f) == 0.0f && step.valueAt (0.5f) == 1.0f);

    // Edición: insertar ordena por x; mover no deja pasar a los vecinos; los extremos no se mueven en x ni se quitan.
    dw::BreakpointCurve edited;
    CHECK (edited.size() == 2 && edited.valueAt (0.5f) == 0.5f); // por defecto, la recta (0, 0) → (1, 1)
    CHECK (edited.insert (0.6f, 0.9f) == 1);
    CHECK (edited.insert (0.3f, 0.1f) == 1);
    CHECK (edited[1].x == 0.3f && edited[2].x == 0.6f);
    edited.move (1, 0.9f, 2.0f); // intenta pasar a su vecino de la derecha y salirse por arriba
    CHECK (edited[1].x == 0.6f && edited[1].y == 1.0f);
    edited.move (0, 0.5f, 0.4f);
    CHECK (edited[0].x == 0.0f && edited[0].y == 0.4f);
    CHECK (! edited.remove (0) && ! edited.remove (edited.size() - 1));
    CHECK (edited.remove (1) && edited.size() == 3);
    edited.setTension (0, 5.0f);
    CHECK (edited[0].tension == 1.0f);

    dw::BreakpointCurve full;
    int inserted = 0;
    while (full.insert (0.5f, 0.5f) >= 0)
        ++inserted;
    CHECK (inserted == dw::BreakpointCurve::maxPoints - 2 && full.size() == dw::BreakpointCurve::maxPoints);

    // Saneado: desordenada, fuera de rango y con NaN → ordenada, dentro de rango y con extremos en 0 y 1.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const dw::BreakpointCurve broken { { 0.7f, 2.0f }, { 0.2f, nan, nan }, { 0.4f, -1.0f, 3.0f } };
    CHECK (broken.size() == 3);
    CHECK (broken[0].x == 0.0f && broken[1].x == 0.4f && broken[2].x == 1.0f);
    CHECK (broken[0].y == 0.0f && broken[0].tension == 0.0f && broken[1].y == 0.0f && broken[1].tension == 1.0f && broken[2].y == 1.0f);
    const dw::BreakpointCurve single { { 0.5f, 0.5f } }; // un solo punto no es una curva: vuelve a la recta
    CHECK (single == dw::BreakpointCurve());
}

void testLfoDivisions()
{
    std::printf ("LFO: divisiones de tempo (1/4 a 120 BPM = 2 Hz) y un ciclo por período\n");

    CHECK (std::abs (dw::lfoDivisionHz (dw::lfoDivisions[dw::defaultLfoDivision], 120.0) - 2.0) < 1.0e-12);
    CHECK (std::string_view (dw::lfoDivisions[dw::defaultLfoDivision].name) == "1/4");
    CHECK (std::abs (dw::lfoDivisionHz (dw::lfoDivisions[3], 120.0) - 0.5) < 1.0e-12);         // 1 bar = 4 negras
    CHECK (std::abs (dw::lfoDivisionHz (dw::lfoDivisions[11], 90.0) - 4.5) < 1.0e-12);         // 1/8 T a 90 BPM
    CHECK (std::abs (dw::lfoDivisions[14].beats - 1.5 * dw::lfoDivisions[5].beats) < 1.0e-12); // puntillo = ×1.5

    // Seno de 3 Hz a 48 kHz: tras 16 000 muestras vuelve a la fase 0 habiendo hecho exactamente un ciclo.
    dw::Lfo lfo;
    lfo.reset (1u);
    lfo.setShape (dw::LfoShape::sine);
    const double increment = 3.0 / 48000.0;
    float peak = 0.0f;
    for (int i = 0; i < 16000; ++i)
    {
        peak = std::max (peak, lfo.getValue());
        lfo.advance (increment);
    }
    CHECK (std::abs (peak - 1.0f) < 1.0e-6f);
    CHECK (lfo.getPhase() < 1.0e-6 || lfo.getPhase() > 1.0 - 1.0e-6);
}

void testAnalyzerTap()
{
    std::printf ("AnalyzerTap: mezcla a mono, ultimas N en orden (tambien al dar la vuelta), sin datos = false\n");

    static dw::AnalyzerTap<16> tap; // static: 16 atómicos + contadores, nada grande, pero así no vive en la pila
    std::array<float, 64> buffer {};
    CHECK (! tap.copyLatest (buffer.data(), 4)); // aún no hay muestras
    CHECK (! tap.copyLatest (buffer.data(), 17)); // más que la capacidad

    // Estéreo: L = n, R = −n + 2 → (L + R) / 2 = 1 siempre; después mono: el valor tal cual.
    std::array<float, 10> left {}, right {};
    for (size_t i = 0; i < left.size(); ++i)
    {
        left[i] = static_cast<float> (i);
        right[i] = 2.0f - static_cast<float> (i);
    }
    const std::array<const float*, 2> stereo { left.data(), right.data() };
    tap.push (stereo.data(), 2, 10);
    CHECK (tap.getWritten() == 10);
    CHECK (tap.copyLatest (buffer.data(), 10));
    bool allOne = true;
    for (size_t i = 0; i < 10; ++i)
        allOne = allOne && buffer[i] == 1.0f;
    CHECK (allOne);

    // 13 muestras más en mono (en total 23: el buffer de 16 ya dio la vuelta). Las últimas 16 = 1, 1, 1, 0, 1, ..., 12.
    std::array<float, 13> ramp {};
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = static_cast<float> (i);
    const float* mono = ramp.data();
    tap.push (&mono, 1, 13);
    CHECK (tap.getWritten() == 23);
    CHECK (tap.copyLatest (buffer.data(), 16));
    bool ordered = buffer[0] == 1.0f && buffer[1] == 1.0f && buffer[2] == 1.0f;
    for (size_t i = 3; i < 16; ++i)
        ordered = ordered && buffer[i] == static_cast<float> (i - 3);
    CHECK (ordered);
    CHECK (tap.copyLatest (buffer.data(), 5) && buffer[0] == 8.0f && buffer[4] == 12.0f);
}

void testSpectrumAnalyzer()
{
    std::printf ("SpectrumAnalyzer: seno de 0 dBFS = 0 dB en su bin, -6 dB con amplitud 0.5, scalloping < 1.5 dB, "
                 "fuga lejos del tono < -90 dB, silencio en el piso\n");

    CHECK (dw::SpectrumAnalyzer::orderForSampleRate (44100.0) == 12);
    CHECK (dw::SpectrumAnalyzer::orderForSampleRate (48000.0) == 12);
    CHECK (dw::SpectrumAnalyzer::orderForSampleRate (88200.0) == 13);
    CHECK (dw::SpectrumAnalyzer::orderForSampleRate (96000.0) == 13);

    for (const double sampleRate : sampleRates)
    {
        dw::SpectrumAnalyzer analyzer;
        analyzer.prepare (dw::SpectrumAnalyzer::orderForSampleRate (sampleRate));
        const auto size = static_cast<size_t> (analyzer.getSize());
        const double binHz = sampleRate / static_cast<double> (size);
        CHECK (analyzer.binsDb().size() == size / 2 + 1);

        // Un seno justo en el bin 93 (~1 kHz), con amplitud 1 y 0.5.
        for (const float amplitude : { 1.0f, 0.5f })
        {
            auto signal = sineSignal (93.0 * binHz, sampleRate, size);
            for (auto& sample : signal)
                sample *= amplitude;
            analyzer.analyze (signal.data());
            const double expected = toDb (amplitude);
            CHECK (std::abs (analyzer.binsDb()[93] - expected) < 0.01);
            CHECK (std::abs (analyzer.levelDb (85.0 * binHz, 100.0 * binHz, sampleRate) - expected) < 0.01);
            // Lejos del tono (40 bins = más de 2 octavas de los lóbulos laterales) casi nada: la ventana evita la fuga.
            CHECK (analyzer.binsDb()[93 + 40] < expected - 90.0);
            CHECK (analyzer.binsDb()[93 - 40] < expected - 90.0);
        }

        // Entre dos bins: el pico se reparte; el más fuerte de los dos queda a menos de 1.5 dB (Hann pierde 1.42 dB).
        {
            const auto signal = sineSignal (200.5 * binHz, sampleRate, size);
            analyzer.analyze (signal.data());
            const float strongest = std::max (analyzer.binsDb()[200], analyzer.binsDb()[201]);
            CHECK (strongest < 0.0f && strongest > -1.5f);
        }

        // Zona más angosta que un bin (graves): interpolada, entre los dos bins vecinos.
        {
            const auto signal = sineSignal (10.0 * binHz, sampleRate, size);
            analyzer.analyze (signal.data());
            const float between = analyzer.levelDb (10.2 * binHz, 10.4 * binHz, sampleRate);
            CHECK (between <= analyzer.binsDb()[10] + 1.0e-4f && between >= analyzer.binsDb()[11] - 1.0e-4f);
        }

        const std::vector<float> silence (size, 0.0f);
        analyzer.analyze (silence.data());
        CHECK (analyzer.binsDb()[93] == dw::SpectrumAnalyzer::floorDb);
        CHECK (analyzer.levelDb (20.0, 20000.0, sampleRate) == dw::SpectrumAnalyzer::floorDb);
    }
}

void testTripleBuffer()
{
    std::printf ("TripleBuffer: el lector ve el ultimo valor completo, nunca mezclado, aun con dos hilos\n");

    struct Pair
    {
        std::uint64_t a = 0;
        std::uint64_t b = 0;
    };

    // Un solo hilo: sin escrituras se lee T{}; varias escrituras seguidas = solo la última; sin nada nuevo, se repite.
    dw::TripleBuffer<Pair> box;
    Pair value;
    CHECK (! box.read (value) && value.a == 0 && value.b == 0);
    for (std::uint64_t i = 1; i <= 5; ++i)
        box.write ({ i, i * 10 });
    CHECK (box.read (value) && value.a == 5 && value.b == 50);
    CHECK (! box.read (value) && value.a == 5);
    box.write ({ 6, 60 });
    CHECK (box.read (value) && value.a == 6 && value.b == 60);

    // Dos hilos: el escritor publica pares (n, 3n) sin parar; el lector comprueba que cada par sea coherente (no
    // mezcla dos escrituras) y que n nunca retroceda.
    dw::TripleBuffer<Pair> shared;
    constexpr std::uint64_t writes = 2'000'000;
    std::thread writer ([&shared]
    {
        for (std::uint64_t n = 1; n <= writes; ++n)
            shared.write ({ n, 3 * n });
    });

    bool coherent = true;
    bool monotonic = true;
    std::uint64_t last = 0;
    int freshReads = 0;
    while (last < writes)
    {
        Pair read;
        if (shared.read (read))
            ++freshReads;
        coherent = coherent && read.b == 3 * read.a;
        monotonic = monotonic && read.a >= last;
        last = read.a;
    }
    writer.join();
    CHECK (coherent);
    CHECK (monotonic);
    CHECK (freshReads > 0);
}

} // namespace

int main()
{
    testFftRoundTrip();
    testHalfbandDecimator();
    testHalfbandInterpolator();
    testOversamplerLatency();
    testBlockProcessingIsBitExact();
    testOversamplerReducesAliasing();
    testDcBlocker();
    testOnePole();
    testTiltFilter();
    testSvfMatchesTheory();
    testSvfIsStableWhenModulated();
    testEqBellMatchesTheory();
    testEqBellIdentityAndInverse();
    testEqBellIsStableWhenModulated();
    testLinkwitzRileySplit();
    testMultibandSplitterSumsFlat();
    testSmoothedParameter();
    testLinearRamp();
    testEffectSwitch();
    testDecibels();
    testLevelMeter();
    testPeakDisplay();
    testDelayLine();
    testLagrangeDelay();
    testFastTanh();
    testSimdMatchesScalar();
    testMidiNoteToHz();
    testEnvelopeFollower();
    testTransientDetector();
    testBreakpointCurve();
    testLfoDivisions();
    testAnalyzerTap();
    testSpectrumAnalyzer();
    testTripleBuffer();

    if (failures == 0)
        std::printf ("\nTodos los tests de deepwater::dsp pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);
    return failures == 0 ? 0 : 1;
}
