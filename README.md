# Monohull

A monosynth firmware for the M-VAVE FM-1: one voice with no per-voice compromises, on the
FM-1's 240 MHz pi32v2. Analogue-modelled oscillators and five filters (transistor ladder, diode
ladder, OTA cascade, Sallen-Key, state-variable), unison, chorus, delay and reverb, with a
scheduler that spends whatever CPU the patch leaves over on the extras. The design, its
references and its measurements are in [docs/spec.md](docs/spec.md).

**Status:** tested on a real FM-1, and it runs in fm1-emu and in Harbor (an FM-1 emulator for
Android). Felucca's README says how to recover a unit whose update was cut off.

## Install

The update package, `monohull.fwsc` (identity `FM-1_928`), is on the [releases page](../../releases). Load it into
Harbor or fm1-emu, or install it on an FM-1 over USB with Felucca's installer:

```sh
git clone --recursive https://github.com/markterm/monohull
pip install mido python-rtmidi
python ext/Felucca/tools/fm1_install.py monohull.fwsc
```

Over USB it is a class-compliant MIDI device, "FM-1 Monohull" (identity `FM-1_928`), so a computer
can play it, and the installers (Felucca's web installer, the Firmware Hub, `fm1_install.py`) install
another firmware over it as they do over Felucca. If that ever fails, hold OCT- and OCT+ for 3 s
for the FM-1's own USB update mode; three failed boots in a row start it too.

Monohull keeps nothing in the flash: no saved patches yet, and it never reads, changes or erases
another firmware's projects or samples (the only part of the flash it may write is the update's
staging area).

## Layout

```
dsp/                 the synth in C99: no HAL, no allocation, no libm in the audio path
  mh_synth           the engine: patch, notes, glide, mod matrix, unison, the signal path
  mh_osc             saw / pulse / triangle / sine morph, BLEP / BLAMP corrected, sync, TZFM, sub
  mh_ladder          the transistor ladder: TPT stages, Newton (High) or cheap solve (Efficient)
  mh_svf             the 12 dB state-variable filter
  mh_diode           the diode ladder (the 303's)
  mh_ota             the OTA cascade (Roland's IR3109), on the ladder's solver
  mh_k35             the Sallen-Key (Korg's KORG-35), Pirkle's model as Surge ships it
  mh_delay           the ping-pong delay
  mh_reverb          Mutable Instruments' reverb (Rings), ported to the bit
  mh_env, mh_lfo     RC-shaped ADSR, LFOs
  mh_chorus          Juno BBD chorus (I, II, I+II) and a tri-phase ensemble
  mh_resample        HIIR-style polyphase halfbands
  mh_params          every patch parameter by name, with range and MIDI CC
  mh_tables          generated: BLEP / BLAMP tables, halfband coefficients
host/                the same C built for the host (MSVC via cargo): tests, references, demos
firmware/            the FM-1 firmware: main.c (panel, pages, scheduler, core 1), presets.h
  usb_ota.c          USB-MIDI and the update path: Dinghy's platform code (plat/, hal/), by zvenson
                     after Felucca, SLOOP and OberVave (GPL-3.0)
ext/                 sources the ports are checked against (Mutable's, Surge's, Open303's) and
                     Felucca (a submodule: its hardware layer, startup and update loader)
tools/build_fw.py    builds the firmware and its update package
tools/gen-tables/    writes dsp/mh_tables.{h,c}
```

## Building

```sh
git submodule update --init
python tools/build_fw.py                  # build/fw/monohull.fwsc
```

`build_fw.py` needs JieLi's pi32v2 toolchain (Windows: `C:/JL`, or set `JIELI_PI32`; see the
spec's §11) and Pillow, which draws the LCD font from Cascadia Mono (Windows' copy, or set
`MONOHULL_FONT`). It downloads the three files of JieLi's AC79 SDK (Apache-2.0) the
package needs into `ext/ac79sdk` and checks their hashes.

The DSP also builds for the host (Rust with MSVC or any C compiler cc-rs knows), for tests and
renders:

```sh
cd host
cargo test --release -- --nocapture       # the spec's tests, with their measurements
cargo run --release --bin render          # demo WAVs in host/out (24-bit, both qualities)
cargo run --release --example selfosc     # self-oscillation pitch per note and resonance
cargo run --release --example audit       # every factory patch: loudness, peaks, drive, low end
cargo run --release --example demo        # every factory patch dry, then wet (out/presets-demo.wav)
cargo run --release --example compose     # a short piece on RING BELL, PWM CHORUS, FAT BASS
cargo run --release --manifest-path ../tools/gen-tables/Cargo.toml   # from the repo root
```

Licence: GPL-3.0 (the firmware builds on Felucca's GPL-3.0 HAL). Borrowed designs and their
licences are credited in the source and in the spec's references.
