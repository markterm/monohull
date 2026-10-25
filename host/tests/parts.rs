//! The parts on their own: approximations, oscillators and resamplers (aliasing), the ladder
//! (tuning, against the reference), envelopes. Measured values are printed (`--nocapture`).

use monohull_host::analysis::*;
use monohull_host::reference::{ladder_rk4, ota_rk4};
use monohull_host::*;

const RATE: f64 = 44_100.0;
const RATE2: f64 = 88_200.0;

#[test]
fn approximations_match_libm() {
    let mut worst = [0.0f64; 6];
    for i in 0..=20_000 {
        let t = i as f64 / 20_000.0;
        let x = -20.0 + 40.0 * t;
        let e = unsafe { mhh_exp2(x as f32) } as f64;
        worst[0] = worst[0].max((e / x.exp2() - 1.0).abs());
        let x = -9.0 + 18.0 * t;
        worst[1] = worst[1].max((unsafe { mhh_tanh(x as f32) } as f64 - x.tanh()).abs());
        let f = 0.25 * t;
        let want = (std::f64::consts::PI * f).tan();
        worst[2] = worst[2].max((unsafe { mhh_tan_pi(f as f32) } as f64 - want).abs() / want.max(1e-3));
        let x = 10f64.powf(-6.0 + 12.0 * t);
        worst[3] = worst[3].max((unsafe { mhh_log2(x as f32) } as f64 - x.log2()).abs());
        let p = -2.0 + 4.0 * t;
        worst[4] = worst[4].max((unsafe { mhh_sin2pi(p as f32) } as f64 - (2.0 * std::f64::consts::PI * p).sin()).abs());
        worst[5] = worst[5].max((unsafe { mhh_log1p01(t as f32) } as f64 - t.ln_1p()).abs());
    }
    eprintln!("exp2 rel {:.1e}, tanh {:.1e}, tan_pi rel {:.1e}, log2 {:.1e}, sin {:.1e}, log1p {:.1e}",
              worst[0], worst[1], worst[2], worst[3], worst[4], worst[5]);
    // exp2's fifth-order series (0.005 cents) and tanh's [7/6] fraction (-80 dB) by design, the
    // rest within a few float ulps
    assert!(worst[0] < 5e-6 && worst[1] < 1.2e-4 && worst[2] < 1e-6);
    assert!(worst[3] < 2e-6 && worst[4] < 5e-7 && worst[5] < 5e-7);
}

/// An oscillator at 2x through the decimator (High), or at 1x (Efficient).
fn osc_output(high: bool, f0: f64, shape: f32, pw: f32, n: usize) -> Vec<f32> {
    osc_output_bent(high, f0, shape, pw, 0.0, n)
}

/// The same with the saw bent by `curve` (Vintage's).
fn osc_output_bent(high: bool, f0: f64, shape: f32, pw: f32, curve: f32, n: usize) -> Vec<f32> {
    let mut o = Osc::new(high, 0.0);
    o.set_curve(curve);
    let mut hb = Halfband::new(false);
    let mut out = Vec::with_capacity(n);
    for _ in 0..n + 2000 {
        let y = if high {
            let inc = (f0 / RATE2) as f32;
            let a = o.step(shape, pw, inc, -1.0, 0).0;
            let b = o.step(shape, pw, inc, -1.0, 0).0;
            hb.down(a, b)
        } else {
            o.step(shape, pw, (f0 / RATE) as f32, -1.0, 0).0
        };
        out.push(y);
    }
    out.split_off(2000)
}

#[test]
fn saw_and_pulse_aliasing() {
    let f0 = 2093.0; // C7
    for (name, shape, pw) in [("saw", 0.0f32, 0.5f32), ("pulse 10%", 1.0 / 3.0, 0.1), ("triangle", 2.0 / 3.0, 0.5)] {
        let hi = worst_alias_db(&osc_output(true, f0, shape, pw, 1 << 16), RATE, f0, 20.0, 16_000.0, 12);
        let lo = worst_alias_db(&osc_output(false, f0, shape, pw, 1 << 16), RATE, f0, 20.0, 16_000.0, 12);
        eprintln!("{name} at C7: High {:.1} dB (at {:.0} Hz), Efficient {:.1} dB (at {:.0} Hz)", hi.0, hi.1, lo.0, lo.1);
        assert!(hi.0 < -80.0, "{name}: High aliases at {:.1} dB", hi.0);
    }
}

#[test]
fn bent_saw_aliasing() {
    // Vintage's full bend: its slope corner at the wrap is BLAMP-corrected, so it aliases no more
    // than the straight saw; and it is there. The bend is a parabola, in quadrature with the
    // saw's harmonics: it changes the wave's shape (what the drive and filter see), not its
    // spectrum's magnitudes, so the check is the difference: 0.3 times the parabola's mean (1/6,
    // a DC offset the output stage blocks) and its rms about that (0.075)
    let f0 = 2093.0;
    let straight = osc_output(true, f0, 0.0, 0.5, 1 << 16);
    let bent = osc_output_bent(true, f0, 0.0, 0.5, 0.15, 1 << 16);
    let (a, at) = worst_alias_db(&bent, RATE, f0, 20.0, 16_000.0, 12);
    let d: Vec<f32> = bent.iter().zip(&straight).map(|(b, s)| b - s).collect();
    let mean = d.iter().map(|&v| v as f64).sum::<f64>() / d.len() as f64;
    let ac = (rms(&d).powi(2) - mean * mean).sqrt();
    eprintln!("bent saw at C7: High {a:.1} dB (at {at:.0} Hz); differs from the straight saw by {mean:.4} DC, {ac:.4} rms");
    assert!(a < -80.0, "the bent saw aliases at {a:.1} dB");
    assert!((mean - 0.05).abs() < 0.002 && (ac - 0.0224).abs() < 0.002);
    // and through hard sync, where the reset's step and slope change include the bend
    let mut o = Osc::new(true, 0.0);
    o.set_curve(0.15);
    let mut m = Osc::new(true, 0.0);
    let mut hb = Halfband::new(false);
    let fm = 523.25;
    let mut out = Vec::new();
    for _ in 0..(1 << 16) + 2000 {
        let mut y = [0.0f32; 2];
        for v in y.iter_mut() {
            let (_, w, _) = m.step(1.0, 0.5, (fm / RATE2) as f32, -1.0, 0);
            *v = o.step(0.0, 0.5, (2.37 * fm / RATE2) as f32, w, 0).0;
        }
        out.push(hb.down(y[0], y[1]));
    }
    let (s, sat) = worst_alias_db(&out[2000..], RATE, fm, 20.0, 16_000.0, 12);
    eprintln!("bent saw hard-synced at 2.37x C5: {s:.1} dB (at {sat:.0} Hz)");
    assert!(s < -70.0);
}

/// The synth's rule for the 303 square's soft edge: whole while its time constant 1 / (140 inc)
/// is 3 samples or more, gone by 1.5.
fn sq303_soft(inc: f64) -> f32 {
    ((1.0 / (140.0 * inc) - 1.5) / 1.5).clamp(0.0, 1.0) as f32
}

const SQ303_PW: f32 = 0.4687857;

#[test]
fn sq303_is_open303s() {
    // a C2 at High's rate, soft edge whole: away from the hard edge's band-limiting the output is
    // Open303's -tanh(140 (p - pw)), at the kernel's latency
    let inc = 65.40639 / RATE2;
    assert_eq!(sq303_soft(inc), 1.0);
    let mut o = Osc::new(true, 0.0);
    o.set_soft(1.0);
    let n = 20_000;
    let y: Vec<f32> = (0..n).map(|_| o.step(1.0 / 3.0, SQ303_PW, inc as f32, -1.0, 0).0).collect();
    let step = (inc as f32 * 4_294_967_296.0) as i32 as u32;
    let mut best = (f64::MAX, 0usize);
    for lag in 0..40 {
        let mut worst = 0.0f64;
        let mut ph = 0u32;
        for i in 0..n - lag {
            ph = ph.wrapping_add(step);
            let p = (ph >> 8) as f64 / 16_777_216.0;
            if !(0.05..0.95).contains(&p) {
                continue; // the hard edge, band-limited
            }
            let want = -(140.0 * (p - SQ303_PW as f64)).tanh();
            worst = worst.max((y[i + lag] as f64 - want).abs());
        }
        if worst < best.0 {
            best = (worst, lag);
        }
    }
    eprintln!("303 square at C2 against Open303's shaper: worst error {:.2e} (lag {} samples)", best.0, best.1);
    assert!(best.0 < 1e-3);
    // and what it does: the upper harmonics softer than a plain pulse of the same width
    let mut plain = Osc::new(true, 0.0);
    let z: Vec<f32> = (0..n).map(|_| plain.step(1.0 / 3.0, SQ303_PW, inc as f32, -1.0, 0).0).collect();
    let row: Vec<String> = [5usize, 10, 20, 40, 80]
        .iter()
        .map(|&h| {
            let f = 65.40639 * h as f64;
            format!("h{h} {:+.1}", 20.0 * (amplitude(&y[4000..], RATE2, f) / amplitude(&z[4000..], RATE2, f)).log10())
        })
        .collect();
    eprintln!("303 square against a plain pulse, C2: {} dB", row.join(", "));
}

#[test]
fn sq303_aliasing() {
    // through the 2x decimator, across the hand-over: C2 and C3 soft, C4 partly, C5 and C7 a step
    for &f0 in &[65.40639f64, 130.8128, 261.6256, 523.2511, 2093.005] {
        let inc = f0 / RATE2;
        let soft = sq303_soft(inc);
        let mut o = Osc::new(true, 0.0);
        o.set_soft(soft);
        let mut hb = Halfband::new(false);
        let mut out = Vec::new();
        for _ in 0..(1 << 16) + 2000 {
            let a = o.step(1.0 / 3.0, SQ303_PW, inc as f32, -1.0, 0).0;
            let b = o.step(1.0 / 3.0, SQ303_PW, inc as f32, -1.0, 0).0;
            out.push(hb.down(a, b));
        }
        let (w, at) = worst_alias_db(&out[2000..], RATE, f0, 20.0, 16_000.0, 12);
        eprintln!("303 square at {f0:.0} Hz (soft {soft:.2}): worst alias {w:.1} dB at {at:.0} Hz");
        assert!(w < -80.0);
    }
}

#[test]
fn hard_sync_aliasing() {
    // the slave at 2.37 times the master: the output repeats at the master's rate
    let f0 = 1046.5; // C6
    for high in [true, false] {
        let mut m = Osc::new(high, 0.0);
        let mut sl = Osc::new(high, 0.0);
        let mut hb = Halfband::new(false);
        let mut out = Vec::new();
        let rate = if high { RATE2 } else { RATE };
        let step = |m: &mut Osc, sl: &mut Osc| {
            let (_, w, _) = m.step(0.0, 0.5, (f0 / rate) as f32, -1.0, 0);
            sl.step(0.0, 0.5, (2.37 * f0 / rate) as f32, w, 0).0
        };
        for _ in 0..(1 << 16) + 2000 {
            out.push(if high {
                let a = step(&mut m, &mut sl);
                let b = step(&mut m, &mut sl);
                hb.down(a, b)
            } else {
                step(&mut m, &mut sl)
            });
        }
        let w = worst_alias_db(&out[2000..], RATE, f0, 20.0, 16_000.0, 12);
        eprintln!("sync saw at C6 x2.37, {}: {:.1} dB (at {:.0} Hz)", if high { "High" } else { "Efficient" }, w.0, w.1);
        if high {
            assert!(w.0 < -80.0);
        }
    }
}

/// Ring modulation and FM of the main pair: at 4x then 4x -> 2x -> 1x (High), or at 1x.
fn ring_fm(high: bool, f1: f64, ratio: f64, ring: bool, index: f32) -> Vec<f32> {
    let mut o1 = Osc::new(high, 0.0);
    let mut o2 = Osc::new(high, 0.25);
    let mut h4 = Halfband::new(true);
    let mut h2 = Halfband::new(false);
    let rate = if high { 4.0 * RATE } else { RATE };
    let mut last2 = 0.0f32;
    let mut step = || {
        let i1 = (f1 / rate) as f32 * (1.0 + index * last2);
        let a = o1.step(0.0, 0.5, i1.clamp(-0.45, 0.45), -1.0, 0).0;
        let b = o2.step(if ring { 0.0 } else { 1.0 }, 0.5, (ratio * f1 / rate) as f32, -1.0, 0).0;
        last2 = b;
        if ring { a * b } else { a }
    };
    let mut out = Vec::new();
    for _ in 0..(1 << 16) + 2000 {
        out.push(if high {
            let x = h4.down(step(), step());
            let y = h4.down(step(), step());
            h2.down(x, y)
        } else {
            step()
        });
    }
    out.split_off(2000)
}

#[test]
fn ring_and_fm_aliasing() {
    let f1 = 1046.5; // C6
    // ring: saws at f and 1.5 f; every product lies on multiples of f / 2
    let hi = worst_alias_db(&ring_fm(true, f1, 1.5, true, 0.0), RATE, f1 / 2.0, 20.0, 16_000.0, 12);
    let lo = worst_alias_db(&ring_fm(false, f1, 1.5, true, 0.0), RATE, f1 / 2.0, 20.0, 16_000.0, 12);
    eprintln!("ring at C6: High {:.1} dB, Efficient {:.1} dB", hi.0, lo.0);
    assert!(hi.0 < -70.0);
    // FM: a sine modulator at 2 f, index 1 and 4 (through zero)
    for index in [1.0f32, 4.0] {
        let hi = worst_alias_db(&ring_fm(true, f1, 2.0, false, index), RATE, f1, 20.0, 16_000.0, 12);
        let lo = worst_alias_db(&ring_fm(false, f1, 2.0, false, index), RATE, f1, 20.0, 16_000.0, 12);
        eprintln!("FM index {index} at C6: High {:.1} dB, Efficient {:.1} dB", hi.0, lo.0);
        assert!(hi.0 < -70.0, "FM index {index}: {:.1}", hi.0);
    }
}

#[test]
fn halfband_passband_and_stopband() {
    // a sine through 1x -> 2x -> 1x keeps its level to 20 kHz; the images are gone at 2x
    for f in [100.0, 1000.0, 10_000.0, 18_000.0, 20_000.0] {
        let mut up = Halfband::new(false);
        let mut down = Halfband::new(false);
        let n = 44_100;
        let mut out = Vec::new();
        for i in 0..n {
            let x = (2.0 * std::f64::consts::PI * f * i as f64 / RATE).sin() as f32;
            let u = up.up(x);
            out.push(down.down(u[0], u[1]));
        }
        let a = amplitude(&out[4100..], RATE, f);
        eprintln!("halfband round trip at {f} Hz: {:.3} dB", 20.0 * a.log10());
        assert!((20.0 * a.log10()).abs() < 0.1);
    }
}

#[test]
fn ladder_self_oscillates_in_tune() {
    // with the tuning correction, C2-C7; the cheap solve is reported, not held to it
    for k in [4.1f32, 4.5] {
        for newton in [true, false] {
            let mut worst = 0.0f64;
            for note in [36, 48, 60, 72, 84, 96] {
                let fc = 440.0 * 2f64.powf((note as f64 - 69.0) / 12.0);
                let g = ladder_g(fc * ladder_tune(k) as f64, RATE2);
                let mut l = Ladder::new();
                let mut out = Vec::new();
                for i in 0..(RATE2 as usize * 6) {
                    out.push(l.step(if i == 0 { 0.5 } else { 0.0 }, g, k, newton).0);
                }
                let c = cents(frequency(&out[RATE2 as usize * 5..], RATE2), fc);
                worst = if c.abs() > worst.abs() { c } else { worst };
            }
            eprintln!("self-oscillation k {k}, {}: worst {worst:+.2} cents (C2-C7)", if newton { "Newton" } else { "cheap" });
            if newton {
                assert!(worst.abs() < 1.0, "k {k}: {worst:.2} cents");
            }
        }
    }
}

#[test]
fn ladder_matches_the_reference() {
    // a sine of 220.5 Hz (400 samples a period at 88.2 kHz) at several levels, cutoffs and
    // resonances: the first nine harmonics against the RK4 reference at 16x
    let f = 220.5;
    let (mut worst_hi, mut worst_lo) = (0.0f64, 0.0f64);
    for &amp in &[0.3f64, 1.0, 3.0] {
        for &fc in &[1000.0f64, 4000.0] {
            for &k in &[0.0f64, 2.0, 3.5] {
                let n = 88_200 + 88_000;
                let x: Vec<f64> = (0..n).map(|i| amp * (2.0 * std::f64::consts::PI * f * i as f64 / RATE2).sin()).collect();
                let refy: Vec<f32> = ladder_rk4(&x, RATE2, fc, k, 16).into_iter().map(|v| v as f32).collect();
                let g = ladder_g(fc, RATE2);
                for newton in [true, false] {
                    let mut l = Ladder::new();
                    let y: Vec<f32> = x.iter().map(|&v| l.step(v as f32, g, k as f32, newton).0).collect();
                    let (a, b) = (&y[88_200..], &refy[88_200..]);
                    let fund = amplitude(b, RATE2, f);
                    for h in 1..=9 {
                        let rb = amplitude(b, RATE2, f * h as f64);
                        if rb < fund * 1e-4 {
                            continue; // below -80 dB
                        }
                        let ra = amplitude(a, RATE2, f * h as f64);
                        let d = 20.0 * (ra / rb).log10();
                        let w = if newton { &mut worst_hi } else { &mut worst_lo };
                        if d.abs() > w.abs() {
                            *w = d;
                            if d.abs() > 0.5 {
                                eprintln!("  {} amp {amp} fc {fc} k {k} h{h}: {d:+.2} dB", if newton { "Newton" } else { "cheap" });
                            }
                        }
                    }
                }
            }
        }
    }
    eprintln!("ladder vs reference, harmonics 1-9 above -80 dB: Newton worst {worst_hi:+.2} dB, cheap worst {worst_lo:+.2} dB");
    assert!(worst_hi.abs() < 1.0);
}

#[test]
fn newton_converges_quickly() {
    let g = ladder_g(1500.0, RATE2);
    let mut l = Ladder::new();
    let (mut total, mut most) = (0, 0);
    let n = 88_200;
    for i in 0..n {
        // a hard-driven saw at full resonance: the worst case for the solve
        let x = 4.0 * (2.0 * ((i as f32 * 110.0 / 88_200.0) % 1.0) - 1.0);
        l.step(x, g, 4.5, true);
        total += l.iters();
        most = most.max(l.iters());
    }
    eprintln!("Newton steps per sample: mean {:.2}, most {most}", total as f64 / n as f64);
    assert!((total as f64 / n as f64) < 2.5);
}

#[test]
fn ota_is_the_ladder_when_linear() {
    // at a millionth of full scale both are the same four poles and feedback
    let mut worst = 0.0f32;
    for &(fc, k) in &[(500.0f64, 0.0f32), (2000.0, 2.0), (8000.0, 3.5), (15_000.0, 3.9)] {
        let g = ladder_g(fc, RATE2);
        let (mut a, mut b) = (Ladder::new(), Ota::new());
        for i in 0..20_000 {
            let x = 1e-6 * ((i as f32 * 0.37).sin() + if i % 1000 == 0 { 1.0 } else { 0.0 });
            let (ya, _) = a.step(x, g, k, true);
            let (yb, _) = b.step(x, g, k, true);
            worst = worst.max((ya - yb).abs() / 1e-6);
        }
    }
    eprintln!("OTA against the ladder at -120 dB: worst difference {worst:.2e} of the input");
    assert!(worst < 1e-3);
}

#[test]
fn ota_self_oscillation_pitch() {
    // measured without correction, then with mh_ota_tune: C2-C7
    let mut worst_all = 0.0f64;
    for k in [4.02f32, 4.05, 4.1, 4.2, 4.3, 4.4, 4.48] {
        let mut row = Vec::new();
        let mut worst = 0.0f64;
        // (just over the edge low notes take longer than these 6 s to settle)
        let notes: &[i32] = if k < 4.1 { &[60, 72, 84, 96] } else { &[36, 48, 60, 72, 84, 96] };
        for &note in notes {
            let fc = 440.0 * 2f64.powf((note as f64 - 69.0) / 12.0);
            let g = ladder_g(fc * unsafe { mhh_ota_tune(k) } as f64, RATE2);
            let mut l = Ota::new();
            let mut out = Vec::new();
            for i in 0..(RATE2 as usize * 6) {
                out.push(l.step(if i == 0 { 0.5 } else { 0.0 }, g, k, true).0);
            }
            let c = cents(frequency(&out[RATE2 as usize * 5..], RATE2), fc);
            row.push(format!("{c:+.1}"));
            worst = if c.abs() > worst.abs() { c } else { worst };
        }
        eprintln!("OTA self-oscillation k {k}: {} cents ({})", row.join(" "), if k < 4.1 { "C4-C7" } else { "C2-C7" });
        worst_all = if worst.abs() > worst_all.abs() { worst } else { worst_all };
    }
    assert!(worst_all.abs() < 1.5, "{worst_all:.2} cents");
}

#[test]
fn ota_matches_the_reference() {
    // as ladder_matches_the_reference: harmonics 1-9 against RK4 at 16x
    let f = 220.5;
    let mut worst = 0.0f64;
    for &amp in &[0.3f64, 1.0, 3.0] {
        for &fc in &[1000.0f64, 4000.0] {
            for &k in &[0.0f64, 2.0, 3.5] {
                let n = 88_200 + 88_000;
                let x: Vec<f64> = (0..n).map(|i| amp * (2.0 * std::f64::consts::PI * f * i as f64 / RATE2).sin()).collect();
                let refy: Vec<f32> = ota_rk4(&x, RATE2, fc, k, 16).into_iter().map(|v| v as f32).collect();
                let g = ladder_g(fc, RATE2);
                let mut l = Ota::new();
                let y: Vec<f32> = x.iter().map(|&v| l.step(v as f32, g, k as f32, true).0).collect();
                let (a, b) = (&y[88_200..], &refy[88_200..]);
                let fund = amplitude(b, RATE2, f);
                for h in 1..=9 {
                    let rb = amplitude(b, RATE2, f * h as f64);
                    if rb < fund * 1e-4 {
                        continue;
                    }
                    let d = 20.0 * (amplitude(a, RATE2, f * h as f64) / rb).log10();
                    if d.abs() > worst.abs() {
                        worst = d;
                        if d.abs() > 0.5 {
                            eprintln!("  amp {amp} fc {fc} k {k} h{h}: {d:+.2} dB");
                        }
                    }
                }
            }
        }
    }
    eprintln!("OTA vs reference, harmonics 1-9 above -80 dB: worst {worst:+.2} dB");
    assert!(worst.abs() < 1.0);
}

#[test]
fn ota_newton_converges_quickly() {
    let g = ladder_g(1500.0, RATE2);
    let mut l = Ota::new();
    let (mut total, mut most) = (0, 0);
    let n = 88_200;
    for i in 0..n {
        let x = 4.0 * (2.0 * ((i as f32 * 110.0 / 88_200.0) % 1.0) - 1.0);
        l.step(x, g, 4.5, true);
        total += l.iters();
        most = most.max(l.iters());
    }
    eprintln!("OTA Newton steps per sample: mean {:.2}, most {most}", total as f64 / n as f64);
    assert!((total as f64 / n as f64) < 2.5);
}

#[test]
fn k35_is_its_transfer_function() {
    // quiet sines (the tanh linear) against w^2 / (s^2 + (2 - K) w s + w^2) and s (s + w) / (the
    // same), the MS-20's 6 dB/oct high-pass with its peak, through the bilinear transform with the
    // cutoff prewarped: S = j tan(W / 2) / g
    let mut worst = 0.0f64;
    for hp in [false, true] {
        for &fc in &[300.0f64, 2000.0, 12_000.0] {
            for &k in &[0.3f32, 1.2, 1.9] {
                let g = ladder_g(fc, RATE2);
                for &f in &[100.0f64, 700.0, 2500.0, 9000.0, 18_000.0] {
                    let mut flt = K35::new(hp);
                    let n = 88_200;
                    let y: Vec<f32> = (0..n)
                        .map(|i| flt.step(1e-4 * (2.0 * std::f64::consts::PI * f * i as f64 / RATE2).sin() as f32, g, k))
                        .collect();
                    let got = amplitude(&y[n / 2..], RATE2, f) / 1e-4;
                    let w = 2.0 * std::f64::consts::PI * f / RATE2;
                    let sv = (w / 2.0).tan() / g as f64; // S = j sv
                    let (re, im) = (1.0 - sv * sv, (2.0 - k as f64) * sv);
                    let num = if hp { sv * (sv * sv + 1.0).sqrt() } else { 1.0 };
                    let want = num / (re * re + im * im).sqrt();
                    let d = 20.0 * (got / want).log10();
                    if d.abs() > 0.05 {
                        eprintln!("  {} fc {fc} K {k} at {f} Hz: {:.2} dB, want {:.2} dB", if hp { "HP" } else { "LP" },
                                  20.0 * got.log10(), 20.0 * want.log10());
                    }
                    if d.abs() > worst.abs() {
                        worst = d;
                    }
                }
            }
        }
    }
    eprintln!("Sallen-Key against its transfer function (LP and HP, K 0.3-1.9): worst {worst:+.3} dB");
    assert!(worst.abs() < 0.05);
}

#[test]
fn k35_stays_bounded() {
    let g = ladder_g(1500.0, RATE2);
    for hp in [false, true] {
        let mut f = K35::new(hp);
        let mut most = 0.0f32;
        for i in 0..88_200 {
            let x = 8.0 * (2.0 * ((i as f32 * 110.0 / 88_200.0) % 1.0) - 1.0);
            let y = f.step(x, g, 1.96);
            assert!(y.is_finite());
            most = most.max(y.abs());
        }
        eprintln!("Sallen-Key {} at full feedback, an 8x saw in: peak {most:.2}", if hp { "HP" } else { "LP" });
        assert!(most < 4.0);
    }
}

#[test]
fn envelope_times() {
    let rate = RATE as f32;
    let mut e = Env::new(0.1, 0.5, 0.0, 0.5, rate);
    e.gate(true);
    let mut n = 0;
    while e.stage() == 1 {
        e.step();
        n += 1;
    }
    let attack = n as f64 / RATE;
    let mut n = 0;
    while e.step() > 1e-3 {
        n += 1;
    }
    let decay = n as f64 / RATE;
    let mut e = Env::new(0.001, 0.1, 1.0, 0.5, rate);
    e.gate(true);
    for _ in 0..4410 {
        e.step();
    }
    e.gate(false);
    let mut n = 0;
    while e.step() > 1e-3 {
        n += 1;
    }
    let release = n as f64 / RATE;
    eprintln!("attack {attack:.4} s (0.1), decay to -60 dB {decay:.4} s (0.5), release {release:.4} s (0.5)");
    assert!((attack / 0.1 - 1.0).abs() < 0.05);
    assert!((decay / 0.5 - 1.0).abs() < 0.05);
    assert!((release / 0.5 - 1.0).abs() < 0.05);
}
