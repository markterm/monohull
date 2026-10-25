//! Aliasing of a unison copy in High: the 2-point polyBLEP at the 2x rate, decimated, against
//! the main oscillator's table kernel, saw and 10 % pulse, C5 to C7.
use monohull_host::analysis::*;
use monohull_host::*;
fn run(table: bool, f0: f64, shape: f32, pw: f32) -> f64 {
    let mut o = Osc::new(table, 0.0);
    let mut hb = Halfband::new(false);
    let inc = (f0 / 88_200.0) as f32;
    let out: Vec<f32> = (0..(1 << 16) + 2000).map(|_| {
        let a = o.step(shape, pw, inc, -1.0, 0).0;
        let b = o.step(shape, pw, inc, -1.0, 0).0;
        hb.down(a, b)
    }).collect();
    worst_alias_db(&out[2000..], 44_100.0, f0, 20.0, 16_000.0, 12).0
}
fn main() {
    for (name, f0) in [("C5", 523.25), ("C6", 1046.5), ("C7", 2093.0)] {
        println!("{name}: saw copy {:.1} dB (main {:.1}), pulse 10% copy {:.1} dB (main {:.1})",
                 run(false, f0, 0.0, 0.5), run(true, f0, 0.0, 0.5), run(false, f0, 1.0 / 3.0, 0.1), run(true, f0, 1.0 / 3.0, 0.1));
    }
}
