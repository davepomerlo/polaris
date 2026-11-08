#pragma once
// Lock-free single-producer / single-consumer ring buffer.
// The audio thread pushes samples; the analysis thread pops them. If full, new data is dropped.
#include <atomic>
#include <vector>
#include <cstddef>
#include <algorithm>

namespace polaris
{
template <typename T>
class SpscFifo
{
public:
    void prepare (size_t minCapacity)
    {
        size_t cap = 1;
        while (cap < minCapacity) cap <<= 1;
        buffer.assign (cap, T {});
        mask = cap - 1;
        readPos.store (0);
        writePos.store (0);
    }

    size_t push (const T* data, size_t n) noexcept
    {
        const size_t w = writePos.load (std::memory_order_relaxed);
        const size_t r = readPos.load (std::memory_order_acquire);
        const size_t space = buffer.size() - (w - r);
        n = std::min (n, space);
        for (size_t i = 0; i < n; ++i) buffer[(w + i) & mask] = data[i];
        writePos.store (w + n, std::memory_order_release);
        return n;
    }

    /** All-or-nothing push: keeps interleaved frames intact. */
    bool pushIfRoom (const T* data, size_t n) noexcept
    {
        const size_t w = writePos.load (std::memory_order_relaxed);
        const size_t r = readPos.load (std::memory_order_acquire);
        if (buffer.size() - (w - r) < n) return false;
        for (size_t i = 0; i < n; ++i) buffer[(w + i) & mask] = data[i];
        writePos.store (w + n, std::memory_order_release);
        return true;
    }

    size_t pop (T* dest, size_t n) noexcept
    {
        const size_t r = readPos.load (std::memory_order_relaxed);
        const size_t w = writePos.load (std::memory_order_acquire);
        n = std::min (n, w - r);
        for (size_t i = 0; i < n; ++i) dest[i] = buffer[(r + i) & mask];
        readPos.store (r + n, std::memory_order_release);
        return n;
    }

    size_t available() const noexcept { return writePos.load (std::memory_order_acquire) - readPos.load (std::memory_order_acquire); }

private:
    std::vector<T> buffer;
    size_t mask = 0;
    std::atomic<size_t> readPos { 0 }, writePos { 0 };
};
} // namespace polaris
