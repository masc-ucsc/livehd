#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.abc.reset_fanout (pass.usyn.reset_fanout for the USYN mapper): the asynchronous
# clear/preset pins of a region never enter the logic network, so no mapper fanout cap
# bounds them. A reset net past the cap is dealt out through a tree of the library's
# smallest buffer (the clock tree's mechanism).
#
# Fixture lhd/tests/abc_rst_tree.v on both hermetic arst libraries (sky130-shaped
# abc_arst_q.lib: DFFRx1 clear / DFFSx1 preset behind a shared inverter; ASAP7-shaped
# abc_arst_qn.lib: DFFASRNx1 SETN/RESETN): a 32-bit register on one reset input and a
# 16-bit one on a reset computed in the region. For each library:
#   * the default (reset_fanout=0) leaves one net driving every pin of a group and mints
#     no buffer (the contract: the option is opt-in);
#   * reset_fanout=4 bounds every reset net to 4 pins, mints BUFx1 cells named
#     *__rstbuf*, and the netlist is proven against the source graph by the graph-native
#     LEC (cvc5, gensim models) and, on the Verilog netlist, by the independent yosys
#     checker (lgyosys);
#   * a flipped reset-value bit refutes against the buffered netlist;
#   * reset_fanout=1 (a tree that can never shrink) is refused, naming the option.
set -u

MAPPER="${MAPPER:-abc}"
case "$MAPPER" in
  abc | usyn) ;;
  *)
    echo "FAIL: bad MAPPER=$MAPPER (expected abc|usyn)" >&2
    exit 1
    ;;
esac

# One tree, two spellings: the USYN entry's Liberty tmap runs the same region writer.
OPT="pass.$MAPPER.reset_fanout"

LHD="${LHD:-lhd/lhd}"
SRC=lhd/tests/abc_rst_tree.v
TOP=abc_rst_tree
W="${TEST_TMPDIR:-/tmp/lhd_abc_reset_tree_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
[ -f "$SRC" ] || fail "missing fixture $SRC"

# stats <netlist.v> <reset pins...>: "<max pins on one reset net> <BUF rstbuf cells>"
stats() {
  python3 - "$@" <<'PY'
import collections, re, sys
text = open(sys.argv[1]).read()
pins = set(sys.argv[2:])
load = collections.Counter()
bufs = 0
for stmt in text.split(";"):
    stmt = " ".join(stmt.split())
    m = re.match(r"^(\S+) (\S+) ?\((.*)\)$", stmt)
    if not m:
        continue
    cell, inst, body = m.groups()
    if cell == "BUFx1" and "__rstbuf" in inst:
        bufs += 1
    if not cell.startswith("DFF"):
        continue
    for pin, net in re.findall(r"\.(\w+)\(([^()]*)\)", body):
        if pin in pins and not net.startswith("1'h"):
            load[net.strip()] += 1
print(max(load.values()) if load else 0, bufs)
PY
}

# run_lib <tag> <cap>: synthesize with reset_fanout=<cap>, leave the netlist in $W/<tag>_<cap>.
run_lib() {
  local tag="$1" cap="$2" lib="lhd/tests/abc_arst_$1.lib" d="$W/$1_$2"
  mkdir -p "$d"
  local r="$d/r.json"
  run() { "$LHD" "$@" -q --result-json "$r" > "$d/last.log" 2>&1 || fail "$tag/$cap: $* -> $(cat "$r" 2>/dev/null)"; }
  # Flat coloring: the reset tracing and the tree are region-local, and one region
  # holds every register of the fixture.
  run compile "$SRC" --reader slang --top "$TOP" --emit-dir lg:"$d/ref" --workdir "$d/Wc"
  run pass color flat --top "$TOP" lg:"$d/ref" --set color.ware_arith=false \
    --set color.ware_cmp=false --set color.ware_shift=false --workdir "$d/Wcolor"
  run pass "$MAPPER" --top "$TOP" lg:"$d/ref" --emit-dir lg:"$d/net" \
    --emit-dir verilog:"$d/netv" --emit diagnostics:"$d/diag.jsonl" \
    --set synth.liberty="$lib" --set "$OPT"="$cap" --workdir "$d/W"
  ! grep -q '"reset-native"' "$d/diag.jsonl" \
    || fail "$tag/$cap: an async-reset register stayed native: $(grep '"reset-native"' "$d/diag.jsonl")"
  cat "$d/netv/"*.v > "$d/net.v"
  ! grep -q "always @" "$d/net.v" || fail "$tag/$cap: a native flop survived: $(grep 'always @' "$d/net.v")"
}

prove() {
  local tag="$1" cap="$2" lib="lhd/tests/abc_arst_$1.lib" d="$W/$1_$2"
  local r="$d/r.json"
  run() { "$LHD" "$@" -q --result-json "$r" > "$d/last.log" 2>&1 || fail "$tag/$cap: $* -> $(cat "$r" 2>/dev/null)"; }
  run pass liberty gensim "$lib" --emit-dir lg:"$d/models" --emit verilog:"$d/models.v" --workdir "$d/Wm"
  run lec --set formal.effort=proof --impl lg:"$d/net" --ref lg:"$d/ref" --lib lg:"$d/models" --top "$TOP" --workdir "$d/Wlec"
  grep -qE '"verdict":"proven-(full|bounded)"' "$r" || fail "$tag/$cap: graph LEC did not prove: $(cat "$r")"
  { cat "$d/net.v"; echo; cat "$d/models.v"; } > "$d/impl.v"
  { cat "$SRC"; echo; cat "$d/models.v"; } > "$d/ref.v"
  run lec --set formal.effort=proof --impl verilog:"$d/impl.v" --ref verilog:"$d/ref.v" --top "$TOP" --set formal.solver=lgyosys --workdir "$d/Wlgy"
  grep -qE '"verdict":"proven-(full|bounded)"' "$r" || fail "$tag/$cap: lgyosys did not prove the Verilog netlist: $(cat "$r")"
}

# --- defaults: no tree, the nets stay as wide as the groups --------------------
run_lib q 0 || exit 1
run_lib qn 0 || exit 1
read -r qmax qbufs < <(stats "$W/q_0/net.v" RB S)
[ "$qbufs" = 0 ] && [ "$qmax" -ge 16 ] || fail "q default: expected no rstbuf and a net of >= 16 reset pins, got max=$qmax bufs=$qbufs"
read -r nmax nbufs < <(stats "$W/qn_0/net.v" SETN RESETN)
[ "$nbufs" = 0 ] && [ "$nmax" -ge 16 ] || fail "qn default: expected no rstbuf and a net of >= 16 reset pins, got max=$nmax bufs=$nbufs"
echo "PASS: reset_fanout defaults off (max reset net: q=$qmax qn=$nmax pins, no buffers)"

# --- reset_fanout=4: every reset net bounded, proven -----------------------------
run_lib q 4 || exit 1
run_lib qn 4 || exit 1
read -r qmax qbufs < <(stats "$W/q_4/net.v" RB S)
[ "$qmax" -le 4 ] && [ "$qbufs" -ge 8 ] || fail "q cap 4: expected every reset net <= 4 pins and >= 8 rstbuf, got max=$qmax bufs=$qbufs"
read -r nmax nbufs < <(stats "$W/qn_4/net.v" SETN RESETN)
[ "$nmax" -le 4 ] && [ "$nbufs" -ge 8 ] || fail "qn cap 4: expected every reset net <= 4 pins and >= 8 rstbuf, got max=$nmax bufs=$nbufs"
prove q 4 || exit 1
prove qn 4 || exit 1
echo "PASS: reset_fanout=4 bounds every reset net (q max=$qmax... qn max=$nmax pins, $qbufs/$nbufs buffers), cvc5 + lgyosys proven"

# --- a flipped reset value must refute against the buffered netlist -------------
for tag in q qn; do
  d="$W/${tag}_4/neg"
  mkdir -p "$d"
  sed "s/a <= 32'ha5a5a5a5;/a <= 32'ha5a5a5a4;/" "$SRC" > "$d/src.v"
  cmp -s "$SRC" "$d/src.v" && fail "$tag: the broken twin changed nothing"
  "$LHD" compile -q --reader slang --top "$TOP" --emit-dir lg:"$d/ref" --workdir "$d/Wc" -- "$d/src.v" > "$d/c.log" 2>&1 \
    || fail "$tag: neg compile"
  "$LHD" lec --set formal.effort=proof --impl lg:"$W/${tag}_4/net" --ref lg:"$d/ref" --lib lg:"$W/${tag}_4/models" --top "$TOP" \
    --set formal.simfail_run=false --workdir "$d/Wlec" -q --result-json "$d/r.json" > "$d/lec.log" 2>&1
  grep -q '"verdict":"refuted-cex"' "$d/r.json" || fail "$tag: flipped reset value was not refuted: $(cat "$d/r.json")"
done
echo "PASS: a flipped reset value refutes against the buffered netlists"

# --- a cap of 1 would never shrink a tree level: refused, naming the option -------
d="$W/one"
mkdir -p "$d"
"$LHD" pass "$MAPPER" --top "$TOP" lg:"$W/q_0/ref" --emit-dir lg:"$d/net" --set synth.liberty=lhd/tests/abc_arst_q.lib \
  --set "$OPT"=1 --workdir "$d/W" -q --result-json "$d/r.json" > "$d/last.log" 2>&1 \
  && fail "reset_fanout=1 must be refused"
grep -q "reset_fanout" "$d/r.json" "$d/last.log" || fail "reset_fanout=1 refusal must name the option: $(cat "$d/r.json" "$d/last.log")"
echo "PASS: reset_fanout=1 refused"
