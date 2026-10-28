#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace deepwater::theory
{

// --- Patrones rítmicos de acordes (Fase 4 de Tidechart) ---------------------------------------------------------------
// Un patrón es una lista de golpes sobre una cuadrícula de semicorcheas (16 por compás de 4/4): dónde empieza cada uno,
// cuánto dura y con qué acento (porcentaje de la velocidad). El acorde suena mientras dura un golpe y calla entre
// golpes. El patrón se repite cada 'steps' semicorcheas contando desde el principio de la canción, así queda alineado
// con la batería.
//
// El dembow es 3+3+2: en cada medio compás, golpes en las semicorcheas 0, 3 y 6 (el "boom-ch-boom-chick", el tresillo
// de la habanera). Sus variantes (largo, a contratiempo, desplazado, con huecos) son patrones de primera clase.
//
// El orden de la tabla es parte del formato (el parámetro de ritmo guarda el índice): lo nuevo, al final.
struct RhythmHit
{
    int start = 0;    // semicorchea donde empieza, desde el principio del patrón
    int length = 1;   // en semicorcheas
    int accent = 100; // % de la velocidad de la tecla
};

struct RhythmPattern
{
    std::string_view name; // en inglés, el identificador estable
    int steps;             // largo del patrón en semicorcheas; 0 = sostenido (sin patrón: el acorde dura todo su paso)
    std::array<RhythmHit, 8> hits;
    int hitCount;
};

inline constexpr int sixteenthsPerQuarter = 4;

inline constexpr std::array<RhythmPattern, 9> rhythmPatterns { {
    // El acorde dura todo su paso de la progresión (o mientras la tecla esté abajo).
    { "sustain", 0, {}, 0 },
    // Negras un poco despegadas: el piano de balada, el pulso más simple.
    { "quarters", 16, { { { 0, 3, 100 }, { 4, 3, 80 }, { 8, 3, 90 }, { 12, 3, 80 } } }, 4 },
    // Corcheas pegadas, con el tiempo fuerte acentuado: empuje constante (pop, synthwave).
    { "eighths", 16, { { { 0, 2, 100 }, { 2, 2, 70 }, { 4, 2, 85 }, { 6, 2, 70 }, { 8, 2, 95 }, { 10, 2, 70 }, { 12, 2, 85 }, { 14, 2, 70 } } }, 8 },
    // Corcheas a contratiempo (el "y" de cada tiempo): el skank del reggae y del house.
    { "offbeat", 16, { { { 2, 2, 95 }, { 6, 2, 85 }, { 10, 2, 95 }, { 14, 2, 85 } } }, 4 },
    // Dembow: 3+3+2 en cada medio compás (0 3 6 | 8 11 14), los golpes en 0 y 8 acentuados.
    { "dembow", 16, { { { 0, 2, 100 }, { 3, 2, 85 }, { 6, 2, 90 }, { 8, 2, 100 }, { 11, 2, 85 }, { 14, 2, 90 } } }, 6 },
    // Dembow largo: el mismo 3+3+2 pero en corcheas, una vez por compás (0 6 12). Pads y acordes que respiran.
    { "dembow long", 16, { { { 0, 5, 100 }, { 6, 5, 90 }, { 12, 4, 95 } } }, 3 },
    // Dembow a contratiempo: solo el "ch" (3 6 | 11 14), los acordes contestan al bombo.
    { "dembow offbeat", 16, { { { 3, 2, 95 }, { 6, 2, 85 }, { 11, 2, 95 }, { 14, 2, 85 } } }, 4 },
    // Dembow desplazado una corchea (2 5 8 | 10 13 0): mismo dibujo, otro apoyo; suena a que tropieza.
    { "dembow shifted", 16, { { { 0, 2, 90 }, { 2, 2, 100 }, { 5, 2, 85 }, { 8, 2, 90 }, { 10, 2, 100 }, { 13, 2, 85 } } }, 6 },
    // Dembow con huecos: el 3+3 de la primera mitad y una anticipación al final (0 3 6 … 14). Deja aire a la voz.
    { "dembow gaps", 16, { { { 0, 2, 100 }, { 3, 2, 85 }, { 6, 2, 90 }, { 14, 2, 95 } } }, 4 },
} };

inline constexpr int rhythmPatternCount = static_cast<int> (rhythmPatterns.size());

[[nodiscard]] constexpr const RhythmPattern& rhythmPattern (int index) noexcept
{
    return rhythmPatterns[static_cast<std::size_t> (index >= 0 && index < rhythmPatternCount ? index : 0)];
}

// El golpe que contiene la semicorchea 'step' del patrón (0..steps−1), o nullptr si cae en un silencio.
[[nodiscard]] constexpr const RhythmHit* hitAt (const RhythmPattern& pattern, int step) noexcept
{
    for (int i = 0; i < pattern.hitCount; ++i)
    {
        const auto& hit = pattern.hits[static_cast<std::size_t> (i)];
        if (step >= hit.start && step < hit.start + hit.length)
            return &hit;
    }
    return nullptr;
}

// La semicorchea del patrón en la que cae la semicorchea absoluta 'sixteenth' (desde el principio de la canción;
// también con posiciones negativas, como el pre-roll de algunos hosts).
[[nodiscard]] constexpr int patternStep (const RhythmPattern& pattern, long long sixteenth) noexcept
{
    if (pattern.steps <= 0)
        return 0;
    const long long step = sixteenth % pattern.steps;
    return static_cast<int> (step < 0 ? step + pattern.steps : step);
}

} // namespace deepwater::theory
