//! Host build of Monohull's DSP: safe wrappers over the C engine and its parts, spectrum
//! analysis, a float64 reference of the ladder, and WAV output. The tests in `tests/` are the
//! spec's objective checks (docs/spec.md §9.2).

use std::ffi::{c_char, c_int, CString};

pub mod analysis;
pub mod reference;
pub mod wav;

/// The firmware's output rate (ALNK0 on the FM-1).
pub const FM1_RATE: f32 = 44_117.647;

#[repr(C)]
pub struct RawSynth {
    _private: [u8; 0],
}
#[repr(C)]
pub struct RawOsc {
    _private: [u8; 0],
}
#[repr(C)]
pub struct RawHalfband {
    _private: [u8; 0],
}
#[repr(C)]
pub struct RawLadder {
    _private: [u8; 0],
}
#[repr(C)]
pub struct RawEnv {
    _private: [u8; 0],
}

extern "C" {
    fn mhh_synth_new(rate: f32, seed: u32) -> *mut RawSynth;
    fn mhh_synth_free(s: *mut RawSynth);
    fn mhh_set(s: *mut RawSynth, name: *const c_char, v: f32) -> c_int;
    fn mhh_get(s: *mut RawSynth, name: *const c_char) -> f32;
    fn mhh_cc(s: *mut RawSynth, cc: c_int, value: c_int) -> c_int;
    fn mhh_matrix(s: *mut RawSynth, slot: c_int, src: c_int, dst: c_int, amount: f32);
    fn mhh_note_on(s: *mut RawSynth, note: c_int, vel: f32);
    fn mhh_note_off(s: *mut RawSynth, note: c_int);
    fn mhh_all_off(s: *mut RawSynth);
    fn mhh_bend(s: *mut RawSynth, v: f32);
    fn mhh_wheel(s: *mut RawSynth, v: f32);
    fn mhh_sustain(s: *mut RawSynth, down: c_int);
    fn mhh_limits(s: *mut RawSynth, unison: c_int, stereo: c_int, chorus: c_int);
    fn mhh_fx_limit(s: *mut RawSynth, fx: c_int);
    fn mhh_x4(s: *mut RawSynth, on: c_int);
    fn mhh_render(s: *mut RawSynth, l: *mut f32, r: *mut f32, n: c_int);
    fn mhh_newton_steps(s: *const RawSynth) -> u32;
    fn mhh_filter_samples(s: *const RawSynth) -> u32;
    fn mhh_reset_counters(s: *mut RawSynth);
    fn mhh_param_count() -> c_int;
    fn mhh_offload_mode(s: *mut RawSynth, mode: c_int);
    fn mhh_clock(s: *mut RawSynth);
    fn mhh_transport(s: *mut RawSynth, run: c_int);
    fn mhh_diode_new() -> *mut RawLadder;
    fn mhh_diode_free(d: *mut RawLadder);
    fn mhh_diode_step(d: *mut RawLadder, x: f32, fc: f32, rate: f32, k: f32, hp_g: f32) -> f32;
    fn mhh_param_name(i: c_int) -> *const c_char;
    fn mhh_preset_count() -> c_int;
    fn mhh_preset_name(n: c_int) -> *const c_char;
    fn mhh_preset_load(s: *mut RawSynth, n: c_int);
    fn mhh_stats(s: *mut RawSynth, out: *mut f32);

    pub fn mhh_exp2(x: f32) -> f32;
    pub fn mhh_tanh(x: f32) -> f32;
    pub fn mhh_tanh_x(x: f32) -> f32;
    pub fn mhh_tan_pi(f: f32) -> f32;
    pub fn mhh_log2(x: f32) -> f32;
    pub fn mhh_log1p01(u: f32) -> f32;
    pub fn mhh_sin2pi(p: f32) -> f32;

    fn mhh_osc_new(table: c_int, phase: f32) -> *mut RawOsc;
    fn mhh_osc_free(o: *mut RawOsc);
    fn mhh_osc_curve(o: *mut RawOsc, c: f32);
    fn mhh_osc_soft(o: *mut RawOsc, soft: f32);
    fn mhh_osc_step(o: *mut RawOsc, shape: f32, pw: f32, inc: f32, sync_d: f32, wrap_d: *mut f32, sub_div: c_int,
                    sub: *mut f32) -> f32;
    fn mhh_hb_new(four: c_int) -> *mut RawHalfband;
    fn mhh_hb_free(h: *mut RawHalfband);
    fn mhh_hb_down(h: *mut RawHalfband, a: f32, b: f32) -> f32;
    fn mhh_hb_up(h: *mut RawHalfband, x: f32, out: *mut f32);
    fn mhh_ladder_new() -> *mut RawLadder;
    fn mhh_ladder_free(l: *mut RawLadder);
    fn mhh_ota_new() -> *mut RawLadder;
    fn mhh_k35_new() -> *mut RawLadder;
    fn mhh_k35_free(f: *mut RawLadder);
    fn mhh_k35_step(f: *mut RawLadder, x: f32, g: f32, k: f32, hp: c_int) -> f32;
    fn mhh_ota_step(l: *mut RawLadder, x: f32, g: f32, k: f32, newton: c_int, out5: *mut f32) -> f32;
    pub fn mhh_ota_tune(k: f32) -> f32;
    fn mhh_ladder_step(l: *mut RawLadder, x: f32, g: f32, k: f32, newton: c_int, out5: *mut f32) -> f32;
    fn mhh_ladder_iters(l: *const RawLadder) -> c_int;
    fn mhh_ladder_tune(k: f32) -> f32;
    fn mhh_env_new() -> *mut RawEnv;
    fn mhh_env_free(e: *mut RawEnv);
    fn mhh_env_set(e: *mut RawEnv, a: f32, d: f32, s: f32, r: f32, lp: c_int, rate: f32);
    fn mhh_env_gate(e: *mut RawEnv, on: c_int);
    fn mhh_env_step(e: *mut RawEnv) -> f32;
    fn mhh_env_stage(e: *const RawEnv) -> c_int;
}

/// Mod matrix sources and destinations (mh_synth.h's enums).
pub mod modulation {
    pub const SRC_LFO1: i32 = 1;
    pub const SRC_LFO2: i32 = 2;
    pub const SRC_FENV: i32 = 3;
    pub const SRC_MENV: i32 = 5;
    pub const SRC_VELOCITY: i32 = 6;
    pub const SRC_WHEEL: i32 = 8;
    pub const SRC_RANDOM: i32 = 11;
    pub const DST_PITCH2: i32 = 3;
    pub const DST_PW1: i32 = 4;
    pub const DST_PW2: i32 = 5;
    pub const DST_CUTOFF: i32 = 13;
    pub const DST_RESONANCE: i32 = 14;
    pub const DST_FM: i32 = 16;
    pub const DST_DETUNE: i32 = 19;
}

/// The whole synth.
pub struct Synth {
    raw: *mut RawSynth,
    pub rate: f32,
}

impl Synth {
    pub fn new(rate: f32) -> Self {
        Synth { raw: unsafe { mhh_synth_new(rate, 0x5eed) }, rate }
    }

    /// Set a parameter by its mh_params.c name. Panics on an unknown name (a test typo).
    pub fn set(&mut self, name: &str, v: f32) -> &mut Self {
        let c = CString::new(name).unwrap();
        assert!(unsafe { mhh_set(self.raw, c.as_ptr(), v) } != 0, "no parameter {name}");
        self
    }

    pub fn get(&mut self, name: &str) -> f32 {
        let c = CString::new(name).unwrap();
        unsafe { mhh_get(self.raw, c.as_ptr()) }
    }

    pub fn cc(&mut self, cc: i32, value: i32) -> bool {
        unsafe { mhh_cc(self.raw, cc, value) != 0 }
    }

    pub fn matrix(&mut self, slot: i32, src: i32, dst: i32, amount: f32) {
        unsafe { mhh_matrix(self.raw, slot, src, dst, amount) }
    }

    pub fn note_on(&mut self, note: i32, vel: f32) {
        unsafe { mhh_note_on(self.raw, note, vel) }
    }

    pub fn note_off(&mut self, note: i32) {
        unsafe { mhh_note_off(self.raw, note) }
    }

    pub fn all_off(&mut self) {
        unsafe { mhh_all_off(self.raw) }
    }

    pub fn bend(&mut self, v: f32) {
        unsafe { mhh_bend(self.raw, v) }
    }

    pub fn wheel(&mut self, v: f32) {
        unsafe { mhh_wheel(self.raw, v) }
    }

    pub fn sustain(&mut self, down: bool) {
        unsafe { mhh_sustain(self.raw, down as c_int) }
    }

    pub fn limits(&mut self, unison: i32, stereo: bool, chorus: bool) {
        unsafe { mhh_limits(self.raw, unison, stereo as c_int, chorus as c_int) }
    }

    /// Render `n` stereo frames.
    pub fn render(&mut self, n: usize) -> (Vec<f32>, Vec<f32>) {
        let (mut l, mut r) = (vec![0.0f32; n], vec![0.0f32; n]);
        unsafe { mhh_render(self.raw, l.as_mut_ptr(), r.as_mut_ptr(), n as c_int) };
        (l, r)
    }

    /// Render into existing buffers (appending).
    pub fn render_into(&mut self, n: usize, l: &mut Vec<f32>, r: &mut Vec<f32>) {
        let (a, b) = self.render(n);
        l.extend_from_slice(&a);
        r.extend_from_slice(&b);
    }

    /// (Newton steps, filter samples) since the last reset.
    pub fn newton(&self) -> (u32, u32) {
        unsafe { (mhh_newton_steps(self.raw), mhh_filter_samples(self.raw)) }
    }

    pub fn reset_counters(&mut self) {
        unsafe { mhh_reset_counters(self.raw) }
    }

    /// One MIDI clock tick (24 a beat).
    pub fn clock(&mut self) {
        unsafe { mhh_clock(self.raw) }
    }

    pub fn transport(&mut self, run: bool) {
        unsafe { mhh_transport(self.raw, run as c_int) }
    }

    /// Run the jobs through a pretend second core: 0 none, 1 at once, 2 at the join, 3 failing.
    pub fn offload_mode(&mut self, mode: i32) {
        unsafe { mhh_offload_mode(self.raw, mode) }
    }

    /// The scheduler's switch for the filters at 4x (High).
    pub fn filter_x4(&mut self, on: bool) {
        unsafe { mhh_x4(self.raw, on as c_int) }
    }

    /// The scheduler's switch for the delay and reverb.
    pub fn fx_limit(&mut self, on: bool) {
        unsafe { mhh_fx_limit(self.raw, on as c_int) }
    }

    /// The firmware's factory patches, by name.
    pub fn preset_names() -> Vec<String> {
        (0..unsafe { mhh_preset_count() })
            .map(|i| unsafe { std::ffi::CStr::from_ptr(mhh_preset_name(i)) }.to_string_lossy().into_owned())
            .collect()
    }

    /// Load factory patch `n` as the firmware does (this synth's quality kept).
    pub fn load_preset(&mut self, n: usize) -> &mut Self {
        unsafe { mhh_preset_load(self.raw, n as c_int) };
        self
    }

    /// Since the last call: the drive's and the filter's deviation from linear (rms ratios), the
    /// drive's peak input (its knee at 1) and the filter's input rms.
    pub fn stats(&mut self) -> [f32; 4] {
        let mut o = [0.0f32; 4];
        unsafe { mhh_stats(self.raw, o.as_mut_ptr()) };
        o
    }

    pub fn param_names() -> Vec<String> {
        (0..unsafe { mhh_param_count() })
            .map(|i| unsafe { std::ffi::CStr::from_ptr(mhh_param_name(i)) }.to_string_lossy().into_owned())
            .collect()
    }
}

impl Drop for Synth {
    fn drop(&mut self) {
        unsafe { mhh_synth_free(self.raw) }
    }
}

/// One oscillator.
pub struct Osc(*mut RawOsc);

impl Osc {
    /// `table`: the windowed-sinc kernel (High), else the polynomial (Efficient).
    pub fn new(table: bool, phase: f32) -> Self {
        Osc(unsafe { mhh_osc_new(table as c_int, phase) })
    }

    /// One sample; returns (out, wrap time or -1, sub).
    pub fn step(&mut self, shape: f32, pw: f32, inc: f32, sync_d: f32, sub_div: i32) -> (f32, f32, f32) {
        let (mut w, mut sub) = (0.0f32, 0.0f32);
        let y = unsafe { mhh_osc_step(self.0, shape, pw, inc, sync_d, &mut w, sub_div, &mut sub) };
        (y, w, sub)
    }

    /// The pulse's falling edge the 303's soft one, by `soft` in [0, 1] (the width is the
    /// caller's: the 303's is 0.4687857).
    pub fn set_soft(&mut self, soft: f32) {
        unsafe { mhh_osc_soft(self.0, soft) }
    }

    /// The saw's analogue bend, `c` in p(1 + c(1 - p)) (the synth's Vintage gives 0 to 0.15).
    pub fn set_curve(&mut self, c: f32) {
        unsafe { mhh_osc_curve(self.0, c) }
    }
}

impl Drop for Osc {
    fn drop(&mut self) {
        unsafe { mhh_osc_free(self.0) }
    }
}

/// A 2x halfband, either the 2x <-> 1x one or the 4x -> 2x one.
pub struct Halfband(*mut RawHalfband);

impl Halfband {
    pub fn new(four: bool) -> Self {
        Halfband(unsafe { mhh_hb_new(four as c_int) })
    }

    pub fn down(&mut self, a: f32, b: f32) -> f32 {
        unsafe { mhh_hb_down(self.0, a, b) }
    }

    pub fn up(&mut self, x: f32) -> [f32; 2] {
        let mut o = [0.0f32; 2];
        unsafe { mhh_hb_up(self.0, x, o.as_mut_ptr()) };
        o
    }
}

impl Drop for Halfband {
    fn drop(&mut self) {
        unsafe { mhh_hb_free(self.0) }
    }
}

/// The ladder on its own.
pub struct Ladder(*mut RawLadder);

impl Ladder {
    pub fn new() -> Self {
        Ladder(unsafe { mhh_ladder_new() })
    }

    /// One sample; returns y4 and the outputs [u, y1, y2, y3, y4].
    pub fn step(&mut self, x: f32, g: f32, k: f32, newton: bool) -> (f32, [f32; 5]) {
        let mut o = [0.0f32; 5];
        let y = unsafe { mhh_ladder_step(self.0, x, g, k, newton as c_int, o.as_mut_ptr()) };
        (y, o)
    }

    pub fn iters(&self) -> i32 {
        unsafe { mhh_ladder_iters(self.0) }
    }
}

impl Default for Ladder {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for Ladder {
    fn drop(&mut self) {
        unsafe { mhh_ladder_free(self.0) }
    }
}

/// The OTA cascade on its own (it shares the ladder's state and its iteration count).
pub struct Ota(*mut RawLadder);

impl Ota {
    pub fn new() -> Self {
        Ota(unsafe { mhh_ota_new() })
    }

    /// One sample; returns y4 and the outputs [u, y1, y2, y3, y4].
    pub fn step(&mut self, x: f32, g: f32, k: f32, newton: bool) -> (f32, [f32; 5]) {
        let mut o = [0.0f32; 5];
        let y = unsafe { mhh_ota_step(self.0, x, g, k, newton as c_int, o.as_mut_ptr()) };
        (y, o)
    }

    pub fn iters(&self) -> i32 {
        unsafe { mhh_ladder_iters(self.0) }
    }
}

impl Default for Ota {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for Ota {
    fn drop(&mut self) {
        unsafe { mhh_ladder_free(self.0) }
    }
}

/// The Sallen-Key (KORG-35) on its own, low-pass or high-pass.
pub struct K35(*mut RawLadder, bool);

impl K35 {
    pub fn new(hp: bool) -> Self {
        K35(unsafe { mhh_k35_new() }, hp)
    }

    pub fn step(&mut self, x: f32, g: f32, k: f32) -> f32 {
        unsafe { mhh_k35_step(self.0, x, g, k, self.1 as c_int) }
    }
}

impl Drop for K35 {
    fn drop(&mut self) {
        unsafe { mhh_k35_free(self.0) }
    }
}

/// One envelope.
pub struct Env(*mut RawEnv);

impl Env {
    pub fn new(a: f32, d: f32, s: f32, r: f32, rate: f32) -> Self {
        let e = Env(unsafe { mhh_env_new() });
        unsafe { mhh_env_set(e.0, a, d, s, r, 0, rate) };
        e
    }

    pub fn gate(&mut self, on: bool) {
        unsafe { mhh_env_gate(self.0, on as c_int) }
    }

    pub fn step(&mut self) -> f32 {
        unsafe { mhh_env_step(self.0) }
    }

    pub fn stage(&self) -> i32 {
        unsafe { mhh_env_stage(self.0) }
    }
}

impl Drop for Env {
    fn drop(&mut self) {
        unsafe { mhh_env_free(self.0) }
    }
}

/// The ladder's self-oscillation tuning correction (a cutoff multiplier) for feedback `k`.
pub fn ladder_tune(k: f32) -> f32 {
    unsafe { mhh_ladder_tune(k) }
}

/// The ladder's prewarped coefficient for a cutoff at a rate.
pub fn ladder_g(fc: f64, rate: f64) -> f32 {
    (std::f64::consts::PI * fc / rate).tan() as f32
}

/// The diode ladder on its own.
pub struct Diode(*mut RawLadder);

impl Diode {
    pub fn new() -> Self {
        Diode(unsafe { mhh_diode_new() })
    }

    /// One sample at `rate`, the feedback high-pass at 150 Hz.
    pub fn step(&mut self, x: f32, fc: f32, rate: f32, k: f32) -> f32 {
        let hp_g = (std::f64::consts::PI * 150.0 / rate as f64).tan() as f32;
        unsafe { mhh_diode_step(self.0, x, fc, rate, k, hp_g) }
    }
}

impl Default for Diode {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for Diode {
    fn drop(&mut self) {
        unsafe { mhh_diode_free(self.0) }
    }
}
