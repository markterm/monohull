/* Monohull: the diode ladder (TB-303 character), run at the 2x rate.

   The normalised diode ladder as Stinchcombe analyses it and Robin Schmidt's Open303 runs it in
   its TB-303 mode: four capacitors coupled both ways,
       y1' = w (2 (u - y1 + y2)),  y2' = w (y1 - 2 y2 + y3),  y3' = w (y2 - 2 y3 + y4),  y4' = w (y3 - 2 y4)
   with the feedback u = x - k hp(sat(y4)) taken through a high-pass (~150 Hz, the 303's coupling).
   Linear, its peak (and the edge of self-oscillation) is at sqrt 2 w with k = 17, its gain at DC
   is 1, and its poles are spread (the lowest at 0.15 w): the passband falls gently from well
   below the peak, the resonance stands over it, the 303's shape. The high-passed feedback adds a
   little phase, so at full resonance it rings hard but, like a 303, does not quite self-oscillate.

   Trapezoidal integration of the whole system, prewarped so self-oscillation lands on the
   cutoff; the feedback solved in the same sample (the stages are tridiagonal: two short solves);
   the feedback's saturation (tanh) linearised by its secant from the previous sample. */
#ifndef MH_DIODE_H
#define MH_DIODE_H

#define MH_DIODE_K_OSC 17.f /* the feedback that starts self-oscillation (linear) */

typedef struct {
    float y[4];  /* capacitor voltages */
    float u;     /* the last feedback node */
    float hp_s;  /* the feedback high-pass's integrator */
    float sig;   /* tanh(y4) / y4, last sample */
} mh_diode;

void mh_diode_init(mh_diode *d);

/* One sample at `rate`: `fc` the cutoff (Hz, where it self-oscillates), `k` the feedback,
   `hp_g` = tan(pi f_hp / rate) for the feedback high-pass. Returns the low-pass output. */
float mh_diode_step(mh_diode *d, float x, float fc, float rate, float k, float hp_g);

#endif
