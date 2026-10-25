#include "mh_osc.h"

#include <string.h>

#include "mh_math.h"
#include "mh_tables.h"

#define MASK (MH_OSC_RING - 1)

void mh_shape_set(mh_shape *s, float shape, float pw) {
    shape = mh_clamp(shape, 0.f, 1.f) * 3.f;
    s->saw = s->pul = s->tri = s->sin = s->bow = s->soft = 0.f;
    if (shape < 1.f) {
        s->saw = 1.f - shape;
        s->pul = shape;
    } else if (shape < 2.f) {
        s->pul = 2.f - shape;
        s->tri = shape - 1.f;
    } else {
        s->tri = 3.f - shape;
        s->sin = shape - 2.f;
    }
    s->pw = mh_clamp(pw, 0.02f, 0.98f);
}

void mh_osc_init(mh_osc *o, int table, float phase) {
    memset(o, 0, sizeof *o);
    o->z = table ? MH_BLEP_Z : 1;
    o->ph = (uint32_t)(mh_clamp(phase, 0.f, 0.999999f) * 4294967040.f);
    o->sub_v = 1.f;
}

void mh_osc_set_kernel(mh_osc *o, int table) {
    int z = table ? MH_BLEP_Z : 1;
    if (z == o->z)
        return;
    memset(o->buf, 0, sizeof o->buf);
    memset(o->sub_buf, 0, sizeof o->sub_buf);
    o->z = z;
}

#define phase_f mh_osc_phase
#define naive mh_osc_naive

float mh_osc_sine(float p) {
    return mh_sin2pi(p);
}

/* The polynomial kernel's residuals at tau samples from a discontinuity: band-limited minus
   ideal, for a unit step and a unit slope change. */
static inline void poly_residual(float tau, float *step, float *ramp) {
    if (tau < 0.f) {
        float a = 1.f + tau, a2 = a * a;
        *step = 0.5f * a2;
        *ramp = a2 * a * (1.f / 6.f);
    } else {
        float a = 1.f - tau, a2 = a * a;
        *step = -0.5f * a2;
        *ramp = a2 * a * (1.f / 6.f);
    }
}

/* A step of `a` and a slope change of `ds` (per sample), `d` samples before the current one:
   their corrections go to the slots around it. */
static void add_event(float *buf, unsigned pos, int z, float d, float a, float ds) {
    if (a == 0.f && ds == 0.f)
        return;
    d = mh_clamp(d, 0.f, 0.99999f);
    if (z == 1) {
        float st, rp;
        poly_residual(d - 1.f, &st, &rp);
        buf[(pos - 1u) & MASK] += a * st + ds * rp;
        poly_residual(d, &st, &rp);
        buf[pos & MASK] += a * st + ds * rp;
        return;
    }
    /* the table: interpolate between the two rows around d, all 2Z taps contiguous */
    float rp = d * MH_BLEP_O;
    int r = (int)rp;
    float f = rp - (float)r;
    const float *s0 = mh_blep[r], *s1 = mh_blep[r + 1], *m0 = mh_blamp[r], *m1 = mh_blamp[r + 1];
    unsigned at = pos - (unsigned)MH_BLEP_Z;
    for (int c = 0; c < 2 * MH_BLEP_Z; c++) {
        float st = s0[c] + f * (s1[c] - s0[c]);
        float rm = m0[c] + f * (m1[c] - m0[c]);
        buf[(at + (unsigned)c) & MASK] += a * st + ds * rm;
    }
}

/* dv / dphase */
static inline float dnaive(const mh_shape *s, float p) {
    float d = 2.f * s->saw + s->tri * (p < 0.5f ? 4.f : -4.f);
    if (s->sin != 0.f)
        d += s->sin * 2.f * MH_PI * mh_sin2pi(p + 0.25f);
    if (s->bow != 0.f)
        d += s->bow * (1.f - 2.f * p);
    if (s->pul != 0.f && s->soft != 0.f) {
        float a = MH_SQ303_SLOPE * (p - s->pw);
        if (a > -9.f && a < 9.f) {
            float t = mh_tanh(a);
            d -= s->pul * s->soft * MH_SQ303_SLOPE * (1.f - t * t);
        }
    }
    return d;
}

static void wrap_event(mh_osc *o, const mh_shape *s, float inc, float d, int fwd, int sub_div) {
    float a = fwd ? -2.f * s->saw + 2.f * s->pul : 2.f * s->saw - 2.f * s->pul;
    /* the slope's corners: the triangle's (-4 to 4) and the bow's (-bow to bow) */
    float ds = (fwd ? 1.f : -1.f) * (8.f * s->tri + 2.f * s->bow) * inc;
    add_event(o->buf, o->pos, o->z, d, a, ds);
    if (sub_div && ++o->sub_count >= sub_div) {
        o->sub_count = 0;
        add_event(o->sub_buf, o->pos, o->z, d, -2.f * o->sub_v, 0.f);
        o->sub_v = -o->sub_v;
    }
}

static inline void pw_event(mh_osc *o, const mh_shape *s, float d, int fwd) {
    /* the soft edge's share is in the naive wave already, continuous: only the step's is corrected */
    add_event(o->buf, o->pos, o->z, d, (fwd ? -2.f : 2.f) * s->pul * (1.f - s->soft), 0.f);
}

static inline void half_event(mh_osc *o, const mh_shape *s, float inc, float d, int fwd) {
    add_event(o->buf, o->pos, o->z, d, 0.f, (fwd ? -8.f : 8.f) * s->tri * inc);
}

/* Advance by `inc * dur`, the stretch ending `d_end` samples before the current sample, adding
   the events crossed on the way. Returns the time of a wrap, or -1. */
static float segment(mh_osc *o, const mh_shape *s, float inc, float dur, float d_end, int sub_div) {
    float dph = inc * dur;
    if (dph == 0.f)
        return -1.f;
    float f0 = phase_f(o->ph);
    uint32_t old = o->ph;
    o->ph = old + (uint32_t)(int32_t)(dph * 4294967296.f);
    float f1 = phase_f(o->ph);
    float pw = s->pw, w = -1.f;
    int pul = s->pul != 0.f, tri = s->tri != 0.f;
    if (dph > 0.f) {
        float ir = 1.f / inc;
        if (o->ph < old) {
            if (pul && f0 < pw)
                pw_event(o, s, d_end + (1.f - pw + f1) * ir, 1);
            if (tri && f0 < 0.5f)
                half_event(o, s, inc, d_end + (0.5f + f1) * ir, 1);
            w = d_end + f1 * ir;
            wrap_event(o, s, inc, w, 1, sub_div);
            if (pul && pw <= f1)
                pw_event(o, s, d_end + (f1 - pw) * ir, 1);
            if (tri && 0.5f <= f1)
                half_event(o, s, inc, d_end + (f1 - 0.5f) * ir, 1);
        } else {
            if (pul && f0 < pw && pw <= f1)
                pw_event(o, s, d_end + (f1 - pw) * ir, 1);
            if (tri && f0 < 0.5f && 0.5f <= f1)
                half_event(o, s, inc, d_end + (f1 - 0.5f) * ir, 1);
        }
    } else {
        float ir = -1.f / inc;
        if (o->ph > old) {
            if (pul && pw < f0)
                pw_event(o, s, d_end + (pw + 1.f - f1) * ir, 0);
            if (tri && 0.5f < f0)
                half_event(o, s, inc, d_end + (1.5f - f1) * ir, 0);
            w = d_end + (1.f - f1) * ir;
            wrap_event(o, s, inc, w, 0, sub_div);
            if (pul && f1 <= pw)
                pw_event(o, s, d_end + (pw - f1) * ir, 0);
            if (tri && f1 <= 0.5f)
                half_event(o, s, inc, d_end + (0.5f - f1) * ir, 0);
        } else {
            if (pul && f1 <= pw && pw < f0)
                pw_event(o, s, d_end + (pw - f1) * ir, 0);
            if (tri && f1 <= 0.5f && 0.5f < f0)
                half_event(o, s, inc, d_end + (0.5f - f1) * ir, 0);
        }
    }
    return w;
}

float mh_osc_step(mh_osc *o, const mh_shape *s, float inc, float sync_d, float *wrap_d, int sub_div,
                  float *sub) {
    float w, f1;
    /* The common sample: running forward with no wrap, no pulse edge and no triangle corner in
       it. The tests are segment's own (the same integer wrap, the same float thresholds), so the
       two never disagree; only a sample with an event pays for the full path. */
    if (sync_d < 0.f && inc > 0.f) {
        uint32_t old = o->ph, nw = old + (uint32_t)(int32_t)(inc * 4294967296.f);
        if (nw >= old) {
            float f0 = phase_f(old);
            f1 = phase_f(nw);
            if (!(s->pul != 0.f && f0 < s->pw && s->pw <= f1) && !(s->tri != 0.f && f0 < 0.5f && 0.5f <= f1)) {
                o->ph = nw;
                w = -1.f;
                goto made;
            }
        }
    }
    if (sync_d >= 0.f) {
        sync_d = mh_clamp(sync_d, 0.f, 0.99999f);
        float w1 = segment(o, s, inc, 1.f - sync_d, sync_d, sub_div);
        float pr = phase_f(o->ph);
        add_event(o->buf, o->pos, o->z, sync_d, naive(s, 0.f) - naive(s, pr),
                  (dnaive(s, 0.f) - dnaive(s, pr)) * inc);
        o->ph = 0;
        float w2 = segment(o, s, inc, sync_d, 0.f, sub_div);
        w = w2 >= 0.f ? w2 : w1;
    } else {
        w = segment(o, s, inc, 1.f, 0.f, sub_div);
    }
    f1 = phase_f(o->ph);
made:;
    unsigned now = o->pos & MASK, out = (o->pos - (unsigned)o->z) & MASK;
    o->buf[now] += naive(s, f1);
    float y = o->buf[out];
    o->buf[out] = 0.f;
    if (sub_div) {
        o->sub_buf[now] += o->sub_v;
        *sub = o->sub_buf[out];
        o->sub_buf[out] = 0.f;
    }
    o->pos++;
    if (wrap_d)
        *wrap_d = w;
    return y;
}

/* mh_osc_run's loop, for a blend with no pulse or triangle (no edges or corners between wraps)
   or any blend: `plain` is a constant where it is inlined, so each gets its own loop */
MH_ALWAYS_INLINE void run_copy(mh_osc *o, const mh_shape *s, const float *inc, float ratio, const float *lv,
                               float gl, float gr, int n, int os, float *out_l, float *out_r, int plain) {
    uint32_t ph = o->ph;
    unsigned pos = o->pos, z = (unsigned)o->z;
    float *buf = o->buf;
    for (int i = 0, x = 0; i < n; i++, x += os) {
        float a = lv[i];
        if (a <= 0.f)
            continue;
        float d = inc[i] * ratio;
        for (int h = 0; h < os; h++) {
            float v;
            uint32_t nw = ph + (uint32_t)(int32_t)(d * 4294967296.f);
            float f1 = mh_osc_phase(nw);
            int common = d > 0.f && nw >= ph;
            if (common && !plain) {
                float f0 = mh_osc_phase(ph);
                common = !(s->pul != 0.f && f0 < s->pw && s->pw <= f1) && !(s->tri != 0.f && f0 < 0.5f && 0.5f <= f1);
            }
            if (common) {
                ph = nw;
                unsigned now = pos & MASK, out = (pos - z) & MASK;
                float w;
                if (plain) {
                    w = 0.f;
                    if (s->saw != 0.f)
                        w += s->saw * (2.f * f1 - 1.f);
                    if (s->sin != 0.f)
                        w += s->sin * mh_osc_sine(f1);
                } else {
                    w = naive(s, f1);
                }
                buf[now] += w;
                v = buf[out];
                buf[out] = 0.f;
                pos++;
            } else {
                o->ph = ph;
                o->pos = pos;
                v = mh_osc_step(o, s, d, -1.f, 0, 0, 0);
                ph = o->ph;
                pos = o->pos;
            }
            out_l[x + h] += v * a * gl;
            out_r[x + h] += v * a * gr;
        }
    }
    o->ph = ph;
    o->pos = pos;
}

void mh_osc_run(mh_osc *o, const mh_shape *s, const float *inc, float ratio, const float *lv, float gl, float gr,
                int n, int os, float *out_l, float *out_r) {
    if (s->pul == 0.f && s->tri == 0.f)
        run_copy(o, s, inc, ratio, lv, gl, gr, n, os, out_l, out_r, 1);
    else
        run_copy(o, s, inc, ratio, lv, gl, gr, n, os, out_l, out_r, 0);
}
