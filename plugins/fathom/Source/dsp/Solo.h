#pragma once

#include <optional>

#include "dsp/Band.h"

namespace fathom::dsp
{

// Solo de banda: en vez de la mezcla se escucha solo la ZONA que toca una banda, para saber qué estás por cortar o
// subir antes de hacerlo (¿esa resonancia es la que suena fea?, ¿el corte de graves se lleva algo del bombo?).
//
// La zona es un filtro con la frecuencia (y el ancho) de la banda, aplicado a la señal ORIGINAL y con 0 dB en el
// centro: se oye lo que llega, no lo que ya hizo el EQ. Según la forma:
//   - campana, notch, pasa banda, all-pass: un pasa banda en su frecuencia y con su Q (Q alto = una franja angosta);
//   - shelf de graves y corte de graves: un pasa bajos en su frecuencia (lo que el shelf mueve o el corte quita);
//   - shelf de agudos y corte de agudos: un pasa altos;
//   - tilt shelf y flat tilt: no marcan una zona (inclinan todo), así que no tienen solo.
// Los cortes usan su misma pendiente (se oye justo lo que quitan); los shelves, 12 dB/oct sin rebote (Q 0.71).
// La zona está en el mismo canal que la banda: la de una banda izquierda suena solo a la izquierda, la de una mid en el
// centro y la de una side en el side (Band::zoneStereo).
inline std::optional<BandSettings> soloZoneFor (const BandSettings& band) noexcept
{
    BandSettings zone;
    zone.enabled = true;
    zone.analog = band.analog;
    zone.placement = band.placement; // Fase 8: la zona de una banda side es la del side
    zone.frequency = band.frequency;
    zone.q = 0.71f;
    zone.slopeIndex = 1; // 12 dB/oct
    switch (band.shape)
    {
        case Shape::bell:
        case Shape::notch:
        case Shape::bandPass:
        case Shape::allPass:
            zone.shape = Shape::bandPass;
            zone.q = band.q;
            return zone;
        case Shape::lowShelf: zone.shape = Shape::highCut; return zone;
        case Shape::highShelf: zone.shape = Shape::lowCut; return zone;
        case Shape::lowCut:
            zone.shape = Shape::highCut;
            zone.slopeIndex = band.slopeIndex;
            return zone;
        case Shape::highCut:
            zone.shape = Shape::lowCut;
            zone.slopeIndex = band.slopeIndex;
            return zone;
        case Shape::tiltShelf:
        case Shape::flatTilt: break;
    }
    return std::nullopt;
}

constexpr bool canSolo (Shape shape) noexcept { return shape != Shape::tiltShelf && shape != Shape::flatTilt; }

} // namespace fathom::dsp
