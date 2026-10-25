#include "mh_svf.h"

#include "mh_math.h"

void mh_svf_init(mh_svf *f) {
    f->ic1 = f->ic2 = 0.f;
}

float mh_svf_step(mh_svf *f, float x, float g, float res, float morph) {
    float k = 2.f - 1.98f * mh_clamp(res, 0.f, 1.f);
    float a1 = 1.f / (1.f + g * (g + k));
    float a2 = g * a1, a3 = g * a2;
    float v3 = x - f->ic2;
    float v1 = a1 * f->ic1 + a2 * v3; /* band-pass */
    float v2 = f->ic2 + a2 * f->ic1 + a3 * v3; /* low-pass */
    /* the OTA's limit: the band-pass state saturates softly around +-2 */
    f->ic1 = 2.f * mh_tanh(0.5f * (2.f * v1 - f->ic1));
    f->ic2 = 2.f * v2 - f->ic2;
    if (!(f->ic2 > -64.f && f->ic2 < 64.f))
        mh_svf_init(f);
    float lp = v2, bp = v1, hp = x - k * v1 - v2, notch = x - k * v1;
    morph = mh_clamp(morph, 0.f, 3.f);
    if (morph < 1.f)
        return lp + morph * (bp - lp);
    if (morph < 2.f)
        return bp + (morph - 1.f) * (hp - bp);
    return hp + (morph - 2.f) * (notch - hp);
}
