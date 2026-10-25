//! The golden reference for the ladder: the ODE Miller Puckette's bob~ integrates
//! (Stinchcombe's analysis of the Moog ladder), in float64 with classic RK4 at a high rate.
//!
//!   dy1/dt = wc (sat(x - k y4) - sat(y1)),  dy_i/dt = wc (sat(y_(i-1)) - sat(y_i))
//!
//! bob~ uses a clipped cubic for `sat`; Monohull's ladder uses tanh, and so does this
//! reference, so the comparison measures Monohull's discretisation and solver only.

/// Integrate the ladder over `x` (input sampled at `rate`), `over` RK4 steps per input sample,
/// the input linearly interpolated between samples. Returns y4 at each input sample.
/// The OTA cascade's equations (mh_ota.h) integrated the same way: dy_i/dt = wc tanh(y_(i-1) - y_i),
/// the first stage driven by u = x - k y_4.
pub fn ota_rk4(x: &[f64], rate: f64, fc: f64, k: f64, over: usize) -> Vec<f64> {
    let wc = 2.0 * std::f64::consts::PI * fc;
    let h = 1.0 / (rate * over as f64);
    let deriv = |y: &[f64; 4], u: f64| -> [f64; 4] {
        [
            wc * (u - k * y[3] - y[0]).tanh(),
            wc * (y[0] - y[1]).tanh(),
            wc * (y[1] - y[2]).tanh(),
            wc * (y[2] - y[3]).tanh(),
        ]
    };
    let mut y = [0.0f64; 4];
    let mut out = Vec::with_capacity(x.len());
    for n in 0..x.len() {
        let x0 = if n == 0 { 0.0 } else { x[n - 1] };
        let x1 = x[n];
        for s in 0..over {
            let a = s as f64 / over as f64;
            let b = (s as f64 + 0.5) / over as f64;
            let c = (s as f64 + 1.0) / over as f64;
            let (ua, ub, uc) = (x0 + (x1 - x0) * a, x0 + (x1 - x0) * b, x0 + (x1 - x0) * c);
            let add = |y: &[f64; 4], d: &[f64; 4], f: f64| [y[0] + d[0] * f, y[1] + d[1] * f, y[2] + d[2] * f, y[3] + d[3] * f];
            let k1 = deriv(&y, ua);
            let k2 = deriv(&add(&y, &k1, h / 2.0), ub);
            let k3 = deriv(&add(&y, &k2, h / 2.0), ub);
            let k4 = deriv(&add(&y, &k3, h), uc);
            for i in 0..4 {
                y[i] += h / 6.0 * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
            }
        }
        out.push(y[3]);
    }
    out
}

pub fn ladder_rk4(x: &[f64], rate: f64, fc: f64, k: f64, over: usize) -> Vec<f64> {
    let wc = 2.0 * std::f64::consts::PI * fc;
    let h = 1.0 / (rate * over as f64);
    let deriv = |y: &[f64; 4], u: f64| -> [f64; 4] {
        let t: [f64; 4] = [y[0].tanh(), y[1].tanh(), y[2].tanh(), y[3].tanh()];
        [
            wc * ((u - k * y[3]).tanh() - t[0]),
            wc * (t[0] - t[1]),
            wc * (t[1] - t[2]),
            wc * (t[2] - t[3]),
        ]
    };
    let mut y = [0.0f64; 4];
    let mut out = Vec::with_capacity(x.len());
    for n in 0..x.len() {
        let x0 = if n == 0 { 0.0 } else { x[n - 1] };
        let x1 = x[n];
        for s in 0..over {
            let a = s as f64 / over as f64;
            let b = (s as f64 + 0.5) / over as f64;
            let c = (s as f64 + 1.0) / over as f64;
            let (ua, ub, uc) = (x0 + (x1 - x0) * a, x0 + (x1 - x0) * b, x0 + (x1 - x0) * c);
            let add = |y: &[f64; 4], d: &[f64; 4], f: f64| [y[0] + d[0] * f, y[1] + d[1] * f, y[2] + d[2] * f, y[3] + d[3] * f];
            let k1 = deriv(&y, ua);
            let k2 = deriv(&add(&y, &k1, h / 2.0), ub);
            let k3 = deriv(&add(&y, &k2, h / 2.0), ub);
            let k4 = deriv(&add(&y, &k3, h), uc);
            for i in 0..4 {
                y[i] += h / 6.0 * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
            }
        }
        out.push(y[3]);
    }
    out
}
