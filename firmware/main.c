/* firmware/main.c - Monohull on the M-VAVE FM-1: the synth (../dsp, mh_synth) rendered in the
 * audio interrupt, played from the keys and the TRS MIDI input, edited from the panel.
 *
 * Built on Felucca's hardware layer (ext/Felucca/firmware/hal, crt0.S, fm1_vec.S, fm1_isr.S;
 * GPL-3.0-only); the boot guard, core 1 hold, LCD and crash screen follow fm1-sid's player
 * firmware (GPL-3.0-only). So this firmware is GPL-3.0-only.
 *
 * Panel: the 27 keys play; OCT- / OCT+ the octave; held REC accents the notes played. KNOB1-4
 * edit the page's four parameters; SELECT steps through the pages; ALGORITHM is always the
 * cutoff; PRESETS picks a factory patch. Pages by button: HOME main, SCL oscillators, EDIT
 * filter, ENV envelopes, LFO LFOs, FX unison, chorus, reverb and delay (held: every effect off
 * and on, DRY in the footer, to hear a patch bare), GLO voice, SEQ the mod matrix. ARP
 * turns the arpeggiator on and off and shows its page. PLAY: all notes off. Hold OCT- and OCT+
 * for 3 s: the USB update mode. MIDI in on the TRS jack (omni): notes, velocity, bend, mod
 * wheel, sustain, channel pressure, clock and start / stop, and the CCs in dsp/mh_params.c.
 *
 * Load: the audio interrupt's time is measured every half buffer; the main loop adds the
 * extras (unison voices, the stereo filter, the chorus, the delay and reverb, the filters at
 * 4x) while there is room and takes them
 * away when there isn't (docs/spec.md section 6).
 *
 * Core 1 (fm1_cpu1.h, Felucca's and X0X's mailbox): the unison copies and the stereo second
 * filter run there while core 0 runs the rest of each block (mh_synth_set_offload). A unit whose
 * core 1 misreads RAM, or one that doesn't answer, runs on core 0 alone from then on.
 * SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_time.h"
#include "fm1_irq.h"
#include "fm1_sys.h"
#include "fm1_gpio.h"
#include "fm1_timer.h"
#include "fm1_lcd_hw.h"
#include "fm1_audio.h"
#include "fm1_input.h"
#include "fm1_adc.h"
#include "fm1_uart.h"

#include "mh_params.h"
#include "mh_synth.h"
#include "presets.h"

void *memset(void *d, int c, unsigned long n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
void *memmove(void *d, const void *s, unsigned long n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    if (p < q)
        while (n--) *p++ = *q++;
    else
        while (n--) p[n] = q[n];
    return d;
}
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}

#include "assets.h"

/* ------------------------------------------------------------------------------- audio --- */
#define HALF_FRAMES 128u
#define HALF_WORDS (HALF_FRAMES * 2u)
#define HALF_TICKS 69632u                   /* TIMER4 (24 MHz) ticks in a half buffer at 44117.6 Hz */
#define OUT_SCALE 4194304.f                 /* full scale at 2^22, Felucca's level (6 dB under the word's) */
#define RATE 44117.647f

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));
static mh_synth syn;
static float out_l[HALF_FRAMES], out_r[HALF_FRAMES];
static volatile uint32_t ms;
static volatile uint32_t isr_ticks, isr_halves, isr_peak;   /* for the load meter */
#ifdef MH_PROFILE
volatile uint32_t mh_half_log[256];
#endif
static volatile int32_t master_q12 = 4096;

/* note events from the main loop (the panel keys) to the audio ISR */
typedef struct { uint8_t on, note, vel; } ev_t;
#define NEV 64u
static ev_t evq[NEV];
static volatile uint32_t ev_head, ev_tail;
static volatile uint8_t panic_req;

static void post(uint32_t on, uint32_t note, uint32_t vel)
{
    uint32_t h = ev_head;
    if (h - ev_tail >= NEV)
        return;
    evq[h % NEV] = (ev_t){(uint8_t)on, (uint8_t)note, (uint8_t)vel};
    ev_head = h + 1u;
}

/* MIDI in on the TRS jack: UART1's DMA ring, read at each half buffer */
#define UM_RING 128u
static volatile uint8_t um_ring[UM_RING] __attribute__((aligned(16)));
static uint32_t um_rd, um_pend;
static uint8_t um_st, um_need, um_got, um_d0;
static volatile uint32_t midi_msgs;

static void midi_msg(uint32_t st, uint32_t d1, uint32_t d2)
{
    mh_patch *p = mh_synth_patch(&syn);
    midi_msgs++;
    switch (st & 0xF0u) {
    case 0x90:
        if (d2) {
            mh_synth_note_on(&syn, (int)d1, (float)d2 * (1.f / 127.f));
            break;
        }
        /* fall through: note on at velocity 0 is note off */
    case 0x80:
        mh_synth_note_off(&syn, (int)d1);
        break;
    case 0xB0:
        if (d1 == 1)
            mh_synth_wheel(&syn, (float)d2 * (1.f / 127.f));
        else if (d1 == 64)
            mh_synth_sustain(&syn, d2 >= 64);
        else if (d1 == 120 || d1 == 123)
            mh_synth_all_notes_off(&syn);
        else
            mh_param_cc(p, (int)d1, (int)d2);
        break;
    case 0xD0:
        mh_synth_aftertouch(&syn, (float)d1 * (1.f / 127.f));
        break;
    case 0xE0:
        mh_synth_bend(&syn, (float)((int)(d1 | d2 << 7) - 8192) * (1.f / 8192.f));
        break;
    default:
        break;
    }
}

static void midi_byte(uint32_t b)
{
    if (b >= 0xF8u) {                       /* realtime: keeps running status */
        if (b == 0xF8u)
            mh_synth_clock(&syn);
        else if (b == 0xFAu || b == 0xFBu)
            mh_synth_transport(&syn, 1);
        else if (b == 0xFCu)
            mh_synth_transport(&syn, 0);
        return;
    }
    if (b & 0x80u) {
        um_st = b < 0xF0u ? (uint8_t)b : 0; /* system common and SysEx cancel running status */
        um_need = (b & 0xE0u) == 0xC0u ? 1 : 2;
        um_got = 0;
        return;
    }
    if (!um_st)
        return;
    if (um_need == 2 && !um_got) {
        um_d0 = (uint8_t)b;
        um_got = 1;
        return;
    }
    um_got = 0;
    if (um_need == 2)
        midi_msg(um_st, um_d0, b);
    else
        midi_msg(um_st, b, 0);
}

static void midi_poll(void)
{
    um_pend += fm1_uart1_rx_take();
    if (um_pend > UM_RING) {                /* overrun: drop to the newest bytes, wait for a status */
        um_rd = (um_rd + um_pend - UM_RING) & (UM_RING - 1u);
        um_pend = UM_RING;
        um_st = 0;
    }
    while (um_pend) {
        midi_byte(um_ring[um_rd]);
        um_rd = (um_rd + 1u) & (UM_RING - 1u);
        um_pend--;
    }
}

void fm1_alnk0_irq(void)
{
    uint8_t p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t t0 = fm1_ticks(), i, dt;
        int32_t *o = &abuf[fm1_audio_free_half() * HALF_WORDS];
        float g = OUT_SCALE * (float)master_q12 * (1.f / 4096.f);
        midi_poll();
        while (ev_tail != ev_head) {
            ev_t e = evq[ev_tail % NEV];
            if (e.on)
                mh_synth_note_on(&syn, e.note, (float)e.vel * (1.f / 127.f));
            else
                mh_synth_note_off(&syn, e.note);
            ev_tail++;
        }
        if (panic_req) {
            panic_req = 0;
            mh_synth_all_notes_off(&syn);
            mh_synth_sustain(&syn, 0);
        }
        mh_synth_render(&syn, out_l, out_r, HALF_FRAMES);
        for (i = 0; i < HALF_FRAMES; i++) {
            o[2u * i] = (int32_t)(out_l[i] * g);
            o[2u * i + 1u] = (int32_t)(out_r[i] * g);
        }
        fm1_audio_ack_half();
        dt = fm1_ticks() - t0;
        isr_ticks += dt;
        if (dt > isr_peak)
            isr_peak = dt;
#ifdef MH_PROFILE
        mh_half_log[isr_halves & 255u] = dt;    /* the last 256 halves' times, for --dump */
#endif
        isr_halves++;
    }
}

void fm1_timer5_irq(void)
{
    static uint32_t last, acc;
    uint32_t now;
    fm1_timer5_ack();
    fm1_input_tick();
    now = fm1_ticks();
    acc += now - last;
    last = now;
    while (acc >= 24000u) {
        acc -= 24000u;
        ms++;
    }
}

#ifdef MH_PROFILE
static uint32_t fm1_ticks_fn(void) { return fm1_ticks(); }
#endif

void isr_alnk0(void);
void isr_timer5(void);

/* ------------------------------------------------------------------------ the scheduler --- */
/* Extras by level, added in this order while the load stays under 70 % and taken away above
 * 80 % (docs/spec.md section 6): 0 none, 1-3 unison 3 / 5 / 7, 4 the stereo filter, 5 the
 * chorus. Past 90 % with none left the ladder drops to its cheap solve until the load eases. */
static uint32_t dry;                       /* FX held: the chorus, delay and reverb off */
static uint32_t extras = 0, extras_cap = 7, load_pc, load_peak_pc, calm_ms, eff_forced;

static void apply_extras(void)
{
    static const uint8_t uni[8] = {1, 3, 5, 7, 7, 7, 7, 7};
    mh_synth_set_limits(&syn, uni[extras], extras >= 4, extras >= 5 && !dry, extras >= 6 && !dry);
    mh_synth_set_filter_x4(&syn, extras >= 7);
}

static uint32_t settling;
/* the peak below which the next level is tried: 70 %, but the filters at 4x cost +11 to +22 % of
   a half with one filter (measured on every preset) and twice that with the stereo two */
static uint32_t climb_below(void)
{
    if (extras + 1u == 7u)
        return syn.stereo_on ? 35u : 55u;
    return 70u;
}

static void schedule(uint32_t now)
{
    static uint32_t last_ms, last_ticks, last_halves;
    uint32_t t = isr_ticks, h = isr_halves, pk;
    if (now - last_ms < 200u || h == last_halves)
        return;
    load_pc = (t - last_ticks) * 100u / ((h - last_halves) * HALF_TICKS);
    pk = isr_peak * 100u / HALF_TICKS;
    isr_peak = 0;
    load_peak_pc = pk;
    last_ticks = t;
    last_halves = h;
#ifdef MH_PIN_X
    last_ms = now;
    return;
#endif
    /* the window just after a change carries its settling (the filters' solves catch up), so
       it drops a level only past 90 %: one change's spike must not start a cascade */
    if (pk > (settling ? 90u : 80u) && extras > 0) {
        /* a level that overloaded stays off until the patch changes: going back up a second later
           would only overload again, and the unison count would audibly flip */
        extras_cap = --extras;
        apply_extras();
        calm_ms = 0;
        settling = 1;
    } else if (pk < climb_below()) {
        settling = 0;
        calm_ms += now - last_ms;
        if (calm_ms >= 400u && extras < extras_cap) { /* climbing from none, a level at a time */
            extras++;
            apply_extras();
            calm_ms = 0;
            settling = 1;
        }
    } else {
        settling = 0;
        calm_ms = 0;
    }
    if (pk > 90u && extras == 0 && !eff_forced) {
        eff_forced = 1;                     /* the ladder's cheap solve; the oscillators stay High */
        mh_synth_set_cheap_filter(&syn, 1);
    } else if (eff_forced && pk < 60u) {
        eff_forced = 0;
        mh_synth_set_cheap_filter(&syn, 0);
    }
    last_ms = now;
}

/* ---------------------------------------------------------------------------- the pages --- */
typedef struct { const char *title; const char *param[4]; const char *label[4]; } page_t;
/* every parameter in dsp/mh_params.c is on a page (tools/build_fw.py checks) */
static const page_t PAGES[] = {
    {"MAIN", {"cutoff", "resonance", "fenv_amount", "drive"}, {"CUTOFF", "RESONANCE", "FENV AMOU", "DRIVE"}},
    {"OSC 1", {"osc1_shape", "osc1_pw", "osc1_semi", "osc1_fine"}, {"OSC1 SHAP", "OSC1 PW", "OSC1 SEMI", "OSC1 FINE"}},
    {"OSC 2", {"osc2_shape", "osc2_pw", "osc2_semi", "osc2_fine"}, {"OSC2 SHAP", "OSC2 PW", "OSC2 SEMI", "OSC2 FINE"}},
    {"MIXER", {"osc1_level", "osc2_level", "sub_level", "noise_level"}, {"OSC1 LEVE", "OSC2 LEVE", "SUB LEVEL", "NOISE LEV"}},
    {"OSC MOD", {"fm", "sync", "ring_level", "sub_oct"}, {"FM", "SYNC", "RING LEVE", "SUB OCT"}},
    {"OSC MODEL", {"osc1_model", "osc2_model", 0, 0}, {"OSC1 MODE", "OSC2 MODE", "", ""}},
    {"FILTER", {"morph", "key_track", "bass_comp", "filter"}, {"MORPH", "KEY TRACK", "BASS COMP", "FILTER"}},
    {"FILTER 2", {"fenv_velocity", "lfo2_cutoff", "hpf", "noise_pink"}, {"FENV VELO", "LFO2 CUTO", "HPF", "NOISE PIN"}},
    {"FILT ENV", {"fenv_attack", "fenv_decay", "fenv_sustain", "fenv_release"}, {"FENV ATTA", "FENV DECA", "FENV SUST", "FENV RELE"}},
    {"AMP ENV", {"aenv_attack", "aenv_decay", "aenv_sustain", "aenv_release"}, {"AENV ATTA", "AENV DECA", "AENV SUST", "AENV RELE"}},
    {"MOD ENV", {"menv_attack", "menv_decay", "menv_sustain", "menv_release"}, {"MENV ATTA", "MENV DECA", "MENV SUST", "MENV RELE"}},
    {"ENV MODES", {"menv_pitch2", "fenv_loop", "aenv_loop", "menv_loop"}, {"MENV PITC", "FENV LOOP", "AENV LOOP", "MENV LOOP"}},
    {"AMP", {"amp_velocity", "vca_exp", "volume", "quality"}, {"AMP VELOC", "VCA EXP", "VOLUME", "QUALITY"}},
    {"LFO 1", {"lfo1_rate", "lfo1_shape", "lfo1_delay", "lfo1_fade"}, {"LFO1 RATE", "LFO1 SHAP", "LFO1 DELA", "LFO1 FADE"}},
    {"LFO 1 DEST", {"vibrato", "wheel_vibrato", "lfo1_pwm", "lfo1_key_reset"}, {"VIBRATO", "WHEEL VIB", "LFO1 PWM", "LFO1 KEY "}},
    {"LFO 2", {"lfo2_rate", "lfo2_shape", "lfo2_delay", "lfo2_fade"}, {"LFO2 RATE", "LFO2 SHAP", "LFO2 DELA", "LFO2 FADE"}},
    {"LFO 2 MORE", {"lfo2_key_reset", "bend_range", "vintage", "priority"}, {"LFO2 KEY ", "BEND RANG", "VINTAGE", "PRIORITY"}},
    {"UNISON", {"osc1_unison", "osc2_unison", "detune", "unison_mix"}, {"OSC1 UNIS", "OSC2 UNIS", "DETUNE", "UNISON MI"}},
    {"STEREO FX", {"spread", "stereo", "chorus", "chorus_mix"}, {"SPREAD", "STEREO", "CHORUS", "CHORUS MI"}},
    {"CHORUS", {"chorus_depth", 0, 0, 0}, {"CHORUS DE", "", "", ""}},
    {"REVERB", {"reverb_mix", "reverb_time", "reverb_tone", 0}, {"MIX", "TIME", "TONE", ""}},
    {"DELAY", {"delay_mix", "delay_div", "delay_feedback", "delay_tone"}, {"MIX", "TIME", "FEEDBACK", "TONE"}},
    {"VOICE", {"glide", "glide_linear", "glide_legato", "legato"}, {"GLIDE", "GLIDE LIN", "GLIDE LEG", "LEGATO"}},
    {"ARP", {"arp_mode", "arp_div", "arp_oct", "arp_gate"}, {"MODE", "RATE", "OCTAVES", "GATE"}},
    {"ARP 2", {"tempo", "arp_latch", "arp_accent", 0}, {"TEMPO", "LATCH", "ACCENT", ""}},
    {"MOD 1", {"mod1_src", "mod1_dst", "mod1_amount", 0}, {"MOD1 SRC", "MOD1 DST", "MOD1 AMOU", ""}},
    {"MOD 2", {"mod2_src", "mod2_dst", "mod2_amount", 0}, {"MOD2 SRC", "MOD2 DST", "MOD2 AMOU", ""}},
    {"MOD 3", {"mod3_src", "mod3_dst", "mod3_amount", 0}, {"MOD3 SRC", "MOD3 DST", "MOD3 AMOU", ""}},
    {"MOD 4", {"mod4_src", "mod4_dst", "mod4_amount", 0}, {"MOD4 SRC", "MOD4 DST", "MOD4 AMOU", ""}},
    {"MOD 5", {"mod5_src", "mod5_dst", "mod5_amount", 0}, {"MOD5 SRC", "MOD5 DST", "MOD5 AMOU", ""}},
    {"MOD 6", {"mod6_src", "mod6_dst", "mod6_amount", 0}, {"MOD6 SRC", "MOD6 DST", "MOD6 AMOU", ""}},
    {"MOD 7", {"mod7_src", "mod7_dst", "mod7_amount", 0}, {"MOD7 SRC", "MOD7 DST", "MOD7 AMOU", ""}},
    {"MOD 8", {"mod8_src", "mod8_dst", "mod8_amount", 0}, {"MOD8 SRC", "MOD8 DST", "MOD8 AMOU", ""}},
};
#define NPAGES (sizeof PAGES / sizeof PAGES[0])
/* the button that opens a group of pages, and the group's size: pressed again, the next */
static const uint8_t BTN_PAGE[14][2] = {
    [8] = {0, 1}, [3] = {1, 5}, [6] = {6, 2}, [4] = {8, 5}, [5] = {13, 4}, [2] = {17, 5},
    [7] = {22, 1}, [10] = {23, 2}, [11] = {25, 8},
};

static uint32_t page, preset, octave_shift = 2;   /* octave 0..4 around the panel's F3-G5 */
static int32_t key_note[27];
static uint32_t keys_held;
static uint8_t dirty = 1;

/* a parameter as a position in [0, 1], and back: times and frequencies move exponentially */
static int is_expo(const mh_param *d) { return d->min > 0.f && d->max / d->min >= 100.f; }
static float to_pos(int i, float v)
{
    const mh_param *d = &mh_params[i];
    if (is_expo(d))
        return mh_log2(v / d->min) / mh_log2(d->max / d->min);
    return (v - d->min) / (d->max - d->min);
}
static float from_pos(int i, float t)
{
    const mh_param *d = &mh_params[i];
    t = mh_clamp(t, 0.f, 1.f);
    if (is_expo(d))
        return d->min * mh_exp2(t * mh_log2(d->max / d->min));
    return d->min + t * (d->max - d->min);
}

static void turn(const char *name, int32_t steps)
{
    int i;
    mh_patch *p = mh_synth_patch(&syn);
    const mh_param *d;
    float v;
    if (!steps)
        return;
    i = mh_param_find(name);
    if (i < 0)
        return;
    d = &mh_params[i];
    v = mh_param_get(p, i);
    if (d->type == MH_PT_INT) {
        v += (float)steps;
    } else {
        /* faster turns go further: 1/256 a detent (half a semitone of cutoff, which the engine
           glides through), up to 8x when spun */
        int32_t a = steps < 0 ? -steps : steps;
        float k = a > 7 ? 8.f : (float)a;
        v = from_pos(i, to_pos(i, v) + (float)steps * k * (1.f / 256.f));
    }
    mh_param_set(p, i, v);
    dirty = 1;
}

static void load_preset(uint32_t n)
{
    mh_patch p;
    const preset_set_t *s;
    mh_patch_default(&p);
    for (s = PRESETS[n].sets; s->name; s++)
        mh_param_set(&p, mh_param_find(s->name), s->value);
    p.quality = mh_synth_patch(&syn)->quality;      /* the player's choice, across patches */
    mh_synth_set_patch(&syn, &p);
    extras_cap = 7;                                 /* a new patch may fit what the last didn't: */
    extras = 0;                                     /* it climbs to what fits */
#ifdef MH_PIN_X
    extras = MH_PIN_X;
#endif
    apply_extras();
    dirty = 1;
}

/* ---------------------------------------------------------------------- core 1, the LCD --- */
/* Core 1: never started here, but the firmware before may have left it running; hold it before
 * the startup writes RAM under it (as fm1-sid's player and Felucca 1.6.1 do). */
#define C1_CON (*(volatile uint32_t *)0x1EEE004u)
#define DBG_WR_EN (*(volatile uint32_t *)0x1EEE240u)
#define DBG_MSG_CLR (*(volatile uint32_t *)0x1EEE248u)
#define DBG_EN (*(volatile uint32_t *)0x1EEE340u)
#define DBG_BUS 0x003F0030u
static void cpu1_halt(void)
{
    uint32_t locked = !(DBG_WR_EN & 1u), en;
    volatile uint32_t n;
    if (locked)
        DBG_WR_EN = 0xE7u;
    en = DBG_EN;
    DBG_EN = en & ~DBG_BUS;
    __asm__ volatile("csync" ::: "memory");
    C1_CON |= 0x2u;
    C1_CON &= ~0x8u;
    __asm__ volatile("csync" ::: "memory");
    for (n = 0; n < 256u; n++)
        ;
    DBG_MSG_CLR = DBG_BUS;
    __asm__ volatile("csync" ::: "memory");
    DBG_EN = en;
    if (locked && (DBG_WR_EN & 1u))
        DBG_WR_EN = 0xE7u;
    __asm__ volatile("csync" ::: "memory");
}

#include "fm1_cpu1.h"

/* the synth's jobs on core 1: the mailbox carries a function of one 32-bit argument */
static void (*c1_job)(void *);
static void c1_tramp(uint32_t arg) { c1_job((void *)(uintptr_t)arg); }
static int c1_offload(void (*job)(void *), void *arg)
{
    c1_job = job;
    return fm1_cpu1_run(c1_tramp, (uint32_t)(uintptr_t)arg);
}
static int c1_join(void) { return fm1_cpu1_wait(); }

/* start core 1 (its waiting loop copied to RAM first) and hand the synth to it, or don't */
static void core1_up(void)
{
    extern uint32_t _c1_start[], _c1_end[], _c1_load[];
    uint32_t *d, *s;
    for (s = _c1_load, d = _c1_start; d < _c1_end; s++, d++)
        *d = *s;
    __asm__ volatile("csync" ::: "memory");
    if (fm1_cpu1_start() == 0)
        mh_synth_set_offload(&syn, c1_offload, c1_join);
}

static uint16_t fb[240 * 240];
static uint8_t lcd_up;

static void lcd_cmd(uint8_t c, const uint8_t *d, uint32_t n)
{
    static uint8_t buf[8];
    uint32_t i;
    fm1_lcd_send_cmd(c);
    fm1_lcd_wait();
    if (n) {
        for (i = 0; i < n; i++)
            buf[i] = d[i];
        fm1_lcd_send_data(buf, n);
        fm1_lcd_wait();
    }
    fm1_lcd_deselect();
}

static void lcd_init(void)
{
    static const uint8_t c565 = 0x55, mad = 0x00;
    fm1_lcd_hw_init();
    fm1_delay_ms(80);
    lcd_cmd(0x01, 0, 0);
    fm1_delay_ms(150);
    lcd_cmd(0x11, 0, 0);
    fm1_delay_ms(120);
    lcd_cmd(0x3A, &c565, 1);
    lcd_cmd(0x36, &mad, 1);
    lcd_cmd(0x21, 0, 0);
    lcd_cmd(0x13, 0, 0);
    fm1_lcd_baud(4);
    lcd_cmd(0x29, 0, 0);
    lcd_up = 1;
}

static void lcd_frame(void)
{
    static const uint8_t win[4] = {0, 0, 0, 239};
    uint32_t y;
    lcd_cmd(0x2A, win, 4);
    lcd_cmd(0x2B, win, 4);
    fm1_lcd_send_cmd(0x2C);
    fm1_lcd_wait();
    for (y = 0; y < 240; y += 24) {
        fm1_lcd_send_data(fb + y * 240, 240u * 24u * 2u);
        fm1_lcd_wait();
    }
    fm1_lcd_deselect();
}

/* RGB565, byte-swapped as the panel takes it */
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8 | ((g) & 0xFC) << 3 | (b) >> 3) >> 8 | \
                                 (((r) & 0xF8) << 8 | ((g) & 0xFC) << 3 | (b) >> 3) << 8))
#define C_BG     RGB(0x14, 0x16, 0x1A)
#define C_PANEL  RGB(0x26, 0x2A, 0x31)
#define C_TEXT   RGB(0xEE, 0xEA, 0xE0)
#define C_DIM    RGB(0x8A, 0x8F, 0x98)
#define C_AMBER  RGB(0xFF, 0xA6, 0x2B)
#define C_RED    RGB(0xE8, 0x4A, 0x3A)
#define C_GREEN  RGB(0x6B, 0xD0, 0x8C)

static void fill(int x, int y, int w, int h, uint16_t c)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if ((unsigned)i < 240u && (unsigned)j < 240u)
                fb[j * 240 + i] = c;
}

static int text(int x, int y, const char *s, uint16_t c)
{
    for (; *s; s++, x += 8) {
        unsigned ch = (unsigned char)*s;
        const uint8_t *g;
        int r, b;
        if (ch < 32 || ch > 126)
            ch = '?';
        g = FONT + (ch - 32u) * 12u;
        for (r = 0; r < 12; r++)
            for (b = 0; b < 8; b++)
                if (g[r] >> (7 - b) & 1)
                    if ((unsigned)(x + b) < 240u && (unsigned)(y + r) < 240u)
                        fb[(y + r) * 240 + x + b] = c;
    }
    return x;
}

static char *cat(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }

static char *fmt_u(char *p, uint32_t v)
{
    char t[10];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 10);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

/* a value with up to `dec` decimals */
static char *fmt_f(char *p, float v, int dec)
{
    uint32_t scale = dec == 2 ? 100u : dec == 1 ? 10u : 1u, n;
    if (v < 0.f) {
        *p++ = '-';
        v = -v;
    }
    n = (uint32_t)(v * (float)scale + 0.5f);
    p = fmt_u(p, n / scale);
    if (dec) {
        *p++ = '.';
        n %= scale;
        if (dec == 2 && n < 10u)
            *p++ = '0';
        p = fmt_u(p, n);
    }
    return p;
}

/* small string helpers the value text needs (no libc in the image) */
static int strlen_(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int strstr_end(const char *s, const char *end)
{
    int a = strlen_(s), b = strlen_(end);
    return a >= b && !strcmp(s + a - b, end);
}

static void value_text(int i, float v, char *b)
{
    const char *name = mh_params[i].name;
    static const char *const LFO[] = {"TRI", "SINE", "SAW UP", "SAW DN", "SQUARE", "S&H", "SMOOTH"};
    static const char *const CHO[] = {"OFF", "JUNO I", "JUNO II", "JUNO I+II", "ENSEMBLE"};
    static const char *const PRIO[] = {"LAST", "LOW", "HIGH"};
    static const char *const ARP[] = {"OFF", "UP", "DOWN", "UP-DOWN", "RANDOM", "AS PLAYED"};
    static const char *const DIV[] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
    static const char *const DDIV[] = {"1/16", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2"};
    static const char *const SRC[] = {"OFF", "LFO 1", "LFO 2", "FILT ENV", "AMP ENV", "MOD ENV", "VELOCITY", "KEY",
                                      "WHEEL", "PRESSURE", "BEND", "RANDOM"};
    static const char *const DST[] = {"OFF", "PITCH", "PITCH 1", "PITCH 2", "PW 1", "PW 2", "SHAPE 1", "SHAPE 2",
                                      "LEVEL 1", "LEVEL 2", "SUB", "NOISE", "DRIVE", "CUTOFF", "RESO", "MODE", "FM",
                                      "LFO1 RATE", "LFO2 RATE", "DETUNE", "CHORUS", "AMP"};
    if (!strcmp(name, "quality")) { cat(b, v < 0.5f ? "HIGH" : "EFFICIENT"); return; }
    if (!strcmp(name, "filter")) {
        static const char *const FIL[] = {"LADDER", "SVF", "DIODE", "OTA", "SALLEN-KEY"};
        cat(b, FIL[(int)v]);
        return;
    }
    if (!strcmp(name, "arp_mode")) { cat(b, ARP[(int)v]); return; }
    if (!strcmp(name, "arp_div")) { cat(b, DIV[(int)v]); return; }
    if (!strcmp(name, "delay_div")) { cat(b, DDIV[(int)v]); return; }
    if (!strcmp(name, "osc1_model") || !strcmp(name, "osc2_model")) { cat(b, v < 0.5f ? "CLEAN" : "ACID"); return; }
    if (!strcmp(name, "arp_accent")) { if (v < 0.5f) cat(b, "OFF"); else cat(fmt_f(cat(b, "EVERY "), v, 0), ""); return; }
    if (!strcmp(name, "arp_gate")) { if (v >= 0.995f) cat(b, "TIE"); else cat(fmt_f(b, v * 100.f, 0), " %"); return; }
    if (!strcmp(name, "tempo")) { cat(fmt_f(b, v, 0), " BPM"); return; }
    if (!strcmp(name, "chorus")) { cat(b, CHO[(int)v]); return; }
    if (!strcmp(name, "priority")) { cat(b, PRIO[(int)v]); return; }
    if (strstr_end(name, "_src")) { cat(b, SRC[(int)v]); return; }
    if (strstr_end(name, "_dst")) { cat(b, DST[(int)v]); return; }
    if (!strcmp(name, "lfo1_shape") || !strcmp(name, "lfo2_shape")) { cat(b, LFO[(int)v]); return; }
    if (mh_params[i].type == MH_PT_INT) {
        if (mh_params[i].max == 1.f) { cat(b, v > 0.5f ? "ON" : "OFF"); return; }
        fmt_f(b, v, 0);
        return;
    }
    if (strstr_end(name, "_attack") || strstr_end(name, "_decay") || strstr_end(name, "_release") ||
        !strcmp(name, "glide") || !strcmp(name, "lfo1_delay")) {
        if (v < 1.f) cat(fmt_f(b, v * 1000.f, 0), " MS");
        else cat(fmt_f(b, v, 2), " S");
        return;
    }
    if (!strcmp(name, "cutoff") || !strcmp(name, "hpf")) { cat(fmt_f(b, v, 0), " HZ"); return; }
    if (!strcmp(name, "lfo1_rate") || !strcmp(name, "lfo2_rate")) { cat(fmt_f(b, v, 2), " HZ"); return; }
    fmt_f(b, v, mh_absf(v) >= 10.f ? 1 : 2);
}

static void draw(void)
{
    const page_t *pg = &PAGES[page];
    mh_patch *p = mh_synth_patch(&syn);
    char b[32];
    uint32_t k;
    fill(0, 0, 240, 240, C_BG);
    fill(0, 0, 240, 22, C_PANEL);
    text(6, 5, PRESETS[preset].title, C_AMBER);
    cat(fmt_u(cat(b, "OCT "), octave_shift), "");
    text(176, 5, b, C_DIM);
    if (dry)                                /* FX held: the effects off */
        text(136, 5, "DRY", C_AMBER);
    text(6, 30, pg->title, C_TEXT);
    for (k = 0; k < 4u; k++) {
        int i = pg->param[k] ? mh_param_find(pg->param[k]) : -1, y = 54 + 38 * (int)k;
        float v, t;
        if (i < 0)
            continue;
        v = mh_param_get(p, i);
        t = mh_clamp(to_pos(i, v), 0.f, 1.f);
        b[0] = (char)('1' + k);
        b[1] = ' ';
        b[2] = 0;
        cat(b + 2, pg->label[k]);
        text(6, y, b, C_DIM);
        b[0] = 0;
        value_text(i, v, b);
        text(234 - 8 * (int)strlen_(b), y, b, C_TEXT);
        fill(6, y + 16, 228, 6, C_PANEL);
        fill(6, y + 16, (int)(228.f * t), 6, C_AMBER);
    }
    /* the footer: quality, extras, load, MIDI */
    fill(0, 214, 240, 26, C_PANEL);
    text(6, 221, p->quality != MH_QUALITY_HIGH ? "EFF" : (eff_forced ? "HI-" : "HIGH"),
         p->quality == MH_QUALITY_HIGH && !eff_forced ? C_GREEN : C_RED);
    cat(fmt_u(cat(b, "X"), extras), "");
    text(46, 221, b, C_DIM);
    text(70, 221, fm1_c1_on ? "2C" : (fm1_c1_gave_up ? "1C!" : "1C"), fm1_c1_on ? C_GREEN : C_DIM);
    cat(fmt_u(cat(b, "LOAD "), load_peak_pc), "%");
    text(102, 221, b, load_peak_pc > 80u ? C_RED : C_TEXT);
    cat(fmt_u(cat(b, "M"), midi_msgs % 1000u), "");
    text(182, 221, b, C_DIM);
}


/* ---------------------------------------------------------------- crashes and boot loops --- */
static volatile struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
#define BOOTGUARD_MAGIC 0x4D4F4E4Fu         /* "MONO" */

static void fm1_fault(const fm1_crash_t *c)
{
    static const char hex[] = "0123456789ABCDEF";
    uint32_t v[3] = {c->vec, c->pc, c->rets}, i, k, t0;
    char b[16];
    fm1_audio_stop();
    if (lcd_up) {
        fill(0, 0, 240, 240, C_BG);
        text(48, 40, "MONOHULL CRASH", C_RED);
        for (i = 0; i < 3u; i++) {
            b[0] = "VPR"[i];
            b[1] = ' ';
            for (k = 0; k < 8u; k++)
                b[2 + k] = hex[(v[i] >> (28u - 4u * k)) & 15u];
            b[10] = 0;
            text(40, 80 + 20 * (int)i, b, C_TEXT);
        }
        text(16, 160, "RESTARTING", C_TEXT);
        text(16, 180, "IF IT REPEATS: USB UPDATE", C_DIM);
        lcd_frame();
    }
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 24000u)
        fm1_wdt_feed();
    fm1_reboot();
}

/* ------------------------------------------------------------------------------- the UI --- */
static void keys(uint32_t e, uint32_t rel)
{
    static uint32_t lit_page = ~0u, fx_down, fx_long;
    uint32_t cur = fm1_in.notes, taps = fm1_input_note_edges() & ~cur & ~keys_held, i;
    int32_t d;
    for (i = 0; i < 27u; i++) {
        uint32_t bit = 1u << i;
        if ((cur & ~keys_held & bit) || (taps & bit)) {
            int32_t n = 53 + (int32_t)i + 12 * ((int32_t)octave_shift - 2);
            key_note[i] = n;
            post(1, (uint32_t)n, (fm1_in.buttons & (1u << 13)) ? 127u : 85u); /* REC held: accent */
        }
        if ((keys_held & ~cur & bit) || (taps & bit))
            post(0, (uint32_t)key_note[i], 0);
    }
    keys_held = cur;
    if (e & (1u << 1) && octave_shift < 4u) { octave_shift++; dirty = 1; }
    if (e & (1u << 0) && octave_shift > 0u) { octave_shift--; dirty = 1; }
    if (e & (1u << 12))
        panic_req = 1;
    if (e & (1u << 10)) {                   /* ARP: on / off, the mode it had kept */
        static int last_mode = MH_ARP_UP;
        mh_patch *p = mh_synth_patch(&syn);
        if (p->arp_mode) {
            last_mode = p->arp_mode;
            p->arp_mode = MH_ARP_OFF;
        } else {
            p->arp_mode = last_mode;
        }
        dirty = 1;
    }
    /* FX: a press steps its pages (on release), a hold of 0.6 s turns the effects off or on */
    if (e & (1u << 2)) {
        fx_down = ms;
        fx_long = 0;
    }
    if ((fm1_in.buttons & (1u << 2)) && !fx_long && ms - fx_down >= 600u) {
        fx_long = 1;
        dry = !dry;
        apply_extras();
        dirty = 1;
    }
    e = (e & ~(1u << 2)) | ((rel & (1u << 2)) && !fx_long ? 1u << 2 : 0u);
    for (i = 0; i < 14u; i++) {
        if ((e & (1u << i)) && BTN_PAGE[i][1]) {
            uint32_t first = BTN_PAGE[i][0], n = BTN_PAGE[i][1];
            page = page >= first && page < first + n - 1u ? page + 1u : first;
            dirty = 1;
        }
    }
    d = fm1_enc_take(0);                    /* SELECT: the pages */
    if (d) {
        page = (page + NPAGES * 8u + (uint32_t)d) % NPAGES;
        dirty = 1;
    }
    d = fm1_enc_take(6);                    /* PRESETS */
    if (d) {
        preset = (preset + NPRESETS * 8u + (uint32_t)d) % NPRESETS;
        load_preset(preset);
    }
    turn("cutoff", fm1_enc_take(1));        /* ALGORITHM: always the cutoff */
    for (i = 0; i < 4u; i++) {
        int32_t st = fm1_enc_take(2u + i);
        if (PAGES[page].param[i])
            turn(PAGES[page].param[i], st);
    }
    {                                       /* the page's button lit; ARP lit while it plays */
        static int lit_arp = -1;
        int arp = mh_synth_patch(&syn)->arp_mode != 0;
        if (page != lit_page || arp != lit_arp) {
            lit_page = page;
            lit_arp = arp;
            for (i = 0; i < 14u; i++)
                fm1_led_key(i, i == 10u ? arp : BTN_PAGE[i][1] && page >= BTN_PAGE[i][0] &&
                                                    page < BTN_PAGE[i][0] + BTN_PAGE[i][1]);
        }
    }
}

static void master_poll(void)
{
    static int32_t knob = -1;
    int32_t a = fm1_adc_read(FM1_ADC_MASTER);
    if (a < 0)
        return;
    knob = knob < 0 ? a * 16 : knob + (a * 16 - knob) / 8;
    master_q12 = (int32_t)(((uint32_t)(knob / 16) * (uint32_t)(knob / 16)) >> 8);
}

static void fm1_main(void)
{
    uint32_t last_draw = 0, hold = 0, last_master = 0, last_ui = ~0u;
    lcd_init();
    fm1_input_init();
    fm1_adc_init();
    master_poll();
    fm1_uart1_midi_init(um_ring, UM_RING);
#ifdef MH_PROFILE
    mh_prof_clock = fm1_ticks_fn;
#endif
    mh_synth_init(&syn, RATE, 0x4D4F4E4Fu);
    load_preset(0);
    apply_extras();
#ifndef MH_ONE_CORE
    core1_up();
#endif
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
    fm1_timer5_start(isr_timer5, 4);
    fm1_irq_enable_all();
    for (;;) {
        uint32_t rel, e, now = ms;
        /* TIMER5 wakes the loop 10000 times a second; the panel needs 500 (the keys are
           debounced in the ISR, nothing is missed) */
        if (now - last_ui < 2u) {
            __asm__ volatile("idle");
            continue;
        }
        last_ui = now;
        fm1_wdt_feed();
        if (bootguard.pending && now > 30000u)
            bootguard.pending = bootguard.failed = 0;   /* 30 s up: this boot made it */
        e = fm1_input_edges(&rel);
        keys(e, rel);
        schedule(now);
        if (now - last_master >= 20u) {
            last_master = now;
            master_poll();
        }
        /* OCT- and OCT+ held 3 s: the USB update mode */
        if ((fm1_in.buttons & 3u) == 3u) {
            if (!hold)
                hold = now;
            else if (now - hold > 3000u) {
                fm1_audio_stop();
                bootguard.pending = 0;
                fm1_enter_uboot();
            }
        } else {
            hold = 0;
        }
        /* the screen only when something on it changed (and the load meter twice a second) */
        if (now - last_draw >= 40u && (dirty || now - last_draw >= 500u)) {
            last_draw = now;
            dirty = 0;
            draw();
            lcd_frame();
        }
        __asm__ volatile("idle");           /* until the next interrupt: power, and emulators skip ahead */
    }
}

void fm1_cstart(void)
{
    extern uint32_t _bss_start[], _bss_end[], _data_start[], _data_end[], _data_load[];
    uint32_t *w;
    cpu1_halt();
    fm1_time_init();
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = bootguard.pending = 0;
    }
    if (bootguard.pending)                  /* the last boot died within its first 30 s */
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 2u) {           /* the third in a row: the USB update mode instead */
        bootguard.failed = bootguard.pending = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (w = _bss_start; w < _bss_end; w++)
        *w = 0;
    for (w = _data_start; w < _data_end; w++)
        *w = _data_load[w - _data_start];
    fm1_mailbox_clear();
    fm1_main();
}
