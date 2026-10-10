#pragma once

#include <algorithm>
#include <cmath>

namespace deepwater::dsp
{

// El oído percibe el volumen de forma logarítmica: duplicar la amplitud se oye como "un poco más fuerte", no como el
// doble. Por eso los niveles se piensan en decibelios: dB = 20·log10(ganancia). +6 dB ≈ ×2, −6 dB ≈ ×0.5,
// 0 dB = ×1 exacto (pow(10, 0) es exactamente 1: una perilla en 0 dB deja la señal intacta bit a bit).

// Nivel que se usa como "silencio" al pasar a dB (log10(0) = −infinito no sirve para dibujar ni para comparar).
inline constexpr float silenceDb = -100.0f;

[[nodiscard]] inline float dbToGain (float db) noexcept
{
    return std::pow (10.0f, db * 0.05f);
}

[[nodiscard]] inline float gainToDb (float gain, float floorDb = silenceDb) noexcept
{
    return gain > 0.0f ? std::max (floorDb, 20.0f * std::log10 (gain)) : floorDb;
}

} // namespace deepwater::dsp
