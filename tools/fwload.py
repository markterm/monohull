#!/usr/bin/env python3
"""Where the load scheduler settles on fm1-emu: a preset picked, one note held, then the extras
level it reached, the peak of its last window and whether it fell back to the cheap filter. In a
profiling build (MH_PROFILE=1) also the last 256 halves' interrupt times.
    python tools/fwload.py [PRESET_STEPS] [MS]       (PRESETS turned that far from SAW LEAD)
This is the number that decides what plays: the audio interrupt's time per half, both cores'
work included. The stage counters (tools/fwstages.sh) add up both cores' time and mislead on two.
SPDX-License-Identifier: GPL-3.0-only"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EMU = Path(os.environ.get("FM1_EMU", ROOT.parent / "mvave/third_party/fm1-emu/target/release/fm1-emu.exe"))
NM = Path(os.environ.get("JIELI_PI32", "C:/JL/pi32")) / "bin" / "pi32v2-nm.exe"
HALF_TICKS = 69632
preset = sys.argv[1] if len(sys.argv) > 1 else "0"
ms = sys.argv[2] if len(sys.argv) > 2 else "6000"
elf = Path(os.environ.get("FWLOAD_ELF", ROOT / "build" / "fw" / "monohull.elf"))
fwsc = elf.with_suffix(".fwsc")
sym = {}
for ln in subprocess.run([str(NM), str(elf)], capture_output=True, text=True).stdout.splitlines():
    p = ln.split()
    if len(p) == 3:
        sym[p[2]] = int(p[0], 16)
names = ["extras", "load_peak_pc", "eff_forced", "isr_halves"] + (["mh_half_log"] if "mh_half_log" in sym else [])
lo, hi = min(sym[n] for n in names), max(sym[n] for n in names) + (1024 if "mh_half_log" in sym else 4)
r = subprocess.run([str(EMU), "--fwsc", str(fwsc), "--ms", ms, "--keys", f"600:PRESET:+{preset},1000-{ms}:n7",
                    "--dump", hex(lo), str((hi - lo) // 4)], capture_output=True, text=True)
lines = r.stdout.splitlines()
i = [k for k, l in enumerate(lines) if l.startswith("dump ")][0]
w = []
for l in lines[i + 1:]:
    if not l.startswith("  0x"):
        break
    w += [int(x, 16) for x in l.split()[1:]]
at = lambda n: w[(sym[n] - lo) // 4]
print(f"preset +{preset}, {ms} ms: extras X{at('extras')}, peak {at('load_peak_pc')} %, "
      f"cheap filter {'on' if at('eff_forced') else 'off'}")
if "mh_half_log" in sym:
    k0, h = (sym["mh_half_log"] - lo) // 4, at("isr_halves")
    log = [w[k0 + ((h + k) & 255)] * 100 / HALF_TICKS for k in range(256)]
    print(f"last 256 halves: mean {sum(log) / 256:.1f} %, min {min(log):.0f} %, max {max(log):.0f} %")
