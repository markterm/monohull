/* Monohull: the synth. One voice: two oscillators (with unison copies), a sub, noise and ring
   modulation into a drive and a filter at twice the output rate, then the VCA, the chorus and
   the output stage. No allocation, no HAL: the firmware owns one mh_synth and calls
   mh_synth_render from its audio interrupt; the host build runs the same code for tests.

   Rates: oscillators, envelopes, LFOs, pitch and cutoff run per sample; the mod matrix and the
   panel's values update every MH_BLOCK samples and reach the audio as per-sample ramps. */
#ifndef MH_SYNTH_H
#define MH_SYNTH_H

#include <stdint.h>

#include "mh_chorus.h"
#include "mh_delay.h"
#include "mh_diode.h"
#include "mh_env.h"
#include "mh_k35.h"
#include "mh_ladder.h"
#include "mh_ota.h"
#include "mh_lfo.h"
#include "mh_osc.h"
#include "mh_resample.h"
#include "mh_reverb.h"
#include "mh_svf.h"

#define MH_BLOCK 16
#define MH_UNISON_MAX 7
#define MH_MATRIX_SLOTS 8
#define MH_STACK 16

enum { MH_QUALITY_HIGH, MH_QUALITY_EFFICIENT };
enum { MH_FILTER_LADDER, MH_FILTER_SVF, MH_FILTER_DIODE, MH_FILTER_OTA, MH_FILTER_SK };
enum { MH_ARP_OFF, MH_ARP_UP, MH_ARP_DOWN, MH_ARP_UPDOWN, MH_ARP_RANDOM, MH_ARP_PLAYED, MH_ARP_MODES };
enum { MH_PRIO_LAST, MH_PRIO_LOW, MH_PRIO_HIGH };
/* CLEAN: the ideal waves; ACID: the pulse is the TB-303's square (mh_osc.h's MH_SQ303_*: its own
   width, the PW knob aside) */
enum { MH_MODEL_CLEAN, MH_MODEL_ACID, MH_MODELS };

enum {
    MH_SRC_NONE, MH_SRC_LFO1, MH_SRC_LFO2, MH_SRC_FENV, MH_SRC_AENV, MH_SRC_MENV, MH_SRC_VELOCITY,
    MH_SRC_KEY, MH_SRC_WHEEL, MH_SRC_AFTERTOUCH, MH_SRC_BEND, MH_SRC_RANDOM, MH_SRC_COUNT
};

/* Each destination's full scale for an amount of 1. */
enum {
    MH_DST_NONE,
    MH_DST_PITCH,     /* both oscillators, 24 semitones */
    MH_DST_PITCH1,    /* 24 semitones */
    MH_DST_PITCH2,    /* 24 semitones */
    MH_DST_PW1,       /* 0.5 */
    MH_DST_PW2,       /* 0.5 */
    MH_DST_SHAPE1,    /* 1 */
    MH_DST_SHAPE2,    /* 1 */
    MH_DST_LEVEL1,    /* 1 */
    MH_DST_LEVEL2,    /* 1 */
    MH_DST_SUB,       /* 1 */
    MH_DST_NOISE,     /* 1 */
    MH_DST_DRIVE,     /* 1 */
    MH_DST_CUTOFF,    /* 8 octaves */
    MH_DST_RESONANCE, /* 1 */
    MH_DST_MORPH,     /* 3 */
    MH_DST_FM,        /* 4 */
    MH_DST_LFO1_RATE, /* 4 octaves */
    MH_DST_LFO2_RATE, /* 4 octaves */
    MH_DST_DETUNE,    /* 1 */
    MH_DST_CHORUS,    /* 1 (mix) */
    MH_DST_AMP,       /* 1 */
    MH_DST_COUNT
};

typedef struct {
    float attack, decay, sustain, release; /* seconds, seconds, [0, 1], seconds */
    int loop;
} mh_adsr;

typedef struct {
    float rate; /* Hz */
    int shape;  /* MH_LFO_* */
    float delay, fade; /* seconds */
    int key_reset;
} mh_lfo_params;

typedef struct {
    int src, dst;
    float amount; /* [-1, 1] */
} mh_mod_slot;

typedef struct {
    int quality; /* MH_QUALITY_* */

    /* oscillators: [0] = osc 1, [1] = osc 2 */
    float shape[2];  /* saw 0 -> pulse 1/3 -> triangle 2/3 -> sine 1 */
    float pw[2];     /* pulse width */
    float semi[2], fine[2]; /* semitones, cents */
    float level[2];
    int unison[2];   /* 1, 3, 5 or 7 */
    int model[2];    /* MH_MODEL_*: the oscillator's circuit */
    float detune;    /* [0, 1], the super saw's curve */
    float unison_mix; /* [0, 1]: centre voice down, side voices up */
    float spread;    /* [0, 1] */
    int stereo;      /* stereo unison: left and right stacks through two filters */
    float sub_level;
    int sub_oct;     /* 1 or 2 octaves below osc 1 */
    float noise_level;
    int noise_pink;
    float ring_level; /* osc 1 x osc 2 */
    float fm;        /* osc 2 -> osc 1 through-zero FM index */
    int sync;        /* osc 2 hard-synced to osc 1 */
    float vintage;   /* [0, 1]: pitch and cutoff drift and spread, jitter, osc 2's tracking, the
                        saw's bend, envelope variance; 0 is exact */

    /* drive and filter */
    float drive;     /* [0, 1] */
    int filter;      /* MH_FILTER_*: the transistor ladder, the SVF, the 303's diode ladder, the OTA
                        cascade (Roland's), the Sallen-Key (Korg's KORG-35) */
    float cutoff;    /* Hz */
    float resonance; /* [0, 1]; the ladder self-oscillates near the top */
    float morph;     /* ladder and OTA: LP24 0, LP12 1, BP 2, HP 3; SVF: LP 0, BP 1, HP 2, notch 3;
                        Sallen-Key: LP12 0 to HP6 3 */
    float key_track; /* [0, 1], 1 = one octave per octave */
    float bass_comp; /* [0, 1], the ladder's bass loss at high resonance made up */
    float fenv_amount; /* octaves, bipolar */
    float fenv_velocity; /* [0, 1] */

    mh_adsr fenv, aenv, menv;
    float amp_velocity; /* [0, 1] */
    int vca_exp;        /* exponential VCA response */

    mh_lfo_params lfo[2];
    float vibrato;      /* LFO 1 -> pitch, semitones (plus the mod wheel's share) */
    float wheel_vibrato; /* the mod wheel's vibrato, semitones at full wheel */
    float lfo2_cutoff;  /* LFO 2 -> cutoff, octaves */
    float lfo1_pwm;     /* LFO 1 -> both pulse widths */
    float menv_pitch2;  /* mod envelope -> osc 2, semitones (sync sweeps) */
    mh_mod_slot matrix[MH_MATRIX_SLOTS];

    float glide;        /* seconds (exponential: to within 1 %; linear: per octave) */
    int glide_linear;
    int glide_legato;   /* glide only between overlapping notes */
    int legato;         /* don't retrigger the envelopes on overlapping notes */
    int priority;       /* MH_PRIO_* */
    float bend_range;   /* semitones */

    /* the arpeggiator */
    int arp_mode;       /* MH_ARP_* (off: the keys play the voice) */
    int arp_oct;        /* 1-4 octaves */
    int arp_div;        /* 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 of a beat */
    float arp_gate;     /* of a step; 1 ties the steps (legato: they glide) */
    int arp_latch;      /* keys released keep playing until the next chord */
    int arp_accent;     /* every Nth step accented (the rest played softer), 0 = the keys' own velocity */
    float tempo;        /* BPM, when no MIDI clock comes */

    float hpf;          /* Hz, 0 = off */
    float volume;
    int chorus;         /* MH_CHORUS_* */
    float chorus_depth, chorus_mix;
    float reverb_mix, reverb_time, reverb_tone; /* [0, 1] each, over Rings' own ranges; 0 mix is off */
    float delay_mix;    /* [0, 1], the echoes' level; 0 is off */
    int delay_div;      /* MH_DELAY_DIVS: 1/16, 1/8T, 1/8, 1/8D, 1/4T, 1/4, 1/4D, 1/2 of a bar's beat */
    float delay_feedback; /* [0, 0.95] */
    float delay_tone;   /* [0, 1]: the repeats' low-pass, 1.5 to 12 kHz */
} mh_patch;

#define MH_DELAY_DIVS 8

/* A plain saw lead: the patch new slots start from. */
void mh_patch_default(mh_patch *p);

typedef struct {
    float cur, step;
} mh_ramp;

/* The patch values that glide when changed (a panel detent or a 7-bit CC is a step: through two
   ~15 ms poles it becomes a sweep). The cutoff glides in octaves. */
enum {
    MH_SM_CUT, MH_SM_RES, MH_SM_DRIVE, MH_SM_LEVEL1, MH_SM_LEVEL2, MH_SM_SUB, MH_SM_NOISE, MH_SM_RING,
    MH_SM_FM, MH_SM_SHAPE1, MH_SM_SHAPE2, MH_SM_PW1, MH_SM_PW2, MH_SM_FINE1, MH_SM_FINE2, MH_SM_DETUNE,
    MH_SM_UMIX, MH_SM_SPREAD, MH_SM_MORPH, MH_SM_FENV, MH_SM_KEYTRACK, MH_SM_BASSCOMP, MH_SM_CHMIX,
    MH_SM_CHDEPTH, MH_SM_VOLUME, MH_SM_VIB, MH_SM_WVIB, MH_SM_LFO2CUT, MH_SM_PWM, MH_SM_MENV2, MH_SM_RVMIX,
    MH_SM_RVTIME, MH_SM_RVTONE, MH_SM_DLMIX, MH_SM_DLFB, MH_SM_DLTONE,
    MH_SM_FX, /* the scheduler's effects switch, 0 or 1: the effects fade rather than cut */
    MH_SM_N
};

/* One control block's work (mh_synth_render): what the modulation computed per frame, the
   sources' and filters' buffers at the oscillators' and the 2x rate, and what the back half
   reads of the patch, kept with the block so the next block's control can't change it under the
   other core. */
typedef struct mh_block {
    int n, os, high, stereo;  /* frames; oscillator samples per frame; the quality; two filters */
    int x4;                   /* the filters at 4x (High, when the scheduler has room) */
    float inc[2][MH_BLOCK];   /* phase increments per oscillator sample, each frame */
    float lv[2][MH_BLOCK], lsub[MH_BLOCK], lnoise[MH_BLOCK], lring[MH_BLOCK], fm[MH_BLOCK];
    float g4[MH_BLOCK];       /* the cutoff prewarped for 4x */
    float g[MH_BLOCK], res[MH_BLOCK], morph[MH_BLOCK], gd[MH_BLOCK], comp[MH_BLOCK], amp[MH_BLOCK];
    float fc[MH_BLOCK];       /* the cutoff in Hz (the diode ladder takes it whole) */
    float cut_r;              /* the second filter's cutoff over the first's (Vintage) */
    float main[2 * MH_BLOCK], copy_l[2 * MH_BLOCK], copy_r[2 * MH_BLOCK];
    float fil[2][2 * MH_BLOCK]; /* the filters' inputs, then their outputs, at the 2x rate */
    int filter, cheap, chorus;
    float bass_comp, hp_a, chorus_depth, chorus_mix;
    int rv_want, dl_want; /* the patch has the effect on and the scheduler allows it */
    float rv_amount, rv_time, rv_lp, dl_mix, dl_fb, dl_lp_a, dl_target;
    float *outl, *outr;       /* where the back writes the block's frames */
    struct mh_synth_s *owner;
} mh_block;

/* Running a job on another core: start `job(arg)` there and return 1, or return 0 (the caller
   runs it); the join waits for it and returns 0, or nonzero if the other core failed it (the
   caller then runs it again: a job restarts cleanly from the state it was handed). */
typedef int (*mh_offload_fn)(void (*job)(void *), void *arg);
typedef int (*mh_join_fn)(void);

typedef struct mh_synth_s {
    float rate, rate2; /* output and filter (2x) rates */
    mh_patch p;
    uint32_t rng;

    /* notes */
    int stack[MH_STACK], nstack;
    int pedal[MH_STACK], npedal;
    int note, gate, sustain;
    float velocity, note_random;
    float pitch, target; /* octaves from A4 */
    float glide_coef, glide_step;
    float bend, wheel, aftertouch;
    float env_var[3];

    /* sources */
    mh_osc osc[2];
    mh_osc copy[2][MH_UNISON_MAX - 1];
    mh_shape shape[2];       /* the copies' blends */
    mh_shape main_shape[2];  /* the main pair's: the same, with the saw's bend */
    float osc2_last;
    int main_os; /* the main pair's rate over the filter's: 1 or 2 (FM and ring in High) */
    float drift[2][MH_UNISON_MAX][4]; /* target, pole 1, pole 2, the fast jitter */
    float note_cents[2][MH_UNISON_MAX]; /* the vintage note-on spread */
    float fdrift[2][3], fnote[2]; /* each filter's drift (as above) and note-on spread, octaves */
    float cut_r1;     /* the second filter's cutoff over the first's */
    float track2;     /* osc 2's tracking error, octaves per octave from middle C */
    float ratio[2][MH_UNISON_MAX], gain_l[2][MH_UNISON_MAX], gain_r[2][MH_UNISON_MAX];
    int ncopies[2];
    float pink[3];

    /* the 2x region */
    mh_halfband hb_main;  /* main pair 4x -> 2x */
    mh_halfband up[2];    /* Efficient: 1x -> 2x */
    mh_halfband down[2];  /* 2x -> 1x */
    mh_halfband drive_up[2], drive_down[2]; /* High: the drive at 4x */
    float adaa_x[2]; /* the drive's last input (its ADAA's state) */
    mh_ladder ladder[2];
    mh_svf svf[2];
    mh_diode diode[2];
    mh_k35 k35[2][2];   /* per channel, the low-pass and the high-pass (MORPH blends them) */
    float diode_hp_g, diode_hp_g4; /* the diode ladder's feedback high-pass (150 Hz) at 2x and 4x */

    /* the arpeggiator: the notes it plays (in the order played), the keys down, where it is */
    int arp_notes[MH_STACK], narp, arp_down, arp_was_on;
    float arp_vels[MH_STACK];
    int arp_step, arp_note;
    float arp_phase, arp_gate_left;
    /* MIDI clock: ticks waiting, ticks counted, samples per tick (smoothed), samples since the last */
    int clk_pending, clk_count;
    float clk_period, clk_age;

    /* control */
    mh_env fenv, aenv, menv;
    mh_lfo lfo[2];
    float lfo_out[2];
    int ctl_left;
    float sm[MH_SM_N][2], smv[MH_SM_N]; /* the gliding values' two poles; their outputs */
    float sm_a;
    int sm_snap; /* jump to the patch at the next control block (a note from silence) */
    float mod[MH_DST_COUNT];
    mh_ramp r_level[2], r_sub, r_noise, r_ring, r_gd, r_comp, r_res, r_morph, r_cut, r_fm, r_amp;
    float lfo_inc[2], lfo_delay_inc[2], lfo_fade_inc[2];
    float fenv_amount, vel_gain, hp_a, chorus_mix;
    int osc2_on, lfo_on[2];
    float drift_a, jitter_a, jitter_k;
    uint32_t drift_p; /* the chance per block of a new drift target, of 2^24 */
    int uni_n[2];
    float uni_mix[2], uni_norm[2];
    int idle;
    int stereo_on;

    /* output */
    mh_chorus chorus;
    float hp_s[2], dc_x[2], dc_y[2];
    int out_quiet, out_need; /* frames the output has been below -80 dBFS; how many it must be */

    /* the effects: each off (its memory clean or holding an old tail), clearing, or on */
    mh_delay delay;
    mh_reverb reverb;
    int dl_state, rv_state, dl_pos, rv_pos;
    float dl_target, dl_lp_a, dl_hp_a, dl_glide;

    /* the scheduler's limits */
    int lim_unison, lim_stereo, lim_chorus, lim_cheap, lim_fx, lim_x4;

    /* two blocks: the front of one is made while the other core runs the back of the other */
    mh_block blk[2];
    int blk_i, back_taken;
    mh_offload_fn offload;
    mh_join_fn join;

#ifdef MH_HOST_STATS
    mh_ladder twin_ota;
    mh_k35 twin_k35[2];
    /* the host's measure of how hard a patch works the nonlinearities: the drive's input against
       its own slope at 0, and the filter's output against a twin fed 1000x quieter (linear) */
    mh_ladder twin_ladder;
    mh_diode twin_diode;
    mh_svf twin_svf;
    double st_u2, st_du2, st_y2, st_dy2, st_in2;
    float st_upeak;
    uint32_t st_n;
#endif
    /* counters for tests and the load meter, per filter channel (each core writes its own) */
    uint32_t newton_steps[2], filter_samples[2];
    uint32_t prof_t; /* MH_PROFILE: the drive / filter split's clock */
} mh_synth;

/* Profiling builds (-DMH_PROFILE): time per stage of mh_synth_render in the firmware's clock
   (TIMER4 ticks; on fm1-emu ticks are instructions), read back from mh_prof. */
enum { MH_P_CONTROL, MH_P_MOD, MH_P_SOURCES, MH_P_DRIVE, MH_P_FILTER, MH_P_OUTPUT, MH_P_WAIT, MH_P_N };
#ifdef MH_PROFILE
extern uint32_t (*mh_prof_clock)(void);
extern volatile uint32_t mh_prof[MH_P_N];
#define MH_PROF_START(t) uint32_t t = mh_prof_clock()
#define MH_PROF_ADD(i, t) do { uint32_t n_ = mh_prof_clock(); mh_prof[i] += n_ - (t); (t) = n_; } while (0)
#else
#define MH_PROF_START(t) do { } while (0)
#define MH_PROF_ADD(i, t) do { } while (0)
#endif

void mh_synth_init(mh_synth *s, float rate, uint32_t seed);

/* The patch, edited in place; changes take effect at the next control block, ramped. */
mh_patch *mh_synth_patch(mh_synth *s);
void mh_synth_set_patch(mh_synth *s, const mh_patch *p);

void mh_synth_note_on(mh_synth *s, int note, float velocity);
void mh_synth_note_off(mh_synth *s, int note);
void mh_synth_all_notes_off(mh_synth *s);
void mh_synth_bend(mh_synth *s, float v);       /* [-1, 1] */
void mh_synth_wheel(mh_synth *s, float v);      /* [0, 1] */
void mh_synth_aftertouch(mh_synth *s, float v); /* [0, 1] */
void mh_synth_sustain(mh_synth *s, int down);

/* What the load scheduler allows: unison voices per oscillator (1-7), the stereo second
   filter, the chorus, the delay and reverb. The patch's own settings are kept; the lower of the
   two is used. */
void mh_synth_set_limits(mh_synth *s, int max_unison, int stereo, int chorus, int fx);

/* Let each block's back half (drive, filters, the output stage) run on another core while this
   one makes the next block's front (NULL: all here). */
void mh_synth_set_offload(mh_synth *s, mh_offload_fn offload, mh_join_fn join);

/* The scheduler's spare room: the filters at 4x in High (on by default; the scheduler turns it on
   only with room to spare, its last extra). */
void mh_synth_set_filter_x4(mh_synth *s, int on);

/* The scheduler's last resort: the ladder's cheap solve even in High (the oscillators, drive and
   everything else stay as they are). */
void mh_synth_set_cheap_filter(mh_synth *s, int cheap);

/* MIDI clock (24 a beat) and start / stop: the arpeggiator follows them while they come. */
void mh_synth_clock(mh_synth *s);
void mh_synth_transport(mh_synth *s, int run);

/* Render `n` frames (any n). */
void mh_synth_render(mh_synth *s, float *l, float *r, int n);

#endif
