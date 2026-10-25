/* Monohull's factory patches: each a name and the parameters it changes from mh_patch_default.
 * SPDX-License-Identifier: GPL-3.0-only */
#ifndef MONOHULL_PRESETS_H
#define MONOHULL_PRESETS_H

typedef struct {
    const char *name;
    float value;
} preset_set_t;

typedef struct {
    const char *title;
    const preset_set_t *sets;
} preset_t;

#define P(...) (const preset_set_t[]){__VA_ARGS__, {0, 0}}

static const preset_t PRESETS[] = {
    {"SAW LEAD", P({"cutoff", 2000}, {"resonance", 0.3f}, {"delay_mix", 0.22f}, {"delay_feedback", 0.3f}, {"delay_tone", 0.45f}, {"reverb_mix", 0.25f}, {"reverb_time", 0.45f},
                    {"volume", 0.48f})},
    {"FAT BASS", P({"osc2_shape", 0.333f}, {"osc2_level", 0.6f}, {"osc2_semi", -12}, {"osc2_fine", 4},
                   {"sub_level", 0.4f}, {"cutoff", 220}, {"resonance", 0.45f}, {"drive", 0.45f},
                   {"fenv_amount", 3.5f}, {"fenv_decay", 0.25f}, {"fenv_sustain", 0.1f}, {"aenv_decay", 0.4f},
                   {"aenv_sustain", 0.7f}, {"aenv_release", 0.06f}, {"key_track", 0.3f}, {"bass_comp", 0.6f},
                   {"vintage", 0.4f}, {"volume", 0.47f})},
    {"SYNC LEAD", P({"osc1_level", 0}, {"osc2_level", 1}, {"sync", 1}, {"osc2_semi", 7}, {"menv_pitch2", 19},
                    {"menv_decay", 0.35f}, {"menv_sustain", 0.25f}, {"cutoff", 3500}, {"resonance", 0.25f},
                    {"drive", 0.35f}, {"fenv_amount", 1.5f}, {"glide", 0.06f}, {"glide_legato", 1},
                    {"lfo1_rate", 5.8f}, {"vibrato", 0.15f}, {"lfo1_delay", 0.3f}, {"lfo1_fade", 0.4f},
                    {"aenv_sustain", 0.9f}, {"aenv_release", 0.25f}, {"chorus", 1}, {"chorus_mix", 0.5f}, {"delay_mix", 0.2f}, {"delay_feedback", 0.3f}, {"delay_tone", 0.45f}, {"reverb_mix", 0.2f},
                    {"volume", 0.37f})},
    {"SUPERSAW", P({"osc1_unison", 7}, {"osc2_unison", 7}, {"osc2_level", 0.7f}, {"osc2_semi", 12},
                   {"detune", 0.42f}, {"unison_mix", 0.7f}, {"spread", 1}, {"stereo", 1}, {"cutoff", 2400},
                   {"resonance", 0.15f}, {"drive", 0.15f}, {"fenv_amount", 1}, {"fenv_attack", 0.6f},
                   {"fenv_decay", 1.5f}, {"fenv_sustain", 0.5f}, {"aenv_attack", 0.25f}, {"aenv_sustain", 1},
                   {"aenv_release", 0.9f}, {"chorus", 4}, {"chorus_mix", 0.6f}, {"volume", 0.45f})},
    {"PWM BRASS", P({"osc1_shape", 0.333f}, {"osc1_pw", 0.4f}, {"osc2_level", 0.7f}, {"osc2_fine", 7},
                    {"lfo1_rate", 0.7f}, {"lfo1_pwm", 0.6f}, {"cutoff", 700}, {"resonance", 0.2f},
                    {"fenv_amount", 3}, {"fenv_attack", 0.08f}, {"fenv_decay", 0.5f}, {"fenv_sustain", 0.45f},
                    {"aenv_attack", 0.03f}, {"aenv_sustain", 0.9f}, {"aenv_release", 0.3f}, {"chorus", 3},
                    {"chorus_mix", 0.8f}, {"vintage", 0.6f}, {"reverb_mix", 0.25f}, {"reverb_time", 0.45f},
                    {"volume", 0.33f})},
    /* the 303 way: the diode ladder, its peak at the cutoff over a gently falling passband; a big, short
       sweep; resonance just under the edge; velocity (accent: REC held, or MIDI) opening the sweep
       and the VCA; a short slide between held notes */
    {"ACID", P({"filter", 2}, {"cutoff", 420}, {"resonance", 0.9f}, {"drive", 0.45f}, {"fenv_amount", 3.6f},
               {"fenv_decay", 0.24f}, {"fenv_sustain", 0}, {"fenv_release", 0.05f}, {"fenv_velocity", 1},
               {"aenv_decay", 1.5f}, {"aenv_sustain", 0.35f}, {"aenv_release", 0.02f}, {"amp_velocity", 0.6f},
               {"glide", 0.06f}, {"glide_legato", 1}, {"key_track", 0.15f}, {"hpf", 30}, {"vintage", 0.1f}, {"volume", 1})},
    /* the same on the 303's square (Open303's model of it), arpeggiated: sixteenths over two
       octaves, every third step accented */
    {"ACID ARP", P({"osc1_shape", 0.333f}, {"osc1_model", 1}, {"filter", 2}, {"cutoff", 420}, {"resonance", 0.9f}, {"drive", 0.45f}, {"fenv_amount", 3.6f},
                   {"fenv_decay", 0.24f}, {"fenv_sustain", 0}, {"fenv_release", 0.05f}, {"fenv_velocity", 1},
                   {"aenv_decay", 1.5f}, {"aenv_sustain", 0.35f}, {"aenv_release", 0.02f}, {"amp_velocity", 0.6f},
                   {"glide", 0.06f}, {"glide_legato", 1}, {"key_track", 0.15f}, {"hpf", 30}, {"vintage", 0.1f}, {"arp_mode", 1}, {"arp_oct", 2}, {"arp_div", 3}, {"arp_gate", 0.55f},
                   {"arp_accent", 3}, {"tempo", 126}, {"delay_mix", 0.18f}, {"delay_feedback", 0.35f}, {"delay_tone", 0.35f},
                    {"volume", 0.8f})},
    {"SQUARE SUB", P({"osc1_shape", 0.333f}, {"sub_level", 0.7f}, {"sub_oct", 2}, {"cutoff", 900},
                     {"resonance", 0.2f}, {"fenv_amount", 2}, {"fenv_decay", 0.3f}, {"fenv_sustain", 0.2f}, {"volume", 0.28f})},
    {"SEM SWEEP", P({"filter", 1}, {"osc2_level", 0.8f}, {"osc2_fine", -6}, {"cutoff", 600},
                    {"resonance", 0.7f}, {"morph", 0.6f}, {"lfo2_rate", 0.25f}, {"lfo2_cutoff", 1.5f},
                    {"aenv_attack", 0.05f}, {"aenv_release", 0.5f}, {"reverb_mix", 0.2f},
                    {"volume", 0.63f})},
    {"WHISTLE", P({"osc1_level", 0}, {"noise_level", 0.4f}, {"cutoff", 1000}, {"resonance", 1},
                  {"key_track", 1}, {"fenv_amount", 0}, {"aenv_attack", 0.02f}, {"aenv_release", 0.3f},
                  {"vintage", 0.1f}, {"delay_mix", 0.2f}, {"delay_div", 5}, {"reverb_mix", 0.3f}, {"reverb_time", 0.6f},
                    {"volume", 1})},
    {"RING BELL", P({"osc1_shape", 1}, {"osc2_shape", 1}, {"osc2_semi", 7}, {"osc2_fine", 30},
                    {"ring_level", 0.8f}, {"osc1_level", 0.2f}, {"cutoff", 6000}, {"aenv_decay", 1.5f},
                    {"aenv_sustain", 0}, {"aenv_release", 1.2f}, {"fenv_amount", 0}, {"reverb_mix", 0.35f}, {"reverb_time", 0.6f}, {"reverb_tone", 0.6f},
                    {"volume", 1})},
    /* the OTA cascade (Roland's): a pulse swept by the LFO with a sub under it, and the chorus */
    {"PWM CHORUS", P({"filter", 3}, {"osc1_shape", 0.333f}, {"osc1_pw", 0.5f}, {"lfo1_rate", 0.6f}, {"lfo1_pwm", 0.7f},
                     {"sub_level", 0.45f}, {"cutoff", 1400}, {"resonance", 0.45f}, {"drive", 0.3f}, {"fenv_amount", 2},
                     {"fenv_decay", 0.6f}, {"fenv_sustain", 0.4f}, {"aenv_attack", 0.01f}, {"aenv_sustain", 0.85f},
                     {"aenv_release", 0.4f}, {"chorus", 1}, {"chorus_mix", 0.8f}, {"reverb_mix", 0.2f}, {"volume", 0.46f})},
    /* the Sallen-Key (Korg's KORG-35): two saws a little apart, the peak high and driven */
    {"SCREAM", P({"filter", 4}, {"osc2_level", 0.8f}, {"osc2_fine", 8}, {"cutoff", 1100}, {"resonance", 0.92f},
                 {"drive", 0.6f}, {"fenv_amount", 2.5f}, {"fenv_decay", 0.45f}, {"fenv_sustain", 0.3f},
                 {"aenv_sustain", 0.9f}, {"aenv_release", 0.2f}, {"glide", 0.04f}, {"glide_legato", 1},
                 {"delay_mix", 0.15f}, {"volume", 0.26f})},
    /* a Juno pad: two pulses a few cents apart, both swept by the slow LFO, a little sub, the OTA
       cascade dark and opening with the swell, Juno I+II and a long room */
    {"PWM PAD", P({"filter", 3}, {"osc1_shape", 0.333f}, {"osc1_pw", 0.5f}, {"osc2_shape", 0.333f}, {"osc2_pw", 0.35f},
                  {"osc2_level", 0.7f}, {"osc2_fine", 6}, {"sub_level", 0.3f}, {"lfo1_rate", 0.35f}, {"lfo1_pwm", 0.75f},
                  {"cutoff", 800}, {"resonance", 0.2f}, {"fenv_amount", 1.2f}, {"fenv_attack", 1.4f},
                  {"fenv_decay", 2}, {"fenv_sustain", 0.6f}, {"aenv_attack", 0.9f}, {"aenv_sustain", 1},
                  {"aenv_release", 1.6f}, {"vintage", 0.5f}, {"chorus", 3}, {"chorus_mix", 0.8f},
                  {"reverb_mix", 0.3f}, {"reverb_time", 0.65f}, {"volume", 0.49f})},
};

#define NPRESETS (sizeof PRESETS / sizeof PRESETS[0])

#endif
