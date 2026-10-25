#include "mh_chorus.h"

#include <math.h>
#include <string.h>

#include "mh_math.h"

#define MASK (MH_CHORUS_LEN - 1)

/* Butterworth low-pass (RBJ cookbook), set once at init: libm is fine here. */
static void lowpass(mh_biquad *b, float fc, float rate) {
    double w0 = 2.0 * 3.14159265358979 * fc / rate, cw = cos(w0), alpha = sin(w0) / (2.0 * 0.70710678);
    double a0 = 1.0 + alpha;
    b->b0 = (float)((1.0 - cw) * 0.5 / a0);
    b->b1 = (float)((1.0 - cw) / a0);
    b->b2 = b->b0;
    b->a1 = (float)(-2.0 * cw / a0);
    b->a2 = (float)((1.0 - alpha) / a0);
    b->z1 = b->z2 = 0.f;
}

static inline float biquad(mh_biquad *b, float x) {
    float y = b->b0 * x + b->z1;
    b->z1 = b->b1 * x - b->a1 * y + b->z2;
    b->z2 = b->b2 * x - b->a2 * y;
    return y;
}

void mh_chorus_init(mh_chorus *c, float rate) {
    memset(c, 0, sizeof *c);
    c->rate = rate;
    lowpass(&c->pre, 15000.f, rate);
    lowpass(&c->post[0], 15000.f, rate);
    lowpass(&c->post[1], 15000.f, rate);
    c->ph[1] = 1.f / 3.f;
    c->ph[2] = 2.f / 3.f;
}

static inline float tri(float p) {
    return p < 0.5f ? 4.f * p - 1.f : 3.f - 4.f * p;
}

/* Catmull-Rom read `d` samples behind the newest. */
static inline float tap(const mh_chorus *c, float d) {
    float pos = (float)c->w - d;
    float fl = mh_floor(pos);
    float t = pos - fl;
    unsigned i = (unsigned)(int)fl;
    float xm = c->buf[(i - 1) & MASK], x0 = c->buf[i & MASK], x1 = c->buf[(i + 1) & MASK],
          x2 = c->buf[(i + 2) & MASK];
    float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.f * x1 - 0.5f * x2,
          c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

/* One BBD tap: the read, then the line's bandwidth (256 stages: stages / (4 delay) Hz). */
static inline float bbd(mh_chorus *c, int n, float ms) {
    float d = ms * 0.001f * c->rate;
    float fc = 256.f / (4.f * ms * 0.001f);
    float a = 1.f - mh_exp2(-2.f * MH_PI * MH_LOG2E * fc / c->rate);
    c->bw[n] += (tap(c, d) - c->bw[n]) * a;
    return c->bw[n];
}

/* The BBD's charge: linear to +-0.7, then softly compressed. */
static inline float charge(float x) {
    float m = mh_absf(x);
    if (m <= 0.7f)
        return x;
    float y = 0.7f + 0.3f * mh_tanh((m - 0.7f) * (1.f / 0.3f));
    return x < 0.f ? -y : y;
}

static inline float advance(float p, float inc) {
    p += inc;
    return p >= 1.f ? p - 1.f : p;
}

void mh_chorus_step(mh_chorus *c, int mode, float depth, float mix, float in_l, float in_r, float *out_l,
                    float *out_r) {
    if (mode <= MH_CHORUS_OFF || mode >= MH_CHORUS_MODES || mix <= 0.f) {
        *out_l = in_l; /* off costs nothing; the line refills within its 23 ms when it comes on */
        *out_r = in_r;
        return;
    }
    float x = charge(biquad(&c->pre, 0.5f * (in_l + in_r)));
    c->w = (c->w + 1) & MASK;
    c->buf[c->w] = x;
    float wl, wr, wet_gain, dry_gain;
    if (mode == MH_CHORUS_ENSEMBLE) {
        c->ph[0] = advance(c->ph[0], 0.6f / c->rate);
        c->ph[1] = advance(c->ph[1], 0.6f / c->rate);
        c->ph[2] = advance(c->ph[2], 0.6f / c->rate);
        c->fast_ph = advance(c->fast_ph, 6.0f / c->rate);
        float t[3];
        for (int k = 0; k < 3; k++) {
            float ms = 7.f + depth * (1.5f * mh_sin2pi(c->ph[k]) + 0.15f * mh_sin2pi(c->fast_ph + (float)k / 3.f));
            t[k] = bbd(c, k, ms);
        }
        wl = (t[0] + 0.5f * t[1]) * (1.f / 1.5f);
        wr = (t[2] + 0.5f * t[1]) * (1.f / 1.5f);
        dry_gain = 0.5f;
        wet_gain = 0.7f;
    } else {
        float l = 0.f, r = 0.f;
        int n = 0;
        if (mode == MH_CHORUS_JUNO_I || mode == MH_CHORUS_JUNO_I_II) {
            c->ph[0] = advance(c->ph[0], 0.513f / c->rate);
            float m = 0.5f * depth * tri(c->ph[0]);
            l += bbd(c, 0, 3.5f + m);
            r += bbd(c, 1, 3.5f - m);
            n++;
        }
        if (mode == MH_CHORUS_JUNO_II || mode == MH_CHORUS_JUNO_I_II) {
            c->ph[1] = advance(c->ph[1], 0.863f / c->rate);
            float m = 1.1f * depth * tri(c->ph[1]);
            l += bbd(c, 2, 3.5f + m);
            r += bbd(c, 3, 3.5f - m);
            n++;
        }
        float s = n == 2 ? 0.5f : 1.f;
        wl = l * s;
        wr = r * s;
        dry_gain = 1.2f * 0.4f;
        wet_gain = 1.2f * 0.6f;
    }
    wl = biquad(&c->post[0], wl);
    wr = biquad(&c->post[1], wr);
    *out_l = in_l + mix * (dry_gain * in_l + wet_gain * wl - in_l);
    *out_r = in_r + mix * (dry_gain * in_r + wet_gain * wr - in_r);
}
