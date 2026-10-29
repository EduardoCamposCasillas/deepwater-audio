#pragma once

#include <algorithm>
#include <vector>

#include "deepwater/theory/Melody.h"
#include "deepwater/theory/Progression.h"
#include "Lines.h"
#include "midi/ChordTrigger.h"

// Fase 7: lo que se arrastra al Piano Roll. Cada capa (acordes, bajo, melodía) es un clip con las MISMAS notas que
// suenan en vivo en el modo progresión, una vuelta, desde el compás 1. Sin JUCE: el archivo .mid lo escribe
// MidiFileExport.h; aquí solo se decide qué notas van.
namespace tidechart
{

enum class ExportLayer
{
    chords,
    bass,
    melody
};

struct ExportClip
{
    std::vector<deepwater::theory::NoteEvent> notes; // en negras desde el principio, ordenadas por inicio y nota
    double lengthBeats = 0.0;                        // la vuelta entera (aunque la última nota termine antes)

    [[nodiscard]] bool empty() const noexcept { return notes.empty(); }
};

// La nota de la tónica en el registro de la paleta: cerca de C5 (60), como cuando se toca con una tecla de esa octava.
inline constexpr int exportOctaveC = 60;

// El clip de una capa con la progresión, los ajustes (tonalidad, voicing, ritmo, bajo de los acordes) y las líneas de
// ahora. Todas las capas duran lo mismo que las líneas (lineLength: la progresión repetida hasta tener al menos una
// frase de 4 compases), para que en la Playlist de FL los tres clips se repitan juntos. Una línea fija que se hizo con
// otra progresión conserva su largo. Reserva memoria: fuera del hilo de audio.
[[nodiscard]] inline ExportClip exportClip (ExportLayer layer, const deepwater::theory::Progression& progression,
                                            const midi::ChordSettings& settings, const Lines& lines)
{
    namespace th = deepwater::theory;
    ExportClip clip;
    if (layer == ExportLayer::chords)
    {
        const int loop = progression.totalBeats() * th::sixteenthsPerQuarter;
        if (loop <= 0)
            return clip;
        const int length = th::lineLength (progression);
        th::RenderOptions options;
        options.style = settings.voicing;
        options.tonicNote = th::tonicNoteNear (settings.key, exportOctaveC);
        options.bass = settings.bass;
        options.rhythm = settings.rhythm;
        options.velocity = 100; // la de la paleta; en vivo, la de la tecla
        options.loops = length / loop;
        clip.notes = th::renderProgression (progression, settings.key, options);
        clip.lengthBeats = static_cast<double> (length) / th::sixteenthsPerQuarter;
    }
    else
    {
        // En vivo, la velocidad de cada nota es la suya por la de la tecla (accentedVelocity): con 100, la suya.
        const auto& line = layer == ExportLayer::bass ? lines.bass : lines.melody;
        clip.notes = th::renderLine (line, 1);
        clip.lengthBeats = clip.notes.empty() ? 0.0 : static_cast<double> (line.length) / th::sixteenthsPerQuarter;
    }
    std::stable_sort (clip.notes.begin(), clip.notes.end(), [] (const th::NoteEvent& a, const th::NoteEvent& b) {
        return a.start != b.start ? a.start < b.start : a.note < b.note;
    });
    return clip;
}

} // namespace tidechart
