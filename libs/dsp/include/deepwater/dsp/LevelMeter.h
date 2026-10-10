#pragma once

#include <algorithm>
#include <cmath>

#include "deepwater/dsp/Decibels.h"

namespace deepwater::dsp
{

// Medición de nivel en dos mitades:
//  - LevelMeter corre en el hilo de audio y mide lo que NO puede medir nadie más: el pico exacto (la muestra más
//    alta desde la última lectura, sin perder ninguna aunque el bloque sea de 4096 muestras) y el RMS.
//  - PeakDisplay corre en la GUI y decide cómo se MUESTRA el pico (caída lenta, marca sostenida, aviso de clip).
//    Así la balística no depende del tamaño de bloque del host ni de cada cuánto se redibuja la ventana.
//
// Pico vs RMS: el pico es la muestra más alta (lo que decide si la señal se recorta al llegar a 0 dBFS); el RMS
// (raíz de la media de los cuadrados) es la "energía" media, mucho más parecido a lo fuerte que se OYE. Un golpe de
// caja tiene pico alto y RMS bajo; un bajo sostenido o un pad, pico y RMS cercanos. La diferencia entre los dos es
// el "factor de cresta": la saturación lo reduce (recorta picos y deja la energía).

// Medidor de un canal para el hilo de audio.
class LevelMeter
{
public:
    // Tiempo de integración del RMS: 300 ms, el de un VU clásico. Más corto tiembla con cada golpe; más largo tarda
    // en reaccionar.
    static constexpr double rmsSeconds = 0.3;

    void prepare (double sampleRate) noexcept
    {
        // Media exponencial de x²: un filtro de 1 polo sobre la potencia. La constante de tiempo es la del RMS.
        rmsCoef = 1.0 - std::exp (-1.0 / (rmsSeconds * sampleRate));
        reset();
    }

    void reset() noexcept
    {
        meanSquare = 0.0;
        peak = 0.0f;
    }

    void push (float x) noexcept
    {
        peak = std::max (peak, std::abs (x));
        // En double: con un coeficiente tan pequeño (~7·10⁻⁵ a 44.1 kHz) el redondeo del float sesgaría el resultado.
        meanSquare += (static_cast<double> (x) * x - meanSquare) * rmsCoef;
    }

    [[nodiscard]] float rms() const noexcept { return static_cast<float> (std::sqrt (meanSquare)); }

    // Devuelve el pico desde la última llamada y empieza a medir de nuevo.
    [[nodiscard]] float takePeak() noexcept
    {
        const float result = peak;
        peak = 0.0f;
        return result;
    }

private:
    double rmsCoef = 1.0;
    double meanSquare = 0.0;
    float peak = 0.0f;
};

// Balística del pico que se dibuja (la parte de la GUI). Sube al instante (no se puede esconder un pico), cae a
// velocidad constante en dB/s (para que el ojo alcance a leerlo), deja una marca en el pico más alto durante
// holdSeconds, y si alguna muestra pasó de 0 dBFS enciende el aviso de clip hasta que se reinicia a mano.
struct PeakDisplay
{
    float releaseDbPerSecond = 20.0f;
    double holdSeconds = 1.5;

    float levelDb = silenceDb;
    float holdDb = silenceDb;
    double holdAge = 0.0;
    bool clipped = false;

    // peakGain = pico lineal medido desde la actualización anterior; elapsedSeconds = tiempo desde entonces.
    void update (float peakGain, double elapsedSeconds) noexcept
    {
        const float inputDb = gainToDb (peakGain);
        const float fall = releaseDbPerSecond * static_cast<float> (elapsedSeconds);
        levelDb = std::max (inputDb, std::max (silenceDb, levelDb - fall));

        if (inputDb >= holdDb)
        {
            holdDb = inputDb;
            holdAge = 0.0;
        }
        else
        {
            holdAge += elapsedSeconds;
            if (holdAge > holdSeconds)
                holdDb = std::max (levelDb, holdDb - fall);
        }

        // 1.0 = 0 dBFS, el máximo de un archivo o un conversor de punto fijo: lo que pasa de ahí se recorta.
        if (peakGain > 1.0f)
            clipped = true;
    }

    void resetClip() noexcept { clipped = false; }
};

} // namespace deepwater::dsp
