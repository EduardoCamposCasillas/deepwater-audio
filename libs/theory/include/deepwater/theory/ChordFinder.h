#pragma once

#include <span>
#include <vector>

#include "deepwater/theory/Chord.h"
#include "deepwater/theory/Scale.h"

namespace deepwater::theory
{

// Reconocer un acorde a partir de notas sueltas (lo que suena, lo que hay en un compás del Piano Roll).
//
// Se prueba cada nota presente como fundamental y cada tipo del catálogo, y se comparan las clases de altura (sin
// octavas). Vale la coincidencia exacta y, con 3 notas o más, la que solo omite la 5.ª justa (la nota que más se
// omite al tocar: C E B♭ sigue siendo C7). Orden de las lecturas, de mejor a peor:
//   1. exacta antes que sin 5.ª;
//   2. la fundamental en el bajo antes que un acorde invertido (C E G A con Do abajo es C6; con La abajo, Am7);
//   3. el tipo más común (el orden del catálogo).
// Sin tonalidad, la fundamental se escribe con la forma que deje menos alteraciones en el acorde (E♭ G B♭ antes que
// D♯ F𝄪 A♯); con tonalidad, como en ella. Reserva memoria: fuera del hilo de audio.
struct ChordReading
{
    Chord chord;
    bool omitsFifth = false;
};

[[nodiscard]] std::vector<ChordReading> findChords (std::span<const int> midiNotes);
[[nodiscard]] std::vector<ChordReading> findChords (std::span<const int> midiNotes, const Key& key);

} // namespace deepwater::theory
