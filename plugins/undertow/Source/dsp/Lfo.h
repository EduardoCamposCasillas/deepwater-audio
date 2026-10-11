#pragma once

#include "deepwater/dsp/Lfo.h"

// El LFO se movió a la librería compartida (deepwater/dsp/Lfo.h) en la Fase 7 de Riptide. Aquí solo se mantienen los
// nombres que usa Undertow.
namespace undertow::dsp
{
using deepwater::dsp::Lfo;
using deepwater::dsp::LfoPhase;
using deepwater::dsp::LfoShape;
} // namespace undertow::dsp
