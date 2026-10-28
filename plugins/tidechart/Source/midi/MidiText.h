#pragma once

#include <array>
#include <string>

#include "deepwater/theory/Text.h"
#include "midi/NoteTracker.h"

namespace tidechart::midi
{

// Nombre de una nota como lo escribe FL Studio: C5 = 60 (el Do central), A5 = 69 (La 440 Hz), con sostenidos como su
// Piano Roll. Los nombres según la tonalidad (Si♭ en Fa mayor, no La♯) los da libs/theory (midiNoteName con una Key).
[[nodiscard]] inline std::string flNoteName (int note)
{
    return deepwater::theory::midiNoteName (note & 0x7f);
}

// Un mensaje en palabras, para el monitor de la GUI (el canal va aparte).
[[nodiscard]] inline std::string describe (ShortMessage message, int size)
{
    if (message.status == 0)
        return "SysEx (" + std::to_string (size) + " bytes)";

    const auto note = [&] { return flNoteName (message.data1) + " (" + std::to_string (message.data1) + ")"; };
    if (message.isNoteOn())
        return "Note on   " + note() + "  vel " + std::to_string (message.data2);
    if (message.isNoteOff())
        return "Note off  " + note();

    switch (message.kind())
    {
        case 0xa0: return "Aftertouch  " + note() + "  " + std::to_string (message.data2);
        case 0xb0:
        {
            std::string name;
            switch (message.data1)
            {
                case 1: name = " (modulacion)"; break;
                case 7: name = " (volumen)"; break;
                case 64: name = " (sustain)"; break;
                case 120: name = " (All Sound Off)"; break;
                case 123: name = " (All Notes Off)"; break;
                default: break;
            }
            return "CC " + std::to_string (message.data1) + name + " = " + std::to_string (message.data2);
        }
        case 0xc0: return "Programa " + std::to_string (message.data1);
        case 0xd0: return "Aftertouch de canal " + std::to_string (message.data1);
        case 0xe0: return "Pitch bend " + std::to_string (((message.data2 << 7) | message.data1) - 8192);
        default: return "Sistema (" + std::to_string (message.status) + ")";
    }
}

} // namespace tidechart::midi
