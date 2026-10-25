//! RING BELL's partials on one held note: the strongest spectral peaks, as ratios of the note.
use monohull_host::analysis::*;
use monohull_host::{Synth, FM1_RATE};

fn main() {
    let rate = FM1_RATE as f64;
    let n = Synth::preset_names().iter().position(|p| p == "RING BELL").unwrap();
    for note in [57, 69, 81] {
        let mut s = Synth::new(FM1_RATE);
        s.load_preset(n);
        s.set("reverb_mix", 0.0).set("vintage", 0.0).set("aenv_sustain", 1.0);
        if std::env::var("TWELFTH").is_ok() {
            s.set("osc2_semi", 19.0).set("osc2_fine", 0.0);
        }
        s.note_on(note, 0.8);
        s.render(4410);
        let (l, _) = s.render(1 << 15);
        let f0 = 440.0 * 2f64.powf((note as f64 - 69.0) / 12.0);
        let sp = spectrum_db(&l);
        let bin = rate / 2.0 / sp.len() as f64;
        let mut peaks: Vec<(f64, f64)> = (2..sp.len() - 2)
            .filter(|&i| sp[i] > sp[i - 1] && sp[i] >= sp[i + 1] && sp[i] > sp[i - 2] && sp[i] >= sp[i + 2])
            .map(|i| (sp[i], i as f64 * bin))
            .collect();
        peaks.sort_by(|a, b| b.0.partial_cmp(&a.0).unwrap());
        let top = peaks[0].0;
        let mut rows: Vec<(f64, f64)> = peaks.into_iter().filter(|p| p.0 > top - 40.0).take(8).collect();
        rows.sort_by(|a, b| a.1.partial_cmp(&b.1).unwrap());
        let txt: Vec<String> = rows.iter().map(|(d, f)| format!("{:.3}x {:+.0} dB", f / f0, d - top)).collect();
        println!("note {note} ({f0:.0} Hz): {}", txt.join(", "));
    }
}
