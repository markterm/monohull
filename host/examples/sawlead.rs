//! SAW LEAD (the default patch, cutoff 2 kHz, resonance 0.3) playing C4: the strongest
//! components of the steady part, to compare with a recording from the FM-1 build.
use monohull_host::analysis::*;
use monohull_host::*;
fn main() {
    for quality in [0.0f32, 1.0] {
        let mut s = Synth::new(44_117.647);
        s.set("quality", quality);
        s.note_on(60, 100.0 / 127.0);
        let (l, _) = s.render(44_117 * 2);
        let seg = &l[22_050..22_050 + 32768];
        let sp = spectrum_db(seg);
        let bin = 44_117.647 / ((sp.len() - 1) * 2) as f64;
        let mut idx: Vec<usize> = (1..sp.len()).collect();
        idx.sort_by(|a, b| sp[*b].partial_cmp(&sp[*a]).unwrap());
        let top: Vec<String> = idx.iter().take(8).map(|&i| format!("{:.0} Hz {:.1}", i as f64 * bin, sp[i] - sp[idx[0]])).collect();
        let w = worst_alias_db(seg, 44_117.647, 261.626, 20.0, 18_000.0, 12);
        println!("quality {quality}: worst inharmonic {:.1} dB at {:.0} Hz; peaks {}", w.0, w.1, top.join(", "));
    }
}
