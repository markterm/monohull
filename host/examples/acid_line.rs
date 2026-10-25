//! A 16-step acid line (accents as velocity, slides as overlapping notes) on the shipped ACID
//! patch and on a retuned one, written to out/ for listening.
use monohull_host::*;

const RATE: f32 = 44_100.0;
/// (note, accent, slide into the next)
const LINE: [(i32, bool, bool); 16] = [
    (36, true, false), (36, false, false), (48, false, true), (36, false, false), (39, true, false), (36, false, true),
    (43, false, false), (36, false, false), (36, true, false), (46, false, true), (48, false, false), (36, false, false),
    (41, true, true), (43, false, false), (39, false, false), (36, false, true),
];

fn shipped(s: &mut Synth) {
    for (n, v) in [("cutoff", 300.0), ("resonance", 0.86), ("drive", 0.55), ("fenv_amount", 3.2), ("fenv_decay", 0.18),
                   ("fenv_sustain", 0.0), ("aenv_decay", 0.3), ("aenv_sustain", 0.6), ("aenv_release", 0.04),
                   ("glide", 0.05), ("glide_legato", 1.0), ("fenv_velocity", 0.8), ("key_track", 0.2), ("hpf", 40.0)] {
        s.set(n, v);
    }
}

/// The 303 way: a low cutoff and a big, short sweep; resonance just under self-oscillation with
/// the bass made up; accents opening the sweep and the VCA; a short fixed slide.
fn retuned(s: &mut Synth) {
    for (n, v) in [("cutoff", 180.0), ("resonance", 0.92), ("bass_comp", 0.6), ("drive", 0.4), ("fenv_amount", 5.0),
                   ("fenv_attack", 0.001), ("fenv_decay", 0.22), ("fenv_sustain", 0.0), ("fenv_release", 0.05),
                   ("fenv_velocity", 1.0), ("aenv_attack", 0.001), ("aenv_decay", 1.5), ("aenv_sustain", 0.35),
                   ("aenv_release", 0.02), ("amp_velocity", 0.6), ("glide", 0.06), ("glide_legato", 1.0),
                   ("key_track", 0.1), ("hpf", 30.0), ("vintage", 0.2), ("volume", 0.55)] {
        s.set(n, v);
    }
}

fn play(patch: fn(&mut Synth)) -> Vec<f32> {
    let mut s = Synth::new(RATE);
    patch(&mut s);
    let step = (RATE * 60.0 / 126.0 / 4.0) as usize;
    let gate = step * 6 / 10;
    let mut out = Vec::new();
    for _ in 0..4 {
        for (i, &(n, acc, slide)) in LINE.iter().enumerate() {
            s.note_on(n, if acc { 1.0 } else { 0.45 });
            let prev = LINE[(i + 15) % 16];
            if prev.2 && prev.0 != n {
                s.note_off(prev.0); // the slide's overlap ends here
            }
            if slide {
                out.extend(s.render(step).0); // held into the next note: legato, glide
            } else {
                out.extend(s.render(gate).0);
                s.note_off(n);
                out.extend(s.render(step - gate).0);
            }
        }
    }
    s.all_off();
    out.extend(s.render(RATE as usize / 2).0);
    out
}

/// The diode ladder, the 303's filter: the peak at the cutoff over a gently falling passband.
fn diode(s: &mut Synth) {
    for (n, v) in [("filter", 2.0), ("cutoff", 420.0), ("resonance", 0.9), ("drive", 0.45), ("fenv_amount", 3.6),
                   ("fenv_attack", 0.001), ("fenv_decay", 0.24), ("fenv_sustain", 0.0), ("fenv_release", 0.05),
                   ("fenv_velocity", 1.0), ("aenv_attack", 0.001), ("aenv_decay", 1.5), ("aenv_sustain", 0.35),
                   ("aenv_release", 0.02), ("amp_velocity", 0.6), ("glide", 0.06), ("glide_legato", 1.0),
                   ("key_track", 0.15), ("hpf", 30.0), ("vintage", 0.2), ("volume", 0.6)] {
        s.set(n, v);
    }
}

fn main() {
    std::fs::create_dir_all("out").unwrap();
    for (name, p) in [("acid-line-shipped", shipped as fn(&mut Synth)), ("acid-line-retuned", retuned), ("acid-line-diode", diode)] {
        let x = play(p);
        let peak = x.iter().fold(0f32, |m, v| m.max(v.abs()));
        wav::write(std::path::Path::new(&format!("out/{name}.wav")), RATE as u32, &x, &x).unwrap();
        println!("out/{name}.wav: {:.1} s, peak {peak:.2}", x.len() as f32 / RATE);
    }
}
