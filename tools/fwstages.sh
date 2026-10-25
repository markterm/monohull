#!/bin/sh
# (On two cores the counters add both cores' time: use tools/fwload.py for the load.)
# Per-stage cost of the synth on fm1-emu (a profiling build: MH_PROFILE=1 python tools/build_fw.py),
# a note held 2.5 s: instructions per output sample. Args: extra --keys entries (comma-led), e.g. ",600:K1:+20"
set -eu
cd "$(dirname "$0")/.."
EMU=${FM1_EMU:-../mvave/third_party/fm1-emu/target/release/fm1-emu.exe}
A=$(/c/JL/pi32/bin/pi32v2-nm.exe build/fw/monohull.elf | awk '/ mh_prof$/{print $1}')
"$EMU" --fwsc build/fw/monohull.fwsc --ms 3000 --keys "500-3000:n7${1:-}" --dump "0x$A" 6 2>&1 |
python -c "
import sys
L = sys.stdin.read().splitlines()
w = [int(x, 16) * 10 for x in L[[i for i, l in enumerate(L) if l.startswith('dump ')][0] + 1].split()[1:]]
n = 2.5 * 44117.6
for k, v in zip(['control', 'mod', 'sources', 'drive', 'filter', 'output'], w):
    print(f'{k:8} {v / n:7.0f}')
print(f'total    {sum(w) / n:7.0f} instr/sample = {sum(w) / n / 5440 * 100:.0f}% of a core')
print([l for l in L if 'in an ISR' in l][0])"
