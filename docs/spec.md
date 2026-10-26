# Monohull: a monosynth firmware for the M-VAVE FM-1 (spec, draft 1)

Working name. Community firmwares are named after boats, and a monohull has a single hull.

One voice with no per-voice compromises: an analogue-style monosynth whose oscillators, filter and
envelopes are as good as the best open-source desktop and Rack code, on the FM-1's 240 MHz
pi32v2. Any cycles left over after that go to **unison** first, then a **high-quality chorus**,
then everything else.

Status: the DSP is built (`dsp/`, C99) and passes the host tests of §9.2 (`host/`, see
section 14 for the measurements). Not yet built for the FM-1: every cost below is an estimate
until it is measured on fm1-emu (instructions) and on a real FM-1 (cycles); section 9 says how.

**Two engine modes.** **High** (the default) uses the accurate option wherever a cheaper one would
be audible at the extremes: oscillators generated at the oversampled rate, the filter's full
nonlinear equations solved every sample, antialiased drive. **Efficient** swaps each for its cheap
counterpart (listed per component as *Efficient:*) for players who want the battery life, the
headroom for extras, or a lighter load in emulation. Extras (unison, chorus, effects) are separate:
the scheduler (section 6) adds them on top of either mode as cycles allow.

**Built on respected open source.** Wherever a well-regarded open project already models a part,
Monohull ports or calibrates against it rather than inventing (section 10).

---

## 1. Goals and non-goals

**Goals**

1. Sound: a voice that holds up against desktop VA synths (Diva-, Repro-, Surge-class) played
   alone: no audible aliasing on a saw at C7, a filter that self-oscillates in tune, that
   stays smooth under fast modulation and that saturates musically, and envelopes that snap.
2. Feel: per-sample modulation (no zipper noise or stepping), and note-on to sound in under
   one audio half (2.9 ms) plus the codec's latency.
3. Headroom spent where it is heard: unison, then chorus, scaled to the cycles really free.
4. Robust: runs on one core (some FM-1s have an unreliable core 1), and degrades smoothly
   rather than crackling when the budget runs short.
5. Emulation-friendly: cheap to run in fm1-emu and Harbor, which pay for every guest
   instruction (section 7).

**Non-goals (for v1)**

- Polyphony or paraphony. One voice, mono legato.
- A sequencer or song mode beyond a simple arpeggiator and hold (another firmware's job; can
  come later from Felucca's code).
- Sample playback, wavetables, FM engines.
- Modelling one specific vintage synth exactly. Borrow from the best models; don't clone.

---

## 2. What the hardware gives

From `third_party/fm1-emu/docs/recon.md`, `fm1-sid/README.md`, `docs/isa-notes.md` and
Felucca's HAL:

| | |
|---|---|
| CPU | JieLi AC791N, **pi32v2**, 240 MHz, **two cores**. Blackfin-style ISA, dual-issue pairs, hardware loops (`rep`), saturating add / shift, 32×32→64 multiply-accumulate, `cls` / `clz`, min / max |
| FPU | **single precision**: `+ − × ÷`, fmin, fmax, multiply-accumulate and multiply-subtract (`rD ±= rA*rB`), int↔float. Built with `-mcpu=r3`; `-mfprev1` lets the compiler pair FPU ops for dual issue. No `double` hardware: `double` goes through compiler-rt soft float |
| Memory | ~512 KB SRAM (~432 KB for the app; fm1-sid used 425 KB of 432), 1 MB flash, XIP at `0x02000000`, ~568 KB of it for the app |
| Audio | ALNK0 I2S, **44 117.6 Hz**, 24-bit words (L/R int32 with the sample in the low 24 bits), double-buffered DMA, 128-frame halves = 2.9 ms |
| Panel | 27 keys, 14 buttons with LEDs, 7 encoders, MASTER pot, 240×240 LCD |
| I/O | USB-C (MIDI, update), TRS MIDI in, BLE (MIDI-capable, unused by most firmwares) |

**Budget per sample:** 240 000 000 / 44 117.6 ≈ **5 440 cycles per core** (≈ 696 000 per
128-frame half). For scale: one SID chip with reSID 0.16 costs ~440 instructions per sample
(8 % of a core), and fm1-sid's whole audio interrupt including chorus, delay and reverb runs at
~14 % of a core.

**Unknowns that matter** (measure on hardware before tuning, section 9.3): FPU latency and
throughput (is a float MAC one cycle, pipelined?), the cost of `÷`, XIP flash cache size and
miss penalty, the codec's real noise floor, and whether the keybed reports velocity.

### 2.1 Corrections to the starting brief

The brief this spec started from (an AI chat) got several things wrong; they change the design.

| The brief said | In fact | Consequence |
|---|---|---|
| pi32v2 is integer / fixed-point; float falls back to soft-float | It has a single-precision FPU with MAC, used by Felucca, X0X and others | Write the DSP in `float`. Q15 / Q31 buys little here and costs headroom bugs. Avoid `double` (soft float) |
| Map guest r0–r15 to host X0–X15 | That is the emulator's JIT, not the firmware (Harbor's patch 0006 already does it) | Not part of a firmware spec |
| x³ in 2 cycles, an envelope in 1 cycle, a 2× ladder in 120–180 instructions | No cycle timing has been measured on the FM-1; the emulator counts instructions | Plan for ~250–500 instructions for a good 2× ladder; measure |
| Replace division with Newton–Raphson | The FPU divides; its cost is unknown | One `÷` per sample is likely fine. Measure before replacing it |
| ZDF SVF: `v1 = (in − k·s2 − s1) / (1 + g(g+k))` | Garbled. Use Simper's / Gillet's published forms (§4.4) | Copy reference code, don't re-derive from a chat |
| `x − x³/3` as a saturator | Only valid for \|x\| ≤ 1 (it turns back beyond), and it aliases without oversampling or ADAA | Clamp, or use a rational tanh inside the oversampled region |
| FM-1+VA uses biquads and hard clipping | Unknown: it is closed source | Don't build on guesses about competitors |

### 2.2 Prior art on the FM-1

| Firmware | What it shows |
|---|---|
| Felucca (GPL-3.0) | The HAL, startup, USB / MIDI, flash storage and update package everyone builds on; an `engine_t` plug-in interface; chorus / delay / reverb in `fx.c` |
| FM-106 (GPL-3.0) | Ultramaster KR-106 (Juno-106) ported: six voices **on both cores** and the BBD chorus already run on this chip. A ready benchmark for the chorus and ladder costs |
| OberVave (GPL-3.0) | Eight-voice VA poly with a morphing filter, chorus, delay, plate reverb |
| CS-1, Jangada | CS-80-style voices; superwave voices with a mod matrix |
| fm1-sid (BSD-2) | The measuring method this spec relies on: count instructions between markers on fm1-emu, compare output bit for bit with a host build |
| Baud Girl FM-1+VA | Closed; VA engine with a superwave mode |

Monohull's difference is one voice that spends what the polys spread across six or eight.

---

## 3. Signal path

```
                  ┌──── drift / vintage (per osc, per unison voice) ────┐
 note, glide ──►  OSC 1 ─┐  (saw / pulse+PWM / tri / sine; sync from 2; FM from 2)
 pitch mod   ──►  OSC 2 ─┤  unison: N copies, detuned, spread L/R
                  SUB ───┤  (−1 / −2 oct square)
                  NOISE ─┤  (white / pink)
                  RING ──┘  (osc1 × osc2)
   (High: all of the above generated at 88.2 kHz, 176.4 kHz for
    FM / ring at high notes; Efficient: at 44.1 kHz, then ↑2)
                          │
                          ▼  mixer (per-source level)
            ┌──────────── 2× oversampled region ────────────┐
            │  [Efficient only: ↑2 (polyphase IIR halfband)] │
            │  drive (rational tanh; High: with ADAA)        │
            │  FILTER: 4-pole transistor ladder (main; High: │
            │          Newton-solved, Efficient: cheap ZDF)  │
            │          or 2-pole OTA SVF (LP/BP/HP/notch)    │
            │  [stereo unison: a second filter for R]        │
            │  ↓2 (polyphase IIR halfband)                   │
            └───────────────────────────────────────────────┘
                          │
                          ▼
                  VCA (amp env × velocity) ─► post-filter HPF (bass trim, 1-pole TPT)
                          │
                          ▼
                  CHORUS (Juno BBD / Ensemble / off) ─► DELAY (optional) ─► REVERB (optional)
                          │
                          ▼
                  DC block ─► soft limiter ─► MASTER ─► 24-bit I2S
```

Control: 2 envelopes (filter, amp; a third "mod" envelope if cheap), 2 LFOs, a small mod
matrix, glide, MIDI (bend, mod wheel, aftertouch, CCs).

---

## 4. Components

Each part gives the chosen algorithm, the reference to copy or check against, and an estimated
cost in pi32v2 **instructions per output sample** (44.1 kHz).

### 4.1 Numerics and rates

- **Format:** `float` throughout the audio path; `int32` phase accumulators for oscillators
  (wrap for free, exact tuning). No `double`, no libm calls per sample (`tanhf`, `expf`, `powf`,
  `tanf` are soft routines here): use the approximations below, or tables in flash.
- **Approximations to use** (all already proven in open code):
  - `tan(πf)` for cutoff prewarping: Gillet's polynomial `FREQUENCY_FAST` (stmlib `filter.h`)
    below ~16 kHz at the 2× rate, or VCV's `tan_3_4` rational.
  - `tanh(x)/x`: VCV Fundamental's `tanhXdX_4_6` rational on [−4, 4] (clamp outside), or the
    Padé `x(27 + x²)/(27 + 9x²)` KR-106 uses.
  - `2^x` for pitch and cutoff: a 5th-order Taylor / minimax `exp2` on the fraction plus an
    exponent-field write (VCV's `exp2_taylor5`).
- **Denormals:** behaviour of the pi32v2 FPU unknown. Add a −120 dB noise / DC offset into
  feedback paths (VCV's ladder does: it also bootstraps self-oscillation) and flush filter
  states below 1e-15 at block end.
- **Rates:** oscillators, filter, envelopes, LFOs and VCA run per sample. The mod matrix,
  MIDI and panel values update every 16 samples (2.75 kHz) and reach the audio path as linear
  per-sample ramps, so a knob or CC never steps. Pitch and cutoff are summed in octaves and
  exponentiated per sample (one `exp2` each).

### 4.2 Oscillators

Two main oscillators plus a sub and noise; one shared engine.

- **Waveforms:** saw, pulse with PWM (5–95 %), triangle, sine, and a continuous saw → square
  → triangle morph.
- **Antialiasing:** **BLEP for steps, BLAMP for slope corners**, after Mutable Instruments
  Plaits' `VariableShapeOscillator` / `VariableSawOscillator` and stmlib's `polyblep.h` (MIT):
  residuals placed at the fractional position of each discontinuity, several per sample handled
  in order.
  - **High (default):** the oscillators run at the filter's 2× rate (88.2 kHz), so they feed
    the oversampled region directly and the upsampler goes away. Aliases must then fold from
    above 66 kHz to land in the audible band, and the downsampler's halfband removes 22–66 kHz.
    Residuals are a short minBLEP / minBLAMP table (Brandt; VCV Fundamental's VCO uses a
    16-zero-crossing minBLEP, GPL-3.0) rather than the 2-point polynomial. Cost ~2 × 35 per
    oscillator per output sample.
  - **Efficient:** 2-point polyBLEP / polyBLAMP at 44.1 kHz, as Plaits ships; ~20–30 per
    oscillator, then the halfband upsampler.
- **Hard sync** (osc 2 synced to osc 1) with a BLEP at the fractional reset time, as Plaits
  does: the slave's phase is projected to the reset instant, the jump corrected. BLEP handles
  sync at any rate; High's 2× just lowers the residue further.
- **FM and ring:** linear through-zero FM from osc 2 to osc 1 (phase-increment modulation), and
  ring modulation (osc 1 × osc 2). Neither can be fixed with BLEPs: FM spreads sidebands without
  limit and a product of two band-limited signals reaches the sum of their bandwidths.
  - **High:** when FM depth or ring level is non-zero *and* the played note is above C4, the
    oscillator block steps up to 4× (176.4 kHz) with one more halfband stage down to 2×; below
    that, or with both off, it stays at 2×. The switch is per note, crossfaded over 5 ms. This
    resolves ring mod (two saws band-limited to 44 kHz multiply to < 88 kHz, below the 4× rate's
    Nyquist) and pushes FM's aliases at moderate indexes below −70 dB.
  - **Efficient:** FM and ring at 44.1 kHz; they alias audibly at high notes and high indexes,
    as on most digital synths.
- **Sub:** square one or two octaves down from osc 1's phase (bit 31 / 30 of a divided
  accumulator) with polyBLEP, plus KR-106's passive RC low-pass (~4 kHz) for the Juno sub tone.
- **Noise:** 32-bit xorshift white; pink via Paul Kellet's economy filter (3 poles).
- **Analogue character** (one "Vintage" amount, 0–100 %; 0 is exact). At full:
  - **drift:** per oscillator (and per unison voice) a slow wander: a new random target every
    2 s on average, through two cascaded one-poles at ~0.5 Hz, ±4 cents.
  - **jitter:** per voice, white noise through a ~15 Hz one-pole, 0.6 cents rms: what keeps a
    held note from sounding static (per-sample white jitter of that size is inaudible).
  - **note-on spread:** a fixed random offset per voice per note (±3 cents).
  - **tracking:** osc 2 runs 2 cents an octave flat above middle C (sharp below), as an expo
    converter's bulk resistance makes it: intervals beat faster up the keyboard. Osc 1 is the
    reference, so the voice stays in tune with other instruments.
  - **filter:** each filter drifts (±0.04 octave), starts each note off (±0.025 octave), and
    the stereo pair are mismatched by a fixed 0.015 octave.
  - **saw shape:** KR-106's capacitor-charge curvature `p(1 + c(1 − p))`, c up to 0.15, on the
    main pair only (lost in a detuned stack, and dear on every copy); its larger slope corner at
    the wrap is BLAMP-corrected. A parabola in quadrature with the saw's harmonics, it changes the
    wave's shape and DC (what the drive and filter see), not the magnitudes. KR-106's discharge
    undershoot is not modelled.
  - Factory patches: ACID and WHISTLE use 10 % (the line stays tight; WHISTLE's pitch is the
    filter's).
  - **phase:** free-running oscillators by default (analogue); "reset on note-on" switch.
- **Model** (per oscillator, CLEAN or ACID): ACID makes the pulse the TB-303's square as Open303
  models it (Robin Schmidt, MIT: the saw through a transistor shaper, `-tanh(70 saw + 4.37)`):
  a hard rising edge and a soft falling one at 46.9 %, about 3 % of the cycle wide. The soft edge
  is the shaper itself while it spans 3 samples or more (aliases below -99 dB), handed over to a
  BLEP'd step by 1.5 (~210 to ~420 Hz at High's rate, where its softening is all but above
  hearing). It softens a C2's upper harmonics: -1.3 dB at the 20th, -2.3 at the 40th, -5.6 at the
  80th; it matches Open303's shaper to 1e-4 and aliases below -103 dB at every pitch.
- **Tuning:** 32-bit phase increments from a per-sample `exp2`; master tune ±100 cents.

### 4.3 Mixer and drive

- Levels for osc 1, osc 2, sub, noise, ring. Sum into the 2× region.
- **Drive** before the filter: gain 1–32 (VCV maps drive as `(1 + d)^5`), then the rational
  tanh, at the 2× rate.
  - **High:** first-order ADAA on the drive (the antiderivative of tanh is log cosh;
    Parker–Zavalishin–Le Bivic 2016, Bilbao et al. 2017; Chowdhury's ADAA notes and
    chowdsp_utils give tested forms, including the fallback when consecutive inputs are
    nearly equal), **at 4×** (one more pair of short halfbands around it). Measured on a C6 saw
    at gain 16, worst alias: 2× tanh −43 dB, 2× ADAA −56 dB, 4× tanh −51 dB, 4× ADAA −72 dB;
    neither alone reaches −70. In the synth, through the ladder: −75 dB.
  - **Efficient:** the plain tanh at 2× (−46 dB in the same test).
- Gain compensation so turning drive up doesn't simply get louder.

### 4.4 Filter (the centre of the instrument)

**Main model: a 4-pole transistor ladder (24 dB/oct), 2× oversampled.**

- **The model:** the transistor ladder's differential equations as Stinchcombe analyses them
  and Miller Puckette's `bob~` implements them (BSD; ported to sst-filters as Surge's "Vintage
  Ladder RK" and to MoogLadders as `RKSimulationModel`): a tanh in each stage's differential
  pair and in the feedback path. Monohull does not invent its own ladder; it discretises this
  one.
- **Structure:** four trapezoidal (TPT) one-pole stages (Zavalishin, *The Art of VA Filter
  Design*, the nonlinear zero-delay-feedback chapter), global feedback k from stage 4, so the
  cutoff is prewarped exactly and the filter stays in tune up to the top of its range, which an
  explicit integrator like `bob~`'s RK4 does not quite manage.
- **High (default): the full nonlinear system solved every 2× sample by Newton–Raphson.**
  - The unknowns are the four stage outputs; the Jacobian of a ladder is lower-bidiagonal plus
    the feedback corner, so each iteration is solved in O(4) (~40 operations) rather than a
    general 4×4 elimination. Each iteration evaluates five tanh (rational) and their slopes
    (`1 − tanh²`, free).
  - Warm start from the previous sample's solution (or the cheap solve below as predictor):
    typically 1–2 iterations converge to 1e-6; a cap of 3, after which the last iterate is
    taken (bounded worst-case cost; a damped step if the residual grows).
  - Pattern to copy: Ultramaster KR-106's IR3109 model takes one Newton step per stage on the
    Padé tanh, 2× with HIIR (GPL-3.0), and runs in real time on the FM-1 inside FM-106.
  - **Golden reference:** `bob~`'s equations integrated on the host at 16× with RK4 and with
    D'Angelo–Välimäki's "Improved" model (ISC, in MoogLadders) as a second opinion; Monohull's
    High ladder must match them within the limits in §9.2 across drive and resonance. If
    Newton misbehaves on the hardware, the fallback is to port `bob~`'s RK4 itself at 4×.
  - Cost: ~220 per 2× sample (two iterations) → ~500 per output sample including halfbands
    and coefficients; worst case (three iterations) ~700.
- **Efficient: Teemu Voipio's ("mystran") cheap nonlinear ZDF**: each stage's tanh is replaced
  by its slope `tanh(x)/x` taken from the previous sample, which makes the sample linear in the
  unknowns, and the linear system (including the feedback) is solved exactly with no iteration.
  Linear behaviour stays fully zero-delay (in tune, no resonance shift at high cutoff); the
  nonlinearity lags by one 2× sample, which shows only with heavy drive and high resonance
  together.
  - VCV Fundamental v2's `VCF.cpp` (GPL-3.0) is the nearest reference: per-stage
    linearisation from mystran, `tanhXdX_4_6`, a slope floor of 0.01 so the filter self-damps,
    2× via polyphase IIR halfbands, cutoff prewarped at the 2× rate. Note it feeds resonance
    back from the *previous* sample (`yPrevious[3]`); Monohull solves the feedback in the same
    sample instead (the 4×4 system collapses to one division per 2× sample).
  - Cost ~250 per output sample.
  - The cheap solve doubles as High's Newton predictor, so both modes share one code path.
- **Resonance:** 0 to self-oscillation (k ≈ 4 at the 2× rate; map the knob with a square law
  as VCV does). **Bass compensation:** add `(k/4)·c` of the input back, c 0–1 (a Moog loses
  bass as resonance rises; a switch lets players choose).
- **Self-oscillation in tune:** cutoff tracks the keyboard at 0–100 % (100 % = 1 V/oct),
  tuned so a self-oscillating filter plays in tune within ±5 cents over five octaves. That
  needs the exact prewarp and the zero-delay feedback.
- **Outputs and modes:** LP24, LP12 (stage 2), BP and HP from the binomial mix of stage
  outputs (`x − 4y1 + 6y2 − 4y3 + y4` style), and a continuous LP24 → LP12 → BP morph.
- **Cutoff range:** 16 Hz to 20 kHz (well inside 0.497 × 88.2 kHz).
- **Oversampling:** HIIR-style polyphase IIR halfbands (Laurent de Soras' design: two
  allpass branches; VCV uses 2 × 3 first-order sections, claiming 78 dB stopband; KR-106 uses
  a 12-coefficient HIIR). Upsampler and downsampler ~20 instructions each.
  - 4× for the ladder is not planned: with the oscillators already at 2× and the drive
    antialiased, the Newton-solved ladder's own harmonics at 2× are the remaining source, and
    §9.2's test decides whether a 4× option (on core 1) is needed.

**Second model: a 2-pole OTA SVF (12 dB/oct), multimode.**

- Andy Simper's linear trapezoidal SVF (Cytomic, *SvfLinearTrapOptimised2*), as Emilie
  Gillet writes it in stmlib's `Svf` (MIT): `hp = (in − (r + g)·s1 − s2)·h; bp = g·hp + s1;
  s1 = g·hp + bp; lp = g·bp + s2; s2 = g·bp + lp`, `h = 1 / (1 + r·g + g²)`.
- Saturation: soft-clip the band-pass integrator's state (the OTA's limit), which also keeps
  high resonance well-behaved; LP → BP → HP and notch as a continuous morph (Oberheim SEM
  character). Voicing calibrated against OB-Xd's filter (GPL-3.0, also in sst-filters).
- High solves the saturating integrator with one Newton step (as KR-106 does per stage);
  Efficient uses the previous sample's slope. Same 2× region. Cost ~50 (Efficient) to ~90
  (High) per output sample.

**Built since** (2026-10-25), beside the ladder, the SVF and the diode ladder (the 303's):
- **OTA cascade** (`mh_ota`; Roland's IR3109: Jupiter-8, Juno, SH-101): four OTA-C one-poles,
  `dy_i/dt = wc tanh(y_(i-1) - y_i)`, the first driven by `x - k y_4` (Zavalishin's OTA
  one-pole). Linear it is the ladder exactly; driven it saturates the stages' differences, a slew
  limit, smoother than the ladder. The ladder's TPT stages, prewarp, cheap solve and Newton (its
  own Jacobian: d tanh(a_i) on the diagonal and below, the feedback's corner), its mode mixes and
  BASS COMP. It self-oscillates far flatter than the ladder (-203 cents at k 4.1, -410 at 4.48,
  the same at every cutoff), undone by `mh_ota_tune` (a fit within 0.3 cents). Checked: the
  ladder to 0 at -120 dB; its own equations' RK4 at 16x in float64, harmonics within 0.17 dB;
  self-oscillation within 0.8 cents C2-C7; 2.0 Newton steps a sample on a driven saw.
- **Sallen-Key** (`mh_k35`; Korg's KORG-35, the early MS-20's): Pirkle's zero-delay K35 as
  Surge ships it (sst-filters' `K35Filter.h`, from Odin 2; GPL-3.0), ported with its arithmetic
  unchanged, its loop through a tanh. Low-pass `w^2 / (s^2 + (2-K) w s + w^2)` and the MS-20's
  6 dB/oct high-pass `s (s + w) / (the same)`, both run, MORPH blending them; K to Surge's 1.96.
  Matches both transfer functions to 0.000 dB (K 0.3-1.9); bounded with an 8x saw at full K.
- **The filters at 4x** (High, the scheduler's last extra): the drive already runs at 4x for its
  ADAA; the filter moves in there, before the decimator, so only the filter's cost doubles. Hit
  hard (three times full scale, as BASS COMP can), the OTA aliases at -61 to -67 dB and the
  Sallen-Key at -88 to -92 at 2x, below -100 at 4x; the ladder is clean either way, and below
  1.5x full scale none alias (`host/examples/filter_4x.rs`). 2x and 4x agree within 0.1 dB where
  linear and self-oscillate in tune (`filters_at_4x`).

Still candidates: Vult's filters (Laguna Ruiz: circuit equations solved with
Newton, written in the Vult language, which compiles to C, including fixed point, for
microcontrollers) are the quality bar to listen against; Freak's models are closed, but the
Vult compiler and the free Rack modules' approach are public.

### 4.5 Envelopes

Two ADSRs (filter, amp) plus an optional third (mod), per sample.

- **Shape: RC, not linear.** Attack charges toward an overshoot target (1.5; KR-106's Juno-6
  model: `env += (1.5 − env)·a`, the stage ends at 1.0), so the attack is fast and convex as on
  analogue envelopes; decay and release are exponential toward their targets (with a small
  undershoot below zero so they actually reach zero in the stated time). Times are to −60 dB.
- **Ranges:** attack 0.5 ms–10 s, decay / release 1 ms–20 s, exponential knob curves; one
  multiply-add per stage per sample (coefficients from a flash table indexed by the knob).
- **Behaviour:** retrigger from the current level (never jump to zero: clicks); legato mode
  (no retrigger while a key is held); loop (AD repeats while held) and AR modes; velocity to
  level and to attack time; "Vintage" ±0–8 % timing variance per note.
- Amp envelope minimum attack 0.5 ms and a 1 ms de-click ramp on hard retriggers.
- Cost ~6 per envelope per sample.

### 4.6 VCA and output stage

- Amp envelope × velocity × level, with a linear ↔ exponential response switch.
- Post-VCA 1-pole TPT high-pass (bass trim: off / 30 / 80 / 240 Hz), as the Juno's HPF.
- **DC block** (1-pole HP at ~5 Hz), then a **soft limiter** (rational tanh with ~1 dB knee
  under full scale), then MASTER. Output as 24-bit words (`sample · 2^23`, saturated).
  On the hardware the full word reaches the codec. fm1-emu keeps 16 bits of it (`>> 8`) unless
  asked for the whole word; Harbor asks, plays all 24 bits and records 16-bit WAVs with TPDF
  dither.

### 4.7 Modulation

- **LFOs (2):** triangle, sine, saw up / down, square, sample & hold, smoothed random;
  0.01–50 Hz or synced to MIDI clock; delay and fade-in; free / key-reset; mono.
- **Fixed routings** on the panel (what a player reaches for): LFO 1 → pitch (vibrato, with
  mod wheel), LFO 2 → cutoff, LFO → PWM, filter env → cutoff (bipolar), env → pitch (osc 2 for
  sync sweeps).
- **Matrix:** 8 slots, source × amount → destination; sources: LFOs, envelopes, velocity, key,
  mod wheel, aftertouch, bend, CC, random-per-note; destinations: pitches, PW, shape, levels,
  drive, cutoff, resonance, morph, LFO rates, unison detune, chorus depth. Evaluated every 16
  samples, ramped.
- **Glide:** exponential (constant time, the analogue RC) or linear (constant rate); always or
  legato only; 0–5 s.

### 4.8 Voice logic and MIDI

- Mono, note stack of 16; priority last / low / high; retrigger or legato; "hold".
- Panel keys and MIDI share the stack. MIDI: one channel (selectable, omni), pitch bend ±0–12
  semitones, mod wheel, channel aftertouch, sustain, CCs for every panel parameter (published
  table), MIDI clock for LFOs and delay, program change. USB and TRS (and BLE later).
- A simple arpeggiator (up, down, up-down, random, as played; 1–4 octaves; clock-synced).

### 4.9 Unison (first call on left-over cycles)

- **Stack:** 1, 3, 5 or 7 copies of each main oscillator (osc 1 and osc 2 independently).
  The sub stays single.
- **Detune:** Adam Szabo's analysis of the JP-8000 super saw: a nonlinear detune curve (little
  spread until halfway, then fast) with fixed relative offsets per voice and the centre voice
  level falling as the side voices rise ("mix"). Copy the curve's shape, not the polynomial fit.
- **Phase:** free-running per voice, randomised on the first note (a super saw's character
  comes from phase independence).
- **Drift:** each unison voice gets its own drift walk (§4.2), so the stack moves like separate
  oscillators.
- **Stereo:** spread 0–100 %. Mono unison runs into one filter (cheap, the classic mono-synth
  sound). **Stereo unison** sums the left- and right-panned voices separately into **two
  filters** with a small "Vintage" cutoff offset between them: true stereo at the cost of a
  second filter.
- The extra copies use 2-point polyBLEP even in High (at the 2× rate its residue is already
  well below the detuned stack's own beating), the main pair keeps the minBLEP.
- Cost: 7 + 7 is 12 copies beyond the main pair: ~50 each in High (2×), ~25 in Efficient, so
  ≈ 600 / ≈ 300; plus a second ladder in stereo (≈ 500 High, ≈ 250 Efficient).

### 4.10 Chorus (second call on left-over cycles)

Three modes plus off. Delay lines are short (≤ 20 ms): float lines of 1024 samples, 4 KB each.

1. **Juno (BBD)**, after Ultramaster KR-106's documented MN3009 model (GPL-3.0; FM-106 already
   runs it on the FM-1):
   - centre delay 3.5 ms; triangle LFO, **mode I 0.513 Hz ±0.5 ms, mode II 0.863 Hz ±1.1 ms,
     I+II both** on separate taps; left and right taps 180° apart.
   - 4-point Hermite (Catmull–Rom) interpolation; the BBD's bandwidth as a one-pole low-pass at
     `stages / (4 · delay)` (256 stages), 15 kHz pre- and post-low-pass (2nd-order Butterworth),
     soft compression of the "charge" above ±0.7; mix `1.2 · (0.4 dry + 0.6 wet)`.
   - Optional BBD hiss (very low, filtered) for those who want it; off by default.
   - Cost ~100.
2. **Ensemble** (string-machine / Dimension style): three taps whose LFOs are 120° apart, each
   modulated by a slow (~0.6 Hz) and a fast (~6 Hz) LFO, panned L / C / R; the tri-phase
   arrangement Surge's Ensemble uses. Cost ~150.
3. **BBD, accurate** (on core 1, when it is healthy): Holters & Parker's combined model of a
   BBD with its input and output filters (DAFx 2018), as chowdsp's `BBDDelayLine` implements it
   with the Juno-60's anti-aliasing and reconstruction filters. Delay set by a virtual clock, no
   interpolation; filter-bank cost several hundred instructions per tap: measure.

Shared: rate, depth and mix knobs per mode; the chorus is after the VCA so it never re-triggers;
its LFOs free-run (or reset with transport).

### 4.11 Delay and reverb (only after everything above)

Built (2026-10-25), on core 0 after the chorus: delay, then reverb, then the DC block and limiter.
- **Reverb** (`mh_reverb`): Mutable Instruments' reverb from Rings (Emilie Gillet; MIT), ported
  line for line: the Griesinger topology Dattorro's paper describes (four input allpasses, a loop
  of two allpass pairs and two long delays swept by two slow LFOs), Rings' delay lengths, 16-bit
  memory (64 KB) and LFO stepped every 32 samples. Its output is Rings' to the bit (Rings' own
  code built on the host beside it, `reverb_is_rings_to_the_bit`). The knobs follow Rings':
  amount 0.6 x MIX (a dry-to-wet crossfade), time 0.35 + 0.63 x, damping 0.3 + 0.6 x, input gain
  0.2, diffusion 0.625. TIME at full is Rings' near-freeze (about 0.8 dB a second).
- **Delay** (`mh_delay`): a ping-pong pair (the input into the left line, its echo into the right,
  back into the left), tempo-synced (1/16 to 1/2, the arpeggiator's tempo or MIDI clock), every
  pass through a tone low-pass (1.5-12 kHz), a 100 Hz high-pass and stmlib's soft limiter, 4-point
  Hermite reads, a change of time gliding (~80 ms) as a tape's speed does. 16-bit lines, 1.09 s
  each (192 KB).
- **Never an old tail:** an effect turned off keeps its memory; turned on again, the memory is
  cleared first, 4 KB a block (a whole clear would be one block's interrupt time). The voice stops
  rendering only once the output has been below -80 dBFS for 4096 frames, and with the delay on
  for a whole line more, since an echo may still be on its way.
- Checked (`host/tests/fx.rs`): tails fall with no rises; held noise through the reverb peaks no
  more than the dry noise's own (+0.3 to +1.0 dB: no ringing mode); left and right correlate
  0.02-0.06; echoes on the beat, left then right, -156 dB between; full feedback and time
  bounded and falling; no old tail on re-enabling; the scheduler's switch-off a fade, not a step.
- Cost: about 3 % of a half for both (SAW LEAD 29 -> 32 %). RAM 397 KB of 442.
- **DRY:** FX held 0.6 s turns the chorus, delay and reverb off and on (DRY in the header), to
  hear a patch bare.

---

## 5. Budget

Estimates in instructions per output sample, core 0, against 5 440 available. "Worst" is High
with FM or ring above C4 (oscillators at 4×) and the ladder taking its third Newton iteration.

| Block | Efficient | High | High, worst |
|---|---:|---:|---:|
| Osc 1 + osc 2 (+ sub, noise, drift) | 100 | 170 (2×, minBLEP) | 340 (4×) |
| Mixer, drive, resampling | 60 (↑2, ↓2) | 70 (ADAA, ↓2 only) | 70 |
| Ladder | 250 (cheap ZDF) | 500 (Newton, 2 it.) | 700 (3 it.) |
| Envelopes ×3, LFOs ×2, glide, matrix (amortised) | 50 | 50 | 50 |
| VCA, HPF, DC block, limiter | 20 | 20 | 20 |
| **Core voice** | **≈ 480 (9 %)** | **≈ 810 (15 %)** | **≈ 1 180 (22 %)** |
| Unison 7 + 7 (12 more copies) | 300 | 600 | 600 |
| Stereo unison: second ladder | 250 | 500 (or core 1) | 700 (or core 1) |
| Chorus (Juno / Ensemble) | 100–150 | 100–150 | 150 |
| Delay + reverb | 300 | 300 | 300 |
| **Everything on core 0** | **≈ 1 480 (27 %)** | **≈ 2 360 (43 %)** | **≈ 2 930 (54 %)** |
| UI, LCD, MIDI, flash | ≤ 10 % | ≤ 10 % | ≤ 10 % |
| Reserve (XIP misses, interrupts) | ≥ 25 % | ≥ 25 % | ≥ 25 % |

Read the percentages as lower bounds (one instruction per cycle). High's core voice fits core 0
with every extra at the estimates; at twice the estimates the scheduler sheds extras (the second
ladder moves to core 1 first) but the voice itself still fits. Core 1 carries the accurate BBD
chorus and, when stereo unison is on, the second ladder.

---

## 6. Spending left-over cycles: the scheduler

The **mode** (High / Efficient) is the player's choice and the scheduler never changes it while
things fit; it only decides which **extras** run.

1. **Measure:** TIMER4 (24 MHz) timestamps the start and end of each 128-frame render; keep a
   fast-attack, slow-release estimate of the load (worst of the last ~1 s).
2. **Extras in order** while the load stays under 70 %: unison voices (X1-X3), the stereo second
   filter (X4), the chorus (X5), the delay and reverb (X6), the filters at 4x (X7: tried only
   below 55 %, 35 % with the stereo filters, as it costs +11 to +22 % with one filter and twice
   that with two). A level that passes 80 % is dropped and stays off until the patch changes; the
   window just after a change drops only past 90 %, as it carries the change's own settling (the
   filters' solves catching up) and one spike must not cascade. Holding FX turns the chorus,
   delay and reverb off (DRY).
3. **Never audibly:** extras change only between notes or with a 20 ms crossfade. A patch
   stores what it *asks* for; the LCD shows when the scheduler gives it less ("UNISON 5/7").
4. **Last resort:** if the load passes 90 % with every extra already off (only possible far
   below 240 MHz, or if the hardware is much slower than the estimates), the ladder drops to
   its cheap solve until the next note-off, and the LCD says "EFFICIENT (LOAD)". Better a small
   change in drive character than a dropout.
5. **Core 1:** used only after Felucca's `fm1_cpu1.h` check passes (some units misread RAM on
   core 1; fm1-emu's `--flaky-core1` reproduces it). Core 1 runs the accurate BBD chorus and the
   stereo second ladder; if it misses a deadline or fails the check, the scheduler moves that
   work back to core 0 (or drops it, in the order above) and stays there.
6. **In emulation:** Harbor's automatic clock (240 → 200 → 160 → 120 MHz) looks to the firmware
   like fewer cycles per sample, so the scheduler sheds extras on its own and the core voice
   keeps sounding the same. Even at 120 MHz, High's worst-case voice is ~45 % of core 0.

---

## 7. Being cheap to emulate

fm1-emu and Harbor cost about a quarter fixed plus the guest cycles actually run (idle is
skipped), so these matter as much as the DSP:

- **Idle when done.** The main loop executes `idle` until the next interrupt (fm1-sid: 6×
  faster in the browser). No busy-wait polling of timers, flags or core 1.
- **No per-sample helper calls:** no `double`, no libm, no compiler-rt division of 64-bit
  integers in the audio loop; inline small functions. Every helper call is a JIT exit.
- **Code placement:** keep the audio code in flash (XIP) unless hardware measurement shows XIP
  misses cost more than ~10 %. If it moves to RAM, never write to words near it at run time
  (the JIT re-checks RAM code on writes; Melodee's RAM-resident audio was the slowest
  firmware until fm1-emu learnt to cope).
- **Redraw the LCD only on change**, not at a fixed 60 Hz.
- **Process in blocks** (16–32 samples per inner loop) so loops stay hot and compiled.
- Target: Monohull in High with Auto extras emulates in real time at 240 MHz on a Pixel 7-class
  phone in Harbor (Felucca is at 2.4×, Melodee 1.2× there); Efficient is the fallback on
  slower phones.

---

## 8. Panel and UI (sketch)

- **Keys:** notes; OCT− / OCT+ shift by octave (hold both for 3 s: USB update mode, as in
  every Felucca-lineage firmware).
- **Pages** on the function buttons, four knobs each (K1–K4), the LCD drawing the page:
  OSC 1, OSC 2 / SUB / NOISE, MIX / DRIVE, FILTER (cutoff, resonance, env amount, key track),
  F-ENV, A-ENV, LFO 1 / 2, UNISON (count, detune, mix, spread), FX (chorus mode, rate, depth,
  mix), GLIDE / VOICE, MATRIX.
- **Always-there controls:** cutoff on a fixed encoder whatever the page; SELECT steps pages;
  PRESETS browses patches; HOME returns to the main page.
- **LCD:** the main page shows the patch name, a live scope, filter cutoff and resonance as a
  curve, the envelope shapes and the scheduler state; edit pages draw the parameter being
  turned large.
- **Presets:** 128 slots in flash (each well under 256 bytes), 32 factory patches covering
  bass, lead, sync lead, PWM brass, super saw, acid, noise percussion; SysEx dump / load.

---

## 9. Verification

### 9.1 Host reference build

The DSP is plain C99 with no HAL calls, built for the host as well (as fm1-sid does). The host
build renders WAVs bit-identical to the FM-1 build's (both use the same float operations; if
the FPU's rounding differs, compare within 1 LSB at 24 bits and document it), and runs the
tests below in CI.

### 9.2 Sound tests (objective)

| Test | Target |
|---|---|
| Saw aliasing at C7 (2093 Hz), unfiltered | strongest alias ≥ 80 dB below the fundamental in 20 Hz–16 kHz (High); Efficient reported (2-point polyBLEP at 1× measures −38 dB on a saw, −28 dB on a 10 % pulse at C7) |
| Pulse at 10 % and hard-sync sweep | same, measured on a sweep spectrogram |
| Ring mod and FM (index up to 4) at C6 | High: aliases ≥ 70 dB down; Efficient: report |
| Drive at 32× on a C6 saw | High: aliases ≥ 70 dB down; Efficient: report |
| Ladder self-oscillation tuning | ±5 cents C1–C6 with 100 % key tracking |
| Ladder vs a reference | magnitude response within 0.5 dB of the analogue transfer function (linear, low drive) to 16 kHz; High's harmonic levels within 1 dB (first 9 harmonics, sine in, drive and resonance swept) of `bob~`'s equations integrated at 16× in float64; Efficient: report the difference |
| Filter modulation | an audio-rate cutoff sweep produces no clicks, NaNs or blow-ups at any resonance; 10 minutes of random-parameter fuzzing stays finite |
| Envelope timing | stated times within ±5 %; no click on retrigger (no step > −60 dBFS) |
| Chorus | delay and LFO rates as specified (Juno modes measured by cross-correlation) |
| Noise floor | idle output below −110 dBFS with the noise source off |
| Latency | key to first non-zero sample ≤ one half buffer |

### 9.3 Cost

- **Instructions:** fm1-sid's method: a bench build with markers, run on fm1-emu, count
  instructions between markers for each block above (`fm1/run.py`), in both modes.
- **Cycles:** on a real FM-1, TIMER4 timestamps per block for the same benches; report the
  cycles-per-instruction ratio, FPU MAC and `÷` costs, and XIP vs RAM code. Update §5 with
  the results.
- **Emulation:** Harbor's harbor-bench on a Pixel 7 Pro, both modes, extras on and off, keys
  held.

### 9.4 Listening

Blind A/B, level matched, against the section 10 references (VCV Fundamental VCF, `bob~`,
Surge XT's Vintage Ladder and OB-Xd filters, KR-106's filter and chorus, Open303 for acid
lines), and a real analogue mono synth if available. Three listeners, short phrases (bass,
lead, sweep, super saw pad). High against Efficient too: if listeners can't tell them apart on
a phrase type, that's worth knowing for the defaults.

---

## 10. Voicing from open-source references

A commercial synth's sound comes from months of comparing against real circuits and adjusting.
Monohull gets as much of that as it can from open projects that have already done it, and
spends its own listening time only on what's left.

### 10.1 What to take from where

| Part | Reference | What it gives | Licence |
|---|---|---|---|
| Moog-style ladder (the default) | Miller Puckette's `bob~` (Stinchcombe's ODE); D'Angelo–Välimäki "Improved" model (MoogLadders) | the equations High solves; harmonic and resonance behaviour to match | BSD; ISC |
| Ladder, as players know it in Rack | VCV Fundamental VCF; Surge XT Vintage Ladder (sst-filters) | drive and resonance mappings, self-oscillation behaviour, a cheap solve that ships | GPL-3.0 |
| Juno character: filter, envelopes, HPF, sub, chorus | Ultramaster KR-106 | curves calibrated against a real Juno-6 ("1982 mode") and the Juno-106 firmware ("1984 mode"): cutoff and resonance laws, RC envelope with overshoot target and ±8 % unit variance, saw curvature, MN3009 chorus | GPL-3.0 |
| SEM / Oberheim 12 dB | OB-Xd (and its port in sst-filters) | the SVF's resonance and saturation feel, the LP → notch → HP morph | GPL-3.0 |
| Acid | Robin Schmidt's Open303 (JC303 is its plug-in port) | 303 filter fit (ladder with feedback HP), accent and slide behaviour, envelope shapes fitted to hardware by the community | core reportedly MIT, JC303 GPL-3.0: confirm |
| A whole monosynth's design | Monique (Thomas Arndt; open source since 2021, Surge Synth Team) | how a commercial monosynth arranges its modulation, its morphing and its voicing; a patch-design reference | GPL-3.0 / MIT: confirm |
| Oscillators | Mutable Plaits / stmlib; VCV Fundamental VCO | antialiasing code, waveform morphs | MIT; GPL-3.0 |
| Super saw | Adam Szabo's JP-8000 analysis | detune and mix curves | paper |

### 10.2 Fit, don't guess

A host tool (part of the host build, §9.1) links Monohull's C DSP and the reference engines
(each built as an offline renderer from its own source) and:

1. renders both through the same test material: cutoff and resonance sweeps, sine and saw in at
   several drive levels, envelope gates at many settings, glides, short musical phrases;
2. fits Monohull's mapping parameters to the reference by minimising a multi-resolution
   log-spectral distance: cutoff knob → Hz, resonance knob → k, drive scaling, bass
   compensation, envelope time curves and overshoot target, glide's RC time, chorus depth;
3. stores each fitted set as a **character** the player picks: Ladder "M" (fitted to `bob~` and
   Fundamental), "Juno" (KR-106), "SEM" (OB-Xd), "Acid" (Open303). A character only changes
   curves and constants, never the algorithms, so it costs nothing at run time;
4. keeps the reference renders as regression tests: a change that moves a character more than
   the §9.2 limits fails CI.

### 10.3 What open source can't give

- Only KR-106 among these is calibrated against measured hardware (that we know of); the
  others inherit their authors' models and ears. Monohull ends up as close to a real Moog as
  `bob~` and the "Improved" model are, not closer. A real synth on the bench for a final
  comparison is still worth borrowing.
- Factory patches are written fresh, not copied from Roland, Moog or other vendors' banks, and
  the patch design is ours. Monique's and KR-106's banks show what good patches on similar
  engines sound like.
- The final ear pass (§9.4) remains: fitting gets the curves right, listening decides the
  defaults and the ranges.

---

## 11. Building it

- **Base:** Felucca's HAL, startup, USB / MIDI, flash and update code (GPL-3.0), so Monohull
  is **GPL-3.0**, which also allows borrowing from the GPL-3.0 references (VCV Fundamental,
  KR-106, Surge's sst-filters / sst-effects, Valley) with attribution; MIT code (Mutable's
  stmlib, Plaits) mixes in freely. Check each borrowed file's licence when it is taken (HIIR
  and Vult's free modules included).
- **Toolchain:** JieLi's clang for pi32v2, `-mcpu=r3` for the FPU; try `-mfprev1` (FPU dual
  issue) and `-O2` vs Felucca's `-Os` on the bench. On Windows: JieLi's own installer
  (`pkgman.jieliapp.com/s/win-toolchain`, 2.5.3, no licence file since 2.4.6), which puts
  `clang.exe` in `C:/JL/pi32/bin`; on Linux the same toolchain as a tarball. No WSL or Docker.
- **Engine shape:** a dedicated firmware (fm1-sid's "path B") rather than a Felucca engine
  plug-in: Felucca's engine interface renders per voice, and Monohull wants the whole audio
  interrupt, its own UI and the scheduler.
- **Package:** an FM-1 update package (`.fwsc`) via Felucca's loader and `fm1pkg_make.py`, so it
  installs like the others and boots in fm1-emu and Harbor; published to thefirmwarehub's
  catalog.

### Milestones

1. **Bench harness:** host + FM-1 builds of a DSP library, fm1-emu instruction counts per block;
   one saw → ladder → VCA rendering WAVs. Measure FPU costs on hardware if one is to hand.
   Measure the FM-1's analogue output (noise floor, distortion, bass roll-off, USB hum) with an
   audio interface: if it is poor, every later goal is capped by it.
2. **Reference harness (§10):** `bob~`, the "Improved" model, VCV's VCF and KR-106 as offline
   renderers; the fitting tool; the §9.2 tests running against them.
3. **Core voice, both modes:** oscillators (2× minBLEP and 1× polyBLEP), sub, noise, drive
   (ADAA), ladder (Newton and cheap solve, one code path), envelopes, LFOs, glide, mono logic;
   panel and MIDI; the "M" character fitted; 8 test patches. Objective tests passing.
4. **Unison and Juno chorus**, the scheduler; the "Juno" character.
5. **SVF ("SEM"), ensemble chorus, matrix, presets in flash, SysEx;** 4× oscillators for FM and
   ring.
6. **Core 1:** accurate BBD, the stereo second ladder; delay and reverb; the "Acid" character;
   listening tests.
7. Release: package, factory patches, manual page; Harbor catalog entry.

---

## 12. Open questions

1. FPU timing: MAC latency, dual issue in practice, `÷` cost. Decides between per-sample
   division and tables in the ladder solve.
2. XIP cache size and miss cost: decides whether hot DSP moves to RAM.
3. Does the keybed give velocity (Harbor's engine API has `velocity(us)`)? If not, velocity
   is MIDI only.
4. Does High's minBLEP at 2× earn its cost over 2-point polyBLEP at 2× once the ladder
   follows? (High uses minBLEP until §9.2 and §9.4 say otherwise.)
5. Should stereo unison (two filters) be the default for super saw patches, given its cost?

---

## 13. References

Code (licence in brackets):

- VCV Rack Fundamental, `src/VCF.cpp` (v2): ladder, `tanhXdX_4_6`, polyphase halfbands
  [GPL-3.0] — https://github.com/VCVRack/Fundamental/blob/v2/src/VCF.cpp
- Mutable Instruments stmlib `dsp/filter.h` (SVF, OnePole, `tan` approximations) and
  `dsp/polyblep.h` [MIT] — https://github.com/pichenettes/stmlib
- Mutable Instruments Plaits `dsp/oscillator/variable_shape_oscillator.h`,
  `variable_saw_oscillator.h` [MIT] — https://github.com/pichenettes/eurorack
- Ultramaster KR-106 (Juno-106: polyBLEP DCO, IR3109 ladder with Newton step, MN3009 BBD
  chorus, RC ADSR), `docs/DSP_ARCHITECTURE.md` [GPL-3.0] —
  https://github.com/kayrockscreenprinting/ultramaster_kr106
- FM-106, KR-106 on the FM-1 [GPL-3.0] — https://github.com/PatttF/fm-106
- Surge XT sst-filters (Vintage Ladder: `RK` from Puckette's `bob~` at 4× and Huovilainen's
  model; OB-Xd; K35 and Diode Ladder from Odin 2; Tri-Pole) [GPL-3.0] —
  https://github.com/surge-synthesizer/sst-filters
- Miller Puckette, `bob~` (Moog ladder by RK4 on Stinchcombe's equations), in Pure Data's
  `extra/` [BSD] — https://github.com/pure-data/pure-data
- MoogLadders, ten ladder models side by side with analysis scripts, among them `bob~`'s
  (`RKSimulationModel`, BSD) and D'Angelo–Välimäki's (`ImprovedModel`, ISC); licences differ
  per model — https://github.com/ddiakopoulos/MoogLadders
- OB-Xd [GPL-3.0] — https://github.com/reales/OB-Xd
- JC303, the plug-in port of Robin Schmidt's Open303 [GPL-3.0] — https://github.com/midilab/jc303
- Monique monosynth (Thomas Arndt) [GPL-3.0 / MIT: confirm] —
  https://github.com/surge-synthesizer/monique-monosynth
- chowdsp_utils `BBD::BBDDelayLine` (Holters–Parker BBD with Juno-60 filters) —
  https://ccrma.stanford.edu/~jatin/chowdsp/chowdsp_utils/classchowdsp_1_1BBD_1_1BBDDelayLine.html
- Jatin Chowdhury, ADAA notebook — https://ccrma.stanford.edu/~jatin/Notebooks/adaa.html
- Vult language and compiler — https://github.com/vult-dsp/vult ; free Vult modules —
  https://github.com/modlfo/VultModules
- Laurent de Soras, HIIR (polyphase IIR halfband) — http://ldesoras.free.fr/prod.html
- Felucca [GPL-3.0] — https://github.com/hugelton/Felucca ; OberVave —
  https://github.com/zvenson/obervave
- fm1-sid (instruction-count method, `fx.c`, firmware structure) —
  `third_party/fm1-emu/fm1-sid/README.md`

Papers and notes:

- V. Zavalishin, *The Art of VA Filter Design*, Native Instruments, rev. 2.1 (2018).
- A. Simper, *Linear Trapezoidal Integrated SVF* (Cytomic technical paper) —
  https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
- A. Huovilainen, "Non-linear digital implementation of the Moog ladder filter", DAFx 2004.
- S. D'Angelo, V. Välimäki, "An improved virtual analog model of the Moog ladder filter",
  ICASSP 2013; "Generalized Moog ladder filter, Parts I and II", IEEE TASLP 2014.
- T. Stinchcombe, "Analysis of the Moog transistor ladder and derivative filters" (2008).
- T. Voipio (mystran), "Cheap non-linear zero-delay filters", KVR Audio DSP forum (2012).
- V. Välimäki, A. Huovilainen, "Antialiasing oscillators in subtractive synthesis", IEEE Signal
  Processing Magazine 2007 (polyBLEP).
- E. Brandt, "Hard sync without aliasing", ICMC 2001 (minBLEP).
- F. Esqueda, V. Välimäki, S. Bilbao, "Rounding corners with BLAMP", DAFx 2016.
- J. Parker, V. Zavalishin, E. Le Bivic, "Reducing the aliasing of nonlinear waveshaping using
  continuous-time convolution", DAFx 2016; S. Bilbao, F. Esqueda, J. Parker, V. Välimäki,
  "Antiderivative antialiasing for memoryless nonlinearities", IEEE SPL 24(7), 2017.
- M. Holters, J. Parker, "A combined model for a bucket brigade device and its input and
  output filters", DAFx 2018.
- A. Szabo, "How to emulate the super saw" (2010).

---

## 14. Status and measurements (2026-10-23)

The DSP (`dsp/`) is built with MSVC on the host and tested there (`host/`, `cargo test
--release`): 19 tests, all passing. Not yet built for the FM-1 (section 11's toolchain).

| Measurement | High | Efficient |
|---|---|---|
| Saw / 10 % pulse / triangle at C7, worst alias | ≤ −104 dB (window-limited) | −38 / −28 / −65 dB |
| Hard sync at C6 (×2.37) | −104 dB | −39 dB |
| Ring at C6 / FM index 1 and 4 at C6 | −113 / −99 / −105 dB | −36 / −42 / −42 dB |
| Drive 32× on a C6 saw, through the ladder | −75 dB | −46 dB |
| Ladder vs the float64 RK4 reference (harmonics 1–9, three levels, two cutoffs, three resonances) | within 0.27 dB | within 2.84 dB (worst at heavy drive) |
| Self-oscillation tuning C2–C7, k 4.1 and 4.5 (with the correction below) | within 0.2 cents | up to +19 cents at C7 |
| Newton steps per 2× sample | 1.0–1.3 on the demo patches, 1.6 on a hard-driven saw at full resonance | – |
| Envelope times (attack, decay and release to −60 dB) | within 0.05 % | same |
| Halfband round trip, 100 Hz–20 kHz | flat within 0.003 dB | same |
| Approximations (exp2, tanh, tan, log2, sin, log1p) against libm | within 1.1e-6 | same |

Findings on the way:

- **Self-oscillation goes flat with resonance**, by an amount that depends on k alone (−5 cents
  at k 4.05 up to −43 at 4.5, the same at every note): the tanh ladder's own physics, not the
  discretisation. `mh_ladder_tune(k)` scales the cutoff to cancel it.
- **Drive needs 4× and ADAA together** (§4.3's figures).
- **Newton's stopping rule**: stop once a step is below 3e-4 (quadratic convergence leaves
  ~step²) rather than below 1e-6: the same accuracy, 1.61 instead of 2.00 steps on the worst
  case.
- **The BLEP table is polyphase** (a row per sub-sample offset, the taps contiguous): linear
  interpolation along τ crossed the ideal step's jump and capped aliasing at −60 dB.

Not yet known, and the next things to measure on fm1-emu: the instruction cost of each block
(§5). The host's cost (MSVC, scalar) is 13–47 ms per second of audio for High and about half
that for Efficient across the demo patches, which hints that High's ladder (two solves per
output sample, each with several tanh and divisions) is heavier than §5's 500. Obvious savings
if so: reuse the last Newton iteration's tanh values as the next sample's secant slopes,
replace tanh's exp2-and-divide with a rational form, and share the reciprocals.

### 14.1 On the FM-1 (2026-10-24)

Built with JieLi's Windows toolchain (`C:\JL`, unpacked from the signed installer, which
otherwise insists on Code::Blocks) by `tools/build_fw.py`: a 71 KB app, 130 KB of RAM, a 610 KB
`.fwsc`. Boots and plays on fm1-emu and in Harbor (Pixel 7 Pro).

Cost of one held note in High, instructions per output sample on fm1-emu (`MH_PROFILE=1`
build, `tools/fwstages.sh`; one TIMER4 tick is 10 instructions at 240 MHz):

| Stage | First build | Now |
|---|---:|---:|
| Control (every 16 samples, amortised) | 147 | 110 |
| Modulation (envelopes, LFOs, pitch, cutoff) | 437 | 281 |
| Sources (osc 1 at 2×) | 855 | 550 |
| Drive (4×, ADAA) | 920 | 488 |
| Filter (Newton ladder, 2×) | 2 015 | 1 142 |
| Output (decimate, VCA, chorus, DC, limiter) | 253 | 221 |
| **Total** | **4 627 (85 %)** | **2 791 (51 %)** |

What got it there, each checked against the host tests (unchanged figures): tanh as Lambert's
[7/6] fraction instead of exp and a division (−80 dB accurate); the ladder keeps its last
evaluation's slopes for the next sample's predictor (five tanh a converged sample, not ten);
the drive is a cubic soft clip whose ADAA is exact and division-free inside the knee (−74.9 dB
in the drive test, as tanh's was); osc 2, the LFOs and the chorus are skipped when nothing uses
them; envelope and unison coefficients are cached. Also fixed on the way: the firmware's main
loop ran the panel at every 10 kHz timer wake (parameter lookups by name, LED writes): now
500 Hz and only on change.

Still to do: §5's budget was 2–3× optimistic. The ladder (~570 a step) and the oscillator
(~275 a step) are the next targets: processing in blocks of 16 rather than per sample, so the
pi32v2's 16 registers stop spilling, is the obvious step. Until then the scheduler holds
unison-heavy patches back.

### 14.2 Since (2026-10-24)

- **The diode ladder** (`dsp/mh_diode.c`, filter type DIODE): the TB-303's filter as Open303
  runs it (four capacitors coupled both ways, the feedback through a 150 Hz high-pass), integrated
  trapezoidally, prewarped so its peak lands on the cutoff, the feedback solved in the same sample
  (two tridiagonal solves). Its poles are spread (the lowest at 0.15 of the stage frequency): a
  resonant peak over a gently falling passband, and like a 303 it rings hard at full resonance
  without quite self-oscillating. Its level is made up in part as resonance rises. ACID uses it.
- **Resonance curve**: the feedback follows a square law to 0.97 of the knob, the edge of
  self-oscillation, then on to 12 % past it at the top (ladder and diode alike): the whistle is
  the last 3 % of the knob, not the last 6 %.
- **The arpeggiator** (in the engine, `mh_synth.c`): up, down, up-down, random, as played; 1-4
  octaves; 1/4 to 1/32 with triplets; gate (1 = tied, which glides); latch; an accent every Nth
  step (the rest held back, the 303's way); its own tempo or MIDI clock (24 a beat, start / stop
  / continue). Timing to the control block (16 frames). Tests: `host/tests/arp.rs`.
- **Accent from the panel**: REC held plays the keys at full velocity (otherwise 85 of 127), so
  velocity-mapped sweeps (ACID's) can be accented without MIDI.
- **High's unison copies** use the table kernel too (they were 2-point polyBLEP and aliased at
  -50 to -70 dB each against the main oscillator's -104); a 7-voice stack at C7 measures -78 dB.
  The overload fallback now drops only the ladder's solve to the cheap one (footer HI-), never
  the oscillators.
- **Core 1** (Felucca's / X0X's mailbox and check word, adapted to the 1.5.1 hal): each control
  block's back (drive, filters, decimation, VCA) runs on core 1 while core 0 finishes the block
  before it (the output stage: high-pass, chorus, DC block, limiter) and makes the next one's
  front (modulation, every source, the mix): a pipeline inside each 128-frame half, no added
  latency. The halves share no state (the back's patch
  values travel with the block); `the_second_core_changes_nothing` checks that the output is the
  same with the back run at once, after the next front, or failed and re-run. A unit whose core 1
  misreads or stalls runs on core 0 from then on (footer 1C!).
- **The oscillator's common sample** (no wrap, edge or corner in it) skips the event set-up: about
  45 instructions instead of ~106.
- **Harbor** (the app): its 44.1 -> 48 kHz resampler was linear interpolation, which left images
  ~20 dB down that folded back into the band and sounded like aliasing on bright notes; now a
  windowed sinc (images below -90 dB).
- **Every preset at X5** (2026-10-24): SUPERSAW used to stop at X3 (7+7 unison, mono, no
  chorus); it now plays at X5, with the stereo second filter and the chorus, at a 73 % peak
  (`tools/fwload.py 3`). Every other preset settles at X5 too, at 18-37 %. No change to the
  sound: each step below gives the same output to the bit on the host (`host/examples/golden.rs`
  hashes four patches with the back run every way; sync and mixed shapes compared against the
  old loops), and the firmware's audio on one core and on two is identical through a held note.
  - The output stage moved from core 1 to core 0, run while core 1 takes the next block: it
    evens the cores' shares in a stereo patch.
  - The unison copies run a copy at a time over the block in a loop of their own
    (`mh_osc_run`, with a saw-only variant), the phase and ring position in registers: 64
    instructions a copy sample instead of ~125 (the per-sample loop spilled ~19 values a step).
  - The halfbands are unrolled at each call for their fixed size; the ladder's Newton step takes
    tanh(v) / v from the same fraction as tanh (five divisions fewer a sample); the LP24 mix
    returns y4 without making the other mixes. The ladder is 514 instructions a call (541).
  - Tried and dropped: skipping the Newton correction when the predictor already solves the
    sample (below 1e-6). It saved 3 % of the steps on SUPERSAW and let errors build up in a
    resonant patch (self-oscillation -38 dB from the converged solve, against -78 dB now).
  - Measuring: the stage counters (`tools/fwstages.sh`) add both cores' time, so on two cores
    they read about double. `tools/fwload.py` reads the scheduler's own meter (per-half
    interrupt time); `MH_PIN_X=N` holds the extras at a level, `MH_G=1` adds line tables for
    mapping a trace's pcs to source.
- **Vintage, finished** (2026-10-24): two bugs meant only the note-on spread was ever heard.
  The drift's chance of a new target per block was rounded to 0 in integer thousandths (its
  target never left 0), and the main pair ignored their own drift (only the unison copies read
  it). Added (§4.2): fast jitter, osc 2's tracking error, each filter's drift and note-on spread,
  the stereo filters' mismatch, the main pair's saw bend. At Vintage 0 the output is exact (no
  added random draws, every offset 0). Cost: SUPERSAW's peak 73 -> 74 %, the rest unchanged.
  Tests: `vintage_drifts_a_little` (full Vintage moves a held A4 within its budget),
  `bent_saw_aliasing` (the bend's wrap corner corrected: -104 dB, as the straight saw).
- **The dry sound, the effects, two filters** (2026-10-25), for a player used to clean analogue
  monos. Each change is either a port checked against its source or measured by a test:
  - **Knobs glide.** A panel detent (now 1/256 of a range, half a semitone of cutoff, up to 8x
    when spun) or a 7-bit CC was a step, ramped over 0.36 ms: a slow resonant sweep was a
    staircase. Thirty patch values now pass through two ~15 ms poles (the cutoff in octaves), at
    once while nothing sounds. `knob_steps_glide`: a self-oscillating filter turned a detent every
    50 ms moves at most 7 cents in 5 ms (48 without).
  - **Levels matched.** Measured as BS.1770 loudness over a phrase (`host/examples/audit.rs`:
    also the drive's and filter's departure from linear, peaks, the limiter's share, low end, mono
    sum): the patches spread from -5.9 to -19.9 LUFS and two leaned on the limiter. Now each is at
    -15 LUFS (ACID -16.8, WHISTLE -18.9 and RING BELL -16.9 at full volume), none touch it.
  - Envelopes and drive levels measured and left alone (RC curves, a 0.25 s decay a 36 ms time
    constant; the presets work the ladder 7-62 % from linear).
  - Delay and reverb (§4.11), the OTA cascade and Sallen-Key and the filters at 4x (§4.4), the
    303 square (§4.2). Presets: modest delay and reverb on the leads, bells and pads (none on the
    basses, ACID or SUPERSAW, which has no room for them); ACID ARP on the 303 square; PWM CHORUS
    (OTA) and SCREAM (Sallen-Key) added.
  - Load (fwload.py, every patch): twelve settle at X7 (effects and the filters at 4x) at 29-60 %;
    SUPERSAW at X5 (77 %). The filter's own function had to be forced inline: as a call (four a
    frame) it put SUPERSAW's X5 over 80 %.
  - **USB (2026-10-25).** A class-compliant USB-MIDI device ("FM-1 Monohull") with the update
    path the installers use: Dinghy's platform code (`firmware/usb_ota.c`: `plat/usb.c`,
    `plat/ota.c`, the RAM flash driver), built on its own at -Os. It answers the identity
    handshake as `FM-1_928` (free in the Firmware Hub's catalogue), stages the next firmware's
    loader when an installer asks (the audio silent meanwhile) and takes the UBOOT soft key. The
    flash driver may write only the update's staging area. USB is polled from TIMER5 at 2 kHz;
    its MIDI goes to the synth with the TRS jack's. fm1-emu has no USB controller, so this is
    untested there beyond booting and the load: with USB in, thirteen patches settle at X7 at
    27-57 % and SUPERSAW at X5 (78 %), as before. Tested on a real FM-1 on 2026-10-26.
  - **PWM PAD (2026-10-25):** a Juno pad (two detuned pulses swept by the slow LFO, a little
    sub, the OTA cascade opening with a 0.9 s swell, Juno I+II, a long room), -15 LUFS, X7 at
    50 %. It joins the piece (`examples/compose.rs`) for the climb from bar 17.
