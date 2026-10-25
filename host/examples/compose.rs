//! A short piece on the factory patches: RING BELL arpeggiating (two copies taking turns, so each
//! bell rings on under the next), PWM CHORUS on a slow melody, FAT BASS under them. D minor,
//! Dm - Bb - F - C a bar each, 84 BPM. Each part rendered by its own synth, mixed, peak at -1 dBFS.
//!
//! The bells' second oscillator is a twelfth up (the preset's is a fifth and 30 cents): ring
//! modulation then gives 2x and 4x the note, octaves of it, where the preset's 0.52x and 2.52x
//! clash with the chords (examples/bell_partials.rs, bell_roughness.rs: Sethares' roughness of the
//! overlapping bells halves, against the bass it falls to a quarter). The loudest partial is then
//! an octave above the note, so the bells are written an octave lower.
//!     cargo run --release --example compose        (host/out/bells-in-d.wav)
use monohull_host::analysis::*;
use monohull_host::{wav, Synth, FM1_RATE};

const BPM: f64 = 84.0;
const BEAT: f64 = 60.0 / BPM;

/// A note: start and length in beats, MIDI note, velocity.
type Note = (f64, f64, i32, f32);

/// One part: the patch's notes rendered in order, each released after its length.
fn render(preset: &str, tweaks: &[(&str, f32)], notes: &[Note], total: f64) -> (Vec<f32>, Vec<f32>) {
    let rate = FM1_RATE as f64;
    let n = Synth::preset_names().iter().position(|p| p == preset).expect(preset);
    let mut s = Synth::new(FM1_RATE);
    s.load_preset(n);
    s.set("tempo", BPM as f32);
    for &(k, v) in tweaks {
        s.set(k, v);
    }
    // the events in time order: (beat, on?, note, velocity)
    let mut ev: Vec<(f64, bool, i32, f32)> = Vec::new();
    for &(at, len, note, vel) in notes {
        ev.push((at, true, note, vel));
        ev.push((at + len, false, note, vel));
    }
    ev.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap().then(a.1.cmp(&b.1))); // offs before ons at a tie
    let (mut l, mut r) = (Vec::new(), Vec::new());
    let mut done = 0usize;
    for (at, on, note, vel) in ev {
        let t = (at * BEAT * rate) as usize;
        if t > done {
            let (a, b) = s.render(t - done);
            l.extend(a);
            r.extend(b);
            done = t;
        }
        if on {
            s.note_on(note, vel);
        } else {
            s.note_off(note);
        }
    }
    let end = (total * rate) as usize;
    let (a, b) = s.render(end.saturating_sub(done));
    l.extend(a);
    r.extend(b);
    (l, r)
}

fn main() {
    // the chords' arpeggios, eight eighths a bar
    let arp: [[i32; 8]; 4] = [
        [69, 74, 76, 77, 81, 77, 76, 74], // Dm(add9): A D E F A F E D
        [70, 74, 77, 81, 84, 81, 77, 74], // Bbmaj7(add9): Bb D F A C A F D
        [69, 72, 76, 77, 81, 77, 76, 72], // Fmaj7: A C E F A F E C
        [67, 72, 74, 76, 79, 76, 74, 72], // C(add9): G C D E G E D C
    ];
    let roots = [38, 34, 41, 36]; // D2 Bb1 F2 C2
    let bars = 24;

    // the bells: bars 1-20 the arpeggio, eighths alternating between two copies (each rings a
    // quarter under the next), louder on the beat; bars 21-23 slowing, one last chord
    let (mut bell_a, mut bell_b): (Vec<Note>, Vec<Note>) = (Vec::new(), Vec::new());
    for bar in 0..20 {
        let ch = &arp[bar % 4];
        for (i, &note) in ch.iter().enumerate() {
            let at = bar as f64 * 4.0 + i as f64 * 0.5;
            let vel = if i % 2 == 0 { 0.85 } else { 0.62 } - if bar < 2 { 0.1 } else { 0.0 };
            let part = if i % 2 == 0 { &mut bell_a } else { &mut bell_b };
            part.push((at, 0.9, note, vel));
        }
    }
    for (k, &note) in [81, 77, 76, 74, 72, 69].iter().enumerate() {
        let at = 80.0 + k as f64 * 1.0 + if k >= 4 { (k - 4) as f64 * 1.0 } else { 0.0 };
        let part = if k % 2 == 0 { &mut bell_a } else { &mut bell_b };
        part.push((at, 1.8, note, 0.7 - 0.05 * k as f32));
    }
    bell_a.push((88.0, 3.0, 74, 0.6)); // D5 and A5, the end
    bell_b.push((88.0, 3.0, 81, 0.5));

    // the bass: bars 5-8 the roots held; 9-20 a 3+3+2 of eighths, the last an octave up
    // from bar 17; 21-22 the D held
    let mut bass: Vec<Note> = Vec::new();
    for bar in 4..8 {
        bass.push((bar as f64 * 4.0, 3.8, roots[bar % 4], 0.8));
    }
    for bar in 8..20 {
        let root = roots[bar % 4];
        let at = bar as f64 * 4.0;
        let top = if bar >= 16 { root + 12 } else { root };
        bass.push((at, 1.3, root, 0.95));
        bass.push((at + 1.5, 1.3, root, 0.7));
        bass.push((at + 3.0, 0.9, top, 0.8));
    }
    bass.push((80.0, 7.5, 38, 0.8));

    // the melody: bars 9-21, long notes, the climb to D5 at bar 17
    let tune: &[(f64, f64, i32)] = &[
        (32.0, 3.0, 65), (35.0, 1.0, 64), // Dm: F, E
        (36.0, 2.0, 62), (38.0, 2.0, 65), // Bb: D, F
        (40.0, 4.0, 72),                  // F: C
        (44.0, 2.0, 70), (46.0, 2.0, 67), // C: Bb, G
        (48.0, 3.0, 69), (51.0, 1.0, 67), // Dm: A, G
        (52.0, 2.0, 65), (54.0, 2.0, 74), // Bb: F, D
        (56.0, 2.0, 72), (58.0, 2.0, 69), // F: C, A
        (60.0, 4.0, 67),                  // C: G
        (64.0, 4.0, 74),                  // Dm: D (the top)
        (68.0, 2.0, 77), (70.0, 2.0, 74), // Bb: F, D
        (72.0, 4.0, 76),                  // F: E
        (76.0, 2.0, 72), (78.0, 2.0, 76), // C: C, E
        (80.0, 6.0, 74),                  // Dm: D, held into the end
    ];
    let melody: Vec<Note> = tune.iter().map(|&(at, len, n)| (at, len * 0.97, n, 0.8)).collect();

    let total = bars as f64 * 4.0 * BEAT + 4.0; // and the tails
    let down = |v: &[Note]| v.iter().map(|&(a, l, n, vel)| (a, l, n - 12, vel)).collect::<Vec<Note>>();
    let (bell_a, bell_b) = (down(&bell_a), down(&bell_b));
    let bell: &[(&str, f32)] = &[("osc2_semi", 19.0), ("osc2_fine", 0.0)];
    let parts: [(&str, &[(&str, f32)], &[Note], f32, f32); 4] = [
        ("RING BELL", bell, &bell_a, 0.42, -0.35), // gain, pan (-1 left .. 1 right)
        ("RING BELL", bell, &bell_b, 0.42, 0.35),
        ("PWM CHORUS", &[], &melody, 0.62, 0.0),
        ("FAT BASS", &[], &bass, 0.7, 0.0),
    ];
    let n = (total * FM1_RATE as f64) as usize;
    let (mut l, mut r) = (vec![0.0f32; n], vec![0.0f32; n]);
    for (name, tweaks, notes, gain, pan) in parts {
        let (a, b) = render(name, tweaks, notes, total);
        let (gl, gr) = (gain * (1.0 - pan.max(0.0)), gain * (1.0 + pan.min(0.0)));
        for i in 0..n.min(a.len()) {
            l[i] += gl * a[i];
            r[i] += gr * b[i];
        }
        eprintln!("{name:<11} {} notes", notes.len());
    }
    let pk = peak(&l).max(peak(&r));
    let g = 10f32.powf(-1.0 / 20.0) / pk;
    for v in l.iter_mut().chain(r.iter_mut()) {
        *v *= g;
    }
    let out = std::path::Path::new("out");
    std::fs::create_dir_all(out).unwrap();
    wav::write(&out.join("bells-in-d.wav"), FM1_RATE as u32, &l, &r).unwrap();
    let mono: Vec<f32> = l.iter().zip(&r).map(|(a, b)| 0.5 * (a + b)).collect();
    eprintln!("out/bells-in-d.wav: {:.0} s, mix gain {:+.1} dB to peak -1 dBFS, rms {:.1} dBFS, mono sum {:+.1} dB",
              total, 20.0 * g.log10(), 20.0 * rms(&l).max(1e-9).log10(),
              20.0 * (rms(&mono) / rms(&l.iter().chain(&r).copied().collect::<Vec<_>>())).log10());
}
