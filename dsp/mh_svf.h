/* Monohull: the 12 dB/oct state-variable filter (Oberheim SEM character), run at the 2x rate.
   Andy Simper's linear trapezoidal SVF (Cytomic), with the band-pass integrator's state
   soft-limited the way an OTA limits its current, which keeps high resonance well-behaved. */
#ifndef MH_SVF_H
#define MH_SVF_H

typedef struct {
    float ic1, ic2;
} mh_svf;

void mh_svf_init(mh_svf *f);

/* `g` = tan(pi fc / fs), `res` in [0, 1] (damping 2 -> 0.02). `morph` in [0, 3]:
   LP (0) -> BP (1) -> HP (2) -> notch (3). */
float mh_svf_step(mh_svf *f, float x, float g, float res, float morph);

#endif
