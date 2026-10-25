//! The diode ladder: self-oscillation pitch against the cutoff, and its level at 100 Hz and at
//! the cutoff as the resonance rises (against the transistor ladder's).
use monohull_host::analysis::*;
use monohull_host::*;
fn main() {
    let rate = 88_200.0f32;
    for k in [17.5f32, 18.5, 19.04] {
        let mut line = format!("k {k:5.2}:");
        for note in [36, 48, 60, 72, 84, 96] {
            let fc = 440.0 * 2f32.powf((note as f32 - 69.0) / 12.0);
            let mut d = Diode::new();
            let out: Vec<f32> = (0..(rate as usize * 4)).map(|i| d.step(if i == 0 { 0.5 } else { 0.0 }, fc, rate, k)).collect();
            let tail = &out[rate as usize * 3..];
            line += &format!(" {note}:{:+.1}c/{:.2}", cents(frequency(tail, rate as f64), fc as f64), peak(tail));
        }
        println!("{line}");
    }
    // a 100 Hz sine, cutoff 1 kHz: the diode keeps its bass as resonance rises, the ladder doesn't
    for share in [0.0f32, 0.5, 0.9] {
        let (mut d, mut l) = (Diode::new(), Ladder::new());
        let g = ladder_g(1000.0, rate as f64);
        let x: Vec<f32> = (0..rate as usize).map(|i| 0.3 * (2.0 * std::f32::consts::PI * 100.0 * i as f32 / rate).sin()).collect();
        let yd: Vec<f32> = x.iter().map(|&v| d.step(v, 1000.0, rate, 17.0 * share)).collect();
        let yl: Vec<f32> = x.iter().map(|&v| l.step(v, g, 4.0 * share, true).0).collect();
        let ld = 20.0 * (amplitude(&yd[44_100..], rate as f64, 100.0) / 0.3).log10();
        let ll = 20.0 * (amplitude(&yl[44_100..], rate as f64, 100.0) / 0.3).log10();
        println!("resonance share {share}: 100 Hz through the diode {ld:+.1} dB, through the transistor ladder {ll:+.1} dB");
    }
}
