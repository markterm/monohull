//! Demo renders: a few patches playing short phrases, written to out/*.wav (24-bit, 44.1 kHz),
//! and the host's cost per second of audio for each (a relative measure only: the FM-1's costs
//! come from fm1-emu's instruction counts).

use std::path::Path;
use std::time::Instant;

use monohull_host::*;

const RATE: f32 = 44_100.0;

/// (start in 16ths, note, length in 16ths, velocity)
type Phrase = &'static [(u32, i32, u32, f32)];

struct Demo {
    name: &'static str,
    bpm: f32,
    phrase: Phrase,
    patch: fn(&mut Synth),
    /// a sweep applied every 16th: (step, synth)
    sweep: Option<fn(u32, &mut Synth)>,
}

const BASS: Phrase = &[(0, 36, 2, 1.0), (3, 36, 1, 0.6), (4, 48, 1, 0.9), (6, 36, 2, 1.0), (8, 39, 2, 0.9),
    (10, 36, 1, 0.6), (11, 43, 1, 0.8), (12, 41, 2, 1.0), (14, 43, 2, 0.7), (16, 36, 2, 1.0), (19, 36, 1, 0.6),
    (20, 48, 1, 0.9), (22, 36, 2, 1.0), (24, 46, 2, 0.9), (26, 43, 2, 0.8), (28, 41, 1, 1.0), (29, 39, 1, 0.7),
    (30, 38, 2, 0.9)];
const LEAD: Phrase = &[(0, 67, 6, 0.9), (6, 70, 2, 0.8), (8, 72, 4, 1.0), (12, 75, 2, 0.8), (14, 74, 2, 0.8),
    (16, 72, 8, 1.0), (24, 70, 2, 0.7), (26, 67, 2, 0.8), (28, 65, 4, 0.9)];
const PAD: Phrase = &[(0, 48, 15, 0.8), (16, 51, 15, 0.8), (32, 46, 15, 0.8), (48, 53, 15, 0.8)];
const ACID: Phrase = &[(0, 36, 1, 1.0), (1, 36, 1, 0.6), (2, 48, 1, 0.9), (3, 36, 1, 0.6), (4, 39, 2, 1.0),
    (6, 36, 1, 0.6), (7, 51, 1, 0.9), (8, 36, 1, 1.0), (9, 46, 2, 0.8), (11, 36, 1, 0.6), (12, 43, 1, 1.0),
    (13, 41, 1, 0.7), (14, 39, 1, 0.9), (15, 36, 1, 0.6)];

fn bass(s: &mut Synth) {
    s.set("osc1_shape", 0.0).set("osc2_shape", 0.333).set("osc2_level", 0.6).set("osc2_semi", -12.0);
    s.set("osc2_fine", 4.0).set("sub_level", 0.4).set("cutoff", 220.0).set("resonance", 0.45).set("drive", 0.45);
    s.set("fenv_amount", 3.5).set("fenv_attack", 0.001).set("fenv_decay", 0.25).set("fenv_sustain", 0.1);
    s.set("aenv_decay", 0.4).set("aenv_sustain", 0.7).set("aenv_release", 0.06).set("key_track", 0.3);
    s.set("bass_comp", 0.6).set("vintage", 0.4);
}

fn sync_lead(s: &mut Synth) {
    s.set("osc1_level", 0.0).set("osc2_level", 1.0).set("sync", 1.0).set("osc2_semi", 7.0);
    s.set("menv_pitch2", 19.0).set("menv_decay", 0.35).set("menv_sustain", 0.25).set("cutoff", 3500.0);
    s.set("resonance", 0.25).set("drive", 0.35).set("fenv_amount", 1.5).set("glide", 0.06).set("glide_legato", 1.0);
    s.set("vibrato", 0.0).set("wheel_vibrato", 0.3).set("lfo1_rate", 5.8).set("lfo1_delay", 0.3).set("lfo1_fade", 0.4);
    s.set("aenv_sustain", 0.9).set("aenv_release", 0.25).set("chorus", 1.0).set("chorus_mix", 0.5);
}

fn supersaw(s: &mut Synth) {
    s.set("osc1_unison", 7.0).set("osc2_unison", 7.0).set("osc2_level", 0.7).set("osc2_semi", 12.0);
    s.set("detune", 0.42).set("unison_mix", 0.7).set("spread", 1.0).set("stereo", 1.0);
    s.set("cutoff", 2400.0).set("resonance", 0.15).set("drive", 0.15).set("fenv_amount", 1.0);
    s.set("fenv_attack", 0.6).set("fenv_decay", 1.5).set("fenv_sustain", 0.5).set("aenv_attack", 0.25);
    s.set("aenv_sustain", 1.0).set("aenv_release", 0.9).set("chorus", 4.0).set("chorus_mix", 0.6).set("volume", 0.7);
}

fn pwm_brass(s: &mut Synth) {
    s.set("osc1_shape", 0.333).set("osc1_pw", 0.4).set("osc2_shape", 0.0).set("osc2_level", 0.7);
    s.set("osc2_fine", 7.0).set("lfo1_rate", 0.7).set("lfo1_pwm", 0.6).set("cutoff", 700.0).set("resonance", 0.2);
    s.set("fenv_amount", 3.0).set("fenv_attack", 0.08).set("fenv_decay", 0.5).set("fenv_sustain", 0.45);
    s.set("aenv_attack", 0.03).set("aenv_sustain", 0.9).set("aenv_release", 0.3).set("chorus", 3.0);
    s.set("chorus_mix", 0.8).set("vintage", 0.6);
}

fn acid(s: &mut Synth) {
    s.set("osc1_shape", 0.0).set("cutoff", 300.0).set("resonance", 0.86).set("drive", 0.55).set("fenv_amount", 3.2);
    s.set("fenv_attack", 0.001).set("fenv_decay", 0.18).set("fenv_sustain", 0.0).set("aenv_decay", 0.3);
    s.set("aenv_sustain", 0.6).set("aenv_release", 0.04).set("glide", 0.05).set("glide_legato", 1.0);
    s.set("fenv_velocity", 0.8).set("key_track", 0.2).set("hpf", 40.0);
}

fn acid_sweep(step: u32, s: &mut Synth) {
    let t = (step % 64) as f32 / 64.0;
    let c = 200.0 * 2f32.powf(4.0 * (std::f32::consts::PI * t).sin());
    s.set("cutoff", c);
}

/// Every demo, at both qualities for the acid sweep (the hardest case for the filter).
fn demos() -> Vec<Demo> {
    vec![
        Demo { name: "bass", bpm: 118.0, phrase: BASS, patch: bass, sweep: None },
        Demo { name: "sync-lead", bpm: 96.0, phrase: LEAD, patch: sync_lead, sweep: None },
        Demo { name: "supersaw-pad", bpm: 90.0, phrase: PAD, patch: supersaw, sweep: None },
        Demo { name: "pwm-brass", bpm: 100.0, phrase: LEAD, patch: pwm_brass, sweep: None },
        Demo { name: "acid", bpm: 128.0, phrase: ACID, patch: acid, sweep: Some(acid_sweep) },
    ]
}

fn play(d: &Demo, quality: f32, repeats: u32) -> (Vec<f32>, Vec<f32>, f64, f64) {
    let mut s = Synth::new(RATE);
    (d.patch)(&mut s);
    s.set("quality", quality);
    let step = (RATE * 60.0 / d.bpm / 4.0) as usize;
    let len = d.phrase.iter().map(|e| e.0 + e.2).max().unwrap().next_multiple_of(16);
    let (mut l, mut r) = (Vec::new(), Vec::new());
    let mut spent = 0.0;
    for rep in 0..repeats {
        for t in 0..len {
            let abs = rep * len + t;
            if let Some(sw) = d.sweep {
                sw(abs, &mut s);
            }
            for e in d.phrase.iter().filter(|e| e.0 + e.2 == t) {
                s.note_off(e.1);
            }
            for e in d.phrase.iter().filter(|e| e.0 == t) {
                s.note_on(e.1, e.3);
            }
            let t0 = Instant::now();
            s.render_into(step, &mut l, &mut r);
            spent += t0.elapsed().as_secs_f64();
        }
    }
    s.all_off();
    s.render_into(RATE as usize, &mut l, &mut r);
    let (steps, samples) = s.newton();
    let audio = l.len() as f64 / RATE as f64;
    (l, r, spent / audio, steps as f64 / samples.max(1) as f64)
}

fn main() {
    let out = Path::new("out");
    std::fs::create_dir_all(out).unwrap();
    println!("{:<14} {:>9} {:>14} {:>14} {:>12}", "demo", "quality", "host us/s", "Newton/sample", "peak");
    for d in demos() {
        let repeats = if d.phrase == PAD { 1 } else { 2 };
        for (q, qn) in [(0.0f32, "high"), (1.0, "efficient")] {
            let (l, r, cost, newton) = play(&d, q, repeats);
            let p = analysis::peak(&l).max(analysis::peak(&r));
            println!("{:<14} {:>9} {:>14.0} {:>14.2} {:>12.2}", d.name, qn, cost * 1e6, newton, p);
            wav::write(&out.join(format!("{}-{qn}.wav", d.name)), RATE as u32, &l, &r).unwrap();
        }
    }
    println!("wrote {}", out.canonicalize().unwrap().display());
}
