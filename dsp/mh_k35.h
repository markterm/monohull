/* Monohull: the Sallen-Key filter, 12 dB/oct, run at the 2x rate: Korg's KORG-35 (the early
   MS-20's) as Will Pirkle's zero-delay-feedback model has it, in the adaptation Surge ships
   (sst-filters' K35Filter.h, from Odin 2's Korg35Filter; GPL-3.0), ported to scalar C with its
   arithmetic unchanged. Two TPT one-poles and a one-pole in the feedback; the loop's signal
   through a tanh, which is where the MS-20 gets its bite when the peak is up. Linear, the
   low-pass is w^2 / (s^2 + (2 - K) w s + w^2), a two-pole of Q = 1 / (2 - K), and the high-pass
   s (s + w) / (the same): the MS-20's 6 dB/oct high-pass with the same peak. K is held to
   Surge's 0.01-1.96 (the edge is 2). */
#ifndef MH_K35_H
#define MH_K35_H

typedef struct {
    float lz, hz, z2; /* LPF1, HPF1 and the second filter's states (Surge's k35_lz, k35_hz, k35_2z) */
} mh_k35;

void mh_k35_init(mh_k35 *f);

/* One sample. `g` = tan(pi fc / fs), `k` the feedback (clamped to 0.01-1.96). The tanh's
   input is scaled by Surge's saturation, here 1 (the drive before the filter sets the level). */
float mh_k35_lp(mh_k35 *f, float x, float g, float k);
float mh_k35_hp(mh_k35 *f, float x, float g, float k);

#endif
