#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Bus-expansion naming standard (core/bus_name.hpp) end to end: `lhd synth`
# splits the 8-bit register `rot` of lhd/tests/lec_busbit.sv into eight
# one-bit DFF cells named `rot[0]..rot[7]` (emitted as the escaped identifiers
# `\rot[i] `). Read back as Verilog with the gensim cell models inlined, each
# bit's state is the model flop one level down (`rot[i].flop_<n>`). Semdiff and
# the LEC bit-blast bridge must regroup those cells into `rot` bit-for-bit, so
# the text-path LEC against the ORIGINAL RTL proves UNBOUNDED with no tier-2
# unpaired state (before the standard it only reached a bounded PASS, every
# cell left unpaired). Pairing is a hint the miter re-verifies:
#   * swapping the D inputs of two bit cells must REFUTE;
#   * inverting one bit cell's D must REFUTE, and so must inverting the D of
#     the unsplit one-bit register `busy` (read back as `busy_cgen1.flop_<n>`:
#     its output port took the plain name);
#   * swapping two bit cells' INSTANCE NAMES (function unchanged, wrong hint)
#     must not refute and must not be claimed by a wrong unbounded pairing.
#
# Hermetic: the vendored inou/prp/tests/abc/test.lib, not a PDK.

set -u

LHD="${LHD:-lhd/lhd}"
LIB=inou/prp/tests/abc/test.lib
SRC=lhd/tests/lec_busbit.sv
W="${TEST_TMPDIR:-/tmp/lhd_lec_busbit_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
[ -f "$LIB" ] || fail "missing liberty $LIB"
[ -f "$SRC" ] || fail "missing fixture $SRC"

run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }
run synth "$SRC" --top busbit --emit-dir lg:"$W/net" --set synth.liberty="$LIB" --workdir "$W/ws"
run compile lg:"$W/net" --top busbit --emit-dir verilog:"$W/netv" --workdir "$W/we"
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --emit verilog:"$W/models.v" --workdir "$W/wm"
cat "$W/netv/"*.v > "$W/net.v"

for b in 0 1 2 3 4 5 6 7; do
  grep -q "^DFF[A-Za-z0-9_]* \\\\rot\\[$b\\] (" "$W/net.v" || fail "no DFF cell named \\rot[$b] in the netlist: $(grep -m3 '^DFF' "$W/net.v")"
done
! grep -q 'rot_[0-7]' "$W/net.v" || fail "netlist still carries a legacy rot_<i> name: $(grep -m3 'rot_[0-7]' "$W/net.v")"
grep -qE "^DFF[A-Za-z0-9_]* busy(_cgen[0-9]+)?\(" "$W/net.v" || fail "no DFF cell for the one-bit register busy: $(grep -m12 '^DFF' "$W/net.v")"
echo "PASS: the 8-bit register maps to DFF cells \\rot[0]..\\rot[7], the one-bit busy keeps its name"

# lec_text <tag> <netlist.v>: the netlist + cell models vs the original RTL.
lec_text() {
  local tag="$1" net="$2"
  cat "$net" "$W/models.v" > "$W/impl_$tag.v"
  "$LHD" lec --impl verilog:"$W/impl_$tag.v" --ref verilog:"$SRC" --top busbit \
      --workdir "$W/wl_$tag" -q --result-json "$W/lec_$tag.json" > "$W/lec_$tag.log" 2>&1
  echo $?
}
verdict() { grep -o '"lec":{[^}]*}' "$1"; }

rc=$(lec_text good "$W/net.v")
[ "$rc" -eq 0 ] || fail "good: text-path LEC exited $rc: $(verdict "$W/lec_good.json")"
grep -q '"verdict":"proven"' "$W/lec_good.json" || fail "good: not PROVEN: $(verdict "$W/lec_good.json")"
grep -q '"bounded":false' "$W/lec_good.json" \
  || fail "good: only BOUNDED -- the rot[i] cells were not paired with rot: $(verdict "$W/lec_good.json")"
! grep -q 'tier-2 unpaired state' "$W/lec_good.log" || fail "good: state left unpaired: $(grep 'unpaired state' "$W/lec_good.log")"
echo "PASS: text-path netlist LEC regroups rot[0..7] into rot and proves unbounded"

# The graph path (mapped lg vs source) keeps the same correspondence.
"$LHD" lec --impl lg:"$W/net" --ref verilog:"$SRC" --lib lg:"$W/models" --top busbit \
    --workdir "$W/wl_graph" -q --result-json "$W/lec_graph.json" > "$W/lec_graph.log" 2>&1 \
  || fail "graph: LEC failed: $(verdict "$W/lec_graph.json")"
grep -q '"bounded":false' "$W/lec_graph.json" || fail "graph: only bounded: $(verdict "$W/lec_graph.json")"
echo "PASS: graph-path netlist LEC proves unbounded"

# mutate <how> <out>: rewrite the D (or the instance name) of \rot[0] / \rot[1].
mutate() {
  python3 - "$1" "$W/net.v" "$2" <<'PY'
import re, sys
how, src, dst = sys.argv[1:4]
text = open(src).read()
cell = {}
for m in re.finditer(r'^(DFF\w*) \\rot\[(\d)\] \(\n\.D\(([^)]*)\)', text, re.M):
    cell[m.group(2)] = m
a, b = cell["0"], cell["1"]
busy = re.search(r'^(DFF\w*) busy(_cgen\d+)?\(\n\.D\(([^)]*)\)', text, re.M)
if how == "busy":      # the unsplit one-bit register loads the complement
    edits = [(busy.span(3), "~(" + busy.group(3) + ")")]
elif how == "swap":      # bit 0 loads bit 1's next state and vice versa
    edits = [(a.span(3), b.group(3)), (b.span(3), a.group(3))]
elif how == "invert":  # bit 0 loads the complement
    edits = [(a.span(3), "~(" + a.group(3) + ")")]
else:                  # rename: same circuit, the two names exchanged
    s0 = a.start(0) + len(a.group(1)) + 1
    s1 = b.start(0) + len(b.group(1)) + 1
    edits = [((s0, s0 + len("\\rot[0]")), "\\rot[1]"), ((s1, s1 + len("\\rot[1]")), "\\rot[0]")]
for (lo, hi), new in sorted(edits, reverse=True):
    text = text[:lo] + new + text[hi:]
open(dst, "w").write(text)
PY
}

# The four mutant checks are independent solver runs: start them together.
for how in swap invert busy rename; do
  mutate "$how" "$W/net_$how.v" || fail "$how: could not build the mutant"
  lec_text "$how" "$W/net_$how.v" > "$W/rc_$how" &
done
wait
for how in swap invert busy; do
  [ "$(cat "$W/rc_$how")" -ne 0 ] || fail "$how: LEC exited 0 on a broken netlist: $(verdict "$W/lec_$how.json")"
  grep -q '"verdict":"refuted"' "$W/lec_$how.json" || fail "$how: expected REFUTED, got $(verdict "$W/lec_$how.json")"
  echo "PASS: $how mutant is REFUTED"
done
! grep -q '"verdict":"refuted"' "$W/lec_rename.json" \
  || fail "rename: an equivalent netlist with exchanged names was REFUTED (pairing treated as an assumption)"
echo "PASS: exchanged bit names are only a wrong hint (no refutation): $(verdict "$W/lec_rename.json")"

echo "PASS: bus-expansion names pair per-bit cells with their register, soundly"
