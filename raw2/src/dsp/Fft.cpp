#include "dsp/Fft.h"

#include <algorithm>
#include <cmath>

namespace raw2 {

namespace {
// tables par taille (une par thread : l'analyse de l'interface, la construction des mipmaps et l'IA
// tournent sur des threads différents) : permutation bit-reverse et facteurs de rotation exacts,
// au lieu de rotations cumulées (plus lentes et qui accumulent l'erreur d'arrondi)
struct Plan {
    size_t n = 0;
    std::vector<uint32_t> rev;
    std::vector<std::complex<double>> tw;  // e^{-2iπk/n}, k < n/2
    void make(size_t size) {
        n = size;
        rev.assign(n, 0);
        for (size_t i = 1, j = 0; i < n; ++i) {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            rev[i] = (uint32_t)j;
        }
        tw.resize(n / 2);
        const double pi = 3.14159265358979323846;
        for (size_t k = 0; k < n / 2; ++k) tw[k] = std::polar(1.0, -2.0 * pi * (double)k / (double)n);
    }
};
} // namespace

void fft(std::vector<std::complex<double>>& a, bool inverse) {
    const size_t n = a.size();
    if (n < 2) return;
    thread_local Plan plans[4];
    thread_local int next = 0;
    Plan* p = nullptr;
    for (auto& q : plans)
        if (q.n == n) p = &q;
    if (!p) {
        p = &plans[next];
        next = (next + 1) & 3;
        p->make(n);
    }
    for (size_t i = 1; i < n; ++i)
        if (i < p->rev[i]) std::swap(a[i], a[p->rev[i]]);
    std::complex<double>* d = a.data();
    const std::complex<double>* tw = p->tw.data();
    for (size_t len = 2; len <= n; len <<= 1) {
        const size_t half = len >> 1, step = n / len;
        for (size_t i = 0; i < n; i += len) {
            for (size_t k = 0; k < half; ++k) {
                std::complex<double> w = tw[k * step];
                if (inverse) w = std::conj(w);
                const std::complex<double> u = d[i + k];
                // produit complexe écrit à la main : std::complex peut appeler une routine lente
                // (gestion des infinis) sans -ffast-math
                const double vr = d[i + k + half].real() * w.real() - d[i + k + half].imag() * w.imag();
                const double vi = d[i + k + half].real() * w.imag() + d[i + k + half].imag() * w.real();
                d[i + k] = {u.real() + vr, u.imag() + vi};
                d[i + k + half] = {u.real() - vr, u.imag() - vi};
            }
        }
    }
    if (inverse) {
        const double s = 1.0 / (double)n;
        for (auto& x : a) x *= s;
    }
}

} // namespace raw2
