//! A C6 saw, drive 16 at 4x with ADAA, into the wide-open ladder at 2x: with and without a
//! band limit between them, and the ladder alone on the saw.
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

struct Bq { b: [f64; 3], a: [f64; 2], z: [f64; 2] }
impl Bq {
    fn lp(fc: f64, fs: f64, q: f64) -> Bq {
        let w = 2.0 * std::f64::consts::PI * fc / fs;
        let al = w.sin() / (2.0 * q);
        let a0 = 1.0 + al;
        Bq { b: [(1.0 - w.cos()) / 2.0 / a0, (1.0 - w.cos()) / a0, (1.0 - w.cos()) / 2.0 / a0], a: [-2.0 * w.cos() / a0, (1.0 - al) / a0], z: [0.0; 2] }
    }
    fn run(&mut self, x: f64) -> f64 {
        let y = self.b[0] * x + self.z[0];
        self.z[0] = self.b[1] * x - self.a[0] * y + self.z[1];
        self.z[1] = self.b[2] * x - self.a[1] * y;
        y
    }
}

fn main() {
    let f0 = 1046.502;
    for (name, drive, lp) in [("ladder alone, saw 0.5", false, 0), ("drive -> ladder", true, 0), ("drive -> LP2 20k -> ladder", true, 2), ("drive -> LP4 20k -> ladder", true, 4)] {
        let mut o = Osc::new(true, 0.0);
        let (mut up, mut dn4, mut dn2) = (Halfband::new(true), Halfband::new(true), Halfband::new(false));
        let mut st = (0.0, 0.0);
        let mut l = Ladder::new();
        let g = ladder_g(20_000.0, 88_200.0);
        let mut bq = [Bq::lp(20_000.0, 88_200.0, 0.5412), Bq::lp(20_000.0, 88_200.0, 1.3065)];
        if lp == 2 { bq[0] = Bq::lp(20_000.0, 88_200.0, 0.7071); }
        let mut out = Vec::new();
        for _ in 0..(1 << 16) + 4000 {
            let mut two = [0.0f32; 2];
            for h in 0..2 {
                let x = o.step(0.0, 0.5, (f0 / 88_200.0) as f32, -1.0, 0).0;
                let mut d = if drive {
                    let u = up.up(16.0 * x);
                    (dn4.down(adaa(&mut st, u[0] as f64) as f32, adaa(&mut st, u[1] as f64) as f32) * 0.33) as f64
                } else {
                    0.5 * x as f64
                };
                if lp >= 2 { d = bq[0].run(d); }
                if lp >= 4 { d = bq[1].run(d); }
                two[h] = l.step(d as f32, g, 0.0, true).0;
            }
            out.push(dn2.down(two[0], two[1]));
        }
        let w = worst_alias_db(&out[4000..], 44_100.0, f0, 20.0, 16_000.0, 12);
        println!("{name}: {:.1} dB at {:.0} Hz", w.0, w.1);
    }
}
