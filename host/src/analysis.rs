//! Spectra and measurements for the tests.

use rustfft::{num_complex::Complex, FftPlanner};

/// Magnitude spectrum in dB (Blackman-Harris window, 0 dB = a full-scale sine), bins 0..=n/2.
pub fn spectrum_db(x: &[f32]) -> Vec<f64> {
    let n = x.len().next_power_of_two();
    let mut buf: Vec<Complex<f64>> = vec![Complex::new(0.0, 0.0); n];
    let mut wsum = 0.0;
    for (i, &v) in x.iter().enumerate() {
        let t = i as f64 / (x.len() - 1) as f64;
        let w = 0.35875 - 0.48829 * (2.0 * std::f64::consts::PI * t).cos()
            + 0.14128 * (4.0 * std::f64::consts::PI * t).cos()
            - 0.01168 * (6.0 * std::f64::consts::PI * t).cos();
        wsum += w;
        buf[i] = Complex::new(v as f64 * w, 0.0);
    }
    FftPlanner::new().plan_fft_forward(n).process(&mut buf);
    buf[..=n / 2]
        .iter()
        .map(|c| 20.0 * (2.0 * c.norm() / wsum).max(1e-20).log10())
        .collect()
}

/// The strongest component in [lo, hi] Hz that is not within `guard` bins of a multiple of
/// `f0`, relative to the strongest harmonic, in dB (negative: below).
pub fn worst_alias_db(x: &[f32], rate: f64, f0: f64, lo: f64, hi: f64, guard: usize) -> (f64, f64) {
    let s = spectrum_db(x);
    let n = (s.len() - 1) * 2;
    let bin = rate / n as f64;
    let mut peak = f64::MIN;
    let mut h = 1;
    while h as f64 * f0 < rate / 2.0 {
        let b = (h as f64 * f0 / bin).round() as usize;
        for i in b.saturating_sub(guard)..=(b + guard).min(s.len() - 1) {
            peak = peak.max(s[i]);
        }
        h += 1;
    }
    let mut worst = f64::MIN;
    let mut at = 0.0;
    for (i, &v) in s.iter().enumerate() {
        let f = i as f64 * bin;
        if f < lo || f > hi {
            continue;
        }
        let near = (f / f0).round();
        if near >= 1.0 && ((f - near * f0) / bin).abs() <= guard as f64 {
            continue;
        }
        if v > worst {
            worst = v;
            at = f;
        }
    }
    (worst - peak, at)
}

/// Amplitude of the component at `f` (a correlation over the whole buffer: pick lengths that
/// hold whole periods).
pub fn amplitude(x: &[f32], rate: f64, f: f64) -> f64 {
    let (mut re, mut im) = (0.0, 0.0);
    for (i, &v) in x.iter().enumerate() {
        let ph = 2.0 * std::f64::consts::PI * f * i as f64 / rate;
        re += v as f64 * ph.cos();
        im += v as f64 * ph.sin();
    }
    2.0 * (re * re + im * im).sqrt() / x.len() as f64
}

/// The frequency of a periodic signal from its rising zero crossings (linearly interpolated).
pub fn frequency(x: &[f32], rate: f64) -> f64 {
    let mut first = None;
    let mut last = 0.0;
    let mut count = 0;
    for i in 1..x.len() {
        if x[i - 1] < 0.0 && x[i] >= 0.0 {
            let t = (i - 1) as f64 + (-x[i - 1] / (x[i] - x[i - 1])) as f64;
            if first.is_none() {
                first = Some(t);
            } else {
                count += 1;
            }
            last = t;
        }
    }
    match first {
        Some(f) if count > 0 => count as f64 * rate / (last - f),
        _ => 0.0,
    }
}

pub fn cents(f: f64, reference: f64) -> f64 {
    1200.0 * (f / reference).log2()
}

pub fn peak(x: &[f32]) -> f32 {
    x.iter().fold(0.0f32, |m, &v| m.max(v.abs()))
}

pub fn rms(x: &[f32]) -> f64 {
    (x.iter().map(|&v| (v as f64) * (v as f64)).sum::<f64>() / x.len().max(1) as f64).sqrt()
}
