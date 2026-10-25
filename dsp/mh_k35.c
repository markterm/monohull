#include "mh_k35.h"

#include <string.h>

#include "mh_math.h"

void mh_k35_init(mh_k35 *f) {
    memset(f, 0, sizeof *f);
}

/* Surge's doLpf / doHpf: a TPT one-pole of coefficient G = g / (1 + g) */
static inline float lpf(float G, float x, float *z) {
    float v = (x - *z) * G;
    float r = v + *z;
    *z = v + r;
    return r;
}

static inline float hpf(float G, float x, float *z) {
    return x - lpf(G, x, z);
}

/* makeCoefficients' K: Surge's reso * 1.96, clamped */
static inline float clamp_k(float k) {
    return k > 1.96f ? 1.96f : k < 0.01f ? 0.01f : k;
}

float mh_k35_lp(mh_k35 *f, float x, float g, float k) {
    float gp1 = 1.f + g, G = g / gp1, mk = clamp_k(k);
    float lb = (mk - mk * G) / gp1, hb = -1.f / gp1;
    float alpha = 1.f / (1.f - mk * G + mk * G * G);
    float y1 = lpf(G, x, &f->lz);
    float s35 = lb * f->z2 + hb * f->hz;
    float u = mh_tanh(alpha * (y1 + s35)); /* saturation 1: wholly the tanh's */
    float y = mk * lpf(G, u, &f->z2);
    hpf(G, y, &f->hz);
    return y / mk;
}

float mh_k35_hp(mh_k35 *f, float x, float g, float k) {
    float gp1 = 1.f + g, G = g / gp1, mk = clamp_k(k);
    float lb = 1.f / gp1, hb = -G / gp1;
    float alpha = 1.f / (1.f - mk * G + mk * G * G);
    float y1 = hpf(G, x, &f->hz);
    float s35 = hb * f->z2 + lb * f->lz;
    float u = alpha * (y1 + s35);
    float y = mh_tanh(mk * u);
    lpf(G, hpf(G, y, &f->z2), &f->lz);
    return y / mk;
}
