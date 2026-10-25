#include "mh_ladder.h"

#include <string.h>

#include "mh_math.h"

void mh_ladder_init(mh_ladder *l) {
    memset(l, 0, sizeof *l);
    for (int i = 0; i < 5; i++)
        l->sig[i] = 1.f;
}

float mh_ladder_step(mh_ladder *l, float x, float g, float k, int newton, float out[5]) {
    /* Cheap solve. With tanh(v) ~ sig v, stage i is y_i = G_i y_(i-1) + B_i (stage 1 takes u),
       so y4 = P u + Q and u = x - k y4 gives u = (x - k Q) / (1 + k P). */
    float y[4], gs[4], bs[4];
    float in_sig = l->sig[0];
    for (int i = 0; i < 4; i++) {
        float sig = l->sig[i + 1];
        float r = 1.f / (1.f + g * sig);
        gs[i] = g * in_sig * r;
        bs[i] = l->s[i] * r;
        in_sig = sig;
    }
    float p = gs[0], q = bs[0];
    for (int i = 1; i < 4; i++) {
        p *= gs[i];
        q = gs[i] * q + bs[i];
    }
    float u = (x - k * q) / (1.f + k * p);
    float v = u;
    for (int i = 0; i < 4; i++)
        v = y[i] = gs[i] * v + bs[i];

    /* Newton on F_i = y_i - s_i - g (tanh(in_i) - tanh(y_i)). The Jacobian is lower bidiagonal
       plus the feedback's corner (row 1, column 4): write each correction as p_i + q_i dy4,
       solve for dy4, substitute back. The last evaluation's tanh(v) / v (the same fraction as
       tanh, mh_tanh_sig) are the next sample's secant slopes, so a converged sample costs five
       divisions for its tanh and none more for the slopes. */
    int it = 0;
    if (newton) {
        float t0, t[4], sg[5];
        for (;;) {
            t0 = mh_tanh_sig(u, &sg[0]);
            float d0 = 1.f - t0 * t0, d[4];
            for (int i = 0; i < 4; i++) {
                t[i] = mh_tanh_sig(y[i], &sg[i + 1]);
                d[i] = 1.f - t[i] * t[i];
            }
            float pp[4], qq[4];
            float ra = 1.f / (1.f + g * d[0]);
            pp[0] = -(y[0] - l->s[0] - g * (t0 - t[0])) * ra;
            qq[0] = -g * k * d0 * ra;
            for (int i = 1; i < 4; i++) {
                float f = y[i] - l->s[i] - g * (t[i - 1] - t[i]);
                float b = g * d[i - 1];
                ra = 1.f / (1.f + g * d[i]);
                pp[i] = (-f + b * pp[i - 1]) * ra;
                qq[i] = b * qq[i - 1] * ra;
            }
            float dy4 = pp[3] / (1.f - qq[3]);
            float big = 0.f;
            for (int i = 0; i < 4; i++) {
                float dy = pp[i] + qq[i] * dy4;
                y[i] += dy;
                big = mh_absf(dy) > big ? mh_absf(dy) : big;
            }
            u = x - k * y[3];
            it++;
            /* quadratic convergence: what is left after a step this small is ~step^2 */
            if (big < 3e-4f || it >= MH_LADDER_ITER)
                break;
        }
        for (int i = 0; i < 5; i++)
            l->sig[i] = sg[i];
    }
    l->iters = it;

    /* a blow-up (only from non-finite input) resets rather than sticking */
    if (!(y[3] > -64.f && y[3] < 64.f)) {
        mh_ladder_init(l);
        for (int i = 0; i < 5; i++)
            out[i] = 0.f;
        return 0.f;
    }
    for (int i = 0; i < 4; i++) {
        l->s[i] = 2.f * y[i] - l->s[i];
        l->y[i] = y[i];
    }
    l->u = u;
    if (!newton) { /* the cheap solve's slopes for the next sample, at this one's outputs */
        l->sig[0] = mh_tanh_x(u);
        for (int i = 0; i < 4; i++)
            l->sig[i + 1] = mh_tanh_x(y[i]);
    }
    out[0] = u;
    out[1] = y[0];
    out[2] = y[1];
    out[3] = y[2];
    out[4] = y[3];
    return y[3];
}

float mh_ladder_mix(const float o[5], float morph) {
    if (morph <= 0.f) /* LP24, the usual case: the same value, without the other mixes */
        return o[4];
    float lp24 = o[4], lp12 = o[2];
    float bp12 = 2.f * (o[1] - o[2]);
    float hp24 = o[0] - 4.f * o[1] + 6.f * o[2] - 4.f * o[3] + o[4];
    morph = mh_clamp(morph, 0.f, 3.f);
    if (morph < 1.f)
        return lp24 + morph * (lp12 - lp24);
    if (morph < 2.f)
        return lp12 + (morph - 1.f) * (bp12 - lp12);
    return bp12 + (morph - 2.f) * (hp24 - bp12);
}
