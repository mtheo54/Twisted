// Minimal 4-lane float vector: SSE2 on x86-64, NEON on ARM64 (Apple Silicon), scalar otherwise.
// The engine keeps stereo in lanes 0/1; the oversampler fills all four lanes
// (left/right x two polyphase paths).
#pragma once

#include <cmath>

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define TW_SIMD_SSE 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
#include <arm_neon.h>
#define TW_SIMD_NEON 1
#endif

namespace tw {

struct alignas(16) V4 {
#if TW_SIMD_SSE
    __m128 v;
    V4() : v(_mm_setzero_ps()) {}
    V4(__m128 x) : v(x) {}
    explicit V4(float s) : v(_mm_set1_ps(s)) {}
    V4(float a, float b, float c, float d) : v(_mm_setr_ps(a, b, c, d)) {}
    friend V4 operator+(V4 a, V4 b) { return _mm_add_ps(a.v, b.v); }
    friend V4 operator-(V4 a, V4 b) { return _mm_sub_ps(a.v, b.v); }
    friend V4 operator*(V4 a, V4 b) { return _mm_mul_ps(a.v, b.v); }
    friend V4 operator/(V4 a, V4 b) { return _mm_div_ps(a.v, b.v); }
    friend V4 vmin(V4 a, V4 b) { return _mm_min_ps(a.v, b.v); }
    friend V4 vmax(V4 a, V4 b) { return _mm_max_ps(a.v, b.v); }
    friend V4 vabs(V4 a) { return _mm_andnot_ps(_mm_set1_ps(-0.f), a.v); }
    friend V4 vsqrt(V4 a) { return _mm_sqrt_ps(a.v); }
    // floor for |x| < 2^31
    friend V4 vfloor(V4 a) {
        __m128 t = _mm_cvtepi32_ps(_mm_cvttps_epi32(a.v));
        return _mm_sub_ps(t, _mm_and_ps(_mm_cmpgt_ps(t, a.v), _mm_set1_ps(1.f)));
    }
    float lane(int i) const { alignas(16) float t[4]; _mm_store_ps(t, v); return t[i]; }
#elif TW_SIMD_NEON
    float32x4_t v;
    V4() : v(vdupq_n_f32(0.f)) {}
    V4(float32x4_t x) : v(x) {}
    explicit V4(float s) : v(vdupq_n_f32(s)) {}
    V4(float a, float b, float c, float d) { alignas(16) float t[4] = {a, b, c, d}; v = vld1q_f32(t); }
    friend V4 operator+(V4 a, V4 b) { return vaddq_f32(a.v, b.v); }
    friend V4 operator-(V4 a, V4 b) { return vsubq_f32(a.v, b.v); }
    friend V4 operator*(V4 a, V4 b) { return vmulq_f32(a.v, b.v); }
    friend V4 operator/(V4 a, V4 b) { return vdivq_f32(a.v, b.v); }
    friend V4 vmin(V4 a, V4 b) { return vminq_f32(a.v, b.v); }
    friend V4 vmax(V4 a, V4 b) { return vmaxq_f32(a.v, b.v); }
    friend V4 vabs(V4 a) { return vabsq_f32(a.v); }
    friend V4 vsqrt(V4 a) { return vsqrtq_f32(a.v); }
    friend V4 vfloor(V4 a) { return vrndmq_f32(a.v); }
    float lane(int i) const { alignas(16) float t[4]; vst1q_f32(t, v); return t[i]; }
#else
    float v[4];
    V4() : v{0, 0, 0, 0} {}
    explicit V4(float s) : v{s, s, s, s} {}
    V4(float a, float b, float c, float d) : v{a, b, c, d} {}
#define TW_V4_OP(op)                                                                       \
    friend V4 operator op(V4 a, V4 b) { return V4(a.v[0] op b.v[0], a.v[1] op b.v[1],      \
                                                  a.v[2] op b.v[2], a.v[3] op b.v[3]); }
    TW_V4_OP(+) TW_V4_OP(-) TW_V4_OP(*) TW_V4_OP(/)
#undef TW_V4_OP
    friend V4 vmin(V4 a, V4 b) { return V4(std::fmin(a.v[0], b.v[0]), std::fmin(a.v[1], b.v[1]), std::fmin(a.v[2], b.v[2]), std::fmin(a.v[3], b.v[3])); }
    friend V4 vmax(V4 a, V4 b) { return V4(std::fmax(a.v[0], b.v[0]), std::fmax(a.v[1], b.v[1]), std::fmax(a.v[2], b.v[2]), std::fmax(a.v[3], b.v[3])); }
    friend V4 vabs(V4 a) { return V4(std::fabs(a.v[0]), std::fabs(a.v[1]), std::fabs(a.v[2]), std::fabs(a.v[3])); }
    friend V4 vsqrt(V4 a) { return V4(std::sqrt(a.v[0]), std::sqrt(a.v[1]), std::sqrt(a.v[2]), std::sqrt(a.v[3])); }
    friend V4 vfloor(V4 a) { return V4(std::floor(a.v[0]), std::floor(a.v[1]), std::floor(a.v[2]), std::floor(a.v[3])); }
    float lane(int i) const { return v[i]; }
#endif
    V4& operator+=(V4 b) { return *this = *this + b; }
    V4& operator*=(V4 b) { return *this = *this * b; }
};

// Lane shuffles used by the polyphase filters.
#if TW_SIMD_SSE
inline V4 dupLo(V4 a) { return _mm_movelh_ps(a.v, a.v); }            // [a0 a1 a0 a1]
inline V4 hiToLo(V4 a) { return _mm_movehl_ps(a.v, a.v); }           // [a2 a3 a2 a3]
inline V4 packLoLo(V4 a, V4 b) { return _mm_movelh_ps(a.v, b.v); }   // [a0 a1 b0 b1]
#elif TW_SIMD_NEON
inline V4 dupLo(V4 a) { return vcombine_f32(vget_low_f32(a.v), vget_low_f32(a.v)); }
inline V4 hiToLo(V4 a) { return vcombine_f32(vget_high_f32(a.v), vget_high_f32(a.v)); }
inline V4 packLoLo(V4 a, V4 b) { return vcombine_f32(vget_low_f32(a.v), vget_low_f32(b.v)); }
#else
inline V4 dupLo(V4 a) { return V4(a.v[0], a.v[1], a.v[0], a.v[1]); }
inline V4 hiToLo(V4 a) { return V4(a.v[2], a.v[3], a.v[2], a.v[3]); }
inline V4 packLoLo(V4 a, V4 b) { return V4(a.v[0], a.v[1], b.v[0], b.v[1]); }
#endif

inline V4 vclamp(V4 x, float lo, float hi) { return vmin(vmax(x, V4(lo)), V4(hi)); }

// Rational tanh approximation, exact at 0, saturates to +-1 beyond |x| = 3.
inline V4 vtanh(V4 x) {
    x = vclamp(x, -3.f, 3.f);
    V4 x2 = x * x;
    return x * (V4(27.f) + x2) / (V4(27.f) + V4(9.f) * x2);
}
inline float ftanh(float x) {
    x = x < -3.f ? -3.f : (x > 3.f ? 3.f : x);
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

// Flush denormals for the scope of a process call (x86: FTZ/DAZ; ARM64 has FZ in FPCR,
// which hosts already enable, so nothing to do there).
struct DenormalGuard {
#if TW_SIMD_SSE
    unsigned int saved;
    DenormalGuard() : saved(_mm_getcsr()) { _mm_setcsr(saved | 0x8040); }
    ~DenormalGuard() { _mm_setcsr(saved); }
#else
    DenormalGuard() {}
#endif
};

} // namespace tw
