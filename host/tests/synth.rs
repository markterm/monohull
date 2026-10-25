//! The whole synth: silence, latency, tuning, fuzzing, the scheduler's limits, CCs, drive
//! aliasing, glide, stereo and the chorus.

use monohull_host::analysis::*;
use monohull_host::*;

const RATE: f32 = 44_100.0;

/// A sine through a wide-open filter: for pitch measurements.
fn pure(s: &mut Synth) {
    s.set("osc1_shape", 1.0).set("cutoff", 20_000.0).set("resonance", 0.0).set("drive", 0.0).set("vintage", 0.0);
    s.set("aenv_attack", 0.001).set("aenv_sustain", 1.0).set("fenv_amount", 0.0).set("key_track", 0.0);
}

#[test]
fn silent_until_played_and_quick_to_sound() {
    let mut s = Synth::new(RATE);
    let (l, r) = s.render(44_100);
    assert!(l.iter().chain(r.iter()).all(|&v| v == 0.0), "idle output is exact silence");
    s.note_on(60, 1.0);
    let (l, _) = s.render(512);
    let first = l.iter().position(|&v| v != 0.0).unwrap();
    eprintln!("first sound {first} frames after the note");
    assert!(first < 128);
}

#[test]
fn plays_in_tune_and_bends() {
    for quality in [0.0, 1.0] {
        let mut s = Synth::new(RATE);
        pure(&mut s);
        s.set("quality", quality);
        s.note_on(69, 1.0);
        let (l, _) = s.render(44_100);
        let c = cents(frequency(&l[22_050..], RATE as f64), 440.0);
        s.bend(1.0);
        let (l, _) = s.render(44_100);
        let b = cents(frequency(&l[22_050..], RATE as f64), 440.0);
        eprintln!("quality {quality}: A4 {c:+.2} cents, bent up {b:+.2} cents (200)");
        assert!(c.abs() < 0.5);
        assert!((b - 200.0).abs() < 0.5);
    }
}

#[test]
fn vintage_drifts_a_little() {
    // full Vintage on a held A4 sine: the pitch moves (drift, jitter), but stays within its
    // budget (4 cents of drift, 3 of note-on spread, jitter of 0.6 rms)
    let mut s = Synth::new(RATE);
    pure(&mut s);
    s.set("vintage", 1.0);
    s.note_on(69, 1.0);
    s.render(4410);
    let (l, _) = s.render(44_100 * 20);
    let c: Vec<f64> = l.chunks(11_025).map(|w| cents(frequency(w, RATE as f64), 440.0)).collect();
    let (lo, hi) = c.iter().fold((f64::MAX, f64::MIN), |(a, b), &v| (a.min(v), b.max(v)));
    eprintln!("Vintage 1, A4 over 20 s in quarter seconds: {lo:+.2} to {hi:+.2} cents");
    assert!(hi - lo > 1.0, "the pitch doesn't move");
    assert!(lo > -10.0 && hi < 10.0);
}

#[test]
fn knob_steps_glide() {
    // the self-oscillating ladder as a pitch meter: the cutoff turned a detent (1/256 of its
    // range, 0.04 octave) every 50 ms; its whistle's pitch must move smoothly, not jump. Measured
    // in 5 ms windows: unsmoothed, a whole detent (48 cents) lands inside one window
    let mut s = Synth::new(RATE);
    s.set("osc1_level", 0.0).set("noise_level", 0.002).set("resonance", 1.0).set("drive", 0.0);
    s.set("vintage", 0.0).set("aenv_sustain", 1.0).set("fenv_amount", 0.0).set("key_track", 0.0);
    s.set("cutoff", 1000.0);
    s.note_on(60, 1.0);
    s.render(22_050);
    let detent = (20_000.0f32 / 16.0).log2() / 256.0;
    let mut out = Vec::new();
    for k in 1..=20 {
        s.set("cutoff", 1000.0 * (k as f32 * detent).exp2());
        out.extend(s.render(2205).0);
    }
    let w = 220;
    let f: Vec<f64> = out.chunks(w).map(|c| frequency(c, RATE as f64)).collect();
    let worst = f.windows(2).map(|p| cents(p[1], p[0]).abs()).fold(0.0, f64::max);
    let total = cents(f[f.len() - 1], f[0]);
    eprintln!("cutoff turned 20 detents in 1 s: the whistle rose {total:.0} cents, at most {worst:.1} cents in 5 ms");
    assert!(total > 700.0, "the whistle doesn't follow the cutoff");
    assert!(worst < 12.0, "steps of {worst:.1} cents");
}

#[test]
fn filters_at_4x() {
    // the same patch with the filters at 2x and at 4x: the same response where it is linear,
    // in tune when it self-oscillates, cleaner when the OTA is hit hard
    let rate = RATE as f64;
    let render = |f: f32, x4: bool, set: &dyn Fn(&mut Synth)| {
        let mut s = Synth::new(RATE);
        s.set("filter", f).set("vintage", 0.0).set("aenv_sustain", 1.0).set("fenv_amount", 0.0).set("key_track", 0.0);
        set(&mut s);
        s.filter_x4(x4);
        s.note_on(48, 1.0);
        s.render(8820);
        s.render(1 << 16).0
    };
    for f in 0..5 {
        // a quiet saw (a tenth of full scale into the filter: linear), resonance moderate:
        // harmonics 1-40 within 0.2 dB
        let quiet = |s: &mut Synth| {
            s.set("drive", 0.0).set("osc1_level", 0.1).set("cutoff", 3000.0).set("resonance", 0.5);
        };
        let (a, b) = (render(f as f32, false, &quiet), render(f as f32, true, &quiet));
        let f0 = 130.8128;
        let mut worst = 0.0f64;
        for h in 1..=40 {
            let (x, y) = (amplitude(&a, rate, f0 * h as f64), amplitude(&b, rate, f0 * h as f64));
            if x > 1e-4 {
                worst = f64::max(worst, (20.0 * (y / x).log10()).abs());
            }
        }
        eprintln!("filter {f}: 2x against 4x, harmonics 1-40 within {worst:.3} dB");
        assert!(worst < 0.2);
    }
    // self-oscillation at 4x, in tune (ladder and OTA, A4 and A6)
    for f in [0.0f32, 3.0] {
        for cut in [440.0f32, 1760.0] {
            let whistle = move |s: &mut Synth| {
                s.set("osc1_level", 0.0).set("noise_level", 0.001).set("resonance", 1.0).set("drive", 0.0).set("cutoff", cut);
            };
            let y = render(f, true, &whistle);
            let c = cents(frequency(&y[y.len() / 2..], rate), cut as f64);
            eprintln!("filter {f} self-oscillating at 4x, {cut} Hz: {c:+.2} cents");
            assert!(c.abs() < 3.0);
        }
    }
    // the OTA hit hard (BASS COMP full, drive full) on a high sine
    let hot = |s: &mut Synth| {
        s.set("osc1_shape", 1.0).set("drive", 1.0).set("bass_comp", 1.0).set("resonance", 0.9).set("cutoff", 4000.0);
        s.set("volume", 0.3);
    };
    let a = render(3.0, false, &hot);
    let b = render(3.0, true, &hot);
    let (ea, eb) = (worst_alias_db(&a, rate, 130.8128, 20.0, 16_000.0, 12), worst_alias_db(&b, rate, 130.8128, 20.0, 16_000.0, 12));
    eprintln!("the OTA hit hard on C3: worst alias {:.1} dB at 2x, {:.1} dB at 4x", ea.0, eb.0);
    assert!(eb.0 <= ea.0 + 1.0);
}

#[test]
fn priority_and_glide() {
    let mut s = Synth::new(RATE);
    pure(&mut s);
    s.set("priority", 1.0); // low
    s.note_on(60, 1.0);
    s.note_on(64, 1.0);
    let (l, _) = s.render(22_050);
    let f = frequency(&l[11_025..], RATE as f64);
    assert!(cents(f, 261.6256).abs() < 1.0, "low priority keeps C4: {f}");

    let mut s = Synth::new(RATE);
    pure(&mut s);
    s.set("glide", 0.2);
    s.note_on(60, 1.0);
    s.render(22_050);
    s.note_on(72, 1.0);
    let (l, _) = s.render(44_100);
    let early = frequency(&l[441..1764], RATE as f64);
    let late = frequency(&l[22_050..], RATE as f64);
    eprintln!("glide C4 -> C5: {early:.1} Hz after 10-40 ms, {late:.1} Hz after 0.5 s");
    assert!(early > 270.0 && early < 500.0);
    assert!(cents(late, 523.2511).abs() < 1.0);
}

#[test]
fn drive_aliasing() {
    // a C6 saw driven hard through a wide-open ladder
    for quality in [0.0f32, 1.0] {
        let mut s = Synth::new(RATE);
        s.set("quality", quality).set("drive", 1.0).set("cutoff", 20_000.0).set("resonance", 0.0);
        s.set("vintage", 0.0).set("aenv_sustain", 1.0).set("fenv_amount", 0.0).set("key_track", 0.0);
        s.note_on(84, 1.0);
        s.render(4410);
        let (l, _) = s.render(1 << 16);
        let w = worst_alias_db(&l, RATE as f64, 1046.502, 20.0, 16_000.0, 12);
        eprintln!("drive 32x on a C6 saw, {}: worst alias {:.1} dB at {:.0} Hz",
                  if quality == 0.0 { "High" } else { "Efficient" }, w.0, w.1);
        if quality == 0.0 {
            assert!(w.0 < -70.0);
        }
    }
}

#[test]
fn survives_random_patches() {
    let names = Synth::param_names();
    let mut rng = 0x1234_5678u32;
    let mut rnd = || {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        rng as f32 / u32::MAX as f32
    };
    let mut worst = 0.0f32;
    for round in 0..40 {
        let mut s = Synth::new(RATE);
        for n in &names {
            // mostly musical values, sometimes past either end (the table clamps them)
            let r = rnd();
            let v = if r < 0.6 { rnd() } else if r < 0.85 { 30.0 * rnd() } else if r < 0.95 { 20_000.0 * rnd() } else { -5.0 };
            s.set(n, v);
        }
        s.set("volume", 1.0).set("aenv_attack", 0.01 * rnd());
        s.set("quality", (round % 2) as f32);
        for step in 0..20 {
            let note = 24 + (rnd() * 84.0) as i32;
            s.note_on(note, rnd());
            if step % 3 == 0 {
                s.bend(rnd() * 2.0 - 1.0);
            }
            let (l, r) = s.render(1500 + (rnd() * 3000.0) as usize);
            for &v in l.iter().chain(r.iter()) {
                assert!(v.is_finite(), "round {round}: not finite");
                worst = worst.max(v.abs());
            }
            if rnd() < 0.5 {
                s.note_off(note);
            }
        }
    }
    eprintln!("40 random patches: peak {worst:.3}");
    assert!(worst <= 1.0);
}

#[test]
fn limits_drop_the_extras_exactly() {
    let render = |unison: f32, limit: i32| {
        let mut s = Synth::new(RATE);
        s.set("osc1_unison", unison).set("detune", 0.5);
        s.limits(limit, true, true);
        s.note_on(48, 1.0);
        s.render(8192).0
    };
    assert_eq!(render(7.0, 1), render(1.0, 7), "a limit of 1 plays the patch as one voice");
    assert_ne!(render(7.0, 7), render(1.0, 7));
}

#[test]
fn stereo_unison_and_chorus() {
    let mut s = Synth::new(RATE);
    s.set("osc1_unison", 7.0).set("detune", 0.5).set("spread", 1.0).set("stereo", 1.0);
    s.note_on(48, 1.0);
    let (l, r) = s.render(22_050);
    assert_ne!(l, r, "stereo unison is stereo");
    let mut s = Synth::new(RATE);
    s.set("osc1_unison", 7.0).set("detune", 0.5).set("spread", 1.0);
    s.note_on(48, 1.0);
    let (l, r) = s.render(22_050);
    assert_eq!(l, r, "mono unison is mono");

    let dry = {
        let mut s = Synth::new(RATE);
        s.note_on(48, 1.0);
        s.render(22_050)
    };
    let mut s = Synth::new(RATE);
    s.set("chorus", 1.0).set("chorus_mix", 0.0);
    s.note_on(48, 1.0);
    assert_eq!(s.render(22_050), dry, "chorus at mix 0 is dry");
    for mode in 1..=4 {
        let mut s = Synth::new(RATE);
        s.set("chorus", mode as f32);
        s.note_on(48, 1.0);
        let (l, r) = s.render(22_050);
        assert_ne!(l, r, "chorus mode {mode} widens");
        assert!(peak(&l) <= 1.0 && peak(&r) <= 1.0);
    }
}

#[test]
fn ccs_sweep_their_parameters() {
    let mut s = Synth::new(RATE);
    assert!(s.cc(74, 127));
    assert!((s.get("cutoff") - 20_000.0).abs() < 1.0);
    s.cc(74, 0);
    assert!((s.get("cutoff") - 16.0).abs() < 0.01);
    s.cc(74, 64);
    let mid = s.get("cutoff");
    eprintln!("CC 74 at 64: cutoff {mid:.0} Hz");
    assert!(mid > 500.0 && mid < 700.0, "exponential: the middle is ~570 Hz");
    s.cc(71, 127);
    assert_eq!(s.get("resonance"), 1.0);
    assert!(!s.cc(3, 10), "unmapped CC");
}

#[test]
fn newton_steps_on_a_patch() {
    let mut s = Synth::new(RATE);
    s.set("resonance", 0.7).set("drive", 0.6).set("cutoff", 800.0).set("fenv_amount", 4.0);
    for n in [36, 43, 48, 55] {
        s.note_on(n, 0.9);
        s.render(11_025);
        s.note_off(n);
    }
    let (steps, samples) = s.newton();
    eprintln!("Newton steps per filter sample on a bass line: {:.2}", steps as f64 / samples as f64);
    assert!((steps as f64 / samples as f64) < 1.6);
}

#[test]
fn output_level() {
    let mut s = Synth::new(RATE);
    s.note_on(48, 1.0);
    let (l, _) = s.render(44_100);
    eprintln!("default patch, C3 at full velocity: peak {:.2}, rms {:.1} dBFS", peak(&l), 20.0 * rms(&l[4410..]).log10());
    assert!(peak(&l) > 0.2 && peak(&l) <= 1.0);
}

#[test]
fn the_second_core_changes_nothing() {
    // a stereo super saw through the chorus: whichever core runs the back, in whichever order,
    // and when the other core fails it, the output is the same
    let render = |mode: i32| {
        let mut s = Synth::new(RATE);
        s.set("osc1_unison", 7.0).set("osc2_unison", 7.0).set("osc2_level", 0.7).set("detune", 0.4);
        s.set("stereo", 1.0).set("spread", 1.0).set("sync", 1.0).set("resonance", 0.6).set("drive", 0.5);
        s.set("chorus", 4.0).set("hpf", 40.0);
        s.offload_mode(mode);
        s.note_on(48, 1.0);
        let a = s.render(10_000);
        s.note_on(55, 0.7);
        let b = s.render(5_000);
        (a, b)
    };
    let here = render(0);
    for mode in 1..=3 {
        assert!(render(mode) == here, "mode {mode} differs");
    }
}

#[test]
fn unison_copies_alias_no_more_than_the_main_oscillator() {
    // seven voices with no detune: every partial is a harmonic, so whatever isn't is aliasing
    for quality in [0.0f32, 1.0] {
        let mut s = Synth::new(RATE);
        s.set("quality", quality).set("osc1_unison", 7.0).set("detune", 0.0).set("vintage", 0.0);
        s.set("cutoff", 20_000.0).set("resonance", 0.0).set("drive", 0.0).set("fenv_amount", 0.0);
        s.set("key_track", 0.0).set("aenv_sustain", 1.0);
        s.note_on(96, 1.0); // C7
        s.render(4410);
        let (l, _) = s.render(1 << 16);
        let w = worst_alias_db(&l, RATE as f64, 2093.005, 20.0, 16_000.0, 12);
        eprintln!("7-voice unison at C7, {}: worst alias {:.1} dB at {:.0} Hz",
                  if quality == 0.0 { "High" } else { "Efficient" }, w.0, w.1);
        if quality == 0.0 {
            assert!(w.0 < -75.0);
        }
    }
}
