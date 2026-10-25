#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

#include "deepwater/dsp/EqFilter.h"
#include "dsp/Band.h"

namespace fathom::dsp
{

// Auto gain: cuánto cambia el EQ el volumen PERCIBIDO, para compensarlo y comparar con el bypass a volumen igual.
//
// No se mide la señal (eso bombearía con la música y cambiaría su dinámica): se calcula de la curva, así la
// compensación solo se mueve cuando se mueve el EQ. La idea es preguntar "¿cuánto cambiaría la sonoridad de una mezcla
// típica?". Una mezcla típica tiene más o menos la misma energía en cada octava (ruido rosa), y el oído no pesa igual
// todas las frecuencias: se usa el filtro K de la norma de sonoridad ITU-R BS.1770 (la de los LUFS): un corte suave
// bajo ~40 Hz y +4 dB sobre ~2 kHz. Con puntos repartidos en octavas iguales el ruido rosa pesa lo mismo en cada uno,
// así que solo queda el peso K:
//
//     potencia = Σ w(f)·|H(f)|² / Σ w(f)          compensación = −10·log10(potencia) dB
//
// con |H(f)|² la respuesta REAL de las 24 bandas (los mismos coeficientes que el audio). Una campana angosta de +6 dB
// casi no cambia la potencia (casi no compensa); un shelf ancho, sí; un corte en 30 Hz, casi nada (el peso K es chico ahí).
//
// Con bandas en un solo canal (Fase 8: L, R, mid o side) la respuesta en cada frecuencia ya no es un número sino una
// matriz 2×2 que lleva (L, R) de la entrada a la salida: estéreo = h·I, izquierda = diag(h, 1), mid = I + (h − 1)·P_M
// (P_M = la proyección al centro, [[½, ½], [½, ½]]), side = I + (h − 1)·P_S ([[½, −½], [−½, ½]]). Las bandas en serie
// multiplican sus matrices (en orden: no conmutan si mezclan canales). La potencia de salida de una señal con matriz
// de covarianza C es traza(T·C·Tᴴ); se divide por traza(C) (la de entrada). C es una mezcla típica: el side 6 dB por
// debajo del mid y sin correlación entre ellos. Con todo estéreo, T = (Π h)·I y la cuenta es la de antes, |Π h|². En
// mono (L = R = M, S = 0) es el producto de las bandas que no son side.
//
// Cada banda guarda su H en los puntos y solo se recalcula cuando cambian sus ajustes: arrastrar un nodo cuesta una
// banda, no 24. Sin JUCE ni memoria dinámica: corre en el hilo de audio.
class AutoGain
{
public:
    static constexpr int numBands = 24;
    static constexpr int numPoints = 60;           // uno cada 1/6 de octava desde 20 Hz (el último, ~18 kHz)
    static constexpr double firstHz = 20.0;
    static constexpr double pointsPerOctave = 6.0;
    static constexpr float maxCompensationDb = 24.0f;
    static constexpr double sidePower = 0.25; // la potencia del side respecto del mid en la mezcla típica (−6 dB)

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        // El filtro K (BS.1770): high-pass de 2 polos en 38 Hz (Q 0.5) y shelf de agudos de +4 dB en 1.68 kHz.
        using deepwater::dsp::EqCoefficients;
        const auto highPass = EqCoefficients::highPass2 (EqCoefficients::warp (38.13, sampleRate), 0.5);
        const auto shelf = EqCoefficients::highShelf (1681.97, 0.7072, 4.0, sampleRate);
        double total = 0.0;
        for (size_t i = 0; i < hz.size(); ++i)
        {
            hz[i] = std::min (pointHz (static_cast<int> (i)), 0.45 * sampleRate);
            weight[i] = std::norm (highPass.response (hz[i], sampleRate)) * std::norm (shelf.response (hz[i], sampleRate));
            total += weight[i];
        }
        for (auto& w : weight)
            w /= total;
        for (size_t b = 0; b < bands.size(); ++b)
        {
            bands[b] = {};
            response[b].fill (1.0);
        }
        compensation = 0.0f;
        dirty = false;
    }

    // Los ajustes con que suena una banda (ya con la escala de ganancia). Solo recalcula si cambiaron.
    void setBand (int index, const BandSettings& settings) noexcept
    {
        const auto b = static_cast<size_t> (index);
        if (same (settings, bands[b]))
            return;
        bands[b] = settings;
        if (! settings.enabled)
            response[b].fill (1.0);
        else
        {
            const auto cascade = Band::coefficientsFor (settings, sampleRate);
            for (size_t i = 0; i < hz.size(); ++i)
                response[b][i] = cascade.response (hz[i], sampleRate);
        }
        dirty = true;
    }

    // Mono: las bandas side no cambian nada (y las de un canal actúan en el único que hay).
    void setMono (bool shouldBeMono) noexcept
    {
        dirty = dirty || shouldBeMono != mono;
        mono = shouldBeMono;
    }

    // La ganancia en dB que deja la sonoridad como sin el EQ (+ si el EQ la baja, − si la sube).
    [[nodiscard]] float compensationDb() noexcept
    {
        if (! dirty)
            return compensation;
        dirty = false;
        bool split = false; // alguna banda encendida en un solo canal
        for (const auto& band : bands)
            split = split || (band.enabled && band.placement != Placement::stereo);
        double total = 0.0;
        for (size_t i = 0; i < hz.size(); ++i)
            total += weight[i] * powerAt (i, split);
        // Todo cortado (o casi): no tiene sentido subir 100 dB; se limita.
        const double db = total > 1.0e-12 ? -10.0 * std::log10 (total) : static_cast<double> (maxCompensationDb);
        compensation = static_cast<float> (std::clamp (db, -static_cast<double> (maxCompensationDb), static_cast<double> (maxCompensationDb)));
        return compensation;
    }

    static double pointHz (int i) noexcept { return firstHz * std::exp2 (i / pointsPerOctave); }

private:
    using Complex = std::complex<double>;

    // Una matriz 2×2 que lleva (L, R) de la entrada a la salida.
    struct Matrix
    {
        Complex a { 1.0 }, b {}, c {}, d { 1.0 }; // [[a, b], [c, d]]

        Matrix operator* (const Matrix& m) const noexcept
        {
            return { a * m.a + b * m.c, a * m.b + b * m.d, c * m.a + d * m.c, c * m.b + d * m.d };
        }
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

    // Cuánto cambia la potencia en el punto i (1 = nada).
    [[nodiscard]] double powerAt (size_t i, bool split) const noexcept
    {
        if (mono || ! split)
        {
            Complex h { 1.0 };
            for (size_t b = 0; b < bands.size(); ++b)
                if (! mono || bands[b].placement != Placement::side)
                    h *= response[b][i];
            return std::norm (h);
        }
        Matrix total;
        for (size_t b = 0; b < bands.size(); ++b)
            if (bands[b].enabled)
                total = matrixFor (bands[b].placement, response[b][i]) * total; // la banda 1 actúa primero
        // C = [[1 + σ, 1 − σ], [1 − σ, 1 + σ]] (mid de potencia 1 y side σ): traza(T·C·Tᴴ) / traza(C).
        const double diagonal = 1.0 + sidePower, offDiagonal = 1.0 - sidePower;
        const auto row = [&] (Complex x, Complex y) {
            return diagonal * (std::norm (x) + std::norm (y)) + 2.0 * offDiagonal * std::real (x * std::conj (y));
        };
        return (row (total.a, total.b) + row (total.c, total.d)) / (2.0 * diagonal);
    }

    static bool same (const BandSettings& a, const BandSettings& b) noexcept
    {
        // Dos bandas apagadas suenan igual (nada), tengan los ajustes que tengan.
        if (! a.enabled && ! b.enabled)
            return true;
        return a.enabled == b.enabled && a.frequency == b.frequency && a.gainDb == b.gainDb && a.q == b.q && a.shape == b.shape
               && a.slopeIndex == b.slopeIndex && a.analog == b.analog && a.placement == b.placement;
    }

    double sampleRate = 44100.0;
    std::array<double, numPoints> hz {}, weight {};
    std::array<BandSettings, numBands> bands {};
    std::array<std::array<Complex, numPoints>, numBands> response {};
    float compensation = 0.0f;
    bool dirty = false;
    bool mono = false;
};

} // namespace fathom::dsp
