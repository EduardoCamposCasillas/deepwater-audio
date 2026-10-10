#pragma once

#include <array>

// --- SIMD: una instrucción, cuatro números ------------------------------------------------------------
//
// Los procesadores x64 tienen registros de 128 bits (SSE2) que guardan 4 floats a la vez, e instrucciones que
// operan sobre los 4 de golpe: una suma SIMD hace 4 sumas en el tiempo de una. Todo procesador x64 tiene SSE2,
// así que no hace falta comprobar nada al arrancar el plugin (AVX, con 8 floats, no está garantizado: un
// plugin compilado para AVX se cuelga en un procesador sin AVX).
//
// Float4 envuelve ese registro con operadores normales (+, −, ×) para que el código DSP se lea casi como el
// escalar. Si algún día se compila para otra arquitectura (ARM), se usa la versión escalar de abajo: hace lo
// mismo con un array de 4 floats (el compilador la vectoriza a su manera).
#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    #define DEEPWATER_SSE2 1
    #include <emmintrin.h>
#else
    #define DEEPWATER_SSE2 0
#endif

namespace deepwater::dsp::simd
{

#if DEEPWATER_SSE2

struct Float4
{
    __m128 v;

    Float4() noexcept : v (_mm_setzero_ps()) {}
    Float4 (float x) noexcept : v (_mm_set1_ps (x)) {} // los 4 iguales
    explicit Float4 (__m128 x) noexcept : v (x) {}

    // Lecturas y escrituras sin requisito de alineación (en procesadores modernos cuestan lo mismo).
    [[nodiscard]] static Float4 load (const float* p) noexcept { return Float4 (_mm_loadu_ps (p)); }
    void store (float* p) const noexcept { _mm_storeu_ps (p, v); }

    friend Float4 operator+ (Float4 a, Float4 b) noexcept { return Float4 (_mm_add_ps (a.v, b.v)); }
    friend Float4 operator- (Float4 a, Float4 b) noexcept { return Float4 (_mm_sub_ps (a.v, b.v)); }
    friend Float4 operator* (Float4 a, Float4 b) noexcept { return Float4 (_mm_mul_ps (a.v, b.v)); }
    Float4& operator+= (Float4 b) noexcept { return *this = *this + b; }

    // Suma de los 4 (para juntar al final lo que se acumuló en paralelo).
    [[nodiscard]] float sum() const noexcept
    {
        const __m128 pairs = _mm_add_ps (v, _mm_movehl_ps (v, v));                 // (0 + 2, 1 + 3)
        return _mm_cvtss_f32 (_mm_add_ss (pairs, _mm_shuffle_ps (pairs, pairs, 1))); // (0 + 2) + (1 + 3)
    }
};

// Transpone una matriz de 4 × 4: las filas pasan a ser columnas. Se cargan 4 muestras SEGUIDAS de la tabla por
// cada copia (una fila por copia) y se necesitan "la muestra −1 de las 4 copias", "la 0 de las 4"... (columnas).
inline void transpose (Float4& r0, Float4& r1, Float4& r2, Float4& r3) noexcept
{
    _MM_TRANSPOSE4_PS (r0.v, r1.v, r2.v, r3.v);
}

// Posición de lectura de 4 copias en una tabla de 'size' muestras: índice entero (truncado) y fracción.
// La fase va en double (p. ej. el WavetableOscillator de Undertow) y SSE2 solo hace 2 doubles por instrucción, así que se parte en
// dos mitades. Mismo redondeo que la versión escalar: static_cast<int> trunca y static_cast<float> redondea.
inline Float4 readPositions (const double* phase, double size, int* index) noexcept
{
    const __m128d scale = _mm_set1_pd (size);
    const __m128d low = _mm_mul_pd (_mm_loadu_pd (phase), scale);
    const __m128d high = _mm_mul_pd (_mm_loadu_pd (phase + 2), scale);
    const __m128i lowIndex = _mm_cvttpd_epi32 (low);
    const __m128i highIndex = _mm_cvttpd_epi32 (high);
    _mm_storeu_si128 (reinterpret_cast<__m128i*> (index), _mm_unpacklo_epi64 (lowIndex, highIndex));
    const __m128 lowFraction = _mm_cvtpd_ps (_mm_sub_pd (low, _mm_cvtepi32_pd (lowIndex)));
    const __m128 highFraction = _mm_cvtpd_ps (_mm_sub_pd (high, _mm_cvtepi32_pd (highIndex)));
    return Float4 (_mm_movelh_ps (lowFraction, highFraction));
}

// Avanza 4 fases (phase += increment) y las devuelve a [0, 1) restando 1 a las que pasaron de 1.
// La comparación da una "máscara" (todo unos o todo ceros por copia): el AND con 1.0 deja 1.0 o 0.0, sin saltos
// condicionales, que en SIMD no existen (las 4 copias siguen siempre el mismo camino).
inline void advancePhases (double* phase, const double* increment) noexcept
{
    const __m128d one = _mm_set1_pd (1.0);
    for (int half = 0; half < 4; half += 2)
    {
        const __m128d next = _mm_add_pd (_mm_loadu_pd (phase + half), _mm_loadu_pd (increment + half));
        const __m128d wrap = _mm_and_pd (_mm_cmpge_pd (next, one), one);
        _mm_storeu_pd (phase + half, _mm_sub_pd (next, wrap));
    }
}

#else // versión escalar (misma interfaz)

struct Float4
{
    std::array<float, 4> v {};

    Float4() noexcept = default;
    Float4 (float x) noexcept { v.fill (x); }

    [[nodiscard]] static Float4 load (const float* p) noexcept
    {
        Float4 r;
        for (size_t i = 0; i < 4; ++i)
            r.v[i] = p[i];
        return r;
    }
    void store (float* p) const noexcept
    {
        for (size_t i = 0; i < 4; ++i)
            p[i] = v[i];
    }

    template <typename Op>
    [[nodiscard]] static Float4 apply (Float4 a, Float4 b, Op op) noexcept
    {
        for (size_t i = 0; i < 4; ++i)
            a.v[i] = op (a.v[i], b.v[i]);
        return a;
    }
    friend Float4 operator+ (Float4 a, Float4 b) noexcept { return apply (a, b, [] (float x, float y) { return x + y; }); }
    friend Float4 operator- (Float4 a, Float4 b) noexcept { return apply (a, b, [] (float x, float y) { return x - y; }); }
    friend Float4 operator* (Float4 a, Float4 b) noexcept { return apply (a, b, [] (float x, float y) { return x * y; }); }
    Float4& operator+= (Float4 b) noexcept { return *this = *this + b; }

    [[nodiscard]] float sum() const noexcept { return (v[0] + v[2]) + (v[1] + v[3]); }
};

inline void transpose (Float4& r0, Float4& r1, Float4& r2, Float4& r3) noexcept
{
    const std::array<Float4, 4> rows { r0, r1, r2, r3 };
    std::array<Float4*, 4> columns { &r0, &r1, &r2, &r3 };
    for (size_t c = 0; c < 4; ++c)
        for (size_t r = 0; r < 4; ++r)
            columns[c]->v[r] = rows[r].v[c];
}

inline Float4 readPositions (const double* phase, double size, int* index) noexcept
{
    Float4 fraction;
    for (size_t i = 0; i < 4; ++i)
    {
        const double position = phase[i] * size;
        index[i] = static_cast<int> (position);
        fraction.v[i] = static_cast<float> (position - index[i]);
    }
    return fraction;
}

inline void advancePhases (double* phase, const double* increment) noexcept
{
    for (size_t i = 0; i < 4; ++i)
    {
        phase[i] += increment[i];
        if (phase[i] >= 1.0)
            phase[i] -= 1.0;
    }
}

#endif

} // namespace deepwater::dsp::simd
