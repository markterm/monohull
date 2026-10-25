/* Monohull: the reverb. Mutable Instruments' (Emilie Gillet's) reverb from Rings, ported line for
   line to C: the Griesinger topology Dattorro's paper describes, four input allpasses into a loop
   of two allpass pairs and two long delays, its long delays swept by two slow LFOs. Rings' delay
   lengths (tuned at 48 kHz), its 16-bit delay memory and its LFO updated every 32 samples are kept
   as they are, so the port's output is Rings' to the bit (host/tests/fx.rs runs both).

   Copyright 2014 Emilie Gillet (MIT licence: LICENSES/MIT-Mutable-Instruments.txt); the port GPL-3.0 with
   the rest of Monohull. */
#ifndef MH_REVERB_H
#define MH_REVERB_H

#include <stdint.h>

#define MH_REVERB_SIZE 32768

typedef struct {
    float y1, y0, iir; /* stmlib's CosineOscillator (approximate) */
} mh_reverb_lfo;

typedef struct {
    uint16_t buf[MH_REVERB_SIZE];
    int32_t write_ptr;
    mh_reverb_lfo lfo[2];
    float amount, input_gain, reverb_time, diffusion, lp;
    float lp_decay_1, lp_decay_2;
} mh_reverb;

/* Rings' Init with its LFOs at 0.5 and 0.3 Hz of `rate` (Rings' own is 48 kHz). */
void mh_reverb_init(mh_reverb *r, float rate);

/* Rings' setters: amount (dry -> wet crossfade), input gain (Rings: 0.2), time (the loop's gain,
   Rings: 0.35 + 0.63 x), diffusion (Rings: 0.625), lp (the loop's damping, Rings: 0.3 + 0.6 x). */
static inline void mh_reverb_set(mh_reverb *r, float amount, float input_gain, float time, float diffusion,
                                 float lp) {
    r->amount = amount;
    r->input_gain = input_gain;
    r->reverb_time = time;
    r->diffusion = diffusion;
    r->lp = lp;
}

/* In place, `n` frames. */
void mh_reverb_process(mh_reverb *r, float *left, float *right, int n);

void mh_reverb_clear(mh_reverb *r);

#endif
