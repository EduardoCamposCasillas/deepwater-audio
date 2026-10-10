#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "deepwater/dsp/Simd.h"

namespace deepwater::dsp
{

// Decimador ×2 con un filtro FIR "halfband". Sin dependencias de JUCE.
//
// Oversampling: cuando el oscilador hace cosas que crean armónicos muy agudos (FM, sync, escalones), se calcula
// al DOBLE de la frecuencia de muestreo. Así hay espacio hasta 2·sr para esos armónicos antes de que se reflejen.
// Después hay que volver a la frecuencia del host quitándose de encima todo lo que no cabe: primero un
// low-pass que elimine lo que está por encima de ~20 kHz, luego quedarse con una de cada dos muestras.
//
// ¿Por qué "halfband"? Un filtro cuyo corte está justo en la mitad de la banda (sr/2 del host, que es la
// cuarta parte de la frecuencia sobremuestreada) tiene la mitad de sus coeficientes a CERO: cuesta la mitad.
// Y su respuesta es simétrica: si deja pasar hasta fp, rechaza desde sr − fp. Con fp = 20 kHz a 48 kHz, rechaza
// desde 28 kHz: justo lo que, al volver a 48 kHz, se reflejaría por debajo de 20 kHz. Lo que queda entre 20 y
// 28 kHz se refleja entre 20 y 24 kHz: fuera del oído.
//
// Se diseña con el método de la ventana: la respuesta ideal (un sinc) recortada con una ventana de Kaiser, que
// permite elegir la atenuación (aquí 100 dB) y calcula la longitud necesaria. FIR = fase lineal: todas las
// frecuencias llegan con el mismo retardo (unas 35 muestras a 44.1 kHz, 0.8 ms), sin deformar la onda.

// Coeficientes de un halfband para una frecuencia "baja" dada (la del host, o la intermedia de un oversampling
// ×4). Los comparten el decimador y el interpolador (Fase 8).
//
// Fase 11 (SIMD): las dos operaciones acaban siendo la misma cuenta, un producto escalar entre las últimas H + 1
// muestras de una señal (H = halfLength) y una "ventana" de pesos simétrica (los coeficientes impares, en espejo
// alrededor del centro). Escrito así, las muestras y los pesos están seguidos en memoria y se multiplican de 4 en 4.
struct HalfbandDesign
{
    // Hasta dónde se conserva todo intacto: 20 kHz, o un poco menos si el host trabaja por debajo de 44.1 kHz.
    [[nodiscard]] static double passbandEdge (double lowSampleRate) noexcept
    {
        return std::min (20000.0, 0.4535 * lowSampleRate);
    }

    // Se llama fuera del hilo de audio (prepareToPlay): calcula los coeficientes para esta frecuencia.
    void design (double lowSampleRate) noexcept
    {
        const double oversampledRate = 2.0 * lowSampleRate;
        const double transitionHz = lowSampleRate - 2.0 * passbandEdge (lowSampleRate);
        const double transition = 2.0 * std::numbers::pi * transitionHz / oversampledRate; // en radianes/muestra

        // Fórmulas de Kaiser: longitud y "beta" para la atenuación pedida.
        const double beta = 0.1102 * (attenuationDb - 8.7);
        int length = static_cast<int> (std::ceil ((attenuationDb - 7.95) / (2.0 * 2.285 * transition)));
        length = std::clamp (length | 1, 1, maxHalfLength); // impar: el último coeficiente cae en un tap no nulo
        halfLength = length;

        // Coeficientes de los taps impares (los pares, salvo el central, son cero en un halfband).
        // Respuesta ideal: h[j] = sin(π·j/2) / (π·j). El central vale 0.5.
        numCoefficients = (halfLength + 1) / 2;
        for (int i = 0; i < numCoefficients; ++i)
        {
            const int j = 2 * i + 1;
            const double ideal = std::sin (std::numbers::pi * j / 2.0) / (std::numbers::pi * j);
            const double ratio = static_cast<double> (j) / static_cast<double> (halfLength);
            const double taper = besselI0 (beta * std::sqrt (1.0 - ratio * ratio)) / besselI0 (beta);
            coefficients[static_cast<size_t> (i)] = static_cast<float> (ideal * taper);
        }

        // Ventana para el producto escalar. Peso de la muestra que está 'b' posiciones antes de la más nueva
        // (b = 0..H): las de los dos lados del centro llevan el mismo coeficiente. Se guarda empezando por la más
        // antigua y con ceros delante hasta un múltiplo de 4 (los grupos SIMD van completos).
        windowLength = (halfLength + 1 + 3) & ~3;
        window.fill (0.0f);
        const int half = (halfLength - 1) / 2;
        for (int b = 0; b <= halfLength; ++b)
        {
            const int i = b <= half ? half - b : b - (half + 1);
            window[static_cast<size_t> (windowLength - 1 - b)] = coefficients[static_cast<size_t> (i)];
        }
    }

    // Función de Bessel modificada de orden 0 (la de la ventana de Kaiser), por su serie de potencias.
    [[nodiscard]] static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < sum * 1.0e-12)
                break;
        }
        return sum;
    }

    static constexpr double attenuationDb = 100.0;
    // 44.1 kHz es el caso más exigente (transición de 20 a 24.1 kHz): unos 69 taps a cada lado del central.
    static constexpr int maxHalfLength = 75;

    // Muestras que mira el producto escalar: H + 1 redondeado a múltiplo de 4.
    static constexpr int maxWindowLength = (maxHalfLength + 1 + 3) & ~3;

    std::array<float, (maxHalfLength + 1) / 2> coefficients {};
    std::array<float, maxWindowLength> window {}; // ver design: el primero es el peso de la muestra más antigua
    int numCoefficients = 1;
    int halfLength = 1; // distancia (impar) del tap no nulo más lejano al central
    int windowLength = 4;
};

// Suma de a[i]·b[i] para i < n (n múltiplo de 4): 4 multiplicaciones-suma por instrucción.
[[nodiscard]] inline float dotProduct (const float* a, const float* b, int n) noexcept
{
    simd::Float4 sum;
    for (int i = 0; i < n; i += 4)
        sum += simd::Float4::load (a + i) * simd::Float4::load (b + i);
    return sum.sum();
}

class HalfbandDecimator
{
public:
    [[nodiscard]] static double passbandEdge (double outputSampleRate) noexcept
    {
        return HalfbandDesign::passbandEdge (outputSampleRate);
    }

    // Se llama fuera del hilo de audio (prepareToPlay): calcula los coeficientes para esta frecuencia.
    void prepare (double outputSampleRate) noexcept
    {
        filter.design (outputSampleRate);
        reset();
    }

    void reset() noexcept
    {
        firsts.fill (0.0f);
        seconds.fill (0.0f);
        writeIndex = 0;
    }

    // Recibe dos muestras a la frecuencia sobremuestreada (la primera es la más antigua) y devuelve una.
    //
    // Las muestras se guardan en dos historias, una con las "primeras" de cada pareja y otra con las "segundas"
    // (descomposición polifásica). Como H es impar, el tap central cae siempre en una primera, y todos los taps no
    // nulos (los impares) en segundas: la salida es 0.5 · centro + el producto escalar con la historia de segundas.
    [[nodiscard]] float process (float first, float second) noexcept
    {
        firsts[writeIndex] = firsts[writeIndex + bufferSize] = first;
        seconds[writeIndex] = seconds[writeIndex + bufferSize] = second;
        writeIndex = (writeIndex + 1) & (bufferSize - 1);

        // Doble escritura en el buffer circular: las últimas muestras están siempre seguidas en memoria.
        const size_t newest = writeIndex + bufferSize - 1;
        const float centre = firsts[newest - static_cast<size_t> ((filter.halfLength - 1) / 2)];
        const float* window = seconds.data() + newest - static_cast<size_t> (filter.windowLength - 1);
        return 0.5f * centre + dotProduct (window, filter.window.data(), filter.windowLength);
    }

    // Para pasar de mono a estéreo sin salto: el canal derecho empieza con la historia del izquierdo.
    void copyStateFrom (const HalfbandDecimator& other) noexcept
    {
        firsts = other.firsts;
        seconds = other.seconds;
        writeIndex = other.writeIndex;
    }

    // Retardo en muestras de salida (la mitad de la longitud del filtro, en muestras sobremuestreadas / 2).
    [[nodiscard]] double getLatency() const noexcept { return 0.5 * filter.halfLength; }
    [[nodiscard]] int getNumTaps() const noexcept { return 2 * filter.halfLength + 1; }

    static constexpr double attenuationDb = HalfbandDesign::attenuationDb;

private:
    static constexpr size_t bufferSize = 128; // potencia de 2 mayor que la ventana

    HalfbandDesign filter;
    std::array<float, 2 * bufferSize> firsts {}, seconds {};
    size_t writeIndex = 0;
};

// Interpolador ×2 (Fase 8): lo contrario del decimador. Para calcular a 2·sr una señal que llega a sr, se
// intercala un cero entre cada par de muestras (eso duplica la frecuencia de muestreo, pero crea una "imagen"
// espejo del espectro por encima de sr/2) y se borra esa imagen con el MISMO low-pass halfband, con ganancia 2
// para compensar los ceros.
//
// Truco polifásico: como la mitad de las muestras que entran al filtro son ceros y la mitad de los coeficientes
// también, cada muestra de salida usa solo una de las dos mitades. Una sale del tap central (es la muestra
// original, retrasada) y la otra de los taps impares (la muestra "nueva", interpolada entre las originales).
class HalfbandInterpolator
{
public:
    // 'inputSampleRate' es la frecuencia BAJA (la de entrada).
    void prepare (double inputSampleRate) noexcept
    {
        filter.design (inputSampleRate);
        reset();
    }

    void reset() noexcept
    {
        history.fill (0.0f);
        writeIndex = 0;
    }

    // Recibe una muestra y escribe dos a la frecuencia doble (primero la más antigua).
    void process (float input, float& first, float& second) noexcept
    {
        history[static_cast<size_t> (writeIndex)] = input;
        history[static_cast<size_t> (writeIndex + bufferSize)] = input;
        writeIndex = (writeIndex + 1) & (bufferSize - 1);

        // newest[-a] = x[n − a]. Con H = halfLength (impar), la muestra nueva combina las parejas
        // x[n − (H ± j)/2]: las H + 1 últimas muestras, cada una con su peso de la ventana (producto escalar).
        // La original es x[n − (H − 1)/2].
        const float* newest = history.data() + writeIndex + bufferSize - 1;
        first = 2.0f * dotProduct (newest - (filter.windowLength - 1), filter.window.data(), filter.windowLength);
        second = newest[-(filter.halfLength - 1) / 2];
    }

    // Retardo en muestras de ENTRADA: H muestras a la frecuencia doble.
    [[nodiscard]] double getLatency() const noexcept { return 0.5 * filter.halfLength; }

private:
    static constexpr int bufferSize = 128; // potencia de 2 mayor que la ventana

    HalfbandDesign filter;
    std::array<float, 2 * bufferSize> history {};
    int writeIndex = 0;
};

} // namespace deepwater::dsp
