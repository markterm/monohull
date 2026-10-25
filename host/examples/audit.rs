//! The factory patches measured the way an ear would judge them, without one: how hard each
//! works the drive and the filter (deviation from a linear twin), its loudness, its peaks, how
//! often the output limiter works, its low end and how it sums to mono.
//!     cargo run --release --example audit
use monohull_host::analysis::*;
use monohull_host::*;

fn db(x: f64) -> f64 {
    20.0 * x.max(1e-12).log10()
}

/// A biquad from the RBJ cookbook (b0 b1 b2 a1 a2, a0 = 1).
fn biquad(x: &[f64], c: [f64; 5]) -> Vec<f64> {
    let (mut x1, mut x2, mut y1, mut y2) = (0.0, 0.0, 0.0, 0.0);
    x.iter()
        .map(|&v| {
            let y = c[0] * v + c[1] * x1 + c[2] * x2 - c[3] * y1 - c[4] * y2;
            x2 = x1;
            x1 = v;
            y2 = y1;
            y1 = y;
            y
        })
        .collect()
}

/// Loudness as BS.1770 weighs it (its K filter: a +4 dB shelf above ~1.7 kHz, a 38 Hz high-pass),
/// ungated, both channels: LUFS of the whole phrase.
fn loudness(l: &[f32], r: &[f32], rate: f64) -> f64 {
    use std::f64::consts::PI;
    let (f0, g, q) = (1681.974450955533, 3.999843853973347, 0.7071752369554196);
    let a = 10f64.powf(g / 40.0);
    let w = 2.0 * PI * f0 / rate;
    let al = w.sin() / (2.0 * q);
    let cw = w.cos();
    let a0 = (a + 1.0) - (a - 1.0) * cw + 2.0 * a.sqrt() * al;
    let shelf = [a * ((a + 1.0) + (a - 1.0) * cw + 2.0 * a.sqrt() * al) / a0,
                 -2.0 * a * ((a - 1.0) + (a + 1.0) * cw) / a0,
                 a * ((a + 1.0) + (a - 1.0) * cw - 2.0 * a.sqrt() * al) / a0,
                 2.0 * ((a - 1.0) - (a + 1.0) * cw) / a0,
                 ((a + 1.0) - (a - 1.0) * cw - 2.0 * a.sqrt() * al) / a0];
    let (f1, q1) = (38.13547087602444, 0.5003270373238773);
    let w = 2.0 * PI * f1 / rate;
    let al = w.sin() / (2.0 * q1);
    let cw = w.cos();
    let a0 = 1.0 + al;
    let hp = [(1.0 + cw) / 2.0 / a0, -(1.0 + cw) / a0, (1.0 + cw) / 2.0 / a0, -2.0 * cw / a0, (1.0 - al) / a0];
    let mut z = 0.0;
    for ch in [l, r] {
        let x: Vec<f64> = ch.iter().map(|&v| v as f64).collect();
        let y = biquad(&biquad(&x, shelf), hp);
        z += y.iter().map(|v| v * v).sum::<f64>() / y.len() as f64;
    }
    -0.691 + 10.0 * z.log10()
}

fn main() {
    let rate = FM1_RATE;
    println!("{:<11} {:>7} {:>7} {:>6} {:>7} {:>7} {:>6} {:>7} {:>7}", "patch", "drive%", "filt%", "u pk",
             "LUFS", "peak dB", "lim%", "<150Hz", "mono dB");
    for (n, name) in Synth::preset_names().iter().enumerate() {
        let mut s = Synth::new(rate);
        s.load_preset(n);
        // a phrase: C2, C3, G3, C4 held, then a short line, at mixed velocities
        let mut l = Vec::new();
        let mut r = Vec::new();
        s.stats();
        for &(note, vel, on, off) in &[(36, 0.9f32, 0.6f32, 0.2f32), (48, 0.7, 0.6, 0.2), (55, 1.0, 0.4, 0.1),
                                       (60, 0.6, 1.2, 0.4), (67, 0.9, 0.15, 0.05), (63, 0.5, 0.15, 0.05),
                                       (60, 1.0, 0.3, 0.6)] {
            s.note_on(note, vel);
            let (a, b) = s.render((on * rate) as usize);
            l.extend(a);
            r.extend(b);
            s.note_off(note);
            let (a, b) = s.render((off * rate) as usize);
            l.extend(a);
            r.extend(b);
        }
        let st = s.stats();
        let both: Vec<f32> = l.iter().chain(&r).copied().collect();
        let lim = both.iter().filter(|v| v.abs() > 0.9).count() as f64 / both.len() as f64;
        let mono: Vec<f32> = l.iter().zip(&r).map(|(a, b)| 0.5 * (a + b)).collect();
        let side_rms = rms(&both);
        // the low end: energy below 150 Hz against the whole, from a one-pole split
        let a = 1.0 - (-2.0 * std::f64::consts::PI * 150.0 / rate as f64).exp();
        let (mut z, mut lo2, mut all2) = (0.0f64, 0.0f64, 0.0f64);
        for &v in &mono {
            z += a * (v as f64 - z);
            lo2 += z * z;
            all2 += (v as f64) * (v as f64);
        }
        println!("{:<11} {:>7.1} {:>7.1} {:>6.2} {:>7.1} {:>7.1} {:>6.2} {:>6.0}% {:>7.1}", name, 100.0 * st[0],
                 100.0 * st[1], st[2], loudness(&l, &r, rate as f64), db(peak(&both) as f64), 100.0 * lim,
                 100.0 * (lo2 / all2.max(1e-30)), db(rms(&mono)) - db(side_rms));
    }
}
