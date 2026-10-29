#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "MidiExport.h"

// Fase 7: un clip (MidiExport.h) como archivo MIDI estándar (.mid), el que se arrastra al Piano Roll de FL.
// - Formato 1 con una sola pista (FL importa cada pista como un canal; una capa = una pista = un instrumento).
// - 960 ticks por negra: una semicorchea = 240 ticks exactos (los ritmos de Tidechart caen en semicorcheas).
// - Al principio, el nombre de la pista, el tempo de FL y el compás 4/4 (FL ignora el tempo al soltar en el Piano Roll,
//   pero otros programas lo usan). Al final, el fin de pista en el largo de la vuelta: así el clip dura los compases
//   enteros aunque la última nota termine antes (un dembow que acaba en silencio).
// - Note-on y note-off de verdad (no note-on con velocidad 0), canal 1. Si un note-off y un note-on caen en el mismo
//   tick, el note-off va primero: una nota repetida no se corta a sí misma.
namespace tidechart
{

inline constexpr int midiFileTicksPerQuarter = 960;

[[nodiscard]] inline juce::MidiFile toMidiFile (const ExportClip& clip, const juce::String& trackName, double bpm)
{
    const auto ticks = [] (double beats) { return static_cast<int> (std::lround (beats * midiFileTicksPerQuarter)); };

    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::textMetaEvent (3, trackName), 0.0); // 3 = nombre de la pista
    track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / juce::jlimit (20.0, 999.0, bpm))), 0.0);
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

    struct Event
    {
        int tick = 0;
        bool on = false;
        int note = 0, velocity = 0;
    };
    std::vector<Event> events;
    int lastTick = 0;
    for (const auto& n : clip.notes)
    {
        const int start = ticks (n.start);
        const int end = std::max (start + 1, ticks (n.start + n.length));
        events.push_back ({ start, true, n.note, n.velocity });
        events.push_back ({ end, false, n.note, 0 });
        lastTick = std::max (lastTick, end);
    }
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        if (a.on != b.on)
            return ! a.on; // el note-off primero
        return a.note < b.note;
    });
    for (const auto& e : events)
        track.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, static_cast<juce::uint8> (juce::jlimit (1, 127, e.velocity)))
                             : juce::MidiMessage::noteOff (1, e.note),
                        static_cast<double> (e.tick));
    track.addEvent (juce::MidiMessage::endOfTrack(), static_cast<double> (std::max (lastTick, ticks (clip.lengthBeats))));

    juce::MidiFile file;
    file.setTicksPerQuarterNote (midiFileTicksPerQuarter);
    file.addTrack (track);
    return file;
}

// El nombre de cada capa en el archivo y en la pista (sin tildes: algunos programas no las leen bien en un .mid).
[[nodiscard]] inline juce::String exportLayerName (ExportLayer layer)
{
    switch (layer)
    {
        case ExportLayer::chords: return "Acordes";
        case ExportLayer::bass: return "Bajo";
        case ExportLayer::melody: return "Melodia";
    }
    return {};
}

// La carpeta donde se escriben los .mid que se arrastran: una propia dentro de la carpeta temporal del sistema. Cada
// capa usa siempre el mismo archivo (se sobrescribe), así no se acumulan; FL copia las notas al soltar, no lo enlaza.
[[nodiscard]] inline juce::File exportFolder()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Tidechart");
}

// Escribe el clip en 'file' (lo reemplaza entero). false si no se pudo.
inline bool writeMidiFile (const ExportClip& clip, const juce::String& trackName, double bpm, const juce::File& file)
{
    if (! file.getParentDirectory().createDirectory())
        return false;
    juce::MemoryOutputStream data;
    if (! toMidiFile (clip, trackName, bpm).writeTo (data, 1))
        return false;
    return file.replaceWithData (data.getData(), data.getDataSize());
}

} // namespace tidechart
