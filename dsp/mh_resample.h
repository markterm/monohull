/* Monohull: 2x halfband resamplers, two paths of first-order allpasses (Laurent de Soras' HIIR
   structure, coefficients from tools/gen-tables). */
#ifndef MH_RESAMPLE_H
#define MH_RESAMPLE_H

#include "mh_math.h"
#include "mh_tables.h"

#define MH_HB_MAX 10

typedef struct {
    const float *c;
    int n; /* MH_HB2_N or MH_HB4_N */
    float x[MH_HB_MAX], y[MH_HB_MAX];
} mh_halfband;

/* `c` is mh_hb2 (2x <-> 1x) or mh_hb4 (4x -> 2x). */
void mh_halfband_init(mh_halfband *h, const float *c, int n);

/* The two paths, `n` coefficients (a constant where it is called, so the loop unrolls). */
MH_ALWAYS_INLINE void mh_halfband_run(mh_halfband *h, int n, float *s0, float *s1) {
    float a = *s0, b = *s1;
    for (int i = 0; i < n; i += 2) {
        float t0 = (a - h->y[i]) * h->c[i] + h->x[i];
        float t1 = (b - h->y[i + 1]) * h->c[i + 1] + h->x[i + 1];
        h->x[i] = a;
        h->x[i + 1] = b;
        h->y[i] = a = t0;
        h->y[i + 1] = b = t1;
    }
    *s0 = a;
    *s1 = b;
}

/* Two samples at the higher rate (a[0] first) to one, `n` the filter's size (MH_HB2_N or
   MH_HB4_N, a constant where it is called). */
MH_ALWAYS_INLINE float mh_halfband_down_n(mh_halfband *h, float a0, float a1, int n) {
    float s0 = a1, s1 = a0;
    mh_halfband_run(h, n, &s0, &s1);
    return 0.5f * (s0 + s1);
}

/* One sample to two at the higher rate (out[0] first). */
MH_ALWAYS_INLINE void mh_halfband_up_n(mh_halfband *h, float in, float out[2], int n) {
    float s0 = in, s1 = in;
    mh_halfband_run(h, n, &s0, &s1);
    out[0] = s0;
    out[1] = s1;
}

/* The same for either size. */
static inline float mh_halfband_down(mh_halfband *h, float a0, float a1) {
    return h->n == MH_HB2_N ? mh_halfband_down_n(h, a0, a1, MH_HB2_N) : mh_halfband_down_n(h, a0, a1, MH_HB4_N);
}

static inline void mh_halfband_up(mh_halfband *h, float in, float out[2]) {
    if (h->n == MH_HB2_N)
        mh_halfband_up_n(h, in, out, MH_HB2_N);
    else
        mh_halfband_up_n(h, in, out, MH_HB4_N);
}

#endif
