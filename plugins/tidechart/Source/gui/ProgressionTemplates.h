#pragma once

#include <array>

#include "deepwater/theory/Scale.h"

// Progresiones para empezar (Fase 4): el contexto de Tidechart (reggaetón, perreo oscuro, R&B) y el pop de referencia.
// Cada una con el modo en el que suena como se describe; al elegirla, Tidechart pone ese modo (la tónica no se toca).
// Los grados van en texto romano ASCII (lo lee libs/theory) y los textos en UTF-8 escapado, como el resto de la GUI.
// Los presets de verdad (con su explicación completa y el ritmo) llegan en la Fase 9.
namespace tidechart::gui
{
struct ProgressionTemplate
{
    const char* name;
    const char* numerals;
    deepwater::theory::Mode mode;
    const char* description;
};

inline constexpr std::array<ProgressionTemplate, 10> progressionTemplates { {
    { "Reggaet\xc3\xb3n  i\xe2\x80\x93\xe2\x99\xadVI\xe2\x80\x93\xe2\x99\xadIII\xe2\x80\x93\xe2\x99\xadVII", "i bVI bIII bVII",
      deepwater::theory::Mode::aeolian,
      "El esqueleto del reggaet\xc3\xb3n y del pop latino. Nunca cierra del todo: el \xe2\x99\xadVII vuelve a la i sin sensible, "
      "por eso aguanta minutos en loop." },
    { "Perreo oscuro  i\xe2\x80\x93\xe2\x99\xadII", "i bII", deepwater::theory::Mode::phrygian,
      "El semitono entre la i y el \xe2\x99\xadII es todo el frigio: una tensi\xc3\xb3n que no se resuelve. Dos acordes que se "
      "sostienen con el ritmo." },
    { "Dos acordes  i\xe2\x80\x93iv", "i iv", deepwater::theory::Mode::aeolian,
      "T\xc3\xb3nica y subdominante menor: lo m\xc3\xadnimo para que algo se mueva. Con dembow sostiene un tema entero." },
    { "D\xc3\xb3rico  i7\xe2\x80\x93IV7", "i7 IV7", deepwater::theory::Mode::dorian,
      "La 6.\xc2\xaa mayor del d\xc3\xb3rico hace mayor al IV: oscuro pero con luz, con groove (\xc2\xabOye como va\xc2\xbb)." },
    { "Andaluza  i\xe2\x80\x93\xe2\x99\xadVII\xe2\x80\x93\xe2\x99\xadVI\xe2\x80\x93V", "i bVII bVI V", deepwater::theory::Mode::aeolian,
      "El bajo baja por grados hasta un V mayor, prestado de la menor arm\xc3\xb3nica (la sensible): drama flamenco." },
    { "R&B  i9\xe2\x80\x93iv9\xe2\x80\x93\xe2\x99\xadIIImaj7\xe2\x80\x93\xe2\x99\xadVImaj7", "i9 iv9 bIIImaj7 bVImaj7",
      deepwater::theory::Mode::aeolian,
      "Novenas y maj7: el color del R&B y el neo-soul. Con conducci\xc3\xb3n de voces las extensiones quedan juntas y suaves." },
    { "ii\xe2\x80\x93V\xe2\x80\x93I  ii7\xe2\x80\x93V7\xe2\x80\x93Imaj7", "ii7 V7 Imaj7 Imaj7", deepwater::theory::Mode::ionian,
      "La cadencia del jazz: subdominante, dominante, t\xc3\xb3nica. La 7.\xc2\xaa de cada acorde baja un semitono a la 3.\xc2\xaa "
      "del siguiente." },
    { "Pop  I\xe2\x80\x93V\xe2\x80\x93vi\xe2\x80\x93IV", "I V vi IV", deepwater::theory::Mode::ionian,
      "El punto de partida del pop. Empieza en el vi (vi\xe2\x80\x93IV\xe2\x80\x93I\xe2\x80\x93V) y es la del reggaet\xc3\xb3n vista "
      "desde la mayor relativa." },
    { "Pr\xc3\xa9stamo  I\xe2\x80\x93\xe2\x99\xadVI\xe2\x80\x93\xe2\x99\xadVII\xe2\x80\x93I", "I bVI bVII I", deepwater::theory::Mode::ionian,
      "\xe2\x99\xadVI y \xe2\x99\xadVII vienen del menor paralelo: \xc3\xa9pico, de final de pel\xc3\xad" "cula." },
    { "Mediante  I\xe2\x80\x93III\xe2\x80\x93IV\xe2\x80\x93iv", "I III IV iv", deepwater::theory::Mode::ionian,
      "Un III mayor (mediante crom\xc3\xa1tica: fuera de la escala) y el iv prestado del menor: nost\xc3\xa1lgico y raro, de "
      "rock alternativo." },
} };
} // namespace tidechart::gui
