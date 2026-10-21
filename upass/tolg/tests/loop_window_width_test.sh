#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Rolled-loop window writes stay the variable's width (review2 L2).
#
# `for i in 0..<8 { v#[i*K + n] = x_n#[i] }` (n in 0..<K, v:U(8K)) keeps the
# K bit writes of one iteration in one lifted body, each in the shift form of
# upass.tolg's shift_window_write. Its `upper` shift was only cut to v's width
# when the window could reach past v's top bit; pass.bitwidth sizes that shift
# by the position's storage envelope instead, so every uncut write widened the
# next write's base by it: at K = 64 the body's widest intermediate was 64961
# bits for a 512-bit v (a K*8K chain; 141 MB of Verilog at K = 256 in one
# build). The cut is unconditional now:
#
#  1. at K = 16 and K = 64 the lifted body's widest net stays within a small
#     constant factor of v's width (the shift itself is v plus the position
#     envelope), so it grows linearly with K, not quadratically;
#  2. the rolled loop is equivalent to the source-unrolled one (K = 8).
set -euo pipefail
trap 'echo "FAIL at line $LINENO" >&2' ERR
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/loop_window_width_$$}"
mkdir -p "$W"
run() { "$LHD" "$@" -q >"$W/latest.log" 2>&1 || { cat "$W/latest.log"; exit 1; }; }

gen() {  # gen <K>: write $W/pack<K>.prp, top pack<K>
  python3 - "$1" "$W" <<'PY'
import sys
k, w = int(sys.argv[1]), sys.argv[2]
ins = ", ".join(f"x{n}:U8" for n in range(k))
lines = [f"pub comb pack{k}({ins}) -> (o:U{8 * k}) {{", f"  mut v:U{8 * k} = 0", "  for i in 0..<8 {"]
lines += [f"    v#[i*{k} + {n}] = x{n}#[i]" for n in range(k)]
lines += ["  }", "  o = v", "}"]
open(f"{w}/pack{k}.prp", "w").write("\n".join(lines) + "\n")
PY
}

widest() {  # widest <verilog file>: the widest declared reg/wire/port
  grep -oE '(reg|wire|input|output)( reg)?( signed)? \[[0-9]+' "$1" | grep -oE '[0-9]+$' | sort -n | tail -1
}

# 1. The widest net grows linearly with K.
for k in 16 64; do
  gen $k
  run compile "$W/pack$k.prp" --top "pack$k" --emit verilog:"$W/pack$k.v" --workdir "$W/c$k" --set lhd.incremental=false
  grep -q '^module .*__loop' "$W/pack$k.v" || { echo "FAIL: pack$k did not roll" >&2; exit 1; }
  wmax=$(( $(widest "$W/pack$k.v") + 1 ))
  vbits=$((8 * k))
  echo "K=$k v=$vbits bits widest=$wmax bits verilog=$(stat -c %s "$W/pack$k.v") bytes"
  [ "$wmax" -le $((4 * vbits)) ] || { echo "FAIL: K=$k widest net $wmax bits for a $vbits-bit v" >&2; exit 1; }
done

# 2. Rolled == unrolled.
gen 8
run compile "$W/pack8.prp" --top pack8 --emit-dir lg:"$W/lg" --workdir "$W/r8" --set lhd.incremental=false
run compile "$W/pack8.prp" --top pack8 --set compile.unroll=true --emit-dir lg:"$W/lgu" --workdir "$W/u8" --set lhd.incremental=false
run lec --impl lg:"$W/lg" --ref lg:"$W/lgu" --top pack8.pack8 --set formal.effort=proof --workdir "$W/lec" \
  --result-json "$W/lec.json"
grep -q '"verdict":"proven-' "$W/lec.json" || { echo "FAIL: rolled vs unrolled not PROVEN: $(cat "$W/lec.json")" >&2; exit 1; }
echo "PASS: rolled window writes stay the variable's width, rolled == unrolled"
