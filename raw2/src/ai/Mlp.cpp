#include "ai/Mlp.h"

#include <algorithm>
#include <cmath>

namespace raw2::ai {

void forward(const Network& net, const float* x, float* y) {
    float a[kMaxWidth], b[kMaxWidth];
    const int nIn = std::min(net.nIn, kMaxWidth);
    for (int i = 0; i < nIn; ++i) {
        const float v = std::isfinite(x[i]) ? x[i] : 0.f;
        a[i] = std::clamp((v - net.mean[i]) * net.scale[i], -8.f, 8.f);
    }
    float* cur = a;
    float* nxt = b;
    for (int l = 0; l < net.nLayers; ++l) {
        const DenseLayer& L = net.layers[l];
        const bool last = l == net.nLayers - 1;
        for (int o = 0; o < L.out; ++o) {
            const float* w = L.W + (size_t)o * (size_t)L.in;
            float s = L.b[o];
            for (int i = 0; i < L.in; ++i) s += w[i] * cur[i];
            nxt[o] = last ? s : std::max(0.f, s);
        }
        std::swap(cur, nxt);
    }
    for (int o = 0; o < net.nOut; ++o) y[o] = cur[o];
}

float sigmoid(float x) { return 1.f / (1.f + std::exp(-std::clamp(x, -30.f, 30.f))); }

void softmax(float* v, int n) {
    float m = v[0];
    for (int i = 1; i < n; ++i) m = std::max(m, v[i]);
    float s = 0.f;
    for (int i = 0; i < n; ++i) { v[i] = std::exp(v[i] - m); s += v[i]; }
    for (int i = 0; i < n; ++i) v[i] /= s;
}

} // namespace raw2::ai
