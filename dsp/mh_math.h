/* Monohull: shared math. Approximations instead of libm, which is soft-float on the pi32v2:
   each is accurate to about 1e-7 relative over the range the engine uses (host tests check
   them against libm). */
#ifndef MH_MATH_H
#define MH_MATH_H

#include <stdint.h>

/* for the few small loops the compiler must unroll in place */
#ifdef _MSC_VER
#define MH_ALWAYS_INLINE static __forceinline
#else
#define MH_ALWAYS_INLINE static inline __attribute__((always_inline))
#endif

#define MH_PI 3.14159265358979f
#define MH_LOG2E 1.44269504089f
#define MH_LN2 0.69314718056f

static inline float mh_clamp(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline float mh_absf(float x) {
    return x < 0.f ? -x : x;
}

static inline float mh_floor(float x) {
    float f = (float)(int32_t)x;
    return f > x ? f - 1.f : f;
}

/* 2^x for x in [-126, 126]: the integer part goes into the exponent field, the fraction is
   e^((f - 0.5) ln 2) by its Taylor series to t^5 (|t| <= 0.35: error < 3e-6, 0.005 cents),
   times sqrt 2. */
static inline float mh_exp2(float x) {
    x = mh_clamp(x, -126.f, 126.f);
    float fl = mh_floor(x);
    float t = (x - fl - 0.5f) * MH_LN2;
    float p = 1.f + t * (1.f + t * (0.5f + t * (1.f / 6.f + t * (1.f / 24.f + t * (1.f / 120.f)))));
    union { float f; uint32_t u; } e;
    e.u = (uint32_t)((int32_t)fl + 127) << 23;
    return p * 1.41421356237f * e.f;
}

/* tanh by Lambert's continued fraction, the [7/6] approximant: within 1e-4 of tanh everywhere
   (-80 dB; it reaches 1 at |x| = 4.972 and is clamped there). One division, no exp. */
#define MH_TANH_EDGE 4.972f
static inline float mh_tanh(float x) {
    if (x > MH_TANH_EDGE)
        return 1.f;
    if (x < -MH_TANH_EDGE)
        return -1.f;
    float x2 = x * x;
    float n = 135135.f + x2 * (17325.f + x2 * (378.f + x2));
    float d = 135135.f + x2 * (62370.f + x2 * (3150.f + 28.f * x2));
    return x * n / d;
}

/* tanh(x) / x, the slope of the secant through 0 (1 at 0): the same fraction without the x. */
static inline float mh_tanh_x(float x) {
    float a = mh_absf(x);
    if (a > MH_TANH_EDGE)
        return 1.f / a;
    float x2 = x * x;
    float n = 135135.f + x2 * (17325.f + x2 * (378.f + x2));
    float d = 135135.f + x2 * (62370.f + x2 * (3150.f + 28.f * x2));
    return n / d;
}

/* tanh(x), with tanh(x) / x into *sig: the same fraction, one division for both. */
static inline float mh_tanh_sig(float x, float *sig) {
    float a = mh_absf(x);
    if (a > MH_TANH_EDGE) {
        *sig = 1.f / a;
        return x > 0.f ? 1.f : -1.f;
    }
    float x2 = x * x;
    float n = 135135.f + x2 * (17325.f + x2 * (378.f + x2));
    float d = 135135.f + x2 * (62370.f + x2 * (3150.f + 28.f * x2));
    float s = n / d;
    *sig = s;
    return x * s;
}

/* tan(pi f) for f in [0, 0.3]: the [5/4] Pade approximant (error < 1e-7 below f = 0.25). */
static inline float mh_tan_pi(float f) {
    float x = MH_PI * f, x2 = x * x;
    return x * (945.f + x2 * (-105.f + x2)) / (945.f + x2 * (-420.f + 15.f * x2));
}

/* ln(1 + u) for u in [0, 1]: 2 atanh(s), s = u / (2 + u) <= 1/3, odd series to s^11. */
static inline float mh_log1p01(float u) {
    float s = u / (2.f + u), s2 = s * s;
    return 2.f * s * (1.f + s2 * (1.f / 3.f + s2 * (1.f / 5.f + s2 * (1.f / 7.f + s2 * (1.f / 9.f + s2 * (1.f / 11.f))))));
}

/* log2(x) for x > 0: the exponent field plus ln(mantissa) / ln 2. */
static inline float mh_log2(float x) {
    union { float f; uint32_t u; } v;
    v.f = x;
    int e = (int)((v.u >> 23) & 0xff) - 127;
    v.u = (v.u & 0x007fffffu) | 0x3f800000u;
    return (float)e + mh_log1p01(v.f - 1.f) * MH_LOG2E;
}

/* sqrt(x) for x >= 0, by way of log2 and exp2 (control rate only). */
static inline float mh_sqrt_approx(float x) {
    return x > 0.f ? mh_exp2(0.5f * mh_log2(x)) : 0.f;
}

/* sin(2 pi p) for any p: folded to a quarter wave, Taylor to x^11 (error < 1e-7). */
static inline float mh_sin2pi(float p) {
    p -= mh_floor(p);
    if (p > 0.75f)
        p -= 1.f;
    else if (p > 0.25f)
        p = 0.5f - p;
    float x = 2.f * MH_PI * p, x2 = x * x;
    return x * (1.f + x2 * (-1.f / 6.f + x2 * (1.f / 120.f + x2 * (-1.f / 5040.f + x2 * (1.f / 362880.f + x2 * (-1.f / 39916800.f))))));
}

/* xorshift32: a full-period generator, never 0. */
static inline uint32_t mh_rand(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* A uniform float in [-1, 1). */
static inline float mh_randf(uint32_t *s) {
    return (float)(int32_t)mh_rand(s) * (1.f / 2147483648.f);
}

/* One-pole coefficient for a time constant of `seconds` at `rate`: 1 - e^(-y), y = 1 / (seconds
   rate). Long times make y tiny, and 1 - e^(-y) would cancel down to exp2's error: there the
   series, exact to float precision. */
static inline float mh_onepole(float seconds, float rate) {
    float y = 1.f / (seconds * rate);
    if (y < 0.05f)
        return y * (1.f - y * 0.5f * (1.f - y * (1.f / 3.f) * (1.f - y * 0.25f)));
    return 1.f - mh_exp2(-MH_LOG2E * y);
}

#endif
