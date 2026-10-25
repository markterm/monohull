/* Monohull: the OTA cascade, 24 dB/oct, run at the 2x rate: Roland's IR3109 (Jupiter-8, Juno-6/60/106,
   SH-101) and its kind. Four OTA-C one-poles, each an OTA driving a capacitor with its own output
   fed back to the OTA's inverting input:

       dy_i/dt = wc tanh(y_(i-1) - y_i),  the first driven by u = x - k y_4

   where the transistor ladder (mh_ladder.h) has wc (tanh(y_(i-1)) - tanh(y_i)). Linear, the two
   are the same filter; driven, the OTA saturates the difference between a stage's input and
   output (a slew limit) rather than the signals themselves, which is why it sounds smoother
   and rounder than a ladder when pushed. The OTA model is Zavalishin's (The Art of VA Filter
   Design, the OTA one-pole) and D'Angelo's.

   The same TPT integrators, prewarp and two solves as the ladder (mh_ladder.h), sharing its
   state struct and mode mixes: the cheap solve with each tanh(a) replaced by its secant slope
   from the previous sample, then (High) Newton on the full system. */
#ifndef MH_OTA_H
#define MH_OTA_H

#include "mh_ladder.h"

/* Above k = 4 the OTA cascade self-oscillates flat, far more than the ladder (its stages'
   saturation is on the signal's differences, so as the oscillation grows each stage's effective
   transconductance falls): -94 cents at k 4.02, -203 at 4.1, -334 at 4.3, -410 at 4.48, the
   same at every cutoff (measured C4-C7 near the edge, C2-C7 above; ota_self_oscillation_pitch
   in host/tests/parts.rs). The fit -683 sqrt(e) + 134 e cents (e = k - 4) is within 0.3 cents
   of each; this is the cutoff multiplier that undoes it. */
static inline float mh_ota_tune(float k) {
    if (k <= 4.f)
        return 1.f;
    float e = k - 4.f;
    return mh_exp2((683.4f * mh_sqrt_approx(e) - 134.f * e) * (1.f / 1200.f));
}

void mh_ota_init(mh_ladder *l);

/* One sample, as mh_ladder_step: `out` gets u, y1, y2, y3, y4 (mh_ladder_mix takes them). */
float mh_ota_step(mh_ladder *l, float x, float g, float k, int newton, float out[5]);

#endif
