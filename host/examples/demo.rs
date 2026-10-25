//! One WAV of every factory patch: a short phrase dry (the effects off, as FX held does), then as
//! the patch is, with a beat of silence between. host/out/presets-demo.wav, at the FM-1's rate.
//!     cargo run --release --example demo
use monohull_host::{wav, Synth, FM1_RATE};

fn main() {
    let rate = FM1_RATE;
    let sec = |t: f32| (t * rate) as usize;
    let (mut l, mut r) = (Vec::new(), Vec::new());
    for (n, name) in Synth::preset_names().iter().enumerate() {
        for wet in [false, true] {
            let mut s = Synth::new(rate);
            s.load_preset(n);
            if !wet {
                s.set("chorus", 0.0).set("delay_mix", 0.0).set("reverb_mix", 0.0);
            }
            // a bass-to-lead phrase: held notes, a quick line, a long last note
            for &(note, vel, on, off) in &[(36, 1.0f32, 0.45f32, 0.05f32), (48, 0.7, 0.2, 0.05), (43, 0.9, 0.2, 0.05),
                                           (48, 0.6, 0.2, 0.05), (55, 1.0, 0.3, 0.1), (60, 0.8, 1.2, 0.0)] {
                s.note_on(note, vel);
                let (a, b) = s.render(sec(on));
                l.extend(a);
                r.extend(b);
                s.note_off(note);
                let (a, b) = s.render(sec(off));
                l.extend(a);
                r.extend(b);
            }
            let (a, b) = s.render(sec(1.2)); // the release and tails
            l.extend(a);
            r.extend(b);
        }
        eprintln!("{name}");
    }
    let out = std::path::Path::new("out");
    std::fs::create_dir_all(out).unwrap();
    wav::write(&out.join("presets-demo.wav"), rate as u32, &l, &r).unwrap();
    eprintln!("out/presets-demo.wav: {:.0} s", l.len() as f32 / rate);
}
