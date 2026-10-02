#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# scripts/coreet_lowering_diffsim.py must be able to BUILD AND SIMULATE an
# emitted netlist that `include`s a tracked cgen memory model, from any working
# directory.
#
# WHY.  cgen writes `\`include "cgen_memory_1rd_1wr.v"` into the netlist but
# does NOT write that file; the implementations are tracked under ware/rtl/.
# The harness passed no include path at all, so verilator searched only the
# netlist's own directory and every design with a memory failed to build --
# measured on txfmafrac_top, whose differential came back NOT-MEASURED after a
# clean certificate.  A path resolved from the caller's cwd would have hidden
# the same bug whenever the caller happened to be the repo root, so this test
# runs from somewhere else on purpose.
#
# Four cases, because "it built" is not the property that matters:
#   1. netlist-vs-itself through the memory model     -> DIFFSIM-PASS
#   2. a planted MEMORY-OUTPUT difference             -> DIFFSIM-MISMATCH
#   3. an unresolvable include                        -> NOT-MEASURED, nonzero
#   4. the recorded provenance names the support file it actually simulated
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." 2>/dev/null && pwd)"
DS=""
for c in "${TEST_SRCDIR:-}/_main/scripts/coreet_lowering_diffsim.py" \
         "${TEST_SRCDIR:-}/scripts/coreet_lowering_diffsim.py" \
         "$ROOT/scripts/coreet_lowering_diffsim.py" \
         "scripts/coreet_lowering_diffsim.py"; do
  [ -r "$c" ] && { DS="$(cd "$(dirname "$c")" && pwd)/$(basename "$c")"; break; }
done
[ -n "$DS" ] || { echo "FAIL: cannot find coreet_lowering_diffsim.py"; exit 1; }

# The memory model must be a declared input, not something we hope is nearby:
# the script resolves ware/rtl relative to ITSELF, so under bazel it has to sit
# beside the script in runfiles.
WARE="$(cd "$(dirname "$DS")/../ware/rtl" 2>/dev/null && pwd || true)"
if [ -z "$WARE" ] || [ ! -r "$WARE/cgen_memory_1rd_1wr.v" ]; then
  echo "FAIL: ware/rtl/cgen_memory_1rd_1wr.v is not reachable from $DS"
  echo "      (the sh_test needs //ware:cgen_memory_rtl in its data)"
  exit 1
fi
echo "support model: $WARE/cgen_memory_1rd_1wr.v"

if ! command -v verilator >/dev/null 2>&1; then
  echo "SKIP-REASON: verilator not on PATH; cases 1-4 did NOT execute"
  echo "PASS: diffsim_memory_include_test (SKIPPED, 0 of 4 cases executed)"
  exit 0
fi

T="${TEST_TMPDIR:-$ROOT/generated/tests}/diffsim_memory_include"
rm -rf "$T"; mkdir -p "$T"

# An emitted-netlist-shaped wrapper around the tracked memory: bare `include`,
# ANSI leading-comma ports (what the harness's port parser expects), one clock.
# FWD=1 forwards a same-cycle write to the read port.
emit_dut() {  # $1 file  $2 FWD value  $3 include name
  cat > "$1" <<EOF
\`include "$3"
module memdut(
   input clk
  ,input [3:0] rd_addr
  ,input rd_en
  ,input [3:0] wr_addr
  ,input wr_en
  ,input [7:0] wr_din
  ,output [7:0] q
);
cgen_memory_1rd_1wr #(.BITS(8), .SIZE(16), .FWD($2), .LATENCY_0(1), .WENSIZE(1))
  u_mem (.clk(clk)
        ,.rd_addr_0(rd_addr), .rd_enable_0(rd_en), .rd_dout_0(q)
        ,.wr_addr_0(wr_addr), .wr_enable_0(wr_en), .wr_din_0(wr_din));
endmodule
EOF
}
emit_dut "$T/golden.v" 1 cgen_memory_1rd_1wr.v
emit_dut "$T/nofwd.v"  0 cgen_memory_1rd_1wr.v
emit_dut "$T/noinc.v"  1 cgen_memory_does_not_exist.v

# FROM A DIFFERENT WORKING DIRECTORY.  This is the regression: the old harness
# only ever worked when cwd happened to contain the include.
cd "$T" || { echo "FAIL: cannot cd to $T"; exit 1; }
echo "cwd for all cases: $(pwd)  (deliberately not the repo root)"

run() {  # <impl> <ref> <outdir>
  python3 "$DS" --top memdut --impl "$1" --ref-netlist "$2" --out "$3" \
    --vectors 64 --seed 5 2>&1
}

rc_all=0
echo "--- 1. netlist vs itself, through the tracked memory model ---"
out="$(run "$T/golden.v" "$T/golden.v" "$T/same")"; rc=$?
echo "$out" | sed 's/^/    /'
if [ "$rc" -ne 0 ] || ! echo "$out" | grep -q "DIFFSIM-PASS"; then
  echo "FAIL: a netlist including the tracked memory model did not build+simulate"
  rc_all=1
else
  echo "ok: built and simulated from $(pwd)"
fi

echo "--- 2. a planted MEMORY-OUTPUT difference (FWD 1 vs 0) must be caught ---"
# Not a wrapper-logic mutation: FWD is resolved INSIDE cgen_memory_1rd_1wr, so
# catching this proves the included model is the thing being simulated.
out="$(run "$T/nofwd.v" "$T/golden.v" "$T/mut")"; rc=$?
echo "$out" | sed 's/^/    /'
if [ "$rc" -eq 0 ] || ! echo "$out" | grep -q "DIFFSIM-MISMATCH"; then
  echo "FAIL: a write-forwarding difference inside the memory model was NOT caught"
  rc_all=1
else
  echo "ok: mismatch detected inside the included model"
fi

echo "--- 3. an unresolvable include stays NOT-MEASURED and nonzero ---"
out="$(run "$T/noinc.v" "$T/golden.v" "$T/bad")"; rc=$?
echo "$out" | sed 's/^/    /'
if [ "$rc" -eq 0 ] || ! echo "$out" | grep -q "NOT-MEASURED"; then
  echo "FAIL: a netlist whose include cannot be resolved did not fail closed (rc=$rc)"
  rc_all=1
else
  echo "ok: build failure reported as NOT-MEASURED, rc=$rc"
fi

echo "--- 4. provenance names the support file that was simulated ---"
python3 - "$T/same/diffsim.json" "$WARE/cgen_memory_1rd_1wr.v" <<'PYEOF' || rc_all=1
import hashlib, json, sys
meta = json.load(open(sys.argv[1]))
want = hashlib.sha256(open(sys.argv[2], 'rb').read()).hexdigest()[:16]
ok = True
for side in ('impl_support_sha256_16', 'reference_support_sha256_16'):
    got = meta.get(side)
    if not got:
        print(f"FAIL: diffsim.json has no {side}; the emitted file's hash alone "
              f"does not identify what was simulated")
        ok = False
        continue
    if got.get('cgen_memory_1rd_1wr.v') != want:
        print(f"FAIL: {side} = {got}, expected cgen_memory_1rd_1wr.v={want}")
        ok = False
print("ok: both sides record the support model hash" if ok else "")
sys.exit(0 if ok else 1)
PYEOF
python3 - "$T/bad/diffsim.json" <<'PYEOF' || rc_all=1
import json, sys
meta = json.load(open(sys.argv[1]))
got = meta.get('impl_support_sha256_16', {})
if got.get('cgen_memory_does_not_exist.v') != 'MISSING':
    print(f"FAIL: an unresolvable include should be recorded MISSING, got {got}")
    sys.exit(1)
print("ok: the unresolvable include is recorded MISSING, not omitted")
PYEOF

[ "$rc_all" -eq 0 ] || { echo "FAIL: diffsim_memory_include_test"; exit 1; }
echo "PASS: diffsim_memory_include_test (4 of 4 cases EXECUTED)"
