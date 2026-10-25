#include "mh_ota.h"

#include <string.h>

#include "mh_math.h"

void mh_ota_init(mh_ladder *l) {
    memset(l, 0, sizeof *l);
    for (int i = 0; i < 5; i++)
        l->sig[i] = 1.f;
}

float mh_ota_step(mh_ladder *l, float x, float g, float k, int newton, float out[5]) {
    /* Cheap solve. With tanh(a) ~ sig a, stage i is y_i = G_i in_i + B_i (in_1 = u), so
       y4 = P u + Q and u = x - k y4 gives u = (x - k Q) / (1 + k P). sig[i] is stage i's. */
    float y[4], gs[4], bs[4];
    for (int i = 0; i < 4; i++) {
        float sig = l->sig[i];
        float r = 1.f / (1.f + g * sig);
        gs[i] = g * sig * r;
        bs[i] = l->s[i] * r;
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

    /* Newton on F_i = y_i - s_i - g tanh(a_i), a_1 = x - k y4 - y1, a_i = y_(i-1) - y_i. The
       Jacobian is lower bidiagonal (dF_i/dy_i = 1 + g d_i, dF_i/dy_(i-1) = -g d_i, d = sech^2 a)
       plus the feedback's corner (dF_1/dy4 = g k d_1): each correction p_i + q_i dy4, solved
       for dy4 and substituted back, as the ladder's. */
    int it = 0;
    if (newton) {
        float sg[4];
        for (;;) {
            float t[4], d[4], f[4];
            for (int i = 0; i < 4; i++) {
                float a = i == 0 ? u - y[0] : y[i - 1] - y[i];
                t[i] = mh_tanh_sig(a, &sg[i]);
                d[i] = 1.f - t[i] * t[i];
                f[i] = y[i] - l->s[i] - g * t[i];
            }
            float pp[4], qq[4];
            float ra = 1.f / (1.f + g * d[0]);
            pp[0] = -f[0] * ra;
            qq[0] = -g * k * d[0] * ra;
            for (int i = 1; i < 4; i++) {
                float b = g * d[i];
                ra = 1.f / (1.f + b);
                pp[i] = (-f[i] + b * pp[i - 1]) * ra;
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
            if (big < 3e-4f || it >= MH_LADDER_ITER)
                break;
        }
        for (int i = 0; i < 4; i++)
            l->sig[i] = sg[i];
    }
    l->iters = it;

    if (!(y[3] > -64.f && y[3] < 64.f)) {
        mh_ota_init(l);
        for (int i = 0; i < 5; i++)
            out[i] = 0.f;
        return 0.f;
    }
    for (int i = 0; i < 4; i++) {
        l->s[i] = 2.f * y[i] - l->s[i];
        l->y[i] = y[i];
    }
    l->u = u;
    if (!newton) {
        l->sig[0] = mh_tanh_x(u - y[0]);
        for (int i = 1; i < 4; i++)
            l->sig[i] = mh_tanh_x(y[i - 1] - y[i]);
    }
    out[0] = u;
    out[1] = y[0];
    out[2] = y[1];
    out[3] = y[2];
    out[4] = y[3];
    return y[3];
}
