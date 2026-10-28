#pragma once

// --- Reservas de memoria en el hilo de audio -------------------------------------------------------------------------
//
// Regla del proyecto: processBlock no puede reservar ni liberar memoria. Este header REEMPLAZA operator new / delete
// (C++ lo permite una vez por ejecutable: incluirlo en UN solo .cpp de cada programa de tests) por versiones que,
// mientras 'counting' está activo, cuentan cada llamada. (La misma técnica que los PresetTests de Undertow, Riptide y
// Ebb.) Solo cuenta el hilo que llama a processBlock: el hilo de diseño de los FIR (Fase 10) y los temporizadores de
// JUCE reservan en su propio hilo, fuera del audio. Lo usan FathomPresetTests (Fase 15) y FathomAutomationTests (16).

#include <atomic>
#include <cstdlib>
#include <new>

namespace allocation
{
inline thread_local bool counting = false;
inline std::atomic<int> count { 0 };

inline void note (void* p) noexcept
{
    if (p != nullptr && counting)
        count.fetch_add (1, std::memory_order_relaxed);
}

inline void* allocate (std::size_t size)
{
    note (reinterpret_cast<void*> (1));
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

inline void* allocateAligned (std::size_t size, std::align_val_t alignment)
{
    note (reinterpret_cast<void*> (1));
    if (void* p = _aligned_malloc (size == 0 ? 1 : size, static_cast<std::size_t> (alignment)))
        return p;
    throw std::bad_alloc();
}

struct Scope
{
    Scope() { counting = true; }
    ~Scope() { counting = false; }
};
} // namespace allocation

void* operator new (std::size_t size) { return allocation::allocate (size); }
void* operator new[] (std::size_t size) { return allocation::allocate (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    allocation::note (reinterpret_cast<void*> (1));
    return std::malloc (size == 0 ? 1 : size);
}
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    allocation::note (reinterpret_cast<void*> (1));
    return std::malloc (size == 0 ? 1 : size);
}
void* operator new (std::size_t size, std::align_val_t alignment) { return allocation::allocateAligned (size, alignment); }
void* operator new[] (std::size_t size, std::align_val_t alignment) { return allocation::allocateAligned (size, alignment); }
void operator delete (void* p) noexcept { allocation::note (p); std::free (p); }
void operator delete[] (void* p) noexcept { allocation::note (p); std::free (p); }
void operator delete (void* p, std::size_t) noexcept { allocation::note (p); std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { allocation::note (p); std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept { allocation::note (p); _aligned_free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { allocation::note (p); _aligned_free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { allocation::note (p); _aligned_free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { allocation::note (p); _aligned_free (p); }
