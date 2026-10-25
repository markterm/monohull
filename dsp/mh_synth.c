#include "mh_synth.h"

#include <string.h>

#include "mh_math.h"

/* The resonance knob to the share of the feedback that starts self-oscillation: a square law up
   to 0.97, the edge, then on to 12 % past it at the top. The last few percent of the knob are
   the whistle, not the last six. */
static inline float res_share(float r) {
    return r <= 0.97f ? r * r * (1.f / 0.9409f) : 1.f + 4.f * (r - 0.97f);
}

/* the ladder's feedback: self-oscillation starts at 4 */
static inline float ladder_k(float r) {
    return 4.f * res_share(r);
}
/* the output stage's gain from the filter to full scale */
#define OUT_GAIN 1.6f
/* after the amp envelope ends, this long before the engine stops rendering (the chorus tail) */
#define IDLE_AFTER 4096

/* Vintage at full: how far an analogue voice's parts wander. Each oscillator (every unison copy
   its own) drifts slowly, starts each note a little off and jitters fast by a fraction of a
   cent; osc 2 tracks a little flat going up (as an expo converter's bulk resistance makes it),
   from middle C where it is trimmed, so intervals beat faster up the keyboard; each filter
   drifts, starts each note a little off, and the stereo pair are mismatched by a fixed amount;
   the main pair's saws bend (V_CURVE, KR-106's capacitor curve). Osc 1 stays the reference: on
   average the voice is in tune. */
#define V_DRIFT_CENTS 4.f
#define V_SPREAD_CENTS 3.f
#define V_JITTER_CENTS 0.6f /* rms, ~15 Hz wide */
#define V_TRACK_CENTS -2.f  /* per octave */
#define V_FDRIFT_OCT 0.04f
#define V_FSPREAD_OCT 0.025f
#define V_FSPLIT_OCT 0.0075f /* each way */
#define V_CURVE 0.15f

#ifdef MH_PROFILE
uint32_t (*mh_prof_clock)(void);
volatile uint32_t mh_prof[MH_P_N];
#endif

/* Adam Szabo's measured JP-8000 super saw offsets (frequency ratio - 1 at full detune), less
   the centre voice; 5 and 3 voices take the outer pairs. */
static const float off7[6] = {-0.11002313f, -0.06288439f, -0.01952356f, 0.01991221f, 0.06216538f, 0.10745242f};
static const float off5[4] = {-0.11002313f, -0.06288439f, 0.06216538f, 0.10745242f};
static const float off3[2] = {-0.06288439f, 0.06216538f};

static const float dst_scale[MH_DST_COUNT] = {
    [MH_DST_PITCH] = 24.f, [MH_DST_PITCH1] = 24.f, [MH_DST_PITCH2] = 24.f, [MH_DST_PW1] = 0.5f,
    [MH_DST_PW2] = 0.5f, [MH_DST_SHAPE1] = 1.f, [MH_DST_SHAPE2] = 1.f, [MH_DST_LEVEL1] = 1.f,
    [MH_DST_LEVEL2] = 1.f, [MH_DST_SUB] = 1.f, [MH_DST_NOISE] = 1.f, [MH_DST_DRIVE] = 1.f,
    [MH_DST_CUTOFF] = 8.f, [MH_DST_RESONANCE] = 1.f, [MH_DST_MORPH] = 3.f, [MH_DST_FM] = 4.f,
    [MH_DST_LFO1_RATE] = 4.f, [MH_DST_LFO2_RATE] = 4.f, [MH_DST_DETUNE] = 1.f, [MH_DST_CHORUS] = 1.f,
    [MH_DST_AMP] = 1.f,
};

void mh_patch_default(mh_patch *p) {
    memset(p, 0, sizeof *p);
    p->quality = MH_QUALITY_HIGH;
    p->shape[0] = p->shape[1] = 0.f;
    p->pw[0] = p->pw[1] = 0.5f;
    p->level[0] = 1.f;
    p->level[1] = 0.f;
    p->unison[0] = p->unison[1] = 1;
    p->detune = 0.3f;
    p->unison_mix = 0.5f;
    p->spread = 0.7f;
    p->sub_oct = 1;
    p->vintage = 0.3f;
    p->drive = 0.2f;
    p->filter = MH_FILTER_LADDER;
    p->cutoff = 2000.f;
    p->resonance = 0.3f;
    p->key_track = 0.5f;
    p->fenv_amount = 2.f;
    p->fenv_velocity = 0.3f;
    p->fenv = (mh_adsr){0.002f, 0.4f, 0.3f, 0.3f, 0};
    p->aenv = (mh_adsr){0.002f, 0.5f, 0.8f, 0.2f, 0};
    p->menv = (mh_adsr){0.001f, 0.3f, 0.f, 0.3f, 0};
    p->amp_velocity = 0.5f;
    p->lfo[0] = (mh_lfo_params){5.5f, MH_LFO_TRI, 0.f, 0.f, 0};
    p->lfo[1] = (mh_lfo_params){0.5f, MH_LFO_TRI, 0.f, 0.f, 0};
    p->wheel_vibrato = 0.5f;
    p->glide = 0.f;
    p->legato = 1;
    p->priority = MH_PRIO_LAST;
    p->bend_range = 2.f;
    p->volume = 0.43f; /* SAW LEAD's, which is this patch: -15 LUFS, as every factory patch */
    p->chorus = MH_CHORUS_OFF;
    p->chorus_depth = 1.f;
    p->chorus_mix = 1.f;
    p->reverb_time = 0.5f;
    p->reverb_tone = 0.5f;
    p->delay_div = 3; /* 1/8 dotted */
    p->delay_feedback = 0.35f;
    p->delay_tone = 0.5f;
    p->arp_mode = MH_ARP_OFF;
    p->arp_oct = 1;
    p->arp_div = 3; /* 1/16 */
    p->arp_gate = 0.5f;
    p->tempo = 120.f;
}

static inline void ramp_to(mh_ramp *r, float target) {
    r->step = (target - r->cur) * (1.f / MH_BLOCK);
}

static inline void ramp_jump(mh_ramp *r, float v) {
    r->cur = v;
    r->step = 0.f;
}

static inline float ramp_next(mh_ramp *r) {
    return r->cur += r->step;
}

void mh_synth_init(mh_synth *s, float rate, uint32_t seed) {
    memset(s, 0, sizeof *s);
    s->rate = rate;
    s->rate2 = 2.f * rate;
    s->rng = seed ? seed : 0x1234567u;
    mh_patch_default(&s->p);
    for (int k = 0; k < 2; k++) {
        mh_osc_init(&s->osc[k], 1, 0.5f * (float)k);
        for (int j = 0; j < MH_UNISON_MAX - 1; j++)
            mh_osc_init(&s->copy[k][j], 0, 0.5f + 0.5f * mh_randf(&s->rng));
        mh_halfband_init(&s->up[k], mh_hb2, MH_HB2_N);
        mh_halfband_init(&s->down[k], mh_hb2, MH_HB2_N);
        mh_halfband_init(&s->drive_up[k], mh_hb4, MH_HB4_N);
        mh_halfband_init(&s->drive_down[k], mh_hb4, MH_HB4_N);
        mh_ladder_init(&s->ladder[k]);
        mh_svf_init(&s->svf[k]);
        mh_diode_init(&s->diode[k]);
        mh_k35_init(&s->k35[k][0]);
        mh_k35_init(&s->k35[k][1]);
#ifdef MH_HOST_STATS
        mh_ladder_init(&s->twin_ladder);
        mh_diode_init(&s->twin_diode);
        mh_svf_init(&s->twin_svf);
        mh_ota_init(&s->twin_ota);
        mh_k35_init(&s->twin_k35[0]);
        mh_k35_init(&s->twin_k35[1]);
#endif
        mh_lfo_init(&s->lfo[k], mh_rand(&s->rng));
    }
    mh_halfband_init(&s->hb_main, mh_hb4, MH_HB4_N);
    mh_env_init(&s->fenv);
    mh_env_init(&s->aenv);
    mh_env_init(&s->menv);
    mh_chorus_init(&s->chorus, rate);
    mh_delay_init(&s->delay);
    mh_reverb_init(&s->reverb, rate);
    s->dl_hp_a = 1.f - mh_exp2(-2.f * MH_PI * 100.f / rate * MH_LOG2E);
    s->dl_glide = 1.f - mh_exp2(-1.f / (0.08f * rate) * MH_LOG2E); /* a tape's speed change, ~80 ms */
    s->out_quiet = s->out_need = IDLE_AFTER;
    s->main_os = 1;
    s->note = 60;
    s->pitch = s->target = (60 - 69) / 12.f;
    s->env_var[0] = s->env_var[1] = s->env_var[2] = 1.f;
    s->lim_unison = MH_UNISON_MAX;
    s->lim_stereo = s->lim_chorus = s->lim_fx = s->lim_x4 = 1;
    s->idle = IDLE_AFTER;
    s->drift_a = 1.f - mh_exp2(-2.f * MH_PI * 0.5f * MH_BLOCK / rate * MH_LOG2E);
    s->drift_p = (uint32_t)(16777216.f * MH_BLOCK / (2.f * rate)); /* every 2 s on average */
    s->jitter_a = 1.f - mh_exp2(-2.f * MH_PI * 15.f * MH_BLOCK / rate * MH_LOG2E);
    /* uniform noise in [-1, 1] through a one-pole of coefficient a has rms sqrt(a / (3 (2 - a))) */
    s->jitter_k = V_JITTER_CENTS / mh_sqrt_approx(s->jitter_a / (3.f * (2.f - s->jitter_a)));
    s->cut_r1 = 1.f;
    s->sm_a = 1.f - mh_exp2(-MH_BLOCK / (0.015f * rate) * MH_LOG2E);
    s->sm_snap = 1;
    s->diode_hp_g = mh_tan_pi(150.f / s->rate2);
    s->diode_hp_g4 = mh_tan_pi(150.f / (2.f * s->rate2));
    s->arp_note = -1;
    s->clk_period = rate * 0.5f / 24.f;
    s->clk_age = 1e9f;
    s->uni_n[0] = s->uni_n[1] = -1;
}

mh_patch *mh_synth_patch(mh_synth *s) {
    return &s->p;
}

void mh_synth_set_patch(mh_synth *s, const mh_patch *p) {
    s->p = *p;
}

void mh_synth_set_filter_x4(mh_synth *s, int on) {
    s->lim_x4 = on;
}

void mh_synth_set_cheap_filter(mh_synth *s, int cheap) {
    s->lim_cheap = cheap;
}

void mh_synth_set_limits(mh_synth *s, int max_unison, int stereo, int chorus, int fx) {
    s->lim_unison = max_unison < 1 ? 1 : max_unison;
    s->lim_stereo = stereo;
    s->lim_chorus = chorus;
    s->lim_fx = fx;
}

/* ----- notes */

static int pick(const mh_synth *s) {
    int n = s->stack[s->nstack - 1];
    for (int i = 0; i < s->nstack; i++) {
        if (s->p.priority == MH_PRIO_LOW && s->stack[i] < n)
            n = s->stack[i];
        if (s->p.priority == MH_PRIO_HIGH && s->stack[i] > n)
            n = s->stack[i];
    }
    return n;
}

static void remove_note(int *list, int *n, int note) {
    for (int i = 0; i < *n; i++) {
        if (list[i] == note) {
            memmove(&list[i], &list[i + 1], (size_t)(*n - i - 1) * sizeof *list);
            (*n)--;
            return;
        }
    }
}

static void trigger(mh_synth *s) {
    float v = s->p.vintage;
    for (int e = 0; e < 3; e++)
        s->env_var[e] = 1.f + 0.08f * v * mh_randf(&s->rng);
    for (int k = 0; k < 2; k++)
        for (int j = 0; j < MH_UNISON_MAX; j++)
            s->note_cents[k][j] = V_SPREAD_CENTS * v * mh_randf(&s->rng);
    s->note_random = mh_randf(&s->rng);
    for (int ch = 0; ch < 2; ch++) /* (no draws at 0: the noise's sequence stays as it was) */
        s->fnote[ch] = v > 0.f ? V_FSPREAD_OCT * v * mh_randf(&s->rng) : 0.f;
    mh_env_gate_on(&s->fenv);
    mh_env_gate_on(&s->aenv);
    mh_env_gate_on(&s->menv);
    for (int k = 0; k < 2; k++)
        if (s->p.lfo[k].key_reset)
            mh_lfo_reset(&s->lfo[k]);
}

/* Move to `note`; `fresh` when no other key was held. */
static void go_to(mh_synth *s, int note, int fresh) {
    s->note = note;
    s->target = (float)(note - 69) * (1.f / 12.f);
    int glide = s->p.glide > 0.f && (!fresh || !s->p.glide_legato);
    if (!glide)
        s->pitch = s->target;
    if (fresh || !s->p.legato)
        trigger(s);
}

static void voice_on(mh_synth *s, int note, float velocity) {
    remove_note(s->stack, &s->nstack, note);
    remove_note(s->pedal, &s->npedal, note);
    if (s->nstack == MH_STACK)
        remove_note(s->stack, &s->nstack, s->stack[0]);
    s->stack[s->nstack++] = note;
    int fresh = !s->gate;
    if (fresh)
        s->velocity = mh_clamp(velocity, 0.f, 1.f);
    if (fresh && s->aenv.stage == MH_ENV_IDLE) /* from silence: the patch as it is, no glide */
        s->sm_snap = 1;
    s->gate = 1;
    s->idle = 0;
    int n = pick(s);
    if (fresh || n != s->note)
        go_to(s, n, fresh);
}

static void release(mh_synth *s, int note) {
    remove_note(s->stack, &s->nstack, note);
    if (s->nstack > 0) {
        int n = pick(s);
        if (n != s->note)
            go_to(s, n, 0);
        return;
    }
    s->gate = 0;
    mh_env_gate_off(&s->fenv);
    mh_env_gate_off(&s->aenv);
    mh_env_gate_off(&s->menv);
}

static void voice_off(mh_synth *s, int note) {
    if (s->sustain) {
        int held = 0;
        for (int i = 0; i < s->nstack; i++)
            held |= s->stack[i] == note;
        if (held && s->npedal < MH_STACK) {
            remove_note(s->pedal, &s->npedal, note);
            s->pedal[s->npedal++] = note;
        }
        return;
    }
    release(s, note);
}

/* ----- the arpeggiator */

static const float div_beats[6] = {1.f, 0.5f, 1.f / 3.f, 0.25f, 1.f / 6.f, 0.125f};
static const int div_ticks[6] = {24, 12, 8, 6, 4, 3};

static inline int arp_active(const mh_synth *s) {
    return s->p.arp_mode > MH_ARP_OFF && s->p.arp_mode < MH_ARP_MODES;
}

void mh_synth_note_on(mh_synth *s, int note, float velocity) {
    if (note < 0 || note > 127)
        return;
    if (!arp_active(s)) {
        voice_on(s, note, velocity);
        return;
    }
    if (s->p.arp_latch && s->arp_down == 0)
        s->narp = 0; /* a new chord replaces the latched one */
    s->arp_down++;
    for (int i = 0; i < s->narp; i++) {
        if (s->arp_notes[i] == note) {
            memmove(&s->arp_notes[i], &s->arp_notes[i + 1], (size_t)(s->narp - i - 1) * sizeof(int));
            memmove(&s->arp_vels[i], &s->arp_vels[i + 1], (size_t)(s->narp - i - 1) * sizeof(float));
            s->narp--;
            break;
        }
    }
    if (s->narp == MH_STACK)
        return;
    s->arp_notes[s->narp] = note;
    s->arp_vels[s->narp] = mh_clamp(velocity, 0.f, 1.f);
    s->narp++;
    if (s->narp == 1 && s->arp_note < 0) { /* the first key: at once, from the top of the pattern */
        s->arp_step = 0;
        s->arp_phase = 0.f;
        s->clk_count = 0;
    }
    s->idle = 0;
}

void mh_synth_note_off(mh_synth *s, int note) {
    if (!arp_active(s)) {
        voice_off(s, note);
        return;
    }
    if (s->arp_down > 0)
        s->arp_down--;
    if (s->p.arp_latch)
        return;
    for (int i = 0; i < s->narp; i++) {
        if (s->arp_notes[i] == note) {
            memmove(&s->arp_notes[i], &s->arp_notes[i + 1], (size_t)(s->narp - i - 1) * sizeof(int));
            memmove(&s->arp_vels[i], &s->arp_vels[i + 1], (size_t)(s->narp - i - 1) * sizeof(float));
            s->narp--;
            break;
        }
    }
}

void mh_synth_clock(mh_synth *s) {
    if (s->clk_age < s->rate) /* a period only between ticks a second apart or closer */
        s->clk_period += (s->clk_age - s->clk_period) * 0.1f;
    s->clk_age = 0.f;
    s->clk_pending++;
}

void mh_synth_transport(mh_synth *s, int run) {
    s->clk_count = 0;
    s->arp_step = 0;
    s->arp_phase = 0.f;
    if (!run && s->arp_note >= 0) {
        voice_off(s, s->arp_note);
        s->arp_note = -1;
    }
}

/* The pattern's next note and its velocity. */
static int arp_next(mh_synth *s, float *vel) {
    const mh_patch *p = &s->p;
    int n = s->narp, oct = p->arp_oct < 1 ? 1 : p->arp_oct > 4 ? 4 : p->arp_oct;
    int list[MH_STACK];
    float vl[MH_STACK];
    for (int i = 0; i < n; i++) {
        list[i] = s->arp_notes[i];
        vl[i] = s->arp_vels[i];
    }
    if (p->arp_mode != MH_ARP_PLAYED) { /* low to high */
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && list[j - 1] > list[j]; j--) {
                int t = list[j];
                list[j] = list[j - 1];
                list[j - 1] = t;
                float v = vl[j];
                vl[j] = vl[j - 1];
                vl[j - 1] = v;
            }
    }
    int len = n * oct, st = s->arp_step, idx;
    switch (p->arp_mode) {
    case MH_ARP_DOWN:
        idx = len - 1 - st % len;
        break;
    case MH_ARP_UPDOWN:
        if (len < 2) {
            idx = 0;
        } else {
            int q = st % (2 * len - 2);
            idx = q < len ? q : 2 * len - 2 - q;
        }
        break;
    case MH_ARP_RANDOM:
        idx = (int)(mh_rand(&s->rng) % (uint32_t)len);
        break;
    default:
        idx = st % len;
        break;
    }
    int note = list[idx % n] + 12 * (idx / n);
    *vel = vl[idx % n];
    if (p->arp_accent > 0) /* the 303's way: accented steps loud and bright, the rest held back */
        *vel = st % p->arp_accent == 0 ? 1.f : (*vel < 0.55f ? *vel : 0.55f);
    return note > 127 ? 127 : note;
}

/* The arpeggiator over the next `c` frames (its timing is to the control block). */
static void arp_run(mh_synth *s, int c) {
    const mh_patch *p = &s->p;
    int on = arp_active(s);
    if (on != s->arp_was_on) {
        if (on) { /* the keys held go to the arpeggiator from now on; the voice lets go */
            s->nstack = s->npedal = 0;
            if (s->gate) {
                s->gate = 0;
                mh_env_gate_off(&s->fenv);
                mh_env_gate_off(&s->aenv);
                mh_env_gate_off(&s->menv);
            }
        } else {
            if (s->arp_note >= 0)
                voice_off(s, s->arp_note);
            s->arp_note = -1;
            s->narp = s->arp_down = 0;
        }
        s->arp_was_on = on;
    }
    s->clk_age += (float)c;
    if (!on)
        return;
    int div = p->arp_div < 0 ? 0 : p->arp_div > 5 ? 5 : p->arp_div;
    int ext = s->clk_age < 0.5f * s->rate; /* MIDI clock in the last half second */
    float tempo = mh_clamp(p->tempo, 20.f, 300.f);
    float step = ext ? s->clk_period * (float)div_ticks[div] : s->rate * 60.f / tempo * div_beats[div];
    if (s->arp_note >= 0 && p->arp_gate < 1.f) {
        s->arp_gate_left -= (float)c;
        if (s->arp_gate_left <= 0.f) {
            voice_off(s, s->arp_note);
            s->arp_note = -1;
        }
    }
    if (s->narp == 0) {
        if (s->arp_note >= 0) {
            voice_off(s, s->arp_note);
            s->arp_note = -1;
        }
        s->clk_pending = 0;
        return;
    }
    int fire = 0;
    if (ext) {
        while (s->clk_pending > 0) {
            s->clk_pending--;
            if (s->clk_count++ % div_ticks[div] == 0)
                fire = 1;
        }
    } else {
        s->clk_pending = 0;
        s->arp_phase -= (float)c;
        if (s->arp_phase <= 0.f) {
            fire = 1;
            s->arp_phase += step;
            if (s->arp_phase <= 0.f)
                s->arp_phase = step;
        }
    }
    if (!fire)
        return;
    float vel;
    int note = arp_next(s, &vel), prev = s->arp_note;
    voice_on(s, note, vel); /* on top of a tied note: legato, it glides */
    if (prev >= 0 && prev != note)
        voice_off(s, prev);
    s->arp_note = note;
    s->arp_gate_left = mh_clamp(p->arp_gate, 0.02f, 1.f) * step;
    s->arp_step++;
}

void mh_synth_all_notes_off(mh_synth *s) {
    s->narp = s->arp_down = 0;
    s->arp_note = -1;
    s->nstack = s->npedal = 0;
    if (s->gate) {
        s->gate = 0;
        mh_env_gate_off(&s->fenv);
        mh_env_gate_off(&s->aenv);
        mh_env_gate_off(&s->menv);
    }
}

void mh_synth_sustain(mh_synth *s, int down) {
    s->sustain = down;
    if (!down) {
        while (s->npedal > 0) {
            int n = s->pedal[--s->npedal];
            release(s, n);
        }
    }
}

void mh_synth_bend(mh_synth *s, float v) {
    s->bend = mh_clamp(v, -1.f, 1.f);
}

void mh_synth_wheel(mh_synth *s, float v) {
    s->wheel = mh_clamp(v, 0.f, 1.f);
}

void mh_synth_aftertouch(mh_synth *s, float v) {
    s->aftertouch = mh_clamp(v, 0.f, 1.f);
}

/* ----- control rate */

/* A slow drift's control block: now and then (while `on`) a new random target, through two
   smoothing poles (~0.5 Hz). Returns it, in [-1, 1]. */
static float drift_step(mh_synth *s, float *d, int on) {
    if (on && (mh_rand(&s->rng) & 0xffffffu) < s->drift_p)
        d[0] = mh_randf(&s->rng);
    d[1] += (d[0] - d[1]) * s->drift_a;
    d[2] += (d[1] - d[2]) * s->drift_a;
    return d[2];
}

static void unison_setup(mh_synth *s, int k, float detune, float spread, float umix) {
    const mh_patch *p = &s->p;
    int n = p->unison[k] < s->lim_unison ? p->unison[k] : s->lim_unison;
    n = n >= 7 ? 7 : n >= 5 ? 5 : n >= 3 ? 3 : 1;
    const float *off = n == 7 ? off7 : n == 5 ? off5 : off3;
    s->ncopies[k] = n - 1;
    float mix = mh_clamp(umix, 0.f, 1.f);
    float c = n > 1 ? -0.55366f * mix + 0.99785f : 1.f;
    float side = -0.73764f * mix * mix + 1.2841f * mix + 0.044372f;
    if (n != s->uni_n[k] || mix != s->uni_mix[k]) {
        s->uni_n[k] = n;
        s->uni_mix[k] = mix;
        s->uni_norm[k] = 1.f / mh_sqrt_approx(c * c + (float)(n - 1) * side * side);
    }
    float norm = s->uni_norm[k];
    float amt = detune * (0.1f + 0.9f * detune * detune);
    s->ratio[k][0] = 1.f;
    s->gain_l[k][0] = s->gain_r[k][0] = c * norm;
    for (int j = 0; j < n - 1; j++) {
        s->ratio[k][j + 1] = 1.f + off[j] * amt;
        float pan = spread * off[j] * (1.f / 0.11f);
        s->gain_l[k][j + 1] = side * norm * (pan > 0.f ? 1.f - pan : 1.f);
        s->gain_r[k][j + 1] = side * norm * (pan < 0.f ? 1.f + pan : 1.f);
    }
    /* each voice's drift, its jitter (white noise through a one-pole, fresh each block) and its
       note-on spread; [0] is the main oscillator */
    int on = p->vintage > 0.f;
    for (int j = 0; j < n; j++) {
        float *d = s->drift[k][j];
        float slow = drift_step(s, d, on);
        if (on)
            d[3] += (mh_randf(&s->rng) - d[3]) * s->jitter_a;
        float cents = p->vintage * (V_DRIFT_CENTS * slow + s->jitter_k * d[3]) + s->note_cents[k][j];
        s->ratio[k][j] *= 1.f + cents * (MH_LN2 / 1200.f);
    }
}

/* The gliding values toward the patch, a control block on; at once while nothing sounds. */
static void smooth(mh_synth *s) {
    const mh_patch *p = &s->p;
    float t[MH_SM_N] = {
        [MH_SM_CUT] = mh_log2(mh_clamp(p->cutoff, 16.f, 20000.f) * (1.f / 440.f)),
        [MH_SM_RES] = p->resonance, [MH_SM_DRIVE] = p->drive, [MH_SM_LEVEL1] = p->level[0],
        [MH_SM_LEVEL2] = p->level[1], [MH_SM_SUB] = p->sub_level, [MH_SM_NOISE] = p->noise_level,
        [MH_SM_RING] = p->ring_level, [MH_SM_FM] = p->fm, [MH_SM_SHAPE1] = p->shape[0],
        [MH_SM_SHAPE2] = p->shape[1], [MH_SM_PW1] = p->pw[0], [MH_SM_PW2] = p->pw[1],
        [MH_SM_FINE1] = p->fine[0], [MH_SM_FINE2] = p->fine[1], [MH_SM_DETUNE] = p->detune,
        [MH_SM_UMIX] = p->unison_mix, [MH_SM_SPREAD] = p->spread, [MH_SM_MORPH] = p->morph,
        [MH_SM_FENV] = p->fenv_amount, [MH_SM_KEYTRACK] = p->key_track, [MH_SM_BASSCOMP] = p->bass_comp,
        [MH_SM_CHMIX] = p->chorus_mix, [MH_SM_CHDEPTH] = p->chorus_depth, [MH_SM_VOLUME] = p->volume,
        [MH_SM_VIB] = p->vibrato, [MH_SM_WVIB] = p->wheel_vibrato, [MH_SM_LFO2CUT] = p->lfo2_cutoff,
        [MH_SM_PWM] = p->lfo1_pwm, [MH_SM_MENV2] = p->menv_pitch2, [MH_SM_RVMIX] = p->reverb_mix,
        [MH_SM_RVTIME] = p->reverb_time, [MH_SM_RVTONE] = p->reverb_tone, [MH_SM_DLMIX] = p->delay_mix,
        [MH_SM_DLFB] = p->delay_feedback, [MH_SM_DLTONE] = p->delay_tone, [MH_SM_FX] = s->lim_fx ? 1.f : 0.f,
    };
    int snap = s->sm_snap || (!s->gate && s->aenv.stage == MH_ENV_IDLE);
    s->sm_snap = 0;
    float a = s->sm_a;
    for (int i = 0; i < MH_SM_N; i++) {
        float *z = s->sm[i];
        if (snap) {
            z[0] = z[1] = t[i];
        } else {
            z[0] += (t[i] - z[0]) * a;
            z[1] += (z[0] - z[1]) * a;
        }
        s->smv[i] = z[1];
    }
}

static void control(mh_synth *s) {
    const mh_patch *p = &s->p;
    smooth(s);
    const float *v = s->smv;
    float src[MH_SRC_COUNT] = {0};
    src[MH_SRC_LFO1] = s->lfo_out[0];
    src[MH_SRC_LFO2] = s->lfo_out[1];
    src[MH_SRC_FENV] = s->fenv.level;
    src[MH_SRC_AENV] = s->aenv.level;
    src[MH_SRC_MENV] = s->menv.level;
    src[MH_SRC_VELOCITY] = s->velocity;
    src[MH_SRC_KEY] = (float)(s->note - 60) * (1.f / 60.f);
    src[MH_SRC_WHEEL] = s->wheel;
    src[MH_SRC_AFTERTOUCH] = s->aftertouch;
    src[MH_SRC_BEND] = s->bend;
    src[MH_SRC_RANDOM] = s->note_random;
    float *m = s->mod;
    memset(m, 0, sizeof s->mod);
    for (int i = 0; i < MH_MATRIX_SLOTS; i++) {
        const mh_mod_slot *sl = &p->matrix[i];
        if (sl->src > 0 && sl->src < MH_SRC_COUNT && sl->dst > 0 && sl->dst < MH_DST_COUNT)
            m[sl->dst] += sl->amount * src[sl->src] * dst_scale[sl->dst];
    }

    int high = p->quality == MH_QUALITY_HIGH;
    float pwm = v[MH_SM_PWM] * 0.45f * s->lfo_out[0];
    mh_shape_set(&s->shape[0], v[MH_SM_SHAPE1] + m[MH_DST_SHAPE1], v[MH_SM_PW1] + m[MH_DST_PW1] + pwm);
    mh_shape_set(&s->shape[1], v[MH_SM_SHAPE2] + m[MH_DST_SHAPE2], v[MH_SM_PW2] + m[MH_DST_PW2] + pwm);
    /* the saw's bend only on the main pair: in a detuned stack it is lost, and it would cost
       every copy's every sample */
    for (int k = 0; k < 2; k++) {
        if (p->model[k] == MH_MODEL_ACID) {
            /* the soft edge as it is while it spans 3 samples or more (its aliases then below
               -99 dB), handed over to the plain step by 1.5, above which it would alias: below
               ~210 Hz at High's rate, gone by ~420 Hz (where the edge's softening is all but
               above hearing) */
            float semi = p->semi[k] + 0.01f * p->fine[k];
            float inc = 440.f * mh_exp2(s->pitch + semi * (1.f / 12.f)) / (high ? s->rate2 : s->rate);
            float tau = 1.f / (MH_SQ303_SLOPE * inc); /* the edge's time constant, samples */
            s->shape[k].pw = MH_SQ303_PW;
            s->shape[k].soft = mh_clamp((tau - 1.5f) * (1.f / 1.5f), 0.f, 1.f);
        }
        s->main_shape[k] = s->shape[k];
        s->main_shape[k].bow = 2.f * V_CURVE * p->vintage * s->shape[k].saw;
    }
    s->track2 = V_TRACK_CENTS * (1.f / 1200.f) * p->vintage;
    float fm = v[MH_SM_FM] + m[MH_DST_FM];
    s->main_os = high && (fm != 0.f || v[MH_SM_RING] > 0.f) ? 2 : 1;
    /* every oscillator, the unison copies too, gets the table kernel in High: the polynomial one
       saves only on the corrections, once a cycle, and leaves a copy's aliases at -50 to -70 dB
       against the table's -104 (several copies add up to audible) */
    for (int k = 0; k < 2; k++) {
        mh_osc_set_kernel(&s->osc[k], high);
        for (int j = 0; j < MH_UNISON_MAX - 1; j++)
            mh_osc_set_kernel(&s->copy[k][j], high);
    }
    float detune = mh_clamp(v[MH_SM_DETUNE] + m[MH_DST_DETUNE], 0.f, 1.f);
    unison_setup(s, 0, detune, v[MH_SM_SPREAD], v[MH_SM_UMIX]);
    unison_setup(s, 1, detune, v[MH_SM_SPREAD], v[MH_SM_UMIX]);
    s->stereo_on = p->stereo && s->lim_stereo && (s->ncopies[0] > 0 || s->ncopies[1] > 0);
    /* the filters' drift and spread, octaves: the first's goes into the cutoff, the second's
       (stereo) is a ratio to the first's */
    float fo[2];
    for (int ch = 0; ch < 2; ch++)
        fo[ch] = p->vintage * (V_FDRIFT_OCT * drift_step(s, s->fdrift[ch], p->vintage > 0.f) +
                               (ch ? V_FSPLIT_OCT : -V_FSPLIT_OCT)) + s->fnote[ch];
    s->cut_r1 = p->vintage > 0.f ? mh_exp2(fo[1] - fo[0]) : 1.f;
    /* what nothing listens to isn't run: osc 2 unless heard, ring-modulating or FM-ing osc 1;
       the LFOs unless something is routed from them */
    s->osc2_on = v[MH_SM_LEVEL2] + m[MH_DST_LEVEL2] > 0.f || v[MH_SM_RING] > 0.f || fm != 0.f;
    s->lfo_on[0] = v[MH_SM_VIB] != 0.f || v[MH_SM_WVIB] * s->wheel != 0.f || v[MH_SM_PWM] != 0.f;
    s->lfo_on[1] = v[MH_SM_LFO2CUT] != 0.f;
    for (int i = 0; i < MH_MATRIX_SLOTS; i++) {
        if (p->matrix[i].dst > 0 && p->matrix[i].amount != 0.f) {
            s->lfo_on[0] |= p->matrix[i].src == MH_SRC_LFO1;
            s->lfo_on[1] |= p->matrix[i].src == MH_SRC_LFO2;
        }
    }

    ramp_to(&s->r_level[0], mh_clamp(v[MH_SM_LEVEL1] + m[MH_DST_LEVEL1], 0.f, 1.f));
    ramp_to(&s->r_level[1], mh_clamp(v[MH_SM_LEVEL2] + m[MH_DST_LEVEL2], 0.f, 1.f));
    ramp_to(&s->r_sub, mh_clamp(v[MH_SM_SUB] + m[MH_DST_SUB], 0.f, 1.f));
    ramp_to(&s->r_noise, mh_clamp(v[MH_SM_NOISE] + m[MH_DST_NOISE], 0.f, 1.f));
    ramp_to(&s->r_ring, mh_clamp(v[MH_SM_RING], 0.f, 1.f));
    ramp_to(&s->r_fm, mh_clamp(fm, -4.f, 4.f));
    float drive = mh_clamp(v[MH_SM_DRIVE] + m[MH_DST_DRIVE], 0.f, 1.f);
    float gd = 0.4f * mh_exp2(5.321928f * drive); /* 0.4 .. 16 */
    ramp_to(&s->r_gd, gd);
    ramp_to(&s->r_comp, mh_exp2(-0.4f * mh_log2(gd)));
    ramp_to(&s->r_res, mh_clamp(v[MH_SM_RES] + m[MH_DST_RESONANCE], 0.f, 1.f));
    ramp_to(&s->r_morph, mh_clamp(v[MH_SM_MORPH] + m[MH_DST_MORPH], 0.f, 3.f));
    float cut = v[MH_SM_CUT] + v[MH_SM_KEYTRACK] * (float)(s->note - 60) * (1.f / 12.f) + m[MH_DST_CUTOFF] + fo[0];
    ramp_to(&s->r_cut, cut);
    ramp_to(&s->r_amp, mh_clamp(1.f + m[MH_DST_AMP], 0.f, 2.f));
    s->chorus_mix = mh_clamp(v[MH_SM_CHMIX] + m[MH_DST_CHORUS], 0.f, 1.f);

    float vel = s->velocity;
    s->fenv_amount = v[MH_SM_FENV] * (1.f - p->fenv_velocity * (1.f - vel));
    s->vel_gain = 1.f - p->amp_velocity * (1.f - vel);
    mh_env_set(&s->fenv, p->fenv.attack * s->env_var[0], p->fenv.decay * s->env_var[0], p->fenv.sustain,
               p->fenv.release * s->env_var[0], p->fenv.loop, s->rate);
    mh_env_set(&s->aenv, p->aenv.attack * s->env_var[1], p->aenv.decay * s->env_var[1], p->aenv.sustain,
               p->aenv.release * s->env_var[1], p->aenv.loop, s->rate);
    mh_env_set(&s->menv, p->menv.attack * s->env_var[2], p->menv.decay * s->env_var[2], p->menv.sustain,
               p->menv.release * s->env_var[2], p->menv.loop, s->rate);
    for (int k = 0; k < 2; k++) {
        float rate = p->lfo[k].rate * mh_exp2(m[k ? MH_DST_LFO2_RATE : MH_DST_LFO1_RATE]);
        s->lfo_inc[k] = mh_clamp(rate, 0.f, 200.f) / s->rate;
        s->lfo_delay_inc[k] = p->lfo[k].delay > 0.f ? 1.f / (p->lfo[k].delay * s->rate) : 1.f;
        s->lfo_fade_inc[k] = p->lfo[k].fade > 0.f ? 1.f / (p->lfo[k].fade * s->rate) : 1.f;
    }
    if (p->glide > 0.f) {
        s->glide_coef = mh_onepole(p->glide * (1.f / 4.6051702f), s->rate); /* to 1 % */
        s->glide_step = 1.f / (p->glide * s->rate);
    }
    /* the delay's time from the tempo (MIDI clock's while it comes, as the arpeggiator's) */
    static const float dl_beats[MH_DELAY_DIVS] = {0.25f, 1.f / 3.f, 0.5f, 0.75f, 2.f / 3.f, 1.f, 1.5f, 2.f};
    int dv = p->delay_div < 0 ? 0 : p->delay_div >= MH_DELAY_DIVS ? MH_DELAY_DIVS - 1 : p->delay_div;
    float beat = s->clk_age < 0.5f * s->rate ? s->clk_period * 24.f : s->rate * 60.f / mh_clamp(p->tempo, 20.f, 300.f);
    s->dl_target = beat * dl_beats[dv];
    float dfc = 1500.f * mh_exp2(3.f * mh_clamp(v[MH_SM_DLTONE], 0.f, 1.f));
    s->dl_lp_a = 1.f - mh_exp2(-2.f * MH_PI * dfc / s->rate * MH_LOG2E);
    s->hp_a = 0.f;
    if (p->hpf > 0.f) {
        float g = mh_tan_pi(mh_clamp(p->hpf, 10.f, 2000.f) / s->rate);
        s->hp_a = g / (1.f + g);
    }
}

/* ----- audio rate */

/* The main pair at its own rate: osc 1 (through-zero FM from osc 2, the sub from its wraps),
   osc 2 (hard-synced to osc 1), ring. Returns their centred mix. */
static float main_pair(mh_synth *s, float inc1, float inc2, float lv1, float lv2, float lsub, float lring,
                       float fm) {
    float i1 = mh_clamp(inc1 * (1.f + fm * s->osc2_last), -0.45f, 0.45f);
    float wd, sub = 0.f;
    int sub_div = lsub > 0.f ? s->p.sub_oct : 0;
    float o1 = mh_osc_step(&s->osc[0], &s->main_shape[0], i1, -1.f, &wd, sub_div, &sub);
    float o2 = s->osc2_on ? mh_osc_step(&s->osc[1], &s->main_shape[1], inc2, s->p.sync ? wd : -1.f, 0, 0, 0) : 0.f;
    s->osc2_last = o2;
    return s->gain_l[0][0] * lv1 * o1 + s->gain_l[1][0] * lv2 * o2 + lsub * sub + lring * o1 * o2;
}

static inline float noise(mh_synth *s) {
    float w = mh_randf(&s->rng);
    if (!s->p.noise_pink)
        return w;
    /* Paul Kellet's economy pink filter */
    s->pink[0] = 0.99765f * s->pink[0] + w * 0.0990460f;
    s->pink[1] = 0.96300f * s->pink[1] + w * 0.2965164f;
    s->pink[2] = 0.57000f * s->pink[2] + w * 1.0526913f;
    return (s->pink[0] + s->pink[1] + s->pink[2] + w * 0.1848f) * 0.25f;
}

/* The drive's curve: a cubic soft clip, f(v) = 1.5 v - 0.5 v^3 inside [-1, 1] and +-1 outside
   (its input is scaled by 2/3, so its slope at 0 is 1, as tanh's). */
static inline float clip3(float v) {
    if (v >= 1.f)
        return 1.f;
    if (v <= -1.f)
        return -1.f;
    return v * (1.5f - 0.5f * v * v);
}

/* its antiderivative, 0.75 v^2 - 0.125 v^4 inside and |v| - 0.375 outside */
static inline float clip3_int(float v) {
    float a = mh_absf(v);
    if (a >= 1.f)
        return a - 0.375f;
    float v2 = v * v;
    return v2 * (0.75f - 0.125f * v2);
}

/* First-order ADAA of the clip between v0 and v1: the mean of f over the step. Inside the knee
   the difference of antiderivatives factors through (v1 - v0): nothing to divide, nothing to
   cancel. Beyond it on one side it is +-1. Only a step across a knee divides. */
static inline float clip3_adaa(float v0, float v1) {
    if (v0 > -1.f && v0 < 1.f && v1 > -1.f && v1 < 1.f)
        return (v1 + v0) * (0.75f - 0.125f * (v1 * v1 + v0 * v0));
    if (v0 >= 1.f && v1 >= 1.f)
        return 1.f;
    if (v0 <= -1.f && v1 <= -1.f)
        return -1.f;
    float dv = v1 - v0;
    if (mh_absf(dv) < 1e-5f)
        return clip3(0.5f * (v0 + v1));
    return (clip3_int(v1) - clip3_int(v0)) / dv;
}

static inline float drive_adaa(mh_synth *s, int ch, float v) {
    float d = clip3_adaa(s->adaa_x[ch], v);
    s->adaa_x[ch] = v;
    return d;
}

/* The Sallen-Key: its low-pass and high-pass both run (so a MORPH move never wakes a stale
   one), blended. The knob's square law up to 0.97, then Surge's top feedback, 1.96. */
static inline float sk_step(mh_k35 *f, float x, float g, float res, float morph) {
    float k = 1.96f * mh_clamp(res_share(res), 0.f, 1.f), m = mh_clamp(morph * (1.f / 3.f), 0.f, 1.f);
    float lp = mh_k35_lp(&f[0], x, g, k), hp = mh_k35_hp(&f[1], x, g, k);
    return lp + m * (hp - lp);
}

/* One filter sample of the drive's output `d` at `rate` (the 2x rate, or 4x when the scheduler
   has room: b->x4), `g` prewarped for it. */
MH_ALWAYS_INLINE float filter_one(mh_synth *s, const mh_block *b, int ch, float d, float g, float fc, float res, float morph,
                        float rate) {
    int newton = b->high && !b->cheap;
    float hp_g = rate > s->rate2 ? s->diode_hp_g4 : s->diode_hp_g, y;
    if (b->filter == MH_FILTER_SVF) {
        y = mh_svf_step(&s->svf[ch], d, g, res, morph);
    } else if (b->filter == MH_FILTER_DIODE) {
        /* resonance takes the level down as it rises (the feedback subtracts): made up in part */
        float k = MH_DIODE_K_OSC * res_share(res);
        y = mh_diode_step(&s->diode[ch], d, fc, rate, k, hp_g) * (1.f + 0.15f * k);
    } else if (b->filter == MH_FILTER_SK) {
        y = sk_step(s->k35[ch], d, g, res, morph);
    } else if (b->filter == MH_FILTER_OTA) {
        float k = ladder_k(res), o[5];
        mh_ota_step(&s->ladder[ch], d * (1.f + b->bass_comp * k), g, k, newton, o);
        s->newton_steps[ch] += (uint32_t)s->ladder[ch].iters;
        s->filter_samples[ch]++;
        y = mh_ladder_mix(o, morph);
    } else {
        float k = ladder_k(res), o[5];
        mh_ladder_step(&s->ladder[ch], d * (1.f + b->bass_comp * k), g, k, newton, o);
        s->newton_steps[ch] += (uint32_t)s->ladder[ch].iters;
        s->filter_samples[ch]++;
        y = mh_ladder_mix(o, morph);
    }
#ifdef MH_HOST_STATS
    if (ch == 0) {
        const float q = 1e-3f;
        float t;
        if (b->filter == MH_FILTER_SVF) {
            t = mh_svf_step(&s->twin_svf, d * q, g, res, morph);
        } else if (b->filter == MH_FILTER_DIODE) {
            float k = MH_DIODE_K_OSC * res_share(res);
            t = mh_diode_step(&s->twin_diode, d * q, fc, rate, k, hp_g) * (1.f + 0.15f * k);
        } else if (b->filter == MH_FILTER_SK) {
            t = sk_step(s->twin_k35, d * q, g, res, morph);
        } else if (b->filter == MH_FILTER_OTA) {
            float k = ladder_k(res), o[5];
            mh_ota_step(&s->twin_ota, d * q * (1.f + b->bass_comp * k), g, k, newton, o);
            t = mh_ladder_mix(o, morph);
        } else {
            float k = ladder_k(res), o[5];
            mh_ladder_step(&s->twin_ladder, d * q * (1.f + b->bass_comp * k), g, k, newton, o);
            t = mh_ladder_mix(o, morph);
        }
        t *= 1.f / q;
        s->st_y2 += (double)y * y;
        s->st_dy2 += (double)(y - t) * (y - t);
        s->st_in2 += (double)d * d;
        s->st_n++;
    }
#endif
    return y;
}

/* Drive then filter, one sample at the 2x rate. In High the drive runs at 4x (with ADAA); with
   room to spare (b->x4) the filter runs there too, before the way back down to 2x: the OTA and
   Sallen-Key, hit hard (three times full scale, as BASS COMP can), alias at -61 to -92 dB at 2x
   and below -100 at 4x (host/examples/filter_4x.rs). */
MH_ALWAYS_INLINE float drive_filter(mh_synth *s, const mh_block *b, int ch, float x, float gd, float comp, float g,
                          float g4, float fc, float res, float morph) {
    int high = b->high;
    float u = gd * x * (2.f / 3.f), d, y;
#ifdef MH_HOST_STATS
    if (ch == 0) {
        float e = clip3(u) - 1.5f * u;
        s->st_u2 += 2.25 * (double)u * u;
        s->st_du2 += (double)e * e;
        s->st_upeak = mh_absf(u) > s->st_upeak ? mh_absf(u) : s->st_upeak;
    }
#endif
    if (high) {
        /* at 4x with ADAA: either alone leaves the hardest drive's aliases well above -70 dB */
        float q[2];
        mh_halfband_up_n(&s->drive_up[ch], u, q, MH_HB4_N);
        float d0 = drive_adaa(s, ch, q[0]); /* in order: ADAA keeps state, C argument order is unspecified */
        float d1 = drive_adaa(s, ch, q[1]);
        if (b->x4) {
            if (ch == 0)
                MH_PROF_ADD(MH_P_DRIVE, s->prof_t);
            float f0 = filter_one(s, b, ch, d0 * comp, g4, fc, res, morph, 2.f * s->rate2);
            float f1 = filter_one(s, b, ch, d1 * comp, g4, fc, res, morph, 2.f * s->rate2);
            y = mh_halfband_down_n(&s->drive_down[ch], f0, f1, MH_HB4_N);
            if (ch == 0)
                MH_PROF_ADD(MH_P_FILTER, s->prof_t);
            return y;
        }
        d = mh_halfband_down_n(&s->drive_down[ch], d0, d1, MH_HB4_N);
    } else {
        d = clip3(u);
    }
    d *= comp;
    if (ch == 0) /* (only core 0's work is timed: there is one clock mark) */
        MH_PROF_ADD(MH_P_DRIVE, s->prof_t);
    y = filter_one(s, b, ch, d, g, fc, res, morph, s->rate2);
    if (ch == 0)
        MH_PROF_ADD(MH_P_FILTER, s->prof_t);
    return y;
}

static inline float soft_limit(float x) {
    float a = mh_absf(x);
    if (a <= 0.9f)
        return x;
    float y = 0.9f + 0.1f * mh_tanh((a - 0.9f) * 10.f);
    return x < 0.f ? -y : y;
}

void mh_synth_set_offload(mh_synth *s, mh_offload_fn offload, mh_join_fn join) {
    s->offload = offload;
    s->join = join;
}

/* ----- the block's three parts: the front (modulation, sources, the mix), the back (drive,
   filters, the way down to the output rate, the VCA) and the output stage (high-pass, chorus, DC
   block, limiter). They touch separate state, so the back of one block can run on the other core
   while this one finishes the block before it and makes the front of the next (mh_synth_render). */

/* The unison copies, into copy_l / copy_r at the oscillators' rate. */
static void copies(mh_synth *s, mh_block *b) {
    /* a copy at a time over the whole block (its settings stay in registers), the copies in the
       order a sample's sum takes them: the same sums, to the bit, as copy-by-copy per sample */
    int nc = s->ncopies[0] > s->ncopies[1] ? s->ncopies[0] : s->ncopies[1], m = b->os * b->n, os = b->os;
    float wrap[2 * MH_BLOCK];
    for (int x = 0; x < m; x++)
        b->copy_l[x] = b->copy_r[x] = 0.f;
    if (!s->p.sync) { /* the usual case: each copy on its own, in a loop of its own */
        for (int j = 0; j < nc; j++)
            for (int k = 0; k < 2; k++)
                if (j < s->ncopies[k])
                    mh_osc_run(&s->copy[k][j], &s->shape[k], b->inc[k], s->ratio[k][j + 1], b->lv[k],
                               s->gain_l[k][j + 1], s->gain_r[k][j + 1], b->n, os, b->copy_l, b->copy_r);
        return;
    }
    for (int j = 0; j < nc; j++) {
        for (int k = 0; k < 2; k++) {
            if (j >= s->ncopies[k])
                continue;
            mh_osc *o = &s->copy[k][j];
            const mh_shape *sh = &s->shape[k];
            float ratio = s->ratio[k][j + 1], gl = s->gain_l[k][j + 1], gr = s->gain_r[k][j + 1];
            int synced = k == 1 && j < s->ncopies[0];
            for (int i = 0, x = 0; i < b->n; i++, x += os) {
                float lv = b->lv[k][i];
                int sync = s->p.sync && b->lv[1][i] > 0.f;
                if (lv <= 0.f && !(k == 0 && sync)) {
                    if (k == 0)
                        for (int h = 0; h < os; h++)
                            wrap[x + h] = -1.f; /* no wrap to sync to */
                    continue;
                }
                float inc = b->inc[k][i] * ratio;
                for (int h = 0; h < os; h++) {
                    float v;
                    if (k == 0)
                        v = mh_osc_next(o, sh, inc, &wrap[x + h]);
                    else if (synced && sync && wrap[x + h] >= 0.f)
                        v = mh_osc_step(o, sh, inc, wrap[x + h], 0, 0, 0);
                    else
                        v = mh_osc_next(o, sh, inc, 0);
                    b->copy_l[x + h] += v * lv * gl;
                    b->copy_r[x + h] += v * lv * gr;
                }
            }
        }
    }
}

/* The main pair (at its own rate, with their own drift) and the noise, into main. */
static void main_run(mh_synth *s, mh_block *b) {
    for (int i = 0, x = 0; i < b->n; i++) {
        float inc1 = b->inc[0][i] * s->ratio[0][0], inc2 = b->inc[1][i] * s->ratio[1][0];
        float lv1 = b->lv[0][i], lv2 = b->lv[1][i], lsub = b->lsub[i], lring = b->lring[i], fm = b->fm[i];
        for (int h = 0; h < b->os; h++, x++) {
            float m;
            if (s->main_os == 2) {
                float a = main_pair(s, 0.5f * inc1, 0.5f * inc2, lv1, lv2, lsub, lring, fm);
                float c = main_pair(s, 0.5f * inc1, 0.5f * inc2, lv1, lv2, lsub, lring, fm);
                m = mh_halfband_down_n(&s->hb_main, a, c, MH_HB4_N);
            } else {
                m = main_pair(s, inc1, inc2, lv1, lv2, lsub, lring, fm);
            }
            if (b->lnoise[i] > 0.f)
                m += b->lnoise[i] * noise(s);
            b->main[x] = m;
        }
    }
}

/* The front: the modulation frame by frame, the sources, their mix at the 2x rate. */
static void front(mh_synth *s, mh_block *b, int n) {
    const mh_patch *p = &s->p;
    int high = p->quality == MH_QUALITY_HIGH;
    float orate = high ? s->rate2 : s->rate;
    b->n = n;
    b->high = high;
    b->os = high ? 2 : 1;
    b->stereo = s->stereo_on;
    b->x4 = high && s->lim_x4;
    /* what the back reads of the patch and the control rate, kept with the block: the back may
       run on the other core while the next control block changes them */
    b->filter = p->filter;
    b->bass_comp = s->smv[MH_SM_BASSCOMP];
    b->cheap = s->lim_cheap;
    b->hp_a = s->hp_a;
    b->chorus = s->lim_chorus ? p->chorus : MH_CHORUS_OFF;
    b->chorus_depth = s->smv[MH_SM_CHDEPTH];
    b->chorus_mix = s->chorus_mix;
    b->cut_r = s->cut_r1;
    {
        const float *v = s->smv;
        float fxg = v[MH_SM_FX];
        b->rv_want = p->reverb_mix > 0.f && s->lim_fx;
        b->rv_amount = 0.6f * mh_clamp(v[MH_SM_RVMIX], 0.f, 1.f) * fxg; /* Rings' amount: 0.1 to 0.6 */
        b->rv_time = 0.35f + 0.63f * mh_clamp(v[MH_SM_RVTIME], 0.f, 1.f);
        b->rv_lp = 0.3f + 0.6f * mh_clamp(v[MH_SM_RVTONE], 0.f, 1.f);
        b->dl_want = p->delay_mix > 0.f && s->lim_fx;
        b->dl_mix = mh_clamp(v[MH_SM_DLMIX], 0.f, 1.f) * fxg;
        b->dl_fb = mh_clamp(v[MH_SM_DLFB], 0.f, 0.95f);
        b->dl_lp_a = s->dl_lp_a;
        b->dl_target = s->dl_target;
    }
    MH_PROF_START(t0);

    for (int i = 0; i < n; i++) {
        float fe = mh_env_step(&s->fenv), ae = mh_env_step(&s->aenv), me = mh_env_step(&s->menv);
        for (int k = 0; k < 2; k++) {
            if (!s->lfo_on[k]) {
                s->lfo_out[k] = 0.f;
                continue;
            }
            float v = mh_lfo_step(&s->lfo[k], s->lfo_inc[k], p->lfo[k].shape);
            s->lfo_out[k] = v * mh_lfo_fade(&s->lfo[k], s->lfo_delay_inc[k], s->lfo_fade_inc[k]);
        }
        if (s->pitch != s->target) {
            if (p->glide_linear) {
                float d = s->target - s->pitch;
                s->pitch = mh_absf(d) <= s->glide_step ? s->target : s->pitch + (d > 0.f ? s->glide_step : -s->glide_step);
            } else {
                s->pitch += (s->target - s->pitch) * s->glide_coef;
                if (mh_absf(s->target - s->pitch) < 1e-5f)
                    s->pitch = s->target;
            }
        }
        float *m = s->mod;
        const float *v = s->smv;
        float semis = s->bend * p->bend_range + (v[MH_SM_VIB] + s->wheel * v[MH_SM_WVIB]) * s->lfo_out[0] +
                      m[MH_DST_PITCH];
        float p1 = s->pitch + (semis + p->semi[0] + 0.01f * v[MH_SM_FINE1] + m[MH_DST_PITCH1]) * (1.f / 12.f);
        float p2 = s->pitch +
                   (semis + p->semi[1] + 0.01f * v[MH_SM_FINE2] + m[MH_DST_PITCH2] + v[MH_SM_MENV2] * me) * (1.f / 12.f);
        p2 += (p2 + 0.75f) * s->track2; /* osc 2's tracking, from middle C (-0.75 octaves from A4) */
        b->inc[0][i] = 440.f * mh_exp2(p1) / orate;
        b->inc[1][i] = s->osc2_on ? 440.f * mh_exp2(p2) / orate : 0.f;

        float cut = ramp_next(&s->r_cut) + s->fenv_amount * fe + v[MH_SM_LFO2CUT] * s->lfo_out[1];
        float res = ramp_next(&s->r_res);
        float fc = mh_clamp(440.f * mh_exp2(mh_clamp(cut, -6.f, 6.f)), 16.f, 20000.f);
        b->fc[i] = fc;
        if (p->filter == MH_FILTER_LADDER)
            fc = mh_clamp(fc * mh_ladder_tune(ladder_k(res)), 16.f, 20000.f);
        else if (p->filter == MH_FILTER_OTA)
            fc = mh_clamp(fc * mh_ota_tune(ladder_k(res)), 16.f, 20000.f);
        b->g[i] = mh_tan_pi(fc / s->rate2);
        b->g4[i] = b->x4 ? mh_tan_pi(fc / (2.f * s->rate2)) : 0.f;
        b->res[i] = res;
        b->lv[0][i] = ramp_next(&s->r_level[0]);
        b->lv[1][i] = ramp_next(&s->r_level[1]);
        b->lsub[i] = ramp_next(&s->r_sub);
        b->lnoise[i] = ramp_next(&s->r_noise);
        b->lring[i] = ramp_next(&s->r_ring);
        b->fm[i] = ramp_next(&s->r_fm);
        b->gd[i] = ramp_next(&s->r_gd);
        b->comp[i] = ramp_next(&s->r_comp);
        b->morph[i] = ramp_next(&s->r_morph);
        float env = p->vca_exp ? ae * ae : ae;
        b->amp[i] = env * s->vel_gain * v[MH_SM_VOLUME] * ramp_next(&s->r_amp) * OUT_GAIN;
        if (s->aenv.stage == MH_ENV_IDLE && !s->gate)
            s->idle++;
        else
            s->idle = 0;
    }
    MH_PROF_ADD(MH_P_MOD, t0);

    int copied = s->ncopies[0] > 0 || s->ncopies[1] > 0;
    main_run(s, b);
    if (copied)
        copies(s, b);
    MH_PROF_ADD(MH_P_SOURCES, t0);

    int m = b->os * n;
    for (int x = 0; x < m; x++) {
        float cl = copied ? b->copy_l[x] : 0.f, cr = copied ? b->copy_r[x] : 0.f;
        float l = b->main[x] + (b->stereo ? cl : 0.5f * (cl + cr)), r = b->main[x] + cr;
        if (high) {
            b->fil[0][x] = l;
            b->fil[1][x] = r;
        } else {
            mh_halfband_up_n(&s->up[0], l, &b->fil[0][2 * x], MH_HB2_N);
            if (b->stereo)
                mh_halfband_up_n(&s->up[1], r, &b->fil[1][2 * x], MH_HB2_N);
        }
    }
}

/* The back: the drive and the filters at the 2x rate, then down and the VCA, into the block's
   output. */
static void back(void *arg) {
    mh_block *b = arg;
    mh_synth *s = b->owner;
#ifdef MH_PROFILE
    uint32_t t0 = mh_prof_clock();
    s->prof_t = t0;
#endif
    for (int ch = 0; ch < (b->stereo ? 2 : 1); ch++) {
        float *x = b->fil[ch], r = ch ? b->cut_r : 1.f;
        for (int j = 0; j < 2 * b->n; j++) {
            int i = j >> 1;
            x[j] = drive_filter(s, b, ch, x[j], b->gd[i], b->comp[i], b->g[i] * r, b->g4[i] * r, b->fc[i] * r,
                                b->res[i], b->morph[i]);
        }
    }
    for (int i = 0; i < b->n; i++) {
        float yl = mh_halfband_down_n(&s->down[0], b->fil[0][2 * i], b->fil[0][2 * i + 1], MH_HB2_N), yr = yl;
        if (b->stereo)
            yr = mh_halfband_down_n(&s->down[1], b->fil[1][2 * i], b->fil[1][2 * i + 1], MH_HB2_N);
        b->outl[i] = yl * b->amp[i];
        b->outr[i] = yr * b->amp[i];
    }
#ifdef MH_PROFILE
    MH_PROF_ADD(MH_P_FILTER, s->prof_t);
#endif
}

/* An effect's memory and whether it runs. Turned off, its lines keep their last audio; before
   it comes back on they are cleared, a slice a block (a whole clear at once would be one block's
   worth of interrupt time spent on memory), so it never replays an old tail. */
enum { FX_CLEAN, FX_STALE, FX_CLEARING, FX_ON };
#define FX_SLICE 2048

/* the effect's state after this block; `level` its gain now (fading), `bytes` its memory */
static int fx_gate(int *state, int *pos, int want, float level, void *mem, int bytes) {
    switch (*state) {
    case FX_CLEAN:
        if (want)
            *state = FX_ON;
        break;
    case FX_STALE:
        if (want) {
            *state = FX_CLEARING;
            *pos = 0;
        }
        break;
    case FX_CLEARING:
        if (!want) {
            *state = FX_STALE;
            break;
        }
        memset((char *)mem + *pos, 0, (size_t)(bytes - *pos < FX_SLICE ? bytes - *pos : FX_SLICE));
        *pos += FX_SLICE;
        if (*pos >= bytes)
            *state = FX_ON;
        break;
    case FX_ON:
        if (!want && level <= 1e-4f) /* faded out: -80 dB */
            *state = FX_STALE;
        break;
    }
    return *state == FX_ON;
}

/* The output stage, in place on the block's output: the high-pass, the chorus, the delay, the
   reverb, the DC block, the limiter. On this core, after the back: the chorus and the effects
   are the dearest part of a stereo patch's last levels, and here they even the two cores'
   shares. */
static void out_stage(mh_synth *s, mh_block *b) {
    MH_PROF_START(t0);
    float rdc = 1.f - 2.f * MH_PI * 5.f / s->rate;
    for (int i = 0; i < b->n; i++) {
        float ol = b->outl[i], orr = b->outr[i];
        if (b->hp_a > 0.f) {
            float v = (ol - s->hp_s[0]) * b->hp_a, lp = v + s->hp_s[0];
            s->hp_s[0] = lp + v;
            ol -= lp;
            v = (orr - s->hp_s[1]) * b->hp_a;
            lp = v + s->hp_s[1];
            s->hp_s[1] = lp + v;
            orr -= lp;
        }
        mh_chorus_step(&s->chorus, b->chorus, b->chorus_depth, b->chorus_mix, ol, orr, &ol, &orr);
        b->outl[i] = ol;
        b->outr[i] = orr;
    }
    int was = s->dl_state;
    if (fx_gate(&s->dl_state, &s->dl_pos, b->dl_want, b->dl_mix, s->delay.line, (int)sizeof s->delay.line)) {
        if (was != FX_ON) { /* from silence: the filters' state too */
            s->delay.lp[0] = s->delay.lp[1] = s->delay.hp[0] = s->delay.hp[1] = 0.f;
        }
        mh_delay_process(&s->delay, b->outl, b->outr, b->n, b->dl_target, b->dl_fb, b->dl_mix, b->dl_lp_a, s->dl_hp_a,
                         s->dl_glide, was != FX_ON);
    }
    was = s->rv_state;
    if (fx_gate(&s->rv_state, &s->rv_pos, b->rv_want, b->rv_amount, s->reverb.buf, (int)sizeof s->reverb.buf)) {
        if (was != FX_ON)
            s->reverb.lp_decay_1 = s->reverb.lp_decay_2 = 0.f;
        /* Rings' input gain and diffusion */
        mh_reverb_set(&s->reverb, b->rv_amount, 0.2f, b->rv_time, 0.625f, b->rv_lp);
        mh_reverb_process(&s->reverb, b->outl, b->outr, b->n);
    }
    float pk = 0.f;
    for (int i = 0; i < b->n; i++) {
        float ol = b->outl[i], orr = b->outr[i];
        /* DC block at ~5 Hz */
        float dl = ol - s->dc_x[0] + rdc * s->dc_y[0], dr = orr - s->dc_x[1] + rdc * s->dc_y[1];
        s->dc_x[0] = ol;
        s->dc_x[1] = orr;
        s->dc_y[0] = dl;
        s->dc_y[1] = dr;
        b->outl[i] = soft_limit(dl);
        b->outr[i] = soft_limit(dr);
        float m = mh_absf(dl) > mh_absf(dr) ? mh_absf(dl) : mh_absf(dr);
        pk = m > pk ? m : pk;
    }
    /* the voice may stop only once its tails (chorus, echoes, reverb) have died away too: quiet for
       IDLE_AFTER, and with the delay on for a whole line more (an echo may still be on its way) */
    int need = IDLE_AFTER + (s->dl_state == FX_ON ? MH_DELAY_LEN : 0);
    if (pk >= 1e-4f)
        s->out_quiet = 0;
    else if (s->out_quiet < need)
        s->out_quiet += b->n;
    s->out_need = need;
    MH_PROF_ADD(MH_P_OUTPUT, t0);
}

/* Hand `b`'s back to the other core, or run it here. */
static void start_back(mh_synth *s, mh_block *b) {
    s->back_taken = s->offload && s->offload(back, b);
    if (!s->back_taken)
        back(b);
}

/* Wait for the back handed over; run it here if the other core failed it. */
static void finish_back(mh_synth *s, mh_block *b) {
#ifdef MH_PROFILE
    uint32_t w0 = mh_prof_clock();
#endif
    if (s->back_taken && s->join && s->join() != 0)
        back(b);
    s->back_taken = 0;
#ifdef MH_PROFILE
    mh_prof[MH_P_WAIT] += mh_prof_clock() - w0;
#endif
}

void mh_synth_render(mh_synth *s, float *outl, float *outr, int n) {
    int done = 0;
    mh_block *pending = 0;
    while (done < n) {
        MH_PROF_START(t0);
        if (s->ctl_left == 0) {
            control(s);
            s->ctl_left = MH_BLOCK;
        }
        int c = n - done < s->ctl_left ? n - done : s->ctl_left;
        s->ctl_left -= c;
        arp_run(s, c);
        MH_PROF_ADD(MH_P_CONTROL, t0);
        if (s->idle >= IDLE_AFTER && s->out_quiet >= s->out_need) {
            for (int i = 0; i < c; i++)
                outl[done + i] = outr[done + i] = 0.f;
            done += c;
            continue;
        }
        /* the front of this block here while the other core runs the back of the last; then
           this block's back to the other core, and the last block's output stage here */
        mh_block *b = &s->blk[s->blk_i];
        s->blk_i ^= 1;
        b->owner = s;
        b->outl = outl + done;
        b->outr = outr + done;
        front(s, b, c);
        if (pending)
            finish_back(s, pending);
        start_back(s, b);
        if (pending)
            out_stage(s, pending);
        pending = b;
        done += c;
    }
    if (pending) {
        finish_back(s, pending);
        out_stage(s, pending);
    }
}
