#pragma once

#include <array>
#include <cstddef>

namespace deepwater::theory
{

// Lista de capacidad fija: lo que devuelve la teoría y se usa en el hilo de audio (las notas de un acorde, los tonos de
// una escala) no puede reservar memoria. Si se llena, push_back ignora lo que sobra (nunca pasa con las capacidades
// elegidas: un acorde tiene como mucho 7 tonos + el bajo).
template <typename T, std::size_t Capacity>
class StaticVector
{
public:
    constexpr void push_back (const T& value) noexcept
    {
        if (count < Capacity)
            items[count++] = value;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return count; }
    [[nodiscard]] constexpr bool empty() const noexcept { return count == 0; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
    constexpr void clear() noexcept { count = 0; }

    [[nodiscard]] constexpr T& operator[] (std::size_t i) noexcept { return items[i]; }
    [[nodiscard]] constexpr const T& operator[] (std::size_t i) const noexcept { return items[i]; }
    [[nodiscard]] constexpr T* begin() noexcept { return items.data(); }
    [[nodiscard]] constexpr T* end() noexcept { return items.data() + count; }
    [[nodiscard]] constexpr const T* begin() const noexcept { return items.data(); }
    [[nodiscard]] constexpr const T* end() const noexcept { return items.data() + count; }

    [[nodiscard]] constexpr bool operator== (const StaticVector& other) const noexcept
    {
        if (count != other.count)
            return false;
        for (std::size_t i = 0; i < count; ++i)
            if (! (items[i] == other.items[i]))
                return false;
        return true;
    }

private:
    std::array<T, Capacity> items {};
    std::size_t count = 0;
};

} // namespace deepwater::theory
