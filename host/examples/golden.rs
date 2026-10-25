//! Hashes of the engine's output over a few patches (to check a refactor changes nothing).
use monohull_host::*;

fn main() {
    let cfgs: &[&[(&str, f32)]] = &[
        &[("osc1_unison", 7.0), ("osc2_unison", 7.0), ("osc2_level", 0.7), ("detune", 0.42), ("stereo", 1.0),
          ("spread", 1.0), ("chorus", 4.0), ("chorus_mix", 0.6), ("hpf", 40.0), ("drive", 0.3)],
        &[("chorus", 3.0), ("resonance", 0.8), ("drive", 0.6), ("hpf", 0.0)],
        &[("filter", 2.0), ("resonance", 0.9), ("drive", 0.45), ("chorus", 1.0)],
        &[("quality", 1.0), ("osc1_unison", 5.0), ("stereo", 1.0), ("chorus", 2.0), ("filter", 1.0)],
    ];
    for (i, c) in cfgs.iter().enumerate() {
        for mode in [0, 1, 2, 3] {
            let mut s = Synth::new(FM1_RATE);
            for &(k, v) in c.iter() {
                s.set(k, v);
            }
            s.offload_mode(mode);
            s.note_on(48, 1.0);
            let (a, b) = s.render(20_000);
            s.note_on(60, 0.6);
            let (c2, d) = s.render(7_001);
            s.note_off(60);
            s.note_off(48);
            let (e, f) = s.render(9_000);
            let mut h: u64 = 0xcbf29ce484222325;
            for v in a.iter().chain(&b).chain(&c2).chain(&d).chain(&e).chain(&f) {
                h = (h ^ v.to_bits() as u64).wrapping_mul(0x100000001b3);
            }
            println!("cfg {i} mode {mode}: {h:016x}");
        }
    }
}
