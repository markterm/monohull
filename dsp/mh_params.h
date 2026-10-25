/* Monohull: the patch's parameters by name, with their ranges and MIDI CCs: one table for the
   panel, presets, CCs and the host harness. */
#ifndef MH_PARAMS_H
#define MH_PARAMS_H

#include <stddef.h>

#include "mh_synth.h"

enum { MH_PT_FLOAT, MH_PT_INT };

typedef struct {
    const char *name;
    size_t offset;
    int type;
    float min, max;
    int cc; /* MIDI CC (0 = none) */
} mh_param;

extern const mh_param mh_params[];
extern const int mh_param_count;

/* Index of `name`, or -1. */
int mh_param_find(const char *name);

/* Set or get by index; values are clamped to the range (ints rounded). */
void mh_param_set(mh_patch *p, int index, float value);
float mh_param_get(const mh_patch *p, int index);

/* A CC value (0-127) to the parameter it is mapped to; returns 0 if none is. */
int mh_param_cc(mh_patch *p, int cc, int value);

#endif
