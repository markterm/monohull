#include "mh_reverb.h"

#include <string.h>

/* Rings' delay memory (rings/dsp/fx/reverb.h): ten lines, each `len` long at `base` (one free
   slot between lines, as FxEngine's DelayLine lays them out) */
enum {
    L_AP1 = 150, L_AP2 = 214, L_AP3 = 319, L_AP4 = 527, L_DAP1A = 2182, L_DAP1B = 2690, L_DEL1 = 4501,
    L_DAP2A = 2525, L_DAP2B = 2197, L_DEL2 = 6312,
    B_AP1 = 0,
    B_AP2 = B_AP1 + L_AP1 + 1,
    B_AP3 = B_AP2 + L_AP2 + 1,
    B_AP4 = B_AP3 + L_AP3 + 1,
    B_DAP1A = B_AP4 + L_AP4 + 1,
    B_DAP1B = B_DAP1A + L_DAP1A + 1,
    B_DEL1 = B_DAP1B + L_DAP1B + 1,
    B_DAP2A = B_DEL1 + L_DEL1 + 1,
    B_DAP2B = B_DAP2A + L_DAP2A + 1,
    B_DEL2 = B_DAP2B + L_DAP2B + 1,
};
#define MASK (MH_REVERB_SIZE - 1)

/* FxEngine's FORMAT_16_BIT */
static inline float decompress(uint16_t v) {
    return (float)(int16_t)v / 32768.0f;
}

static inline uint16_t compress(float x) {
    int32_t v = (int32_t)(x * 32768.0f);
    return (uint16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

/* stmlib's CosineOscillator, COSINE_OSCILLATOR_APPROXIMATE */
static void lfo_init(mh_reverb_lfo *o, float frequency) {
    float sign = 16.0f;
    frequency -= 0.25f;
    if (frequency < 0.0f) {
        frequency = -frequency;
    } else {
        if (frequency > 0.5f)
            frequency -= 0.5f;
        else
            sign = -16.0f;
    }
    o->iir = sign * frequency * (1.0f - 2.0f * frequency);
    o->y1 = o->iir * 0.25f;
    o->y0 = 0.5f;
}

static inline float lfo_next(mh_reverb_lfo *o) {
    float temp = o->y0;
    o->y0 = o->iir * o->y0 - o->y1;
    o->y1 = temp;
    return temp + 0.5f;
}

void mh_reverb_clear(mh_reverb *r) {
    memset(r->buf, 0, sizeof r->buf);
    r->write_ptr = 0;
}

void mh_reverb_init(mh_reverb *r, float rate) {
    memset(r, 0, sizeof *r);
    mh_reverb_clear(r);
    /* SetLFOFrequency: the oscillator steps once every 32 samples */
    lfo_init(&r->lfo[0], 0.5f / rate * 32.0f);
    lfo_init(&r->lfo[1], 0.3f / rate * 32.0f);
    r->lp = 0.7f;
    r->diffusion = 0.625f;
}

void mh_reverb_process(mh_reverb *r, float *left, float *right, int n) {
    const float kap = r->diffusion, klp = r->lp, krt = r->reverb_time, amount = r->amount, gain = r->input_gain;
    float lp_1 = r->lp_decay_1, lp_2 = r->lp_decay_2;
    uint16_t *buf = r->buf;
    /* FxEngine's Context, one sample at a time: the accumulator and the last value read */
    float acc, prev, lfo0, lfo1;
    int32_t wp;
#define TAIL(base, len) buf[(wp + (base) + (len) - 1) & MASK]
#define HEAD(base) buf[(wp + (base)) & MASK]
#define READ(base, len, scale) (prev = decompress(TAIL(base, len)), acc += prev * (scale))
#define WRITE_AP(base, scale) (HEAD(base) = compress(acc), acc *= (scale), acc += prev)
#define INTERP(base, offset, lfo, amplitude, scale)                                                    \
    do {                                                                                              \
        float o_ = (offset) + (amplitude) * (lfo);                                                    \
        int32_t i_ = (int32_t)o_;                                                                     \
        float f_ = o_ - (float)i_;                                                                    \
        float a_ = decompress(buf[(wp + i_ + (base)) & MASK]);                                        \
        float b_ = decompress(buf[(wp + i_ + (base) + 1) & MASK]);                                    \
        float x_ = a_ + (b_ - a_) * f_;                                                               \
        prev = x_;                                                                                    \
        acc += x_ * (scale);                                                                          \
    } while (0)
    for (int i = 0; i < n; i++) {
        float wet, apout;
        /* Start */
        if (--r->write_ptr < 0)
            r->write_ptr += MH_REVERB_SIZE;
        wp = r->write_ptr;
        acc = 0.0f;
        prev = 0.0f;
        if ((wp & 31) == 0) {
            lfo0 = lfo_next(&r->lfo[0]);
            lfo1 = lfo_next(&r->lfo[1]);
        } else {
            lfo0 = r->lfo[0].y1 + 0.5f;
            lfo1 = r->lfo[1].y1 + 0.5f;
        }

        acc += (left[i] + right[i]) * gain;

        /* diffuse through 4 allpasses */
        READ(B_AP1, L_AP1, kap);
        WRITE_AP(B_AP1, -kap);
        READ(B_AP2, L_AP2, kap);
        WRITE_AP(B_AP2, -kap);
        READ(B_AP3, L_AP3, kap);
        WRITE_AP(B_AP3, -kap);
        READ(B_AP4, L_AP4, kap);
        WRITE_AP(B_AP4, -kap);
        apout = acc;

        /* the main loop */
        acc = apout;
        INTERP(B_DEL2, 6261.0f, lfo1, 50.0f, krt);
        lp_1 += klp * (acc - lp_1);
        acc = lp_1;
        READ(B_DAP1A, L_DAP1A, -kap);
        WRITE_AP(B_DAP1A, kap);
        READ(B_DAP1B, L_DAP1B, kap);
        WRITE_AP(B_DAP1B, -kap);
        HEAD(B_DEL1) = compress(acc);
        acc *= 2.0f;
        wet = acc;
        left[i] += (wet - left[i]) * amount;

        acc = apout;
        INTERP(B_DEL1, 4460.0f, lfo0, 40.0f, krt);
        lp_2 += klp * (acc - lp_2);
        acc = lp_2;
        READ(B_DAP2A, L_DAP2A, kap);
        WRITE_AP(B_DAP2A, -kap);
        READ(B_DAP2B, L_DAP2B, -kap);
        WRITE_AP(B_DAP2B, kap);
        HEAD(B_DEL2) = compress(acc);
        acc *= 2.0f;
        wet = acc;
        right[i] += (wet - right[i]) * amount;
    }
#undef TAIL
#undef HEAD
#undef READ
#undef WRITE_AP
#undef INTERP
    r->lp_decay_1 = lp_1;
    r->lp_decay_2 = lp_2;
}
