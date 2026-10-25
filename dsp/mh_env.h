/* Monohull: the ADSR, shaped like an analogue RC envelope (Ultramaster KR-106's Juno-6 model):
   the attack charges toward 1.5 and ends at 1.0, so it is fast and convex; decay and release
   fall exponentially. Times are to -60 dB (attack: to full level). */
#ifndef MH_ENV_H
#define MH_ENV_H

enum { MH_ENV_IDLE, MH_ENV_ATTACK, MH_ENV_DECAY, MH_ENV_SUSTAIN, MH_ENV_RELEASE };

typedef struct {
    int stage;
    float level;
    float ac, dc, rc; /* per-sample coefficients */
    float sustain;
    int loop; /* attack-decay repeats while the gate is held */
    float set[5]; /* the times and rate the coefficients were made for */
} mh_env;

void mh_env_init(mh_env *e);

/* Times in seconds, sustain in [0, 1], at `rate`. Cheap enough per control block. */
void mh_env_set(mh_env *e, float attack, float decay, float sustain, float release, int loop, float rate);

/* Gate on: attack from the current level (never from zero: that clicks). */
void mh_env_gate_on(mh_env *e);
void mh_env_gate_off(mh_env *e);

static inline float mh_env_step(mh_env *e) {
    switch (e->stage) {
    case MH_ENV_ATTACK:
        e->level += (1.5f - e->level) * e->ac;
        if (e->level >= 1.f) {
            e->level = 1.f;
            e->stage = MH_ENV_DECAY;
        }
        break;
    case MH_ENV_DECAY:
        e->level += (e->sustain - e->level) * e->dc;
        if (e->level - e->sustain < 1e-4f)
            e->stage = e->loop ? MH_ENV_ATTACK : MH_ENV_SUSTAIN;
        break;
    case MH_ENV_SUSTAIN:
        e->level += (e->sustain - e->level) * e->dc; /* follows a moved sustain smoothly */
        break;
    case MH_ENV_RELEASE:
        e->level -= e->level * e->rc;
        if (e->level < 1e-5f) {
            e->level = 0.f;
            e->stage = MH_ENV_IDLE;
        }
        break;
    default:
        break;
    }
    return e->level;
}

#endif
