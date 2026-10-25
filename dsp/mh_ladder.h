/* Monohull: the transistor ladder, 24 dB/oct, run at the 2x rate.

   The model is the one Stinchcombe analyses and Miller Puckette's bob~ integrates: four stages
   dy_i/dt = wc (tanh(y_(i-1)) - tanh(y_i)), the first driven by tanh(x - k y_4). Each stage is
   a trapezoidal (TPT) integrator with the cutoff prewarped, so the filter stays in tune and
   self-oscillates at its cutoff.

   Two solves, one code path:
   - cheap (Efficient): each tanh replaced by its secant slope tanh(v)/v from the previous
     sample (Teemu Voipio's method); the system is then linear and solved exactly, feedback
     included, with no iteration.
   - Newton (High): the cheap solve as the first guess, then Newton-Raphson on the full
     nonlinear system until it converges (usually 1-2 steps, at most MH_LADDER_ITER). */
#ifndef MH_LADDER_H
#define MH_LADDER_H

#include "mh_math.h"

#define MH_LADDER_ITER 3

/* Above k = 4 the tanh ladder self-oscillates flat by an amount that depends on k alone, the
   same at every cutoff (measured C2-C7: -5 cents at k 4.05, -10 at 4.1, -28 at 4.3, -43 at
   4.5). This is the cutoff multiplier that puts self-oscillation back in tune; 1 up to k = 4. */
static inline float mh_ladder_tune(float k) {
    if (k <= 4.f)
        return 1.f;
    float e = k - 4.f;
    return mh_exp2((107.4f * e - 43.5f * e * e) * (1.f / 1200.f));
}

typedef struct {
    float s[4]; /* integrator states */
    float y[4]; /* stage outputs, last sample */
    float u;    /* the feedback node x - k y4, last sample */
    float sig[5]; /* tanh(v) / v of u and the stage outputs, last sample: the next one's slopes */
    int iters;  /* Newton steps taken last sample (for tests and the load meter) */
} mh_ladder;

void mh_ladder_init(mh_ladder *l);

/* One sample. `g` = tan(pi fc / fs), `k` the feedback (4 = the edge of self-oscillation in the
   linear limit). `newton` picks the solve. `out` gets u, y1, y2, y3, y4 (for the mode mixes);
   returns y4. */
float mh_ladder_step(mh_ladder *l, float x, float g, float k, int newton, float out[5]);

/* The Oberheim Xpander's mixes of the stage outputs, blended by `morph` in [0, 3]:
   LP24 (0) -> LP12 (1) -> BP12 (2) -> HP24 (3). */
float mh_ladder_mix(const float out[5], float morph);

#endif
