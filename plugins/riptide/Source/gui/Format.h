#pragma once

#include <array>
#include <cmath>

#include <juce_core/juce_core.h>

// Textos de valores que comparten el editor y los visores.
namespace riptide::gui::format
{

// "80 Hz", "2.5 kHz" o "20 kHz".
inline juce::String hz (float value)
{
    if (value < 999.5f)
        return juce::String (juce::roundToInt (value)) + " Hz";
    return value < 9950.0f ? juce::String (value / 1000.0f, 1) + " kHz"
                           : juce::String (juce::roundToInt (value / 1000.0f)) + " kHz";
}

// "+3.5 dB", "-12.0 dB" o "0.0 dB".
inline juce::String db (float value)
{
    const float rounded = std::round (value * 10.0f) / 10.0f;
    return (rounded > 0.0f ? "+" : "") + juce::String (rounded == 0.0f ? 0.0f : rounded, 1) + " dB";
}

// La nota más cercana a una frecuencia, con los nombres del Piano Roll de FL Studio (C5 = nota MIDI 60, A5 = 440 Hz:
// una octava más arriba que la notación científica).
inline juce::String noteName (float frequency)
{
    static constexpr std::array<const char*, 12> names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (frequency < 8.0f)
        return {};
    const int note = juce::roundToInt (69.0f + 12.0f * std::log2 (frequency / 440.0f));
    return juce::String (names[static_cast<size_t> (note % 12)]) + juce::String (note / 12);
}

} // namespace riptide::gui::format
