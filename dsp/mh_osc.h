/* Monohull: the oscillator. One phase drives a blend of saw, pulse, triangle and sine; every
   step and corner of the naive waveform is corrected by a band-limited residual (BLEP for steps,
   BLAMP for slope changes) placed at the discontinuity's fractional time.

   Two kernels: the table (linear phase, MH_BLEP_Z zero crossings, used by High's main
   oscillators) and the 2-point polynomial (polyBLEP / polyBLAMP, as Mutable's Plaits uses;
   Efficient's oscillators and every unison copy). Linear phase means the output lags the phase
   by the kernel's half-width: MH_BLEP_Z samples for the table, 1 for the polynomial. */
#ifndef MH_OSC_H
#define MH_OSC_H

#include <stdint.h>

#include "mh_math.h"

#define MH_OSC_RING 32 /* > 2 * MH_BLEP_Z, a power of two */

/* The blend, from a shape in [0, 1]: saw (0) -> pulse (1/3) -> triangle (2/3) -> sine (1).
   `bow` bends the saw's ramp as an analogue core's capacitor charge does (KR-106's curve,
   p(1 + c(1 - p)) for a saw of 2p - 1): bow p(1 - p) added, the same at both ends, so the wrap's
   step is unchanged and only its slope corner grows. mh_shape_set leaves it at 0. */
typedef struct {
    float saw, pul, tri, sin;
    float pw; /* pulse width, [0.02, 0.98] */
    float bow; /* 2 c saw */
    float soft; /* [0, 1]: the pulse's falling edge the 303's soft one (MH_SQ303_*), not a step */
} mh_shape;

/* The TB-303's square as Open303 models it (Robin Schmidt; rosic_MipMappedWaveTable.cpp's
   fillWithSquare303; MIT licence, LICENSES/MIT-Open303.txt): its saw through a transistor shaper,
   -tanh(70 saw + 4.37), shifted half a cycle. In this oscillator's phase that is
   -tanh(140 (p - pw)) with pw = (70 - 4.37) / 140 = 0.468786: a hard rising edge at the wrap and
   a soft falling one, about 3 % of the cycle wide, at 46.9 %. */
#define MH_SQ303_PW 0.4687857f
#define MH_SQ303_SLOPE 140.f

void mh_shape_set(mh_shape *s, float shape, float pw);

typedef struct {
    uint32_t ph;
    unsigned pos; /* ring slot of the sample being made */
    int z;        /* kernel half-width: MH_BLEP_Z or 1 */
    float buf[MH_OSC_RING];
    /* the sub-oscillator, a square flipped by this oscillator's wraps */
    float sub_buf[MH_OSC_RING];
    float sub_v;
    int sub_count;
} mh_osc;

/* `table`: use the windowed-sinc kernel (else the polynomial); `phase` in [0, 1). */
void mh_osc_init(mh_osc *o, int table, float phase);

/* Change kernels (resets the pending corrections, keeps the phase). */
void mh_osc_set_kernel(mh_osc *o, int table);

/* One sample. `inc`: phase per sample, |inc| < 0.5, negative runs backwards (through-zero FM).
   `sync_d` >= 0 resets the phase that many samples before this one (hard sync), < 0 doesn't.
   Returns the output; `*wrap_d` gets the time of a wrap in this sample (samples before it) or
   -1. `sub_div` 1 (one octave down) or 2 (two) makes `*sub` the sub-oscillator; 0 skips it. */
float mh_osc_step(mh_osc *o, const mh_shape *s, float inc, float sync_d, float *wrap_d, int sub_div,
                  float *sub);

/* ----- the common sample, inline: what mh_osc_step does for a sample with no sync, no sub, and
   no wrap, pulse edge or triangle corner in it (most of them); anything else goes to it. */

MH_ALWAYS_INLINE float mh_osc_phase(uint32_t ph) {
    return (float)(ph >> 8) * (1.f / 16777216.f);
}

float mh_osc_sine(float p); /* mh_sin2pi, for the blend's sine */

/* the naive waveform at phase p */
MH_ALWAYS_INLINE float mh_osc_naive(const mh_shape *s, float p) {
    float v = 0.f;
    if (s->saw != 0.f)
        v += s->saw * (2.f * p - 1.f);
    if (s->pul != 0.f) {
        float st = p < s->pw ? 1.f : -1.f, a = MH_SQ303_SLOPE * (p - s->pw);
        if (s->soft != 0.f && a > -9.f && a < 9.f) /* (beyond, tanh is +-1 to 3e-8) */
            st += s->soft * (-mh_tanh(a) - st);
        v += s->pul * st;
    }
    if (s->tri != 0.f)
        v += s->tri * (p < 0.5f ? 4.f * p - 1.f : 3.f - 4.f * p);
    if (s->sin != 0.f)
        v += s->sin * mh_osc_sine(p);
    if (s->bow != 0.f)
        v += s->bow * p * (1.f - p);
    return v;
}

/* mh_osc_step(o, s, inc, -1, wrap_d, 0, 0), the same to the bit */
MH_ALWAYS_INLINE float mh_osc_next(mh_osc *o, const mh_shape *s, float inc, float *wrap_d) {
    if (inc > 0.f) {
        uint32_t old = o->ph, nw = old + (uint32_t)(int32_t)(inc * 4294967296.f);
        if (nw >= old) {
            float f0 = mh_osc_phase(old), f1 = mh_osc_phase(nw);
            if (!(s->pul != 0.f && f0 < s->pw && s->pw <= f1) && !(s->tri != 0.f && f0 < 0.5f && 0.5f <= f1)) {
                o->ph = nw;
                unsigned now = o->pos & (MH_OSC_RING - 1), out = (o->pos - (unsigned)o->z) & (MH_OSC_RING - 1);
                o->buf[now] += mh_osc_naive(s, f1);
                float y = o->buf[out];
                o->buf[out] = 0.f;
                o->pos++;
                if (wrap_d)
                    *wrap_d = -1.f;
                return y;
            }
        }
    }
    return mh_osc_step(o, s, inc, -1.f, wrap_d, 0, 0);
}

/* A unison copy over a block, added into out_l / out_r: for frame i (os samples each) at
   increment inc[i] * ratio and level lv[i] (none where lv[i] <= 0), each sample v adds
   v * lv[i] * gl and v * lv[i] * gr. The same, to the bit, as mh_osc_next per sample; no sync. */
void mh_osc_run(mh_osc *o, const mh_shape *s, const float *inc, float ratio, const float *lv, float gl, float gr,
                int n, int os, float *out_l, float *out_r);

#endif
