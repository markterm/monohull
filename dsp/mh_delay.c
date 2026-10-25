#include "mh_delay.h"

#include <string.h>

#include "mh_math.h"

void mh_delay_init(mh_delay *d) {
    memset(d, 0, sizeof *d);
    d->t = 2.f;
}

/* stmlib's InterpolateHermite: between x0 and x1 at f, x-1 and x2 the neighbours */
static inline float hermite(float xm1, float x0, float x1, float x2, float f) {
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b_neg = w + a;
    return (((a * f) - b_neg) * f + c) * f + x0;
}

/* stmlib's SoftClip: x (27 + x^2) / (27 + 9 x^2), +-1 beyond +-3 */
static inline float soft_clip(float x) {
    if (x < -3.f)
        return -1.f;
    if (x > 3.f)
        return 1.f;
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}

static inline int16_t to16(float x) {
    float v = x * 32767.f;
    return (int16_t)(v >= 0.f ? v + 0.5f : v - 0.5f); /* |x| <= 1: no overflow */
}

/* the line's sample `t` samples back, `w` the slot about to be written (t in [2, LEN - 3]) */
static inline float tap(const int16_t *line, int w, int k, float f) {
    int i = w - k + 1; /* delay k - 1 */
    if (i < 0)
        i += MH_DELAY_LEN;
    float x[4];
    for (int j = 0; j < 4; j++) {
        x[j] = (float)line[i] * (1.f / 32767.f);
        if (--i < 0)
            i += MH_DELAY_LEN;
    }
    return hermite(x[0], x[1], x[2], x[3], f);
}

void mh_delay_process(mh_delay *d, float *l, float *r, int n, float target, float feedback, float mix, float lp_a,
                      float hp_a, float glide, int snap) {
    target = mh_clamp(target, 2.f, (float)(MH_DELAY_LEN - 3));
    if (snap)
        d->t = target;
    float t = d->t, peak = 0.f;
    int w = d->w;
    for (int i = 0; i < n; i++) {
        t += (target - t) * glide;
        int k = (int)t;
        float f = t - (float)k;
        float yl = tap(d->line[0], w, k, f), yr = tap(d->line[1], w, k, f);
        float a = 0.5f * (l[i] + r[i]) + feedback * yr, b = feedback * yl;
        d->lp[0] += (a - d->lp[0]) * lp_a;
        d->lp[1] += (b - d->lp[1]) * lp_a;
        a = d->lp[0];
        b = d->lp[1];
        d->hp[0] += (a - d->hp[0]) * hp_a;
        d->hp[1] += (b - d->hp[1]) * hp_a;
        d->line[0][w] = to16(soft_clip(a - d->hp[0]));
        d->line[1][w] = to16(soft_clip(b - d->hp[1]));
        if (++w == MH_DELAY_LEN)
            w = 0;
        l[i] += mix * yl;
        r[i] += mix * yr;
        float m = mh_absf(yl) > mh_absf(yr) ? mh_absf(yl) : mh_absf(yr);
        peak = m > peak ? m : peak;
    }
    d->t = t;
    d->w = w;
    d->peak = peak;
}
