//! How much the filters' own nonlinearity aliases at the 2x rate: a sine (no oscillator aliases of
//! its own), driven, at high resonance, through each filter; the strongest component that isn't a
//! harmonic, against the fundamental. This is what running the filters at 4x would buy.
//!     cargo run --release --example filter_alias
use monohull_host::analysis::*;
use monohull_host::{Synth, FM1_RATE};

fn main() {
    let names = ["ladder", "SVF", "diode", "OTA", "Sallen-Key"];
    for (fi, name) in names.iter().enumerate() {
        let mut worst = (0.0f64, 0.0f64, String::new());
        for &note in &[84, 91, 96, 100] {
            for &drive in &[0.5f32, 1.0] {
                for &res in &[0.5f32, 0.8, 0.9] {
                    for &cut_ratio in &[1.0f32, 2.0] {
                        let f0 = 440.0 * 2f64.powf((note as f64 - 69.0) / 12.0);
                        let mut s = Synth::new(FM1_RATE);
                        s.set("osc1_shape", 1.0).set("filter", fi as f32).set("drive", drive).set("resonance", res);
                        s.set("cutoff", (f0 as f32 * cut_ratio).min(18_000.0)).set("key_track", 0.0).set("fenv_amount", 0.0);
                        s.set("vintage", 0.0).set("aenv_sustain", 1.0).set("volume", 0.5);
                        s.note_on(note, 1.0);
                        s.render(8820);
                        let (l, _) = s.render(1 << 16);
                        let (a, at) = worst_alias_db(&l, FM1_RATE as f64, f0, 20.0, 16_000.0, 12);
                        if a > worst.0 || worst.2.is_empty() {
                            worst = (a, at, format!("note {note} drive {drive} res {res} cutoff {cut_ratio}x"));
                        }
                    }
                }
            }
        }
        println!("{name:<11} worst alias {:.1} dB (at {:.0} Hz; {})", worst.0, worst.1, worst.2);
    }
}
