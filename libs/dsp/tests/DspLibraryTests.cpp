// Tests de la librería DSP compartida (deepwater::dsp). No dependen de JUCE: se compilan como un ejecutable normal y
// se lanzan con ctest. Si algo falla, imprime el archivo, la línea y la condición.
//
// Los de la FFT y el oversampling halfband vienen de los tests de Undertow (Fases 3, 7 y 8): se movieron con las piezas.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <vector>

#include "deepwater/dsp/Crossover.h"
#include "deepwater/dsp/DcBlocker.h"
#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/DelayLine.h"
#include "deepwater/dsp/FastMath.h"
#include "deepwater/dsp/Fft.h"
#include "deepwater/dsp/Halfband.h"
#include "deepwater/dsp/LevelMeter.h"
#include "deepwater/dsp/OnePole.h"
#include "deepwater/dsp/Oversampler.h"
#include "deepwater/dsp/Pitch.h"
#include "deepwater/dsp/Simd.h"
#include "deepwater/dsp/Smoothing.h"
#include "deepwater/dsp/Svf.h"

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

} // namespace

int main()
{
    testFftRoundTrip();
    testHalfbandDecimator();
    testHalfbandInterpolator();
    testOversamplerLatency();
    testOversamplerReducesAliasing();
    testDcBlocker();
    testOnePole();
    testTiltFilter();
    testSvfMatchesTheory();
    testSvfIsStableWhenModulated();
    testLinkwitzRileySplit();
    testMultibandSplitterSumsFlat();
    testSmoothedParameter();
    testLinearRamp();
    testEffectSwitch();
    testDecibels();
    testLevelMeter();
    testPeakDisplay();
    testDelayLine();
    testFastTanh();
    testSimdMatchesScalar();
    testMidiNoteToHz();

    if (failures == 0)
        std::printf ("\nTodos los tests de deepwater::dsp pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);
    return failures == 0 ? 0 : 1;
}
