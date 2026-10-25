#include "mh_resample.h"

#include <string.h>

void mh_halfband_init(mh_halfband *h, const float *c, int n) {
    memset(h, 0, sizeof *h);
    h->c = c;
    h->n = n;
}
