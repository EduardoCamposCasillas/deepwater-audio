#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "deepwater/theory/Melody.h"

// Las líneas de la Fase 6 (bajo y melodía) como datos del proyecto: con qué ajustes se generan y las notas que salieron.
// Las NOTAS mandan: al cargar un proyecto suenan las guardadas, aunque una versión futura del generador diera otras
// (así un proyecto viejo suena igual). Los ajustes solo se usan cuando el usuario cambia algo y hay que regenerar.
// Sin JUCE: lo usan el procesador, la GUI y los tests.
namespace tidechart
{

struct BassSettings
{
    deepwater::theory::BassOptions generator;

    bool operator== (const BassSettings&) const = default;
};

// La melodía sale de una BASE: la generada con 'generator', o una fija (capturada de lo que se tocó, o una variación que
// se fijó). Sobre la base, una variación opcional (amount > 0).
struct MelodySettings
{
    deepwater::theory::MelodyOptions generator;
    bool hasSource = false;            // true = la base es 'source' (capturada o fijada), no la generada
    deepwater::theory::Line source;
    double variation = 0.0;            // 0..1
    std::uint32_t variationSeed = 1;

    bool operator== (const MelodySettings&) const = default;
};

struct Lines
{
    BassSettings bassSettings;
    deepwater::theory::Line bass;
    MelodySettings melodySettings;
    deepwater::theory::Line melody;
};

// El bajo de estos ajustes con la progresión y la tonalidad de ahora.
[[nodiscard]] inline deepwater::theory::Line computeBass (const BassSettings& settings, const deepwater::theory::Progression& progression,
                                                          const deepwater::theory::Key& key)
{
    return deepwater::theory::generateBass (progression, key, settings.generator);
}

// La melodía: la base (generada o fija) y, si se pide, su variación. El rango de la variación abarca el del generador y
// el de la base (una melodía capturada puede salirse del rango elegido: no se la recorta).
[[nodiscard]] inline deepwater::theory::Line computeMelody (const MelodySettings& settings, const deepwater::theory::Progression& progression,
                                                            const deepwater::theory::Key& key)
{
    namespace th = deepwater::theory;
    const th::Line base = settings.hasSource ? settings.source : th::generateMelody (progression, key, settings.generator);
    if (settings.variation <= 0.0)
        return base;
    th::VariationOptions options;
    options.amount = settings.variation;
    options.seed = settings.variationSeed;
    options.low = std::min (settings.generator.low, settings.generator.high);
    options.high = std::max (settings.generator.low, settings.generator.high);
    for (const auto& n : base.notes)
    {
        options.low = std::min (options.low, n.note);
        options.high = std::max (options.high, n.note);
    }
    return th::varyMelody (base, progression, key, options);
}

// Capturar una melodía tocada (o escrita en el Piano Roll de Tidechart) con FL reproduciendo: cada nota con su posición
// en negras. Al terminar, se cuantiza a semicorcheas y se dobla sobre la vuelta de la línea (contada desde el compás 1,
// como la progresión): lo tocado en el compás 5 de una vuelta de 4 compases cae en el compás 1. Si dos notas caen en el
// mismo sitio con la misma altura, gana la última (la última pasada manda). Las notas tocadas con FL parado no cuentan:
// sin posición no se sabe dónde van.
class MelodyCapture
{
public:
    void clear()
    {
        notes.clear();
        open = {};
    }

    // Un note-on (velocidad > 0) o note-off en la posición 'ppq'; 'playing' = FL estaba reproduciendo.
    void noteOn (int channel, int note, int velocity, double ppq, bool playing)
    {
        auto& slot = open[static_cast<std::size_t> ((channel & 15) * 128 + (note & 127))];
        slot = playing ? Open { true, ppq, velocity } : Open {};
    }

    void noteOff (int channel, int note, double ppq)
    {
        auto& slot = open[static_cast<std::size_t> ((channel & 15) * 128 + (note & 127))];
        if (slot.active && ppq >= slot.start)
            notes.push_back ({ slot.start, ppq, note & 127, slot.velocity });
        slot = {};
    }

    [[nodiscard]] int count() const noexcept { return static_cast<int> (notes.size()); }

    [[nodiscard]] deepwater::theory::Line toLine (int length) const
    {
        namespace th = deepwater::theory;
        th::Line line;
        line.length = length;
        if (length <= 0)
            return line;
        std::vector<th::LineNote> folded;
        for (const auto& n : notes)
        {
            const auto start16 = static_cast<long long> (std::floor (n.start * th::sixteenthsPerQuarter + 0.5));
            const auto end16 = static_cast<long long> (std::floor (n.end * th::sixteenthsPerQuarter + 0.5));
            const int position = static_cast<int> (((start16 % length) + length) % length);
            const int len = static_cast<int> (std::clamp<long long> (end16 - start16, 1, length - position));
            std::erase_if (folded, [&] (const th::LineNote& m) { return m.start == position && m.note == n.note; });
            folded.push_back ({ position, len, n.note, std::clamp (n.velocity, 1, 127) });
        }
        std::sort (folded.begin(), folded.end(), [] (const th::LineNote& a, const th::LineNote& b) {
            return a.start != b.start ? a.start < b.start : a.note < b.note;
        });
        for (const auto& n : folded)
            line.notes.push_back (n);
        return line;
    }

private:
    struct Captured
    {
        double start = 0.0, end = 0.0;
        int note = 60, velocity = 100;
    };
    struct Open
    {
        bool active = false;
        double start = 0.0;
        int velocity = 0;
    };
    std::vector<Captured> notes;
    std::array<Open, 16 * 128> open {};
};

} // namespace tidechart
