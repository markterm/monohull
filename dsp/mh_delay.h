/* Monohull: the delay. A ping-pong pair of lines, the input (both channels summed) into the left,
   the left's echo into the right, the right's back into the left: echoes alternate sides, each
   one feedback down. Every pass through a line goes through a tone low-pass, a 100 Hz high-pass
   (repeats thin out rather than boom) and stmlib's soft limiter (repeats saturate gently, and
   full feedback stays bounded), as an analogue delay's repeats darken and soften. Lines in 16-bit
   memory (about -96 dB of noise, below the repeats' own), read with 4-point Hermite
   interpolation; a change of time glides, as a tape's speed does. */
#ifndef MH_DELAY_H
#define MH_DELAY_H

#include <stdint.h>

#define MH_DELAY_LEN 48000 /* samples a line: 1.09 s at the FM-1's rate */

typedef struct {
    int16_t line[2][MH_DELAY_LEN];
    int w;          /* the slot written this sample */
    float t;        /* the delay now, samples (gliding to the target) */
    float lp[2], hp[2];
    float peak;     /* the echoes' peak over the last call: a tail still ringing */
} mh_delay;

void mh_delay_init(mh_delay *d);

/* In place, `n` frames: echoes of `target` samples (clamped to the lines), `feedback` in [0, 1),
   added at `mix`. `lp_a`, `hp_a`: one-pole coefficients of the repeats' tone filters; `glide`:
   the time's one-pole coefficient per sample; `snap` jumps the time to the target. */
void mh_delay_process(mh_delay *d, float *l, float *r, int n, float target, float feedback, float mix, float lp_a,
                      float hp_a, float glide, int snap);

#endif
