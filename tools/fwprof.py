#!/usr/bin/env python3
"""Profile the firmware on fm1-emu: trace a window of instructions while a note is held and
attribute them to functions (and instruction classes).
    python tools/fwprof.py [START_STEP] [COUNT]
SPDX-License-Identifier: GPL-3.0-only"""
import bisect
import collections
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EMU = Path(os.environ.get("FM1_EMU", ROOT.parent / "mvave/third_party/fm1-emu/target/release/fm1-emu.exe"))
NM = Path(os.environ.get("JIELI_PI32", "C:/JL/pi32")) / "bin" / "pi32v2-nm.exe"
start = int(sys.argv[1]) if len(sys.argv) > 1 else 400_000_000
count = int(sys.argv[2]) if len(sys.argv) > 2 else 2_000_000
elf = ROOT / "build" / "fw" / "monohull.elf"
syms = []
for ln in subprocess.run([str(NM), "-n", str(elf)], capture_output=True, text=True).stdout.splitlines():
    p = ln.split()
    if len(p) == 3 and p[1] in "tT":
        syms.append((int(p[0], 16), p[2]))
addrs = [a for a, _ in syms]
env = dict(os.environ, FM1_TRACE_RANGE=f"{start}:{start + count}")
keys = os.environ.get("FWPROF_KEYS", "500-4000:n7")
proc = subprocess.Popen([str(EMU), "--fwsc", str(ROOT / "build" / "fw" / "monohull.fwsc"), "--ms", "4000",
                         "--keys", keys], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
fn = collections.Counter()
calls = collections.Counter()
entry = {a: n for a, n in syms}
cls = collections.defaultdict(collections.Counter)
n = 0
for ln in proc.stdout:
    if not ln.startswith("T "):
        continue
    p = ln.split()
    pc, c = int(p[3], 16), p[4]
    i = bisect.bisect_right(addrs, pc) - 1
    f = syms[i][1] if i >= 0 else "?"
    fn[f] += 1
    cls[f][c] += 1
    if pc in entry:
        calls[entry[pc]] += 1
    n += 1
proc.wait()
print(f"{n} instructions traced from step {start}")
for f, k in fn.most_common(int(os.environ.get("FWPROF_TOP", "18"))):
    top = ", ".join(f"{c} {v * 100 // k}%" for c, v in cls[f].most_common(4))
    per = f"{k / calls[f]:6.0f}/call" if calls[f] else "          "
    print(f"{k * 100 / n:5.1f}% {f:<22} {per}  {top}")
