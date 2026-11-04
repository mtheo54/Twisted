#pragma once
// File à un producteur et un consommateur, sans verrou ni allocation.
#include <atomic>
#include <memory>
#include <cstddef>

namespace raw2 {

template <typename T, size_t N>
class SpscQueue {
    static_assert((N & (N - 1)) == 0, "N doit être une puissance de 2");

public:
    bool push(const T& v) {
        const size_t w = w_.load(std::memory_order_relaxed);
        if (w - r_.load(std::memory_order_acquire) >= N) return false;
        buf_[w & (N - 1)] = v;
        w_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& v) {
        const size_t r = r_.load(std::memory_order_relaxed);
        if (r == w_.load(std::memory_order_acquire)) return false;
        v = buf_[r & (N - 1)];
        r_.store(r + 1, std::memory_order_release);
        return true;
    }

private:
    T buf_[N];
    std::atomic<size_t> w_{0}, r_{0};
};

// Anneau audio -> interface : l'audio écrit en continu, l'interface lit les N derniers échantillons.
// Les tampons sont sur le tas (512 Ko) : un Model peut rester sur la pile (1 Mo sous MSVC).
class ScopeRing {
public:
    static constexpr size_t kSize = 1 << 16;
    ScopeRing() : l_(new float[kSize]()), r_(new float[kSize]()) {}
    ScopeRing(const ScopeRing&) = delete;
    ScopeRing& operator=(const ScopeRing&) = delete;
    void write(const float* L, const float* R, int n) {
        size_t w = pos_.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i, ++w) {
            l_[w & (kSize - 1)] = L[i];
            r_[w & (kSize - 1)] = R ? R[i] : L[i];
        }
        pos_.store(w, std::memory_order_release);
    }
    // Copie les n derniers échantillons (n <= kSize / 2). Retourne la position d'écriture.
    size_t read(float* L, float* R, size_t n) const {
        const size_t w = pos_.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i) {
            const size_t k = (w - n + i) & (kSize - 1);
            L[i] = l_[k];
            if (R) R[i] = r_[k];
        }
        return w;
    }
    size_t position() const { return pos_.load(std::memory_order_acquire); }

private:
    std::unique_ptr<float[]> l_, r_;
    std::atomic<size_t> pos_{0};
};

} // namespace raw2
