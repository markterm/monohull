#include "mh_params.h"

#include <string.h>

#include "mh_math.h"

#define F(name, field, lo, hi, cc) {name, offsetof(mh_patch, field), MH_PT_FLOAT, lo, hi, cc}
#define I(name, field, lo, hi, cc) {name, offsetof(mh_patch, field), MH_PT_INT, lo, hi, cc}

const mh_param mh_params[] = {
    I("quality", quality, 0, 1, 0),
    F("osc1_shape", shape[0], 0, 1, 14),
    F("osc2_shape", shape[1], 0, 1, 15),
    I("osc1_model", model[0], 0, MH_MODELS - 1, 0),
    I("osc2_model", model[1], 0, MH_MODELS - 1, 0),
    F("osc1_pw", pw[0], 0.02f, 0.98f, 16),
    F("osc2_pw", pw[1], 0.02f, 0.98f, 17),
    F("osc1_semi", semi[0], -24, 24, 0),
    F("osc2_semi", semi[1], -24, 24, 18),
    F("osc1_fine", fine[0], -100, 100, 0),
    F("osc2_fine", fine[1], -100, 100, 19),
    F("osc1_level", level[0], 0, 1, 20),
    F("osc2_level", level[1], 0, 1, 21),
    I("osc1_unison", unison[0], 1, 7, 0),
    I("osc2_unison", unison[1], 1, 7, 0),
    F("detune", detune, 0, 1, 22),
    F("unison_mix", unison_mix, 0, 1, 23),
    F("spread", spread, 0, 1, 24),
    I("stereo", stereo, 0, 1, 0),
    F("sub_level", sub_level, 0, 1, 25),
    I("sub_oct", sub_oct, 1, 2, 0),
    F("noise_level", noise_level, 0, 1, 26),
    I("noise_pink", noise_pink, 0, 1, 0),
    F("ring_level", ring_level, 0, 1, 27),
    F("fm", fm, 0, 4, 28),
    I("sync", sync, 0, 1, 0),
    F("vintage", vintage, 0, 1, 29),
    F("drive", drive, 0, 1, 30),
    I("filter", filter, 0, 4, 0),
    F("cutoff", cutoff, 16, 20000, 74),
    F("resonance", resonance, 0, 1, 71),
    F("morph", morph, 0, 3, 31),
    F("key_track", key_track, 0, 1, 0),
    F("bass_comp", bass_comp, 0, 1, 0),
    F("fenv_amount", fenv_amount, -8, 8, 47),
    F("fenv_velocity", fenv_velocity, 0, 1, 0),
    F("fenv_attack", fenv.attack, 0.0005f, 10, 48),
    F("fenv_decay", fenv.decay, 0.001f, 20, 49),
    F("fenv_sustain", fenv.sustain, 0, 1, 50),
    F("fenv_release", fenv.release, 0.001f, 20, 51),
    I("fenv_loop", fenv.loop, 0, 1, 0),
    F("aenv_attack", aenv.attack, 0.0005f, 10, 73),
    F("aenv_decay", aenv.decay, 0.001f, 20, 75),
    F("aenv_sustain", aenv.sustain, 0, 1, 76),
    F("aenv_release", aenv.release, 0.001f, 20, 72),
    I("aenv_loop", aenv.loop, 0, 1, 0),
    F("menv_attack", menv.attack, 0.0005f, 10, 0),
    F("menv_decay", menv.decay, 0.001f, 20, 0),
    F("menv_sustain", menv.sustain, 0, 1, 0),
    F("menv_release", menv.release, 0.001f, 20, 0),
    I("menv_loop", menv.loop, 0, 1, 0),
    F("amp_velocity", amp_velocity, 0, 1, 0),
    I("vca_exp", vca_exp, 0, 1, 0),
    F("lfo1_rate", lfo[0].rate, 0.01f, 50, 77),
    I("lfo1_shape", lfo[0].shape, 0, MH_LFO_SHAPES - 1, 0),
    F("lfo1_delay", lfo[0].delay, 0, 5, 0),
    F("lfo1_fade", lfo[0].fade, 0, 5, 0),
    I("lfo1_key_reset", lfo[0].key_reset, 0, 1, 0),
    F("lfo2_rate", lfo[1].rate, 0.01f, 50, 78),
    I("lfo2_shape", lfo[1].shape, 0, MH_LFO_SHAPES - 1, 0),
    F("lfo2_delay", lfo[1].delay, 0, 5, 0),
    F("lfo2_fade", lfo[1].fade, 0, 5, 0),
    I("lfo2_key_reset", lfo[1].key_reset, 0, 1, 0),
    F("vibrato", vibrato, 0, 2, 0),
    F("wheel_vibrato", wheel_vibrato, 0, 2, 0),
    F("lfo2_cutoff", lfo2_cutoff, -4, 4, 79),
    F("lfo1_pwm", lfo1_pwm, 0, 1, 0),
    F("menv_pitch2", menv_pitch2, -48, 48, 80),
    F("glide", glide, 0, 5, 5),
    I("glide_linear", glide_linear, 0, 1, 0),
    I("glide_legato", glide_legato, 0, 1, 0),
    I("legato", legato, 0, 1, 0),
    I("priority", priority, 0, 2, 0),
    F("bend_range", bend_range, 0, 12, 0),
    I("arp_mode", arp_mode, 0, MH_ARP_MODES - 1, 0),
    I("arp_oct", arp_oct, 1, 4, 0),
    I("arp_div", arp_div, 0, 5, 0),
    F("arp_gate", arp_gate, 0.05f, 1, 0),
    I("arp_latch", arp_latch, 0, 1, 0),
    I("arp_accent", arp_accent, 0, 8, 0),
    F("tempo", tempo, 40, 240, 0),
    F("hpf", hpf, 0, 2000, 81),
    F("volume", volume, 0, 1, 7),
    I("chorus", chorus, 0, MH_CHORUS_MODES - 1, 93),
    F("chorus_depth", chorus_depth, 0, 2, 0),
    F("chorus_mix", chorus_mix, 0, 1, 0),
    F("reverb_mix", reverb_mix, 0, 1, 91),
    F("reverb_time", reverb_time, 0, 1, 85),
    F("reverb_tone", reverb_tone, 0, 1, 86),
    F("delay_mix", delay_mix, 0, 1, 94),
    I("delay_div", delay_div, 0, MH_DELAY_DIVS - 1, 88),
    F("delay_feedback", delay_feedback, 0, 0.95f, 87),
    F("delay_tone", delay_tone, 0, 1, 89),
    I("mod1_src", matrix[0].src, 0, MH_SRC_COUNT - 1, 0),
    I("mod1_dst", matrix[0].dst, 0, MH_DST_COUNT - 1, 0),
    F("mod1_amount", matrix[0].amount, -1, 1, 0),
    I("mod2_src", matrix[1].src, 0, MH_SRC_COUNT - 1, 0),
    I("mod2_dst", matrix[1].dst, 0, MH_DST_COUNT - 1, 0),
    F("mod2_amount", matrix[1].amount, -1, 1, 0),
    I("mod3_src", matrix[2].src, 0, MH_SRC_COUNT - 1, 0),
    I("mod3_dst", matrix[2].dst, 0, MH_DST_COUNT - 1, 0),
    F("mod3_amount", matrix[2].amount, -1, 1, 0),
    I("mod4_src", matrix[3].src, 0, MH_SRC_COUNT - 1, 0),
    I("mod4_dst", matrix[3].dst, 0, MH_DST_COUNT - 1, 0),
    F("mod4_amount", matrix[3].amount, -1, 1, 0),
    I("mod5_src", matrix[4].src, 0, MH_SRC_COUNT - 1, 0),
    I("mod5_dst", matrix[4].dst, 0, MH_DST_COUNT - 1, 0),
    F("mod5_amount", matrix[4].amount, -1, 1, 0),
    I("mod6_src", matrix[5].src, 0, MH_SRC_COUNT - 1, 0),
    I("mod6_dst", matrix[5].dst, 0, MH_DST_COUNT - 1, 0),
    F("mod6_amount", matrix[5].amount, -1, 1, 0),
    I("mod7_src", matrix[6].src, 0, MH_SRC_COUNT - 1, 0),
    I("mod7_dst", matrix[6].dst, 0, MH_DST_COUNT - 1, 0),
    F("mod7_amount", matrix[6].amount, -1, 1, 0),
    I("mod8_src", matrix[7].src, 0, MH_SRC_COUNT - 1, 0),
    I("mod8_dst", matrix[7].dst, 0, MH_DST_COUNT - 1, 0),
    F("mod8_amount", matrix[7].amount, -1, 1, 0),
};

const int mh_param_count = (int)(sizeof mh_params / sizeof mh_params[0]);

int mh_param_find(const char *name) {
    for (int i = 0; i < mh_param_count; i++)
        if (strcmp(mh_params[i].name, name) == 0)
            return i;
    return -1;
}

void mh_param_set(mh_patch *p, int index, float value) {
    if (index < 0 || index >= mh_param_count)
        return;
    const mh_param *d = &mh_params[index];
    value = value < d->min ? d->min : value > d->max ? d->max : value;
    char *at = (char *)p + d->offset;
    if (d->type == MH_PT_INT)
        *(int *)at = (int)(value + (value < 0 ? -0.5f : 0.5f));
    else
        *(float *)at = value;
}

float mh_param_get(const mh_patch *p, int index) {
    if (index < 0 || index >= mh_param_count)
        return 0.f;
    const mh_param *d = &mh_params[index];
    const char *at = (const char *)p + d->offset;
    return d->type == MH_PT_INT ? (float)*(const int *)at : *(const float *)at;
}

int mh_param_cc(mh_patch *p, int cc, int value) {
    for (int i = 0; i < mh_param_count; i++) {
        const mh_param *d = &mh_params[i];
        if (d->cc != cc || cc == 0)
            continue;
        float t = (float)value * (1.f / 127.f);
        /* cutoff and times sweep exponentially, as the ear hears them */
        int expo = d->min > 0.f && d->max / d->min >= 100.f;
        float v = expo ? d->min * mh_exp2(t * mh_log2(d->max / d->min)) : d->min + t * (d->max - d->min);
        mh_param_set(p, i, v);
        return 1;
    }
    return 0;
}
