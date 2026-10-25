//! Drive aliasing options on a C6 saw at gain 16: plain tanh or ADAA1, at 2x or 4x.
use monohull_host::analysis::*;
use monohull_host::*;

fn adaa(prev: &mut (f64, f64), u: f64) -> f64 {
    let f = |x: f64| x.abs() + (-2.0 * x.abs()).exp().ln_1p();
    let fu = f(u);
    let d = u - prev.0;
    let y = if d.abs() > 1e-3 { (fu - prev.1) / d } else { (0.5 * (u + prev.0)).tanh() };
    *prev = (u, fu);
    y
}

fn main() {
    let f0 = 1046.502;
    let gd = 16.0;
    for (name, four, use_adaa) in [("2x tanh", false, false), ("2x ADAA", false, true), ("4x tanh", true, false), ("4x ADAA", true, true)] {
        let mut o = Osc::new(true, 0.0);
        let (mut up, mut dn4, mut dn2) = (Halfband::new(true), Halfband::new(true), Halfband::new(false));
        let mut st = (0.0, 0.0);
        let mut drive = |x: f32| -> f32 {
            let u = gd * x as f64;
            (if use_adaa { adaa(&mut st, u) } else { u.tanh() }) as f32
        };
        let mut out = Vec::new();
        for _ in 0..(1 << 16) + 4000 {
            let mut two = [0.0f32; 2];
            for h in 0..2 {
                let x = o.step(0.0, 0.5, (f0 / 88_200.0) as f32, -1.0, 0).0;
                two[h] = if four {
                    let u = up.up(x);
                    dn4.down(drive(u[0]), drive(u[1]))
                } else {
                    drive(x)
                };
            }
            out.push(dn2.down(two[0], two[1]));
        }
        let w = worst_alias_db(&out[4000..], 44_100.0, f0, 20.0, 16_000.0, 12);
        println!("{name}: {:.1} dB at {:.0} Hz", w.0, w.1);
    }
}
