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

#include "deepwater/dsp/Decibels.h"
#include "deepwater/dsp/DelayLine.h"
#include "deepwater/dsp/FastMath.h"
#include "deepwater/dsp/Fft.h"
#include "deepwater/dsp/Halfband.h"
#include "deepwater/dsp/LevelMeter.h"
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
    testSvfMatchesTheory();
    testSvfIsStableWhenModulated();
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
