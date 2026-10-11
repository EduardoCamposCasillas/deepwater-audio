#pragma once

#include <array>
#include <numbers>

#include "deepwater/dsp/Svf.h"

namespace deepwater::dsp
{

// --- Crossover Linkwitz-Riley de 4º orden (LR4, 24 dB/oct) -------------------------------------
//
// Un crossover parte la señal en bandas (graves | agudos) para procesarlas por separado y después volver a sumarlas.
// La condición es que, si no se procesa nada, la suma suene igual que la entrada. Un par low-pass + high-pass
// cualquiera no lo cumple: alrededor del corte las dos salidas llegan con fases distintas y al sumarlas aparece un
// pozo o una joroba. Linkwitz-Riley resuelve eso:
//
//   LP = Butterworth_LP²   HP = Butterworth_HP²   (dos Butterworth de 2º orden en cascada: 24 dB/oct)
//
// Cada una vale −6 dB en el corte y, como están EN FASE en todas las frecuencias, la suma es
//
//   LP + HP = (s⁴ + w⁴) / (s² + √2·w·s + w²)² = (s² − √2·w·s + w²) / (s² + √2·w·s + w²)
//
// un ALLPASS de 2º orden: magnitud exactamente 1 (plana) y solo un giro de fase alrededor del corte (360° en total, que
// no se oye como color). Con la transformada bilineal la identidad sigue siendo exacta (es una sustitución de s), así
// que en digital la suma también es plana salvo el redondeo del float.
//
// Con el SVF (Svf.h, Q = 1/√2) un solo integrador doble da a la vez el LP y el HP Butterworth de la misma entrada: el
// LR4 cuesta 3 etapas (la común, otra LP y otra HP). El allpass equivalente sale de otra etapa con los mismos
// coeficientes: x = LP + k·BP + HP, así que LP − k·BP + HP = x − 2k·BP.
struct LinkwitzRiley
{
    using Coefficients = SvfStage::Coefficients;

    // Butterworth (Q = 1/√2): los mismos coeficientes sirven para el cruce y para su allpass. Llama a tan().
    [[nodiscard]] static Coefficients forFrequency (double hz, double sampleRate) noexcept
    {
        return Coefficients::forCutoff (hz, std::numbers::sqrt2 / 2.0, sampleRate);
    }
};

// Un cruce de 2 vías: low = LP4, high = HP4, low + high = allpass.
class LinkwitzRileySplit
{
public:
    struct Bands
    {
        float low, high;
    };

    void reset() noexcept
    {
        common.reset();
        lowStage.reset();
        highStage.reset();
    }

    [[nodiscard]] Bands process (float x, const LinkwitzRiley::Coefficients& c) noexcept
    {
        const auto first = common.process (x, c);
        return { lowStage.process (first.lowPass, c).lowPass, highStage.process (first.highPass, c).highPass };
    }

private:
    SvfStage common, lowStage, highStage;
};

// El allpass que deja una señal con la misma fase que la suma de un cruce LR4 (sin partirla).
class LinkwitzRileyAllPass
{
public:
    void reset() noexcept { stage.reset(); }

    [[nodiscard]] float process (float x, const LinkwitzRiley::Coefficients& c) noexcept
    {
        return x - 2.0f * c.k * stage.process (x, c).bandPass;
    }

private:
    SvfStage stage;
};

// --- Divisor multibanda (hasta MaxBands bandas, un canal) --------------------------------------
//
// Con más de un cruce no basta con encadenarlos. Árbol en cascada: el primer cruce saca la banda más grave, el resto
// (sus agudos) entra al segundo cruce, y así:
//
//   banda 0 = LP₁                     banda 1 = LP₂·HP₁           banda 2 = LP₃·HP₂·HP₁       banda 3 = HP₃·HP₂·HP₁
//
// Las bandas 2 y 3 suman AP₃·HP₂·HP₁, pero la banda 1 no tiene el giro de fase del tercer cruce: al sumarla con ellas
// alrededor de ese corte ya no están en fase. La solución es pasar las bandas graves por el allpass de cada cruce que
// NO atravesaron (banda 0 por AP₂ y AP₃, banda 1 por AP₃). Entonces cada etapa se suma en fase con la siguiente:
//
//   Σ bandas = AP₁·AP₂·AP₃·x        (magnitud plana: con las bandas sin procesar, la suma es la entrada con fase girada)
//
// Para alinear otra señal con la suma (la señal original de un mix en paralelo), basta pasarla por el MISMO divisor y
// sumar sus bandas (o pasarla por los allpasses en cadena).
//
// Las frecuencias deben estar ordenadas (crossover[0] < crossover[1] < ...): el que llama lo garantiza. Los
// coeficientes se comparten entre canales (se calculan una vez por muestra mientras una frecuencia se mueve).
template <int MaxBands>
class MultibandSplitter
{
public:
    static_assert (MaxBands >= 2);
    static constexpr int maxBands = MaxBands;
    static constexpr int maxCrossovers = MaxBands - 1;
    using CrossoverCoefficients = std::array<LinkwitzRiley::Coefficients, maxCrossovers>;

    void reset() noexcept
    {
        for (auto& split : splits)
            split.reset();
        for (auto& row : allPasses)
            for (auto& allPass : row)
                allPass.reset();
    }

    // Parte x en numBands bandas (1..MaxBands), de grave a agudo, en bands[0..numBands). Con 1 banda la copia tal cual.
    void process (float x, const CrossoverCoefficients& c, int numBands, float* bands) noexcept
    {
        float rest = x;
        for (int crossover = 0; crossover < numBands - 1; ++crossover)
        {
            const auto& coefficients = c[static_cast<size_t> (crossover)];
            const auto split = splits[static_cast<size_t> (crossover)].process (rest, coefficients);
            // Las bandas que ya salieron no pasan por este cruce: reciben su giro de fase.
            for (int band = 0; band < crossover; ++band)
                bands[band] = allPasses[static_cast<size_t> (band)][static_cast<size_t> (crossover)].process (bands[band], coefficients);
            bands[crossover] = split.low;
            rest = split.high;
        }
        bands[numBands - 1] = rest;
    }

private:
    std::array<LinkwitzRileySplit, maxCrossovers> splits;
    // allPasses[banda][cruce]: solo se usan los de cruce > banda.
    std::array<std::array<LinkwitzRileyAllPass, maxCrossovers>, maxCrossovers> allPasses;
};

} // namespace deepwater::dsp
