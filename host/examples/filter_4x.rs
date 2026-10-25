//! What running a filter at 4x instead of 2x buys: each filter alone, fed a clean sine (2637 Hz,
//! E7) at a level a hard drive leaves, the cutoff at twice it and resonance high, at 88.2 kHz and
//! at 176.4 kHz; the strongest non-harmonic component in 20 Hz-16 kHz against the fundamental.
//!     cargo run --release --example filter_4x
use monohull_host::analysis::*;
use monohull_host::*;

fn run(rate: f64, f0: f64, amp: f32, mut step: impl FnMut(f32, f32) -> f32, fc: f64) -> (f64, f64) {
    let g = (std::f64::consts::PI * fc / rate).tan() as f32;
    let n = (1usize << 16) * (rate / 88_200.0) as usize;
    let skip = rate as usize / 10;
    let y: Vec<f32> = (0..n + skip)
        .map(|i| step(amp * (2.0 * std::f64::consts::PI * f0 * i as f64 / rate).sin() as f32, g))
        .collect();
    worst_alias_db(&y[skip..], rate, f0, 20.0, 16_000.0, 12)
}

fn main() {
    let f0 = 2637.02;
    for &amp in &[0.7f32, 1.5, 3.0] {
        for &(k4, k2) in &[(3.44f32, 1.69f32), (3.9, 1.9)] {
            let mut rows = Vec::new();
            for over in [1.0, 2.0] {
                let rate = 88_200.0 * over;
                let mut l = Ladder::new();
                let a = run(rate, f0, amp, |x, g| l.step(x, g, k4, true).0, 2.0 * f0);
                let mut o = Ota::new();
                let b = run(rate, f0, amp, |x, g| o.step(x, g, k4, true).0, 2.0 * f0);
                let mut s = K35::new(false);
                let c = run(rate, f0, amp, |x, g| s.step(x, g, k2), 2.0 * f0);
                rows.push(format!("{}x: ladder {:.1}, OTA {:.1}, Sallen-Key {:.1} dB", 2.0 * over, a.0, b.0, c.0));
            }
            println!("level {amp}, ladder/OTA k {k4} (SK K {k2}): {}", rows.join(" | "));
        }
    }
}
