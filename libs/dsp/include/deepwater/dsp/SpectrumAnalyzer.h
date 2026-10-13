#pragma once

#include <complex>
#include <vector>

namespace deepwater::dsp
{

// Analizador de espectro: de un trozo de señal (las últimas N muestras) a cuánto nivel hay en cada frecuencia, en dB.
// Corre FUERA del hilo de audio (en la GUI; reserva memoria en prepare). La señal le llega por un AnalyzerTap.
//
// Cómo funciona:
//  1. Ventana de Hann: la FFT supone que el trozo se repite sin fin. Si el trozo no contiene un número entero de ciclos,
//     el final no empalma con el principio y ese "salto" aparece como energía falsa en todas las frecuencias (fuga
//     espectral). La ventana lleva los dos extremos suavemente a 0: el salto desaparece y la fuga cae muy rápido
//     (−31 dB en el primer lóbulo lateral y 18 dB más por octava), así que un armónico chico al lado de una fundamental
//     fuerte se ve. Precio: cada tono se ensancha un poco (ocupa unos 4 bins).
//  2. FFT de N puntos: N/2 + 1 bins separados sampleRate / N Hz (con N = 4096 a 44.1 kHz, 10.8 Hz por bin).
//  3. Calibración: con la ventana, un seno de amplitud A que cae justo en un bin da |X| = A·N/4. Se multiplica por 4/N
//     para que un seno de 0 dBFS marque 0 dB (entre dos bins marca hasta 1.4 dB menos: "scalloping" de Hann).
//
// N es un compromiso: más grande = más detalle en graves (bins más juntos) pero reacciona más lento (el trozo dura más).
// orderForSampleRate elige el que dura unos 93 ms a cualquier frecuencia de muestreo.
class SpectrumAnalyzer
{
public:
    // Reserva los buffers para una FFT de 2^order puntos.
    void prepare (int order);

    // El tamaño de FFT (2^order) que da una ventana de unos 93 ms: 4096 a 44.1/48 kHz, 8192 a 88.2/96 kHz.
    [[nodiscard]] static int orderForSampleRate (double sampleRate) noexcept;

    [[nodiscard]] int getOrder() const noexcept { return order; }
    [[nodiscard]] int getSize() const noexcept { return static_cast<int> (window.size()); }

    // Analiza getSize() muestras (la más vieja primero). Después, binsDb() y levelDb() describen ese trozo.
    void analyze (const float* samples);

    // Nivel de cada bin en dB (0 dB = seno de amplitud 1); getSize() / 2 + 1 valores, con piso en floorDb.
    [[nodiscard]] const std::vector<float>& binsDb() const noexcept { return levels; }

    // Nivel de la zona [lowHz, highHz]: el bin más fuerte dentro de ella (así un armónico no se pierde aunque caiga
    // entre dos píxeles), o, si la zona es más angosta que un bin (en graves), el nivel interpolado en su centro.
    [[nodiscard]] float levelDb (double lowHz, double highHz, double sampleRate) const noexcept;

    // El pico más fuerte de la zona [lowHz, highHz] con más precisión que un bin: se ajusta una parábola (en dB) por el
    // bin más fuerte y sus dos vecinos y se toma su vértice. Con Hann, un tono queda a menos de 0.05 bin de su
    // frecuencia real (a 44.1 kHz y N = 4096, medio Hz) y su nivel a menos de 0.4 dB (sin interpolar, hasta 1.42 dB
    // menos: el "scalloping"). Sirve para poner una banda
    // justo encima de una resonancia. Sin bins en la zona devuelve el centro con el nivel interpolado.
    struct Peak
    {
        double hz = 0.0;
        float levelDb = floorDb;
    };
    [[nodiscard]] Peak peak (double lowHz, double highHz, double sampleRate) const noexcept;

    static constexpr float floorDb = -140.0f;

private:
    int order = 0;
    std::vector<float> window;
    std::vector<std::complex<double>> bins;
    std::vector<float> levels;
};

} // namespace deepwater::dsp
