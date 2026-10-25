//! A 24-bit stereo WAV writer (the FM-1's output depth), with TPDF dither at the last bit.

use std::io::Write;

pub fn write(path: &std::path::Path, rate: u32, l: &[f32], r: &[f32]) -> std::io::Result<()> {
    let frames = l.len().min(r.len());
    let data = (frames * 6) as u32;
    let mut b = Vec::with_capacity(44 + data as usize);
    b.extend_from_slice(b"RIFF");
    b.extend_from_slice(&(36 + data).to_le_bytes());
    b.extend_from_slice(b"WAVEfmt ");
    b.extend_from_slice(&16u32.to_le_bytes());
    b.extend_from_slice(&1u16.to_le_bytes());
    b.extend_from_slice(&2u16.to_le_bytes());
    b.extend_from_slice(&rate.to_le_bytes());
    b.extend_from_slice(&(rate * 6).to_le_bytes());
    b.extend_from_slice(&6u16.to_le_bytes());
    b.extend_from_slice(&24u16.to_le_bytes());
    b.extend_from_slice(b"data");
    b.extend_from_slice(&data.to_le_bytes());
    let mut rng = 0x9e37_79b9u32;
    let mut rnd = || {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        (rng >> 8) as f32 / 16_777_216.0
    };
    for i in 0..frames {
        for v in [l[i], r[i]] {
            let d = rnd() - rnd();
            let s = ((v as f64 * 8_388_607.0) + d as f64).round().clamp(-8_388_608.0, 8_388_607.0) as i32;
            b.extend_from_slice(&s.to_le_bytes()[..3]);
        }
    }
    std::fs::File::create(path)?.write_all(&b)
}
