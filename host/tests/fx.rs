//! The effects against the code they were ported from, to the bit, and the behaviour an ear
//! would check: stability, decay, ringing, stereo.

use std::ffi::c_int;

use monohull_host as _; // links the C libraries

#[repr(C)]
struct RawReverb {
    _private: [u8; 0],
}

extern "C" {
    fn mhr_rings_init();
    fn mhr_rings_set(amount: f32, input_gain: f32, time: f32, diffusion: f32, lp: f32);
    fn mhr_rings_process(l: *mut f32, r: *mut f32, n: c_int);
    fn mhh_reverb_new(rate: f32) -> *mut RawReverb;
    fn mhh_reverb_free(r: *mut RawReverb);
    fn mhh_reverb_set(r: *mut RawReverb, amount: f32, input_gain: f32, time: f32, diffusion: f32, lp: f32);
    fn mhh_reverb_process(r: *mut RawReverb, l: *mut f32, rr: *mut f32, n: c_int);
}

/// A test signal: noise bursts, a decaying sine and silence, louder than full scale at times
/// (the 16-bit memory's clipping is part of what must match).
fn signal(n: usize) -> (Vec<f32>, Vec<f32>) {
    let mut x = 0x2468_ace1u32;
    let mut l = Vec::with_capacity(n);
    let mut r = Vec::with_capacity(n);
    for i in 0..n {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        let w = (x as i32) as f32 / 2_147_483_648.0;
        let t = i % 24_000;
        let burst = if t < 2400 { 1.0 } else { 0.0 };
        let sine = (i as f32 * 0.031).sin() * (-(t as f32) / 6000.0).exp();
        let big = if (i / 24_000) % 5 == 3 { 4.0 } else { 1.0 };
        l.push(big * (0.6 * burst * w + 0.4 * sine));
        r.push(big * (0.6 * burst * w - 0.3 * sine));
    }
    (l, r)
}

#[test]
fn reverb_is_rings_to_the_bit() {
    // Rings' own settings at a few positions of its knobs, changed mid-stream
    let settings = [(0.1, 0.2, 0.35, 0.625, 0.3), (0.35, 0.2, 0.665, 0.625, 0.6), (0.6, 0.2, 0.98, 0.625, 0.9),
                    (0.5, 0.2, 0.5, 0.7, 0.7)];
    let (l0, r0) = signal(240_000);
    let (mut la, mut ra) = (l0.clone(), r0.clone());
    let (mut lb, mut rb) = (l0.clone(), r0.clone());
    unsafe {
        mhr_rings_init();
        let ours = mhh_reverb_new(48_000.0);
        for (k, (lc, rc)) in la.chunks_mut(60_000).zip(ra.chunks_mut(60_000)).enumerate() {
            let s = settings[k];
            mhr_rings_set(s.0, s.1, s.2, s.3, s.4);
            for (l, r) in lc.chunks_mut(64).zip(rc.chunks_mut(64)) {
                mhr_rings_process(l.as_mut_ptr(), r.as_mut_ptr(), l.len() as c_int);
            }
        }
        for (k, (lc, rc)) in lb.chunks_mut(60_000).zip(rb.chunks_mut(60_000)).enumerate() {
            let s = settings[k];
            mhh_reverb_set(ours, s.0, s.1, s.2, s.3, s.4);
            for (l, r) in lc.chunks_mut(37).zip(rc.chunks_mut(37)) {
                mhh_reverb_process(ours, l.as_mut_ptr(), r.as_mut_ptr(), l.len() as c_int);
            }
        }
        mhh_reverb_free(ours);
    }
    let diff = la.iter().zip(&lb).chain(ra.iter().zip(&rb)).filter(|(a, b)| a.to_bits() != b.to_bits()).count();
    let first = la.iter().zip(&lb).position(|(a, b)| a.to_bits() != b.to_bits());
    eprintln!("reverb against Rings: {diff} of {} samples differ (first at {first:?})", 2 * la.len());
    assert_eq!(diff, 0);
}

// ----- the effects in the synth, as an ear would check them

use monohull_host::analysis::*;
use monohull_host::{Synth, FM1_RATE};

/// A noise burst through a wide-open filter: a flat source, so any ringing in a tail is the
/// effect's own.
fn burst(s: &mut Synth) -> (Vec<f32>, Vec<f32>) {
    s.set("osc1_level", 0.0).set("noise_level", 1.0).set("cutoff", 20_000.0).set("resonance", 0.0);
    s.set("drive", 0.0).set("fenv_amount", 0.0).set("aenv_attack", 0.001).set("aenv_release", 0.005);
    s.set("vintage", 0.0).set("volume", 0.5);
    s.note_on(60, 1.0);
    let (mut l, mut r) = s.render(2205);
    s.note_off(60);
    let (a, b) = s.render((FM1_RATE * 4.0) as usize);
    l.extend(a);
    r.extend(b);
    (l, r)
}

/// The strongest peak over the running median (200 Hz-8 kHz) of a held noise note's spectrum,
/// through the reverb at (time, tone), or dry.
fn colour(reverb: Option<(f32, f32)>) -> f64 {
    let rate = FM1_RATE as f64;
    let mut s = Synth::new(FM1_RATE);
    if let Some((time, tone)) = reverb {
        s.set("reverb_mix", 1.0).set("reverb_time", time).set("reverb_tone", tone);
    }
    s.set("osc1_level", 0.0).set("noise_level", 1.0).set("cutoff", 20_000.0).set("resonance", 0.0);
    s.set("drive", 0.0).set("fenv_amount", 0.0).set("vintage", 0.0).set("volume", 0.5).set("aenv_sustain", 1.0);
    s.note_on(60, 1.0);
    let (l, _) = s.render(2 * 32_768 + 4410);
    let sp = spectrum_db(&l[4410 + 32_768..]);
    let band = |f: f64| (f / rate * sp.len() as f64 * 2.0) as usize;
    let mut worst = -100.0f64;
    for i in band(200.0)..band(8000.0) {
        let mut nb: Vec<f64> = sp[i - 64..i + 64].to_vec();
        nb.sort_by(|a, b| a.partial_cmp(b).unwrap());
        worst = worst.max(sp[i] - nb[64]);
    }
    worst
}

fn db(x: f64) -> f64 {
    20.0 * x.max(1e-12).log10()
}

#[test]
fn reverb_tails_are_smooth_wide_and_die() {
    let rate = FM1_RATE as f64;
    for (time, tone) in [(0.0f32, 0.5f32), (0.5, 0.0), (0.5, 1.0), (1.0, 0.5)] {
        let mut s = Synth::new(FM1_RATE);
        s.set("reverb_mix", 1.0).set("reverb_time", time).set("reverb_tone", tone);
        let (l, r) = burst(&mut s);
        let at = |t: f64| (t * rate) as usize;
        // the decay: 100 ms windows from 0.2 s after the burst, each quieter than the one before
        // (allowing 1 dB of wobble), and well down by the end
        let w: Vec<f64> = (0..30).map(|k| db(rms(&l[at(0.25 + 0.1 * k as f64)..at(0.35 + 0.1 * k as f64)]))).collect();
        let rises = w.windows(2).filter(|p| p[1] > p[0] + 1.0).count();
        let drop = w[0] - w[w.len() - 1];
        // ringing: with noise held, the strongest peak of the output's spectrum over its running
        // median, against the dry noise's own (a metallic mode would stand out; noise alone
        // reaches ~12 dB by chance over these 5800 bins)
        let worst = colour(Some((time, tone))) - colour(None);
        // width: the tail's left and right correlation
        let (t0, t1) = (at(0.3), at(1.3));
        let (mut lr, mut ll, mut rr) = (0.0f64, 0.0f64, 0.0f64);
        for i in t0..t1 {
            lr += l[i] as f64 * r[i] as f64;
            ll += l[i] as f64 * l[i] as f64;
            rr += r[i] as f64 * r[i] as f64;
        }
        let corr = lr / (ll * rr).sqrt();
        eprintln!("reverb time {time} tone {tone}: tail falls {drop:.1} dB over 3 s ({rises} rises), peaks {worst:+.1} dB against dry noise, L/R correlation {corr:.2}");
        assert!(l.iter().chain(&r).all(|v| v.is_finite()));
        assert_eq!(rises, 0);
        assert!(drop > 6.0);
        assert!(worst < 3.0, "a ringing mode {worst:.1} dB above the dry noise's peaks");
        assert!(corr.abs() < 0.5);
    }
}

#[test]
fn delay_echoes_on_the_beat_and_ping_pong() {
    // 1/4 at 120 BPM: half a second; the first echo on the left, the second on the right
    let mut s = Synth::new(FM1_RATE);
    s.set("delay_mix", 1.0).set("delay_div", 5.0).set("tempo", 120.0).set("delay_feedback", 0.5).set("delay_tone", 1.0);
    let (l, r) = burst(&mut s);
    let rate = FM1_RATE as f64;
    let energy = |x: &[f32], t: f64| rms(&x[(t * rate) as usize..((t + 0.05) * rate) as usize]);
    let (l1, r1, l2, r2) = (energy(&l, 0.5), energy(&r, 0.5), energy(&l, 1.0), energy(&r, 1.0));
    let gap = energy(&l, 0.75);
    eprintln!("echo 1: L {:.1} dB, R {:.1} dB; echo 2: L {:.1} dB, R {:.1} dB; between them {:.1} dB",
              db(l1), db(r1), db(l2), db(r2), db(gap));
    assert!(db(l1) > db(r1) + 20.0, "the first echo is on the left");
    assert!(db(r2) > db(l2) + 20.0, "the second on the right");
    assert!(db(r2) < db(l1) - 3.0, "and quieter");
    assert!(db(gap) < db(l1) - 40.0, "silence between echoes");
}

#[test]
fn effects_stay_bounded_and_die_away() {
    // the most of everything, fed hard for 10 s, then 30 s of nothing (Rings' longest reverb is
    // nearly a freeze: about 0.8 dB a second, so the check is that it falls, steadily)
    let mut s = Synth::new(FM1_RATE);
    s.set("delay_mix", 1.0).set("delay_feedback", 0.95).set("delay_tone", 1.0).set("delay_div", 0.0);
    s.set("reverb_mix", 1.0).set("reverb_time", 1.0).set("reverb_tone", 1.0).set("drive", 1.0).set("volume", 1.0);
    s.note_on(48, 1.0);
    let (l, _) = s.render((FM1_RATE * 10.0) as usize);
    s.note_off(48);
    let (l2, _) = s.render((FM1_RATE * 30.0) as usize);
    let sec = |k: usize| db(rms(&l2[k * 44_100..(k + 1) * 44_100]));
    eprintln!("full feedback and time: peak {:.2} playing; after: {:.1} dB at 1 s, {:.1} dB at 10 s, {:.1} dB at 29 s",
              peak(&l), sec(1), sec(10), sec(29));
    assert!(l.iter().chain(&l2).all(|v| v.is_finite() && v.abs() <= 1.0));
    assert!(sec(10) < sec(1) - 4.0 && sec(29) < sec(10) - 8.0);
}

#[test]
fn no_old_tails_and_no_clicks() {
    let rate = FM1_RATE as usize;
    // a delay turned off mid-tail and on again later, with nothing played (a note held at no
    // level, so the voice keeps running): silence, not the old echoes
    let mut s = Synth::new(FM1_RATE);
    s.set("delay_mix", 1.0).set("delay_feedback", 0.8).set("aenv_sustain", 1.0);
    s.note_on(60, 1.0);
    s.render(rate / 4);
    s.set("osc1_level", 0.0);
    s.render(rate / 4);
    s.set("delay_mix", 0.0);
    s.render(rate);
    s.set("delay_mix", 1.0);
    let (l, r) = s.render(2 * rate);
    let ghost = peak(&l).max(peak(&r));
    eprintln!("delay back on after an old tail: peak {:.1} dB", db(ghost as f64));
    assert!(ghost < 1e-4);

    // the scheduler taking the effects away mid-note: a fade, not a step
    let mut s = Synth::new(FM1_RATE);
    s.set("osc1_shape", 1.0).set("reverb_mix", 0.8).set("delay_mix", 0.6).set("aenv_sustain", 1.0);
    s.set("fenv_amount", 0.0);
    s.note_on(57, 1.0);
    let (a, _) = s.render(rate);
    s.fx_limit(false);
    let (b, _) = s.render(rate / 2);
    let jump = |x: &[f32]| x.windows(2).map(|p| (p[1] - p[0]).abs()).fold(0.0f32, f32::max);
    let (before, after) = (jump(&a[rate / 2..]), jump(&b[..rate / 10]));
    eprintln!("effects switched off mid-note: largest step {after:.4} (before {before:.4})");
    assert!(after < 1.5 * before);
}
