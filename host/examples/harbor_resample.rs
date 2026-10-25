//! Monohull's SAW LEAD at C6 through Harbor's 44.1 -> 48 kHz linear interpolation (engine.rs
//! AudioOut::render), and through a windowed-sinc resampler, measured at 48 kHz.
use monohull_host::analysis::*;
use monohull_host::*;

fn linear(x: &[f32]) -> Vec<f32> {
    let step = 44_100.0 / 48_000.0;
    let (mut pos, mut i, mut out) = (1.0f64, 0usize, Vec::new());
    let (mut prev, mut next) = (0.0f32, 0.0f32);
    while i < x.len() {
        while pos >= 1.0 && i < x.len() {
            prev = next;
            next = x[i];
            i += 1;
            pos -= 1.0;
        }
        out.push(prev + (next - prev) * pos as f32);
        pos += step;
    }
    out
}

fn sinc(x: &[f32]) -> Vec<f32> {
    // 32 zero crossings a side, Blackman-Harris, cutoff 20.5 kHz
    let fc = 20_500.0 / 44_100.0;
    let z = 32.0;
    let n_out = ((x.len() as f64 - 2.0 * z - 2.0) * 48_000.0 / 44_100.0) as usize;
    (0..n_out).map(|k| {
        let t = k as f64 * 44_100.0 / 48_000.0 + z;
        let c = t.floor() as i64;
        let mut acc = 0.0;
        for j in (c - z as i64 + 1)..=(c + z as i64) {
            let d = t - j as f64;
            let w = 0.35875 + 0.48829 * (std::f64::consts::PI * d / z).cos() + 0.14128 * (2.0 * std::f64::consts::PI * d / z).cos()
                + 0.01168 * (3.0 * std::f64::consts::PI * d / z).cos();
            let s = if d == 0.0 { 2.0 * fc } else { (2.0 * std::f64::consts::PI * fc * d).sin() / (std::f64::consts::PI * d) };
            acc += x[j as usize] as f64 * s * w;
        }
        acc as f32
    }).collect()
}

fn main() {
    // a Harbor recording (16-bit stereo) if one is given, else Monohull's SAW LEAD at C6 rendered here
    let (l, f0, what) = if let Some(path) = std::env::args().nth(1) {
        let b = std::fs::read(&path).unwrap();
        let d = b.windows(4).position(|w| w == b"data").unwrap() + 8;
        let l: Vec<f32> = b[d..].chunks_exact(4).map(|c| i16::from_le_bytes([c[0], c[1]]) as f32 / 32768.0).collect();
        let start = l.iter().position(|v| v.abs() > 0.001).unwrap_or(0) + 22_050;
        let seg = l[start..start + 70_000].to_vec();
        let f = refine(&seg[..32768], 44_100.0);
        (seg, f, path)
    } else {
        let mut s = Synth::new(44_100.0);
        s.set("vintage", 0.0).set("aenv_sustain", 1.0).set("fenv_sustain", 1.0);
        s.note_on(84, 1.0);
        s.render(8820);
        (s.render(1 << 17).0, 1046.502, "SAW LEAD C6".to_string())
    };
    let n = 1 << 15;
    let at44 = worst_alias_db(&l[..n], 44_100.0, f0, 20.0, 16_000.0, 12);
    let lin = linear(&l);
    let snc = sinc(&l);
    let a = worst_alias_db(&lin[2000..2000 + n], 48_000.0, f0, 20.0, 16_000.0, 12);
    let b = worst_alias_db(&snc[2000..2000 + n], 48_000.0, f0, 20.0, 16_000.0, 12);
    println!("{what} ({f0:.2} Hz)");
    println!("  the firmware's output, 44.1 kHz:    worst inharmonic {:.1} dB at {:.0} Hz", at44.0, at44.1);
    println!("  Harbor before, linear to 48 kHz:    worst inharmonic {:.1} dB at {:.0} Hz", a.0, a.1);
    println!("  Harbor now, windowed sinc to 48 kHz: worst inharmonic {:.1} dB at {:.0} Hz", b.0, b.1);
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
