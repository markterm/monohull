//! Is the self-oscillation clean? ACID as left on the phone (cutoff 6792 Hz, resonance 1, drive
//! 0.55), C4, rendered at 44.1 kHz and with the whole synth at 4x (176.4 kHz) decimated by a
//! windowed sinc: what the normal render has that the 4x one doesn't is aliasing.
use monohull_host::analysis::*;
use monohull_host::*;

fn acid(rate: f32, quality: f32) -> Vec<f32> {
    let mut s = Synth::new(rate);
    for (n, v) in [("cutoff", 6792.0), ("resonance", 1.0), ("drive", 0.55), ("fenv_amount", 3.2), ("fenv_attack", 0.001),
                   ("fenv_decay", 0.18), ("fenv_sustain", 0.0), ("aenv_decay", 0.3), ("aenv_sustain", 0.6),
                   ("aenv_release", 0.04), ("glide", 0.05), ("glide_legato", 1.0), ("fenv_velocity", 0.8),
                   ("key_track", 0.2), ("hpf", 40.0), ("quality", quality)] {
        s.set(n, v);
    }
    s.note_on(60, 100.0 / 127.0);
    s.render((rate * 2.0) as usize).0
}

fn decimate4(x: &[f32]) -> Vec<f32> {
    // 64 zero crossings at the low rate's 20 kHz, Blackman-Harris
    let z = 64.0 * 4.0;
    let fc = 20_000.0 / 176_400.0;
    let h: Vec<f64> = (-(z as i64)..=z as i64).map(|i| {
        let d = i as f64;
        let w = 0.35875 + 0.48829 * (std::f64::consts::PI * d / z).cos() + 0.14128 * (2.0 * std::f64::consts::PI * d / z).cos()
            + 0.01168 * (3.0 * std::f64::consts::PI * d / z).cos();
        let s = if i == 0 { 2.0 * fc } else { (2.0 * std::f64::consts::PI * fc * d).sin() / (std::f64::consts::PI * d) };
        s * w
    }).collect();
    let m = h.len();
    (0..(x.len() - m) / 4).map(|k| h.iter().enumerate().map(|(j, c)| x[4 * k + j] as f64 * c).sum::<f64>() as f32).collect()
}

fn peaks(x: &[f32], rate: f64, f0: f64) -> Vec<(f64, f64)> {
    let s = spectrum_db(x);
    let bin = rate / ((s.len() - 1) * 2) as f64;
    let top = s.iter().cloned().fold(f64::MIN, f64::max);
    let mut p = Vec::new();
    for i in 2..s.len() - 2 {
        let f = i as f64 * bin;
        if f < 100.0 || f > 20_000.0 || s[i] < top - 90.0 || s[i] < s[i - 1] || s[i] < s[i + 1] || s[i] < s[i - 2] || s[i] < s[i + 2] {
            continue;
        }
        let n = (f / f0).round();
        if n >= 1.0 && ((f - n * f0) / bin).abs() < 8.0 {
            continue; // a harmonic of the note
        }
        p.push((f, s[i] - top));
    }
    p.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap());
    p
}

fn main() {
    let f0 = 261.626;
    // to listen: High at the normal rate, and the 4x reference
    let n = acid(44_100.0, 0.0);
    let r = decimate4(&acid(176_400.0, 0.0));
    std::fs::create_dir_all("out").unwrap();
    wav::write(std::path::Path::new("out/acid-res1-normal.wav"), 44_100, &n, &n).unwrap();
    wav::write(std::path::Path::new("out/acid-res1-4x-reference.wav"), 44_100, &r, &r).unwrap();
    for (q, qn) in [(0.0, "High"), (1.0, "Efficient")] {
        let norm = acid(44_100.0, q);
        let reference = decimate4(&acid(176_400.0, q));
        let a = &norm[22_050..22_050 + 32768];
        let b = &reference[22_050 - 256..22_050 - 256 + 32768];
        let pr = peaks(b, 44_100.0, f0);
        println!("{qn}: components that aren't the note's harmonics (dB re the loudest), top 10");
        for (f, l) in peaks(a, 44_100.0, f0).iter().take(10) {
            let in_ref = pr.iter().filter(|(g, _)| (g - f).abs() < 6.0).map(|(_, m)| *m).fold(f64::MIN, f64::max);
            let tag = if in_ref > *l - 6.0 { "also in the 4x reference: the filter itself" } else if in_ref > f64::MIN { "weaker in the reference: partly aliasing" } else { "ALIAS (absent at 4x)" };
            println!("  {f:8.0} Hz {l:6.1} dB   {tag}");
        }
    }
}
