/* Monohull: the LFO. Bipolar output in [-1, 1]. */
#ifndef MH_LFO_H
#define MH_LFO_H

#include <stdint.h>

enum { MH_LFO_TRI, MH_LFO_SINE, MH_LFO_SAW_UP, MH_LFO_SAW_DOWN, MH_LFO_SQUARE, MH_LFO_SH, MH_LFO_SMOOTH,
       MH_LFO_SHAPES };

typedef struct {
    float ph;
    float held, from, to; /* sample & hold and smooth random */
    uint32_t rng;
    float fade; /* delay and fade-in envelope, [0, 1] */
    float delay_left;
} mh_lfo;

void mh_lfo_init(mh_lfo *l, uint32_t seed);
void mh_lfo_reset(mh_lfo *l); /* key reset: phase 0, delay restarts */

/* One sample; `inc` = rate / sample rate. The delay and fade-in are mh_lfo_fade's. */
float mh_lfo_step(mh_lfo *l, float inc, int shape);

/* The delay-then-fade-in gain: `delay_inc` > 0 counts the delay down (1 / delay samples),
   `fade_inc` ramps the gain up (1 / fade samples). */
float mh_lfo_fade(mh_lfo *l, float delay_inc, float fade_inc);

#endif
