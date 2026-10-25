#include "mh_diode.h"

#include <string.h>

#include "mh_math.h"

void mh_diode_init(mh_diode *d) {
    memset(d, 0, sizeof *d);
    d->sig = 1.f;
}

/* (I - c A) z = b for the ladder's matrix A (rows -2 2 . . / 1 -2 1 . / . 1 -2 1 / . . 1 -2):
   tridiagonal, by elimination down and back. */
static inline void solve(float c, const float b[4], float z[4]) {
    float d0 = 1.f + 2.f * c, off_up0 = -2.f * c; /* row 0: d0 z0 + off_up0 z1 */
    float lo = -c, di = 1.f + 2.f * c, up = -c;   /* rows 1..3 */
    float cp[4], dp[4];
    cp[0] = off_up0 / d0;
    dp[0] = b[0] / d0;
    for (int i = 1; i < 4; i++) {
        float m = di - lo * cp[i - 1];
        cp[i] = i < 3 ? up / m : 0.f;
        dp[i] = (b[i] - lo * dp[i - 1]) / m;
    }
    z[3] = dp[3];
    for (int i = 2; i >= 0; i--)
        z[i] = dp[i] - cp[i] * z[i + 1];
}

float mh_diode_step(mh_diode *d, float x, float fc, float rate, float k, float hp_g) {
    /* prewarped so the oscillation at sqrt 2 w maps onto fc: c = h w / 2 = tan(pi fc / fs) / sqrt 2 */
    float c = mh_tan_pi(mh_clamp(fc / rate, 0.f, 0.24f)) * 0.70710678f;
    float *y = d->y;
    /* the known side: (I + c A) y + c B u, with B = (2, 0, 0, 0) */
    float r[4];
    r[0] = y[0] + c * (-2.f * y[0] + 2.f * y[1]) + c * 2.f * d->u;
    r[1] = y[1] + c * (y[0] - 2.f * y[1] + y[2]);
    r[2] = y[2] + c * (y[1] - 2.f * y[2] + y[3]);
    r[3] = y[3] + c * (y[2] - 2.f * y[3]);
    /* y+ = z1 + z2 u+ */
    float z1[4], z2[4], e[4] = {2.f * c, 0.f, 0.f, 0.f};
    solve(c, r, z1);
    solve(c, e, z2);
    /* the feedback: u+ = x - k v+, v+ = hp(sig y4+) = a y4+ + beta (a TPT one-pole high-pass) */
    float ih = 1.f / (1.f + hp_g);
    float a = d->sig * ih, beta = -d->hp_s * ih;
    float u = (x - k * beta - k * a * z1[3]) / (1.f + k * a * z2[3]);
    for (int i = 0; i < 4; i++)
        y[i] = z1[i] + z2[i] * u;
    /* the high-pass's state, on what it was fed */
    float w = d->sig * y[3];
    float lp = (hp_g * w + d->hp_s) * ih;
    d->hp_s = 2.f * lp - d->hp_s;
    d->u = u;
    d->sig = mh_tanh_x(y[3]);
    if (!(y[3] > -64.f && y[3] < 64.f))
        mh_diode_init(d); /* only non-finite input gets here */
    return y[3];
}
