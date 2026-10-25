#include "mh_lfo.h"

#include "mh_math.h"

void mh_lfo_init(mh_lfo *l, uint32_t seed) {
    l->ph = 0.f;
    l->rng = seed ? seed : 1;
    l->held = l->from = 0.f;
    l->to = mh_randf(&l->rng);
    l->fade = 1.f;
    l->delay_left = 0.f;
}

void mh_lfo_reset(mh_lfo *l) {
    l->ph = 0.f;
    l->fade = 0.f;
    l->delay_left = 1.f;
}

float mh_lfo_step(mh_lfo *l, float inc, int shape) {
    float p = l->ph + inc;
    if (p >= 1.f) {
        p -= 1.f;
        l->held = mh_randf(&l->rng);
        l->from = l->to;
        l->to = mh_randf(&l->rng);
    }
    l->ph = p;
    switch (shape) {
    case MH_LFO_SINE:
        return mh_sin2pi(p);
    case MH_LFO_SAW_UP:
        return 2.f * p - 1.f;
    case MH_LFO_SAW_DOWN:
        return 1.f - 2.f * p;
    case MH_LFO_SQUARE:
        return p < 0.5f ? 1.f : -1.f;
    case MH_LFO_SH:
        return l->held;
    case MH_LFO_SMOOTH: {
        float c = 0.5f - 0.5f * mh_sin2pi(p + 0.25f); /* raised cosine, 0 -> 1 */
        return l->from + c * (l->to - l->from);
    }
    default:
        return p < 0.5f ? 4.f * p - 1.f : 3.f - 4.f * p;
    }
}

float mh_lfo_fade(mh_lfo *l, float delay_inc, float fade_inc) {
    if (l->delay_left > 0.f) {
        l->delay_left -= delay_inc;
        return 0.f;
    }
    if (l->fade < 1.f) {
        l->fade += fade_inc;
        if (l->fade > 1.f)
            l->fade = 1.f;
    }
    return l->fade;
}
