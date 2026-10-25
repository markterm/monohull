//! Self-oscillation pitch against the cutoff, per note and feedback, with the tuning correction.
use monohull_host::analysis::*;
use monohull_host::*;
fn main() {
    let rate2 = 88_200.0;
    for newton in [true, false] {
        for k in [4.05f32, 4.1, 4.3, 4.5] {
            let mut line = format!("{} k {k}:", if newton { "Newton" } else { "cheap " });
            for note in [36, 48, 60, 72, 84, 96] {
                let fc = 440.0 * 2f64.powf((note as f64 - 69.0) / 12.0);
                let g = ladder_g(fc * ladder_tune(k) as f64, rate2);
                let mut l = Ladder::new();
                let mut out = Vec::new();
                for i in 0..(rate2 as usize * 6) {
                    out.push(l.step(if i == 0 { 0.5 } else { 0.0 }, g, k, newton).0);
                }
                let tail = &out[rate2 as usize * 5..];
                line += &format!(" {note}:{:+.1}c/{:.2}", cents(frequency(tail, rate2), fc), peak(tail));
            }
            println!("{line}");
        }
    }
}
