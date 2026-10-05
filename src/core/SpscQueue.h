// Single-producer / single-consumer lock-free ring buffer. Wait-free on both sides,
// no allocation after construction: safe to use from the audio thread.
#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace tw {

template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(size_t capacityPow2 = 1024) { resize(capacityPow2); }

    // Not thread-safe: call before the producer and consumer start.
    void resize(size_t capacityPow2) {
        size_t cap = 1;
        while (cap < capacityPow2) cap <<= 1;
        buf_.assign(cap, T{});
        mask_ = cap - 1;
        head_.store(0);
        tail_.store(0);
    }

    bool push(const T& v) {
        const size_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) > mask_) return false;  // full
        buf_[h & mask_] = v;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    // Pushes as many items as fit, returns how many were written.
    size_t pushMany(const T* v, size_t n) {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t free = mask_ + 1 - (h - tail_.load(std::memory_order_acquire));
        if (n > free) n = free;
        for (size_t i = 0; i < n; ++i) buf_[(h + i) & mask_] = v[i];
        head_.store(h + n, std::memory_order_release);
        return n;
    }

    bool pop(T& out) {
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;  // empty
        out = buf_[t & mask_];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    size_t popMany(T* out, size_t n) {
        const size_t t = tail_.load(std::memory_order_relaxed);
        const size_t avail = head_.load(std::memory_order_acquire) - t;
        if (n > avail) n = avail;
        for (size_t i = 0; i < n; ++i) out[i] = buf_[(t + i) & mask_];
        tail_.store(t + n, std::memory_order_release);
        return n;
    }

private:
    std::vector<T> buf_;
    size_t mask_ = 0;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

} // namespace tw
