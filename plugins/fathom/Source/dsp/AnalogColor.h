#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

#include "deepwater/dsp/DcBlocker.h"
#include "deepwater/dsp/DelayLine.h"
#include "deepwater/dsp/FastMath.h"
#include "deepwater/dsp/OnePole.h"
#include "deepwater/dsp/Oversampler.h"
#include "deepwater/dsp/Smoothing.h"

namespace fathom::dsp
{

// Fase 13: color analógico. Un EQ de hardware no es solo sus filtros: la señal pasa por amplificadores (bulbos,
// transistores) y transformadores que, con nivel, redondean un poco los picos y suman armónicos. Con poca señal no se
// nota; con la señal fuerte (o subiendo mucho una banda) aparece el "calor". Aquí: después de las bandas, una curva
// suave por canal (como dos canales de hardware, L y R separados) con sobremuestreo, lo aprendido en Riptide.
//
//   entrada ── énfasis de graves ── ↑N ── curva ── ↓N ── de-énfasis ── filtro DC ── salida (retrasada L)
//
// - La curva: f(x) = (tanh(d·x + β) − tanh β) / (d·(1 − tanh² β)). Pendiente 1 en el origen (con señal baja no cambia
//   el volumen ni la curva del EQ), d = cuánto se curva (el drive del circuito: los armónicos impares crecen con d²) y
//   β = la asimetría (el punto de trabajo de un bulbo: aparecen los pares, que crecen con d·tanh β). Es la "tube" de
//   Riptide con el drive y el bias como números.
// - El énfasis: los transformadores saturan antes en los graves (el núcleo se satura con el flujo, que crece al bajar la
//   frecuencia). Antes de la curva, los graves suben G (x + (G − 1)·LP); después, el inverso exacto (1 + (1/G − 1)·LP en
//   G veces la frecuencia, el mismo truco bilineal que el énfasis de la cinta de Riptide). Con señal baja se anulan: la
//   respuesta es plana; con nivel, el bajo se ensucia más que los agudos.
// - El filtro DC (2 Hz): una curva asimétrica corre la onda hacia arriba o abajo.
//
// Modos: LIMPIO no hace nada (bit a bit y sin latencia; es el de siempre). SUTIL y CÁLIDO son la misma cadena con otra
// curva y otro énfasis; pasar de uno a otro es una rampa de 20 ms de d, β y G. Entre LIMPIO y un color cambia la
// latencia: como la etapa espectral, la cadena arranca sin oírse, se llena y después se funde (20 ms, curva en S) de la
// salida sin retraso a la retrasada; al volver, al revés, y se detiene (sin CPU en LIMPIO).
enum class ColorMode { clean, subtle, warm };
inline constexpr int numColorModes = 3;

struct ColorVoicing
{
    float drive = 1.0f; // d
    float bias = 0.0f;  // β
    float lowBoost = 1.0f; // G (lineal): cuánto más fuerte llegan los graves a la curva
};

// Medido en los tests (seno de 1 kHz; THD = armónicos / fundamental):
//   SUTIL:  −12 dBFS −57 dB (0.15 %), −6 dBFS −49 dB, 0 dBFS −39 dB (1.1 %): lo de una consola bien ajustada.
//   CÁLIDO: −12 dBFS −37 dB (1.4 %), −6 dBFS −31 dB (sobre todo pares), 0 dBFS −24 dB (6 %); un bajo de 60 Hz a
//   −6 dBFS, −23.5 dB (el énfasis: 7 dB más que en 1 kHz). Aliasing de 15 kHz a 0 dBFS: bajo −130 dB.
inline constexpr ColorVoicing voicingFor (ColorMode mode) noexcept
{
    switch (mode)
    {
        case ColorMode::subtle: return { 0.35f, 0.03f, 1.41421356f }; // +3 dB de graves a la curva
        case ColorMode::warm:   return { 0.75f, 0.15f, 2.0f };        // +6 dB
        case ColorMode::clean:  break;
    }
    return {};
}

// La curva de un voicing, con sus constantes calculadas una vez (no en cada muestra sobremuestreada).
struct ColorCurve
{
    float drive = 1.0f, bias = 0.0f, offset = 0.0f, scale = 1.0f;

    static ColorCurve make (float drive, float bias) noexcept
    {
        const float t = deepwater::dsp::fastTanh (bias);
        return { drive, bias, t, 1.0f / (drive * (1.0f - t * t)) };
    }

    [[nodiscard]] float operator() (float x) const noexcept { return (deepwater::dsp::fastTanh (drive * x + bias) - offset) * scale; }
};

struct ColorSizes
{
    // Sobremuestreo hasta ~176-192 kHz: ×4 a 44.1/48, ×2 a 88.2/96, nada por encima. La curva es suave (sus armónicos
    // caen rápido): con eso lo que se refleja queda bajo −100 dB.
    static int stagesFor (double sampleRate) noexcept { return sampleRate < 60000.0 ? 2 : sampleRate < 120000.0 ? 1 : 0; }
    static constexpr double cornerHz = 100.0; // el énfasis sube los graves por debajo de aquí (del todo bajo cornerHz)
    static constexpr double dcCutHz = 2.0;    // a 20 Hz, −0.04 dB
};

class ColorStage
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int maxChunk = 64; // el motor procesa por tramos de este tamaño
    static constexpr double fadeSeconds = 0.02;

    // Fuera del hilo de audio (diseña los filtros del sobremuestreo). Si ya hay un color (un proyecto que se abre, un
    // render) arranca directo con él, retrasado y sin fundido.
    void prepare (double newSampleRate, ColorMode initialMode) noexcept
    {
        sampleRate = newSampleRate;
        const int stages = ColorSizes::stagesFor (sampleRate);
        for (auto& channel : channels)
        {
            channel.oversampler.prepare (sampleRate, stages);
            channel.dc.prepare (ColorSizes::dcCutHz, sampleRate);
            channel.dryHistory.prepare (maxLatency + maxChunk);
        }
        latency = channels[0].oversampler.getLatency();
        // Lo que tarda la cadena en llenarse desde cero: la latencia de ida y la cola de los filtros (otra vez lo mismo).
        warmUpSamples = 2 * latency + 32;
        cornerG = static_cast<float> (std::tan (std::numbers::pi * ColorSizes::cornerHz / sampleRate));
        delayFade.setTime (fadeSeconds, sampleRate);
        for (auto* ramp : { &drive, &bias, &lowBoost })
            ramp->setTime (fadeSeconds, sampleRate);

        mode = initialMode;
        running = false;
        if (mode != ColorMode::clean)
        {
            start();
            delayFade.snap (1.0f);
            sinceStart = std::numeric_limits<int>::max();
        }
        else
            delayFade.snap (0.0f);
    }

    // Una vez por bloque. Si aparece un color, la cadena arranca (sin oírse hasta llenarse).
    void setMode (ColorMode newMode) noexcept
    {
        mode = newMode;
        if (mode == ColorMode::clean)
            return;
        if (! running)
            start();
        else
            setVoicingTarget (voicingFor (mode));
    }

    // La latencia del modo pedido (la que se informa al host) y la de cualquier color a esta frecuencia (para la GUI).
    [[nodiscard]] int getLatencySamples() const noexcept { return mode == ColorMode::clean ? 0 : latency; }
    [[nodiscard]] int getColorLatency() const noexcept { return latency; }
    [[nodiscard]] bool isRunning() const noexcept { return running; }

    // Un tramo del motor (≤ maxChunk), en el sitio. 'dry' = la original retrasada como main; después, dryOutput() = la
    // original retrasada como lo que sale de aquí.
    void process (float* const* main, const float* const* dry, int numChannels, int count) noexcept
    {
        numChannels = std::min (numChannels, maxChannels);
        // La historia de la original se guarda siempre: al arrancar, lo retrasado ya tiene el pasado.
        if (! running)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& history = channels[static_cast<size_t> (ch)].dryHistory;
                for (int i = 0; i < count; ++i)
                    history.push (dry[ch][i]);
                dryPointers[static_cast<size_t> (ch)] = dry[ch];
            }
            return;
        }

        // La cadena de color. Pasando de un color al otro, d, β y G cambian en cada muestra (cambiarlos cada 32 dejaba
        // escalones de −70 dB que el test de clics veía); quietos, en trozos que entran en el sobremuestreo en bloque
        // (más rápido e idéntico bit a bit).
        if (! (drive.isSteady() && bias.isSteady() && lowBoost.isSteady()))
            processRamping (main, numChannels, count);
        else
            processSteady (main, numChannels, count);

        // El fundido de la latencia: hacia lo coloreado (retrasado) cuando la cadena ya se llenó; de vuelta si se pide
        // LIMPIO.
        for (int i = 0; i < count; ++i)
        {
            if (sinceStart < std::numeric_limits<int>::max())
                ++sinceStart;
            if (mode == ColorMode::clean)
                delayFade.setTarget (0.0f);
            else if (sinceStart >= warmUpSamples)
                delayFade.setTarget (1.0f);
            const float t = delayFade.next();
            const float s = t * t * (3.0f - 2.0f * t); // curva en S, como en los modos de fase
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& channel = channels[static_cast<size_t> (ch)];
                channel.dryHistory.push (dry[ch][i]);
                const float colored = channel.colored[static_cast<size_t> (i)];
                const float delayedDry = channel.dryHistory.read (latency);
                const float original = dry[ch][i];
                float& sample = main[ch][i];
                sample = s == 1.0f ? colored : s == 0.0f ? sample : sample + (colored - sample) * s;
                channel.dryOut[static_cast<size_t> (i)] = s == 1.0f ? delayedDry : s == 0.0f ? original : original + (delayedDry - original) * s;
            }
        }
        for (int ch = 0; ch < numChannels; ++ch)
            dryPointers[static_cast<size_t> (ch)] = channels[static_cast<size_t> (ch)].dryOut.data();

        // De vuelta sin retraso del todo: se detiene.
        if (mode == ColorMode::clean && delayFade.value == 0.0f && delayFade.isSteady())
            running = false;
    }

    [[nodiscard]] const float* dryOutput (int channel) const noexcept { return dryPointers[static_cast<size_t> (channel)]; }

private:
    static constexpr int subBlock = deepwater::dsp::Oversampler::maxBlockSize;
    // ×4 a 44.1 kHz: 75 muestras; el mayor posible está por debajo de esto (el halfband más largo: 75 de medio largo).
    static constexpr int maxLatency = 160;

    struct Channel
    {
        deepwater::dsp::Oversampler oversampler;
        deepwater::dsp::OnePole emphasis, deEmphasis;
        deepwater::dsp::DcBlocker dc;
        deepwater::dsp::DelayLine dryHistory;
        std::array<float, maxChunk> colored {}, dryOut {};
    };

    // Una muestra por la cadena entera (énfasis, curva sobremuestreada, de-énfasis, filtro DC).
    template <typename Curve>
    static float colorSample (Channel& channel, float x, const Curve& curve, float g, float pre, float post) noexcept
    {
        const float y = channel.oversampler.process (x + (g - 1.0f) * channel.emphasis.lowPass (x, pre), curve);
        return channel.dc.process (y + (1.0f / g - 1.0f) * channel.deEmphasis.lowPass (y, post));
    }

    void processRamping (float* const* main, int numChannels, int count) noexcept
    {
        const float pre = deepwater::dsp::OnePole::coefficient (cornerG);
        for (int i = 0; i < count; ++i)
        {
            const auto curve = ColorCurve::make (drive.next(), bias.next());
            const float g = lowBoost.next();
            const float post = deepwater::dsp::OnePole::coefficient (cornerG * g);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& channel = channels[static_cast<size_t> (ch)];
                channel.colored[static_cast<size_t> (i)] = colorSample (channel, main[ch][i], curve, g, pre, post);
            }
        }
    }

    void processSteady (float* const* main, int numChannels, int count) noexcept
    {
        const auto curve = ColorCurve::make (drive.value, bias.value);
        const float g = lowBoost.value;
        const float pre = deepwater::dsp::OnePole::coefficient (cornerG);
        const float post = deepwater::dsp::OnePole::coefficient (cornerG * g);
        for (int offset = 0; offset < count; offset += subBlock)
        {
            const int n = std::min (subBlock, count - offset);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& channel = channels[static_cast<size_t> (ch)];
                std::array<float, subBlock> buffer;
                for (int i = 0; i < n; ++i)
                {
                    const float x = main[ch][offset + i];
                    buffer[static_cast<size_t> (i)] = x + (g - 1.0f) * channel.emphasis.lowPass (x, pre);
                }
                channel.oversampler.process (buffer.data(), buffer.data(), n, curve);
                for (int i = 0; i < n; ++i)
                {
                    const float y = buffer[static_cast<size_t> (i)];
                    const float flat = y + (1.0f / g - 1.0f) * channel.deEmphasis.lowPass (y, post);
                    channel.colored[static_cast<size_t> (offset + i)] = channel.dc.process (flat);
                }
            }
        }
    }

    // La cadena en limpio, con el color pedido ya en su sitio; no se oye hasta llenarse (sinceStart).
    void start() noexcept
    {
        running = true;
        sinceStart = 0;
        for (auto& channel : channels)
        {
            channel.oversampler.reset();
            channel.emphasis.reset();
            channel.deEmphasis.reset();
            channel.dc.reset();
        }
        const auto voicing = voicingFor (mode);
        drive.snap (voicing.drive);
        bias.snap (voicing.bias);
        lowBoost.snap (voicing.lowBoost);
        delayFade.snap (0.0f);
    }

    void setVoicingTarget (const ColorVoicing& voicing) noexcept
    {
        drive.setTarget (voicing.drive);
        bias.setTarget (voicing.bias);
        lowBoost.setTarget (voicing.lowBoost);
    }

    double sampleRate = 44100.0;
    std::array<Channel, maxChannels> channels;
    std::array<const float*, maxChannels> dryPointers {};
    ColorMode mode = ColorMode::clean;
    bool running = false;
    int latency = 0, warmUpSamples = 0;
    int sinceStart = 0;
    float cornerG = 0.0f;
    deepwater::dsp::LinearRamp delayFade; // 0 = sin retraso (sin color), 1 = coloreada y retrasada
    deepwater::dsp::LinearRamp drive, bias, lowBoost;
};

} // namespace fathom::dsp
