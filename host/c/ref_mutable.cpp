// The reference the port is checked against: Mutable Instruments' code, unchanged, from
// ext/mutable (MIT), built for the host (stmlib's TEST: no ARM assembly).
#include "rings/dsp/fx/reverb.h"

static uint16_t rings_buffer[32768];
static rings::Reverb rings_reverb;

extern "C" void mhr_rings_init(void) { rings_reverb.Init(rings_buffer); }

extern "C" void mhr_rings_set(float amount, float input_gain, float time, float diffusion, float lp) {
    rings_reverb.set_amount(amount);
    rings_reverb.set_input_gain(input_gain);
    rings_reverb.set_time(time);
    rings_reverb.set_diffusion(diffusion);
    rings_reverb.set_lp(lp);
}

extern "C" void mhr_rings_process(float *left, float *right, int n) { rings_reverb.Process(left, right, n); }
