//! A 16-bit stereo WAV: level, pitch, harmonics and the worst inharmonic component, over the
//! steadiest second (0.5-1.5 s after the note starts).
use monohull_host::analysis::*;
fn main() {
    let path = std::env::args().nth(1).unwrap();
    let b = std::fs::read(&path).unwrap();
    let rate = u32::from_le_bytes(b[24..28].try_into().unwrap()) as f64;
    let bits = u16::from_le_bytes(b[34..36].try_into().unwrap());
    let d = b.windows(4).position(|w| w == b"data").unwrap() + 8;
    let l: Vec<f32> = b[d..].chunks_exact(4).map(|c| i16::from_le_bytes([c[0], c[1]]) as f32 / 32768.0).collect();
    let start = l.iter().position(|v| v.abs() > 0.001).unwrap_or(0);
    println!("{path}: {rate} Hz, {bits}-bit, {:.2} s, sound from {:.3} s, peak {:.2}", l.len() as f64 / rate, start as f64 / rate, peak(&l));
    let seg = &l[start + rate as usize / 2..start + rate as usize / 2 + 32768.min(l.len() - start - rate as usize / 2)];
    let f = refine(seg, rate);
    println!("pitch {f:.2} Hz ({:+.1} cents from C4)", cents(f, 261.6256));
    let fund = amplitude(seg, rate, f);
    let mut h = String::new();
    for k in 1..=12 { h += &format!(" {:.0}", 20.0 * (amplitude(seg, rate, f * k as f64) / fund).log10()); }
    println!("harmonics 1-12 (dB re the fundamental):{h}");
    let w = worst_alias_db(seg, rate, f, 20.0, 18_000.0, 12);
    println!("worst inharmonic component: {:.1} dB at {:.0} Hz", w.0, w.1);
    let s = spectrum_db(seg);
    let bin = rate / ((s.len() - 1) * 2) as f64;
    let mut floor: Vec<f64> = s.iter().enumerate().filter(|(i, _)| { let fr = *i as f64 * bin; let n = (fr / f).round(); fr > 50.0 && fr < 18000.0 && ((fr - n * f) / bin).abs() > 20.0 }).map(|(_, &v)| v).collect();
    floor.sort_by(|a, b| a.partial_cmp(b).unwrap());
    println!("median level between harmonics: {:.1} dBFS", floor[floor.len() / 2]);
}

/// The fundamental: the strongest component between 30 Hz and 2 kHz, refined to 0.01 Hz.
fn refine(x: &[f32], rate: f64) -> f64 {
    let s = spectrum_db(x);
    let bin = rate / ((s.len() - 1) * 2) as f64;
    let (lo, hi) = ((30.0 / bin) as usize, (2000.0 / bin) as usize);
    let k = (lo..hi).max_by(|&a, &b| s[a].partial_cmp(&s[b]).unwrap()).unwrap();
    let mut best = (0.0, k as f64 * bin);
    let mut f = (k as f64 - 2.0) * bin;
    while f < (k as f64 + 2.0) * bin {
        let a = amplitude(x, rate, f);
        if a > best.0 {
            best = (a, f);
        }
        f += 0.01;
    }
    best.1
}
