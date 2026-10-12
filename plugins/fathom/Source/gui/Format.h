#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <juce_core/juce_core.h>

// Textos de valores que comparten el editor y el visor.
namespace fathom::gui::format
{

// "80 Hz", "2.5 kHz" o "20 kHz". Bajo 100 Hz con un decimal: en graves 1 Hz es casi un cuarto de tono.
inline juce::String hz (float value)
{
    if (value < 99.95f)
        return juce::String (value, 1) + " Hz";
    if (value < 999.5f)
        return juce::String (juce::roundToInt (value)) + " Hz";
    return value < 9950.0f ? juce::String (value / 1000.0f, 2) + " kHz" : juce::String (value / 1000.0f, 1) + " kHz";
}

// "+3.5 dB", "-12.0 dB" o "0.0 dB".
inline juce::String db (float value)
{
    const float rounded = std::round (value * 10.0f) / 10.0f;
    return (rounded > 0.0f ? "+" : "") + juce::String (rounded == 0.0f ? 0.0f : rounded, 1) + " dB";
}

// El nombre de cada forma en la GUI, en el orden de fathom::dsp::Shape.
inline constexpr std::array<const char*, 10> shapeNames { "Campana",      "Shelf graves", "Shelf agudos", "Tilt shelf", "Flat tilt",
                                                          "Corte graves", "Corte agudos", "Notch",        "Pasa banda", "All-pass" };

// Ancho en octavas de un Q (definición del Cookbook): el de la campana a la mitad de su ganancia en dB, y también el
// del notch y el pasa banda a −3 dB.
inline double octavesForQ (double q)
{
    return 2.0 / std::log (2.0) * std::asinh (1.0 / (2.0 * std::max (0.001, q)));
}

// La nota más cercana a una frecuencia, con los nombres del Piano Roll de FL Studio (C5 = nota MIDI 60, A5 = 440 Hz:
// una octava más arriba que la notación científica), y cuántos cents se aleja de ella: "A5", "C#3 +23".
inline juce::String noteName (float frequency)
{
    static constexpr std::array<const char*, 12> names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (frequency < 8.0f)
        return {};
    const float exact = 69.0f + 12.0f * std::log2 (frequency / 440.0f);
    const int note = juce::roundToInt (exact);
    const int cents = juce::roundToInt ((exact - static_cast<float> (note)) * 100.0f);
    auto text = juce::String (names[static_cast<size_t> (note % 12)]) + juce::String (note / 12);
    if (cents != 0)
        text << (cents > 0 ? " +" : " ") << cents;
    return text;
}

} // namespace fathom::gui::format
