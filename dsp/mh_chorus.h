/* Monohull: the chorus, at the output rate, mono in, stereo out.

   JUNO: the Juno-60 / 106's MN3009 bucket-brigade chorus as Ultramaster KR-106 documents it
   (docs/DSP_ARCHITECTURE.md there): 3.5 ms centre, triangle LFOs (mode I 0.513 Hz +-0.5 ms,
   mode II 0.863 Hz +-1.1 ms, I+II both), left and right taps 180 degrees apart, 4-point Hermite
   reads, the BBD's bandwidth as a one-pole at stages / (4 delay), 15 kHz Butterworth pre- and
   post-filters, the charge soft-compressed above +-0.7, mix 1.2 (0.4 dry + 0.6 wet).

   ENSEMBLE: three taps around 7 ms whose slow LFOs are 120 degrees apart, each with a faster
   shimmer, panned left, centre and right (the tri-phase arrangement of string ensembles). */
#ifndef MH_CHORUS_H
#define MH_CHORUS_H

enum { MH_CHORUS_OFF, MH_CHORUS_JUNO_I, MH_CHORUS_JUNO_II, MH_CHORUS_JUNO_I_II, MH_CHORUS_ENSEMBLE,
       MH_CHORUS_MODES };

#define MH_CHORUS_LEN 1024 /* 23 ms at 44.1 kHz */

typedef struct {
    float b0, b1, b2, a1, a2, z1, z2;
} mh_biquad;

typedef struct {
    float buf[MH_CHORUS_LEN];
    unsigned w;
    float rate;
    float ph[3];          /* LFO phases */
    float fast_ph;
    mh_biquad pre, post[2];
    float bw[4];          /* the BBD bandwidth one-poles' states */
} mh_chorus;

void mh_chorus_init(mh_chorus *c, float rate);

/* `depth` scales the modulation (1 = the original), `mix` in [0, 1] fades the effect in
   (1 = the original's balance). */
void mh_chorus_step(mh_chorus *c, int mode, float depth, float mix, float in_l, float in_r, float *out_l,
                    float *out_r);

#endif
