//! The arpeggiator: patterns, latch, MIDI clock. A sine through an open filter, so each step's
//! pitch can be read off.
use monohull_host::analysis::*;
use monohull_host::*;

const RATE: f32 = 44_100.0;
const STEP: usize = 5512; // a 1/16 at 120 BPM

fn arp(mode: f32) -> Synth {
    let mut s = Synth::new(RATE);
    s.set("osc1_shape", 1.0).set("cutoff", 20_000.0).set("resonance", 0.0).set("drive", 0.0).set("vintage", 0.0);
    s.set("fenv_amount", 0.0).set("key_track", 0.0).set("aenv_attack", 0.001).set("aenv_sustain", 1.0);
    s.set("aenv_release", 0.005).set("arp_mode", mode).set("arp_div", 3.0).set("tempo", 120.0).set("arp_gate", 0.8);
    s
}

/// The note sounding in each step (from the pitch in its first half).
fn steps(s: &mut Synth, n: usize) -> Vec<i32> {
    (0..n).map(|_| {
        let (l, _) = s.render(STEP);
        let f = frequency(&l[200..STEP / 2], RATE as f64);
        if f <= 0.0 { -1 } else { (69.0 + 12.0 * (f / 440.0).log2()).round() as i32 }
    }).collect()
}

#[test]
fn patterns() {
    for (mode, want) in [(1.0, vec![60, 64, 67, 60, 64, 67]), (2.0, vec![67, 64, 60, 67, 64, 60]),
                         (3.0, vec![60, 64, 67, 64, 60, 64])] {
        let mut s = arp(mode);
        for n in [64, 60, 67] {
            s.note_on(n, 0.8);
        }
        let got = steps(&mut s, 6);
        assert_eq!(got, want, "mode {mode}");
    }
    // two octaves, played order
    let mut s = arp(5.0);
    s.set("arp_oct", 2.0);
    for n in [64, 60] {
        s.note_on(n, 0.8);
    }
    assert_eq!(steps(&mut s, 4), vec![64, 60, 76, 72]);
}

#[test]
fn stops_with_the_keys_unless_latched() {
    let mut s = arp(1.0);
    s.note_on(60, 0.8);
    s.note_on(67, 0.8);
    steps(&mut s, 2);
    s.note_off(60);
    s.note_off(67);
    assert_eq!(steps(&mut s, 2), vec![-1, -1], "released: silent");

    let mut s = arp(1.0);
    s.set("arp_latch", 1.0);
    s.note_on(60, 0.8);
    s.note_on(67, 0.8);
    s.note_off(60);
    s.note_off(67);
    assert_eq!(steps(&mut s, 4), vec![60, 67, 60, 67], "latched: plays on");
    s.note_on(62, 0.8); // a new chord replaces it
    let got = steps(&mut s, 2);
    assert_eq!(got, vec![62, 62]);
}

#[test]
fn follows_midi_clock() {
    // clock at 150 BPM: a 1/16 is 6 ticks of RATE * 60 / 150 / 24 samples
    let mut s = arp(1.0);
    s.note_on(60, 0.8);
    s.note_on(72, 0.8);
    let tick = (RATE * 60.0 / 150.0 / 24.0) as usize;
    s.transport(true);
    let mut audio = Vec::new();
    for _ in 0..24 * 4 {
        s.clock();
        audio.extend(s.render(tick).0);
    }
    // the notes change every 6 ticks
    let step = 6 * tick;
    let got: Vec<i32> = (1..8).map(|k| {
        let f = frequency(&audio[k * step + 300..k * step + step / 2], RATE as f64);
        (69.0 + 12.0 * (f / 440.0).log2()).round() as i32
    }).collect();
    assert_eq!(got, vec![72, 60, 72, 60, 72, 60, 72]);
}
