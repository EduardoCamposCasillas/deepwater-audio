#pragma once

#include "deepwater/dsp/HostClock.h"

// El reloj musical pasó a la librería de la suite (deepwater::dsp, con sus tests) cuando Tidechart también lo necesitó.
// Estos nombres lo dejan donde el código de Ebb lo busca.
namespace ebb::dsp
{
using deepwater::dsp::ClockSource;
using deepwater::dsp::HostClock;
using deepwater::dsp::MusicalPosition;
using deepwater::dsp::musicalPosition;
using deepwater::dsp::TimeSignature;
using deepwater::dsp::TransportInfo;
} // namespace ebb::dsp
