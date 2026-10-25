#include "mh_env.h"

#include "mh_math.h"

void mh_env_init(mh_env *e) {
    e->stage = MH_ENV_IDLE;
    e->level = 0.f;
    e->ac = e->dc = e->rc = 1.f;
    e->sustain = 1.f;
    e->loop = 0;
    for (int i = 0; i < 5; i++)
        e->set[i] = -1.f;
}

void mh_env_set(mh_env *e, float attack, float decay, float sustain, float release, int loop, float rate) {
    e->sustain = mh_clamp(sustain, 0.f, 1.f);
    e->loop = loop;
    if (attack == e->set[0] && decay == e->set[1] && release == e->set[2] && rate == e->set[3])
        return; /* unchanged: the coefficients are three exp2 and three divisions */
    e->set[0] = attack;
    e->set[1] = decay;
    e->set[2] = release;
    e->set[3] = rate;
    /* 0 -> 1 toward 1.5 takes tau ln 3; -60 dB takes tau ln 1000 */
    e->ac = mh_onepole(mh_clamp(attack, 1e-4f, 30.f) * (1.f / 1.0986123f), rate);
    e->dc = mh_onepole(mh_clamp(decay, 1e-3f, 30.f) * (1.f / 6.9077553f), rate);
    e->rc = mh_onepole(mh_clamp(release, 1e-3f, 30.f) * (1.f / 6.9077553f), rate);
}

void mh_env_gate_on(mh_env *e) {
    e->stage = MH_ENV_ATTACK;
}

void mh_env_gate_off(mh_env *e) {
    if (e->stage != MH_ENV_IDLE)
        e->stage = MH_ENV_RELEASE;
}
