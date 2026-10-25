//! Writes `dsp/mh_tables.h` and `dsp/mh_tables.c`: the oscillators' band-limited step and ramp
//! residuals, and the halfband resamplers' allpass coefficients.
//!
//! Run from the repository root: `cargo run --release --manifest-path tools/gen-tables/Cargo.toml`.

use std::f64::consts::PI;
use std::fmt::Write as _;

/// Zero crossings of the windowed sinc on each side: the residuals span 2·BLEP_Z samples.
const BLEP_Z: usize = 8;
/// Table rows per sample (read with linear interpolation between rows).
const BLEP_O: usize = 128;
/// Integration steps per table point.
const SUB: usize = 32;

/// The windowed sinc, band-limited to the oscillator rate's Nyquist, over (-Z, Z) samples.
fn kernel(t: f64) -> f64 {
    let z = BLEP_Z as f64;
    if t.abs() >= z {
        return 0.0;
    }
    let sinc = if t == 0.0 { 1.0 } else { (PI * t).sin() / (PI * t) };
    // 4-term Blackman-Harris over the whole span
    let x = (t + z) / (2.0 * z);
    let w = 0.35875 - 0.48829 * (2.0 * PI * x).cos() + 0.14128 * (4.0 * PI * x).cos()
        - 0.01168 * (6.0 * PI * x).cos();
    sinc * w
}

/// Residual tables, the band-limited step minus the ideal one and the band-limited ramp minus
/// the ideal one, laid out polyphase: row r (0..=O) serves an event r / O samples before the
/// current sample, column c (0..2Z) its tap at tau = c - Z + r / O. Within a column tau stays on
/// one side of 0, so the ideal step's jump never lies between two rows that get interpolated,
/// and an event's 2Z taps are contiguous in memory.
fn blep_tables() -> (Vec<f64>, Vec<f64>) {
    let n = 2 * BLEP_Z * BLEP_O + 1;
    let h = 1.0 / (BLEP_O * SUB) as f64;
    let z = BLEP_Z as f64;
    // cumulative integrals by the trapezoid rule at a fine step
    let steps = (n - 1) * SUB;
    let mut s = vec![0.0f64; steps + 1]; // step
    for i in 1..=steps {
        let t0 = -z + (i - 1) as f64 * h;
        s[i] = s[i - 1] + 0.5 * h * (kernel(t0) + kernel(t0 + h));
    }
    let total = s[steps];
    for v in s.iter_mut() {
        *v /= total;
    }
    let mut a = vec![0.0f64; steps + 1]; // ramp
    for i in 1..=steps {
        a[i] = a[i - 1] + 0.5 * h * (s[i - 1] + s[i]);
    }
    let mut blep = Vec::with_capacity((BLEP_O + 1) * 2 * BLEP_Z);
    let mut blamp = Vec::with_capacity((BLEP_O + 1) * 2 * BLEP_Z);
    for r in 0..=BLEP_O {
        for c in 0..2 * BLEP_Z {
            let k = c * BLEP_O + r; // on the table grid from -Z
            let after = c >= BLEP_Z; // the column's taps are at or after the event
            let t = -z + k as f64 / BLEP_O as f64;
            // the ends are exactly zero so an event's taps can stop there
            if k == 0 || k == n - 1 {
                blep.push(0.0);
                blamp.push(0.0);
                continue;
            }
            blep.push(s[k * SUB] - if after { 1.0 } else { 0.0 });
            blamp.push(a[k * SUB] - if after { t } else { 0.0 });
        }
    }
    (blep, blamp)
}

// ----- halfband coefficients: Laurent de Soras' HIIR designer (WTFPL), ported

fn ipowp(x: f64, n: i32) -> f64 {
    x.powi(n)
}

fn transition_param(transition: f64) -> (f64, f64) {
    let mut k = ((1.0 - transition * 2.0) * PI / 4.0).tan();
    k *= k;
    let kksqrt = (1.0 - k * k).powf(0.25);
    let e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
    let e2 = e * e;
    let e4 = e2 * e2;
    let q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    (k, q)
}

fn atten(q: f64, order: i32) -> f64 {
    let a = 4.0 * (order as f64 * 0.5 * q.ln()).exp();
    -10.0 * (a / (1.0 + a)).log10()
}

fn acc_num(q: f64, order: i32, c: i32) -> f64 {
    let (mut i, mut j, mut acc) = (0i32, 1.0f64, 0.0f64);
    loop {
        let t = ipowp(q, i * (i + 1)) * (((i * 2 + 1) * c) as f64 * PI / order as f64).sin() * j;
        acc += t;
        j = -j;
        i += 1;
        if t.abs() <= 1e-100 {
            return acc;
        }
    }
}

fn acc_den(q: f64, order: i32, c: i32) -> f64 {
    let (mut i, mut j, mut acc) = (1i32, -1.0f64, 0.0f64);
    loop {
        let t = ipowp(q, i * i) * ((i * 2 * c) as f64 * PI / order as f64).cos() * j;
        acc += t;
        j = -j;
        i += 1;
        if t.abs() <= 1e-100 {
            return acc;
        }
    }
}

/// `nbr` allpass coefficients for a halfband with the given normalised transition band, and
/// its stopband attenuation in dB.
fn halfband(nbr: usize, transition: f64) -> (Vec<f64>, f64) {
    let (k, q) = transition_param(transition);
    let order = nbr as i32 * 2 + 1;
    let coefs = (0..nbr as i32)
        .map(|index| {
            let c = index + 1;
            let num = acc_num(q, order, c) * q.powf(0.25);
            let den = acc_den(q, order, c) + 0.5;
            let ww = num / den;
            let wwsq = ww * ww;
            let x = ((1.0 - wwsq * k) * (1.0 - wwsq / k)).sqrt() / (1.0 + wwsq);
            (1.0 - x) / (1.0 + x)
        })
        .collect();
    (coefs, atten(q, order))
}

fn table(out: &mut String, name: &str, v: &[f64]) {
    writeln!(out, "const float {name}[{}] = {{", v.len()).unwrap();
    for chunk in v.chunks(6) {
        let line: Vec<String> = chunk.iter().map(|x| format!("{:.9e}f", x)).collect();
        writeln!(out, "    {},", line.join(", ")).unwrap();
    }
    writeln!(out, "}};\n").unwrap();
}

fn table2(out: &mut String, name: &str, v: &[f64], cols: usize) {
    writeln!(out, "const float {name}[{}][{cols}] = {{", v.len() / cols).unwrap();
    for row in v.chunks(cols) {
        let line: Vec<String> = row.iter().map(|x| format!("{:.9e}f", x)).collect();
        writeln!(out, "    {{{}}},", line.join(", ")).unwrap();
    }
    writeln!(out, "}};\n").unwrap();
}

fn main() {
    let (blep, blamp) = blep_tables();

    // 2x <-> 1x: the passband reaches 20 kHz at 44.1 kHz; transition from 20 to 24.1 kHz at
    // the 2x rate, normalised to it.
    let tbw_2x = 0.25 - 20_000.0 / 88_200.0;
    // 4x -> 2x: everything below 44.1 kHz at the 4x rate passes to the 2x stage, whose own
    // decimator then keeps the audio band; the transition can be wide.
    let tbw_4x = 0.25 - 26_000.0 / 176_400.0;
    let (hb2, at2) = halfband(10, tbw_2x);
    let (hb4, at4) = halfband(6, tbw_4x);
    eprintln!("halfband 2x: 10 coefs, tbw {tbw_2x:.4}, stopband {at2:.1} dB");
    eprintln!("halfband 4x:  6 coefs, tbw {tbw_4x:.4}, stopband {at4:.1} dB");

    let header = format!(
        "/* Generated by tools/gen-tables: do not edit. */\n\
         #ifndef MH_TABLES_H\n#define MH_TABLES_H\n\n\
         /* Band-limited step and ramp residuals, linear phase, {z} zero crossings each side, polyphase:\n   \
         row r (0..MH_BLEP_O) for an event r / MH_BLEP_O samples before the current sample, column c\n   \
         (0..2 MH_BLEP_Z) for its tap c - MH_BLEP_Z samples away. */\n\
         #define MH_BLEP_Z {z}\n#define MH_BLEP_O {o}\n\
         extern const float mh_blep[MH_BLEP_O + 1][2 * MH_BLEP_Z];\n\
         extern const float mh_blamp[MH_BLEP_O + 1][2 * MH_BLEP_Z];\n\n\
         /* Halfband allpass coefficients (HIIR's design): 2x <-> 1x, {at2:.0} dB stopband above\n   \
         24.1 kHz at 44.1 kHz; 4x -> 2x, {at4:.0} dB stopband. */\n\
         #define MH_HB2_N {n2}\n#define MH_HB4_N {n4}\n\
         extern const float mh_hb2[MH_HB2_N];\nextern const float mh_hb4[MH_HB4_N];\n\n#endif\n",
        z = BLEP_Z,
        o = BLEP_O,
        n2 = hb2.len(),
        n4 = hb4.len(),
    );
    let mut src = String::from("/* Generated by tools/gen-tables: do not edit. */\n#include \"mh_tables.h\"\n\n");
    table2(&mut src, "mh_blep", &blep, 2 * BLEP_Z);
    table2(&mut src, "mh_blamp", &blamp, 2 * BLEP_Z);
    table(&mut src, "mh_hb2", &hb2);
    table(&mut src, "mh_hb4", &hb4);
    std::fs::write("dsp/mh_tables.h", header).expect("run from the repository root");
    std::fs::write("dsp/mh_tables.c", src).unwrap();
}
