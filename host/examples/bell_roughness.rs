//! Sethares' roughness (Plomp-Levelt's curve as Sethares fits it) of the piece's bell writing for a
//! few tunings of RING BELL's second oscillator: each pair of overlapping bell notes in the
//! arpeggios, and each bell note against the bar's bass (saw harmonics). Lower is smoother.
fn rough(a: &[(f64, f64)], b: &[(f64, f64)]) -> f64 {
    let mut d = 0.0;
    for &(f1, a1) in a {
        for &(f2, a2) in b {
            let (lo, df) = (f1.min(f2), (f1 - f2).abs());
            let s = 0.24 / (0.0207 * lo + 18.96);
            d += a1.min(a2) * ((-3.51 * s * df).exp() - (-5.75 * s * df).exp());
        }
    }
    d
}

fn hz(n: f64) -> f64 {
    440.0 * 2f64.powf((n - 69.0) / 12.0)
}

/// a bell note's partials: the ring products f (r - 1), f (r + 1) at 0.4, osc 1 at f at 0.2
fn bell(note: f64, semis: f64) -> Vec<(f64, f64)> {
    let (f, r) = (hz(note), 2f64.powf(semis / 12.0));
    vec![(f * (r - 1.0), 0.4), (f, 0.2), (f * (r + 1.0), 0.4)]
}

fn main() {
    let arp = [[69, 74, 76, 77, 81, 77, 76, 74], [70, 74, 77, 81, 84, 81, 77, 74], [69, 72, 76, 77, 81, 77, 76, 72],
               [67, 72, 74, 76, 79, 76, 74, 72]];
    let roots = [38.0, 34.0, 41.0, 36.0];
    let bass = |n: f64| (1..=10).map(|k| (hz(n) * k as f64, 0.6 / k as f64)).collect::<Vec<_>>();
    for shift in [0.0, -12.0] {
        for (name, semis) in [("+7 +30 cents (now)", 7.3), ("+7 exact (a fifth)", 7.0), ("+12 (an octave)", 12.0),
                              ("+12 +4 cents", 12.04), ("+19 (a twelfth)", 19.0), ("+24 (two octaves)", 24.0)] {
            let (mut pairs, mut vs_bass) = (0.0, 0.0);
            for (c, ch) in arp.iter().enumerate() {
                for i in 0..8 {
                    let a = bell(ch[i] as f64 + shift, semis);
                    let b = bell(ch[(i + 1) % 8] as f64 + shift, semis);
                    pairs += rough(&a, &b);
                    vs_bass += rough(&a, &bass(roots[c]));
                }
            }
            println!("bells {:>3} semitones, osc 2 {name:<20}: pairs {pairs:.2}, against the bass {vs_bass:.2}", shift);
        }
    }
}
