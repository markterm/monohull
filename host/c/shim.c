/* The host harness's view of the DSP: heap-allocated objects and extern wrappers for the
   inline functions, so Rust can reach them without mirroring the C structs. */
#include <math.h>
#include <stdlib.h>

#include "mh_env.h"
#include "mh_ladder.h"
#include "mh_math.h"
#include "mh_osc.h"
#include "mh_params.h"
#include "mh_resample.h"
#include "mh_svf.h"
#include "mh_synth.h"

/* ----- synth */

mh_synth *mhh_synth_new(float rate, uint32_t seed) {
    mh_synth *s = malloc(sizeof *s);
    mh_synth_init(s, rate, seed);
    return s;
}

void mhh_synth_free(mh_synth *s) { free(s); }

int mhh_set(mh_synth *s, const char *name, float v) {
    int i = mh_param_find(name);
    if (i < 0)
        return 0;
    mh_param_set(mh_synth_patch(s), i, v);
    return 1;
}

float mhh_get(mh_synth *s, const char *name) {
    return mh_param_get(mh_synth_patch(s), mh_param_find(name));
}

int mhh_cc(mh_synth *s, int cc, int value) { return mh_param_cc(mh_synth_patch(s), cc, value); }

void mhh_matrix(mh_synth *s, int slot, int src, int dst, float amount) {
    mh_patch *p = mh_synth_patch(s);
    if (slot < 0 || slot >= MH_MATRIX_SLOTS)
        return;
    p->matrix[slot].src = src;
    p->matrix[slot].dst = dst;
    p->matrix[slot].amount = amount;
}

void mhh_note_on(mh_synth *s, int note, float vel) { mh_synth_note_on(s, note, vel); }
void mhh_note_off(mh_synth *s, int note) { mh_synth_note_off(s, note); }
void mhh_all_off(mh_synth *s) { mh_synth_all_notes_off(s); }
void mhh_bend(mh_synth *s, float v) { mh_synth_bend(s, v); }
void mhh_wheel(mh_synth *s, float v) { mh_synth_wheel(s, v); }
void mhh_sustain(mh_synth *s, int down) { mh_synth_sustain(s, down); }
void mhh_limits(mh_synth *s, int unison, int stereo, int chorus) { mh_synth_set_limits(s, unison, stereo, chorus, 1); }
void mhh_fx_limit(mh_synth *s, int fx) { s->lim_fx = fx; }
void mhh_x4(mh_synth *s, int on) { mh_synth_set_filter_x4(s, on); }
void mhh_render(mh_synth *s, float *l, float *r, int n) { mh_synth_render(s, l, r, n); }
void mhh_clock(mh_synth *s) { mh_synth_clock(s); }
void mhh_transport(mh_synth *s, int run) { mh_synth_transport(s, run); }
uint32_t mhh_newton_steps(const mh_synth *s) { return s->newton_steps[0] + s->newton_steps[1]; }
uint32_t mhh_filter_samples(const mh_synth *s) { return s->filter_samples[0] + s->filter_samples[1]; }
void mhh_reset_counters(mh_synth *s) {
    s->newton_steps[0] = s->newton_steps[1] = s->filter_samples[0] = s->filter_samples[1] = 0;
}
int mhh_param_count(void) { return mh_param_count; }
const char *mhh_param_name(int i) { return i >= 0 && i < mh_param_count ? mh_params[i].name : 0; }

/* the firmware's factory patches, loaded as the firmware does (the player's quality kept) */
#include "../../firmware/presets.h"

int mhh_preset_count(void) { return (int)NPRESETS; }
const char *mhh_preset_name(int n) { return n >= 0 && n < (int)NPRESETS ? PRESETS[n].title : 0; }

void mhh_preset_load(mh_synth *s, int n) {
    mh_patch p;
    mh_patch_default(&p);
    for (const preset_set_t *e = PRESETS[n].sets; e->name; e++)
        mh_param_set(&p, mh_param_find(e->name), e->value);
    p.quality = mh_synth_patch(s)->quality;
    mh_synth_set_patch(s, &p);
}

/* the stats above, since the last call: drive and filter deviation from linear (rms ratios),
   the drive's peak input (its knee is 1), the filter's input rms; then reset */
void mhh_stats(mh_synth *s, float out[4]) {
    out[0] = s->st_u2 > 0 ? (float)sqrt(s->st_du2 / s->st_u2) : 0.f;
    out[1] = s->st_y2 > 0 ? (float)sqrt(s->st_dy2 / s->st_y2) : 0.f;
    out[2] = s->st_upeak;
    out[3] = s->st_n ? (float)sqrt(s->st_in2 / s->st_n) : 0.f;
    s->st_u2 = s->st_du2 = s->st_y2 = s->st_dy2 = s->st_in2 = 0;
    s->st_upeak = 0.f;
    s->st_n = 0;
}

/* ----- effects */

#include "mh_reverb.h"

mh_reverb *mhh_reverb_new(float rate) {
    mh_reverb *r = malloc(sizeof *r);
    mh_reverb_init(r, rate);
    return r;
}
void mhh_reverb_free(mh_reverb *r) { free(r); }
void mhh_reverb_set(mh_reverb *r, float amount, float input_gain, float time, float diffusion, float lp) {
    mh_reverb_set(r, amount, input_gain, time, diffusion, lp);
}
void mhh_reverb_process(mh_reverb *r, float *l, float *rr, int n) { mh_reverb_process(r, l, rr, n); }

/* ----- math */

float mhh_exp2(float x) { return mh_exp2(x); }
float mhh_tanh(float x) { return mh_tanh(x); }
float mhh_tanh_x(float x) { return mh_tanh_x(x); }
float mhh_tan_pi(float f) { return mh_tan_pi(f); }
float mhh_log2(float x) { return mh_log2(x); }
float mhh_log1p01(float u) { return mh_log1p01(u); }
float mhh_sin2pi(float p) { return mh_sin2pi(p); }

/* ----- parts */

typedef struct {
    mh_osc o;
    mh_shape s;
    float curve; /* the saw's bend, c (mh_shape's bow is 2 c saw) */
    float soft;  /* the 303 square's soft edge, [0, 1] */
} mhh_osc;

mhh_osc *mhh_osc_new(int table, float phase) {
    mhh_osc *o = malloc(sizeof *o);
    mh_osc_init(&o->o, table, phase);
    mh_shape_set(&o->s, 0.f, 0.5f);
    o->curve = 0.f;
    o->soft = 0.f;
    return o;
}

void mhh_osc_soft(mhh_osc *o, float soft) { o->soft = soft; }

void mhh_osc_curve(mhh_osc *o, float c) { o->curve = c; }

void mhh_osc_free(mhh_osc *o) { free(o); }

float mhh_osc_step(mhh_osc *o, float shape, float pw, float inc, float sync_d, float *wrap_d, int sub_div,
                   float *sub) {
    mh_shape_set(&o->s, shape, pw);
    o->s.bow = 2.f * o->curve * o->s.saw;
    o->s.soft = o->soft;
    return mh_osc_step(&o->o, &o->s, inc, sync_d, wrap_d, sub_div, sub);
}

mh_halfband *mhh_hb_new(int four) {
    mh_halfband *h = malloc(sizeof *h);
    if (four)
        mh_halfband_init(h, mh_hb4, MH_HB4_N);
    else
        mh_halfband_init(h, mh_hb2, MH_HB2_N);
    return h;
}

void mhh_hb_free(mh_halfband *h) { free(h); }
float mhh_hb_down(mh_halfband *h, float a, float b) { return mh_halfband_down(h, a, b); }
void mhh_hb_up(mh_halfband *h, float x, float *out) { mh_halfband_up(h, x, out); }

mh_ladder *mhh_ladder_new(void) {
    mh_ladder *l = malloc(sizeof *l);
    mh_ladder_init(l);
    return l;
}

void mhh_ladder_free(mh_ladder *l) { free(l); }

float mhh_ladder_step(mh_ladder *l, float x, float g, float k, int newton, float *out5) {
    return mh_ladder_step(l, x, g, k, newton, out5);
}

int mhh_ladder_iters(const mh_ladder *l) { return l->iters; }
float mhh_ladder_tune(float k) { return mh_ladder_tune(k); }

mh_env *mhh_env_new(void) {
    mh_env *e = malloc(sizeof *e);
    mh_env_init(e);
    return e;
}

void mhh_env_free(mh_env *e) { free(e); }

void mhh_env_set(mh_env *e, float a, float d, float s, float r, int loop, float rate) {
    mh_env_set(e, a, d, s, r, loop, rate);
}

void mhh_env_gate(mh_env *e, int on) {
    if (on)
        mh_env_gate_on(e);
    else
        mh_env_gate_off(e);
}

float mhh_env_step(mh_env *e) { return mh_env_step(e); }
int mhh_env_stage(const mh_env *e) { return e->stage; }

/* ----- a pretend second core: 1 runs the job at once (before the caller's half), 2 at the join
   (after it), 3 never and reports a failure, so the caller runs it */
static int fake_mode;
static void (*fake_job)(void *);
static void *fake_arg;

static int fake_offload(void (*job)(void *), void *arg) {
    if (fake_mode == 1) {
        job(arg);
        return 1;
    }
    fake_job = job;
    fake_arg = arg;
    return 1;
}

static int fake_join(void) {
    if (fake_mode == 2)
        fake_job(fake_arg);
    return fake_mode == 3 ? -1 : 0;
}

void mhh_offload_mode(mh_synth *s, int mode) {
    fake_mode = mode;
    if (mode)
        mh_synth_set_offload(s, fake_offload, fake_join);
    else
        mh_synth_set_offload(s, 0, 0);
}

/* ----- the OTA cascade (mh_ladder's state) */

#include "mh_ota.h"

mh_ladder *mhh_ota_new(void) {
    mh_ladder *l = malloc(sizeof *l);
    mh_ota_init(l);
    return l;
}
float mhh_ota_step(mh_ladder *l, float x, float g, float k, int newton, float *out5) {
    return mh_ota_step(l, x, g, k, newton, out5);
}
float mhh_ota_tune(float k) { return mh_ota_tune(k); }

/* ----- the Sallen-Key */

#include "mh_k35.h"

mh_k35 *mhh_k35_new(void) {
    mh_k35 *f = malloc(sizeof *f);
    mh_k35_init(f);
    return f;
}
void mhh_k35_free(mh_k35 *f) { free(f); }
float mhh_k35_step(mh_k35 *f, float x, float g, float k, int hp) {
    return hp ? mh_k35_hp(f, x, g, k) : mh_k35_lp(f, x, g, k);
}

/* ----- the diode ladder */
#include "mh_diode.h"
mh_diode *mhh_diode_new(void) {
    mh_diode *d = malloc(sizeof *d);
    mh_diode_init(d);
    return d;
}
void mhh_diode_free(mh_diode *d) { free(d); }
float mhh_diode_step(mh_diode *d, float x, float fc, float rate, float k, float hp_g) {
    return mh_diode_step(d, x, fc, rate, k, hp_g);
}



