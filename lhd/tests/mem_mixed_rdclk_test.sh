#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# PER-PORT memory read timing, at the importer.
#
# graph/cell.hpp documents the Memory `type` pin as a SCALAR -- 0 async, 1 sync,
# 2 combinational array -- but the reader used to write yosys's RD_CLK_ENABLE
# BITMASK into it with `as_int()`. Masks 0 and 1 coincide with the scalar
# meaning by accident, so every all-async and all-sync memory looked correct and
# no existing test could catch this. The failures start at two ports: one async
# + one sync gives mask 0b10 = 2, which ALIASES ONTO "combinational array".
#
# lhd/tests/mem_mixed_rdclk.v has 3 async + 2 sync reads, so RD_CLK_ENABLE is
# 5'11000 = 24. Measured before the fix: pass.lean refused by name (fail-closed,
# and why three CORE-ET modules are gated), but cgen_verilog took the array path
# and emitted a STATELESS combinational lookup -- mem_data zeroed at the top of
# an always_comb, the write folded in, and all five reads combinational with
# both synchronous read registers gone.
#
# So this checks the REPRESENTATION, not just that something was refused: a
# refusal alone would also be produced by a tool that simply rejects mixed
# memories without recording anything.
set -u

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; }
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RTL="$HERE/mem_mixed_rdclk.v"
[ -r "$RTL" ] || { echo "FAIL: missing fixture $RTL"; exit 1; }

T="${TEST_TMPDIR:-$(cd "$HERE/../.." && pwd)/generated/tests}/mem_mixed_rdclk"
rm -rf "$T"; mkdir -p "$T"; cd "$T" || exit 1
fails=0

LIVEHD_MEM_TIMING_DEBUG=1 "$OLDPWD/$LHD" compile verilog "$RTL" --top mem_mixed_rdclk --reader yosys-slang \
  --workdir "$T/w" --emit-dir "lg:$T/lg" > "$T/compile.log" 2>&1 \
  || { echo "FAIL: the fixture did not import"; tail -3 "$T/compile.log"; exit 1; }
echo "ok: the mixed-read fixture imports"

# The input shape itself, so a yosys change that stops producing mask 24 is
# visible as such rather than as a mysterious pass. Its ABSENCE is a failure:
# skipping here would quietly drop the only check that the fixture still
# exercises mixed timing at all.
if [ ! -r "$T/pp-mem.il" ]; then
  echo "FAIL: no pp-mem.il -- cannot confirm the fixture still produces a mixed RD_CLK_ENABLE"
  fails=$((fails+1))
elif grep -q "RD_CLK_ENABLE 5'11000" "$T/pp-mem.il"; then
  echo "ok: yosys reports RD_CLK_ENABLE = 5'11000 (mask 24)"
else
  echo "FAIL: the fixture no longer produces the 3-async/2-sync mask"
  grep -oP "RD_CLK_ENABLE.{0,20}" "$T/pp-mem.il" | head -1 | sed 's/^/      /'
  fails=$((fails+1))
fi

# ---- EXACT per-port timing, read back out of the graph ----------------------
# This is the assertion the rest of the file cannot make. Downstream, async(1)
# and sync(2) are indistinguishable -- every consumer decides "is this a read
# port" with `!is_known_false()` -- so a regression that wrote all five ports as
# 1 would satisfy `type=3` and `rdports=5` and be caught by nothing else.
TIMING="$(grep -oP 'memory read timing: \K.*' "$T/compile.log" | head -1)"
WANT="type=3 rdports=5 port0=async port1=async port2=async port3=sync port4=sync"
if [ -z "$TIMING" ]; then
  echo "FAIL: no per-port timing readback (LIVEHD_MEM_TIMING_DEBUG produced nothing)"
  fails=$((fails+1))
elif [ "$TIMING" = "$WANT" ]; then
  echo "ok: per-port timing reads back exactly: $TIMING"
else
  echo "FAIL: per-port timing mismatch"
  echo "      want: $WANT"
  echo "      got:  $TIMING"
  fails=$((fails+1))
fi

# ---- the Lean certificate: state exists ONLY for the sync read ports -------
# These assertions used to scrape pass.lean's REFUSAL text for `type=`/`rdports=`.
# That message is gone now that the certificate path models per-port timing, and
# a test that depends on a failure message silently stops testing anything the
# moment the failure is fixed. The readback above already pins type and port
# count from the GRAPH; what matters here is the model that comes out.
"$OLDPWD/$LHD" compile "lg:$T/lg" --top mem_mixed_rdclk --workdir "$T/lw" \
  --emit-dir "lean:$T/lean" --set formal.lean.mode=verified_compiler \
  --set formal.lean.strict=true > "$T/lean.log" 2>&1
if [ $? -ne 0 ]; then
  echo "FAIL: pass.lean refused the mixed-timing memory"
  grep -oP '"message":"\K[^"]{0,120}' "$T/lean.log" | head -1 | sed 's/^/      /'
  fails=$((fails+1))
else
  echo "ok: pass.lean emits a certificate for a mixed-timing memory"
  CERT="$T/lean/mem_mixed_rdclk_Lgraph.lean"
  # Exactly TWO synthetic read-data registers: one per SYNC read port. Three
  # (or five) would mean state was allocated for asynchronous outputs, which
  # nothing ever writes; one would mean a sync port lost its register.
  NFLOP="$(sed -n '/flops    :=/,/memories :=/p' "$CERT" | grep -c 'din :=')"
  if [ "$NFLOP" = "2" ]; then
    echo "ok: exactly 2 synthetic read registers (one per sync read port)"
  else
    echo "FAIL: $NFLOP synthetic read register(s), expected 2 -- one per SYNC read port only"
    fails=$((fails+1))
  fi
  NQ="$(grep -c 'SourceDesc.flopQ' "$CERT")"
  [ "$NQ" = "2" ] && echo "ok: exactly 2 flopQ sources, so the async outputs carry no state" \
    || { echo "FAIL: $NQ flopQ source(s), expected 2"; fails=$((fails+1)); }
  NMEM="$(sed -n '/memories :=/,/clocks   :=/p' "$CERT" | grep -c 'aw :=')"
  [ "$NMEM" = "1" ] && echo "ok: one memory image" \
    || { echo "FAIL: $NMEM memory image(s), expected 1"; fails=$((fails+1)); }
fi

# ---- the consumers must FAIL CLOSED until they read per-port timing ---------
# Without this, `type=3` is simply not 1, so cgen's `m.type == 1` sync tests all
# go false and every read silently becomes asynchronous.
"$OLDPWD/$LHD" compile "lg:$T/lg" --top mem_mixed_rdclk --emit "verilog:$T/out.v" \
  --workdir "$T/cg" > "$T/cgen.log" 2>&1
if [ $? -eq 0 ]; then
  echo "FAIL: cgen_verilog accepted a mixed-timing memory; it would emit a combinational array"
  fails=$((fails+1))
elif grep -q "memory-mixed-read-timing" "$T/cgen.log"; then
  echo "ok: cgen_verilog refuses a mixed-timing memory by name"
else
  echo "FAIL: cgen_verilog failed, but not with the mixed-read-timing diagnostic"
  grep -oP '"message":"\K[^"]{0,110}' "$T/cgen.log" | head -1 | sed 's/^/      /'
  fails=$((fails+1))
fi

# cgen_sim has its OWN sync-read decision (`p.rd && m.type == 1`, at nine
# sites), so its refusal needs its own check: a fix to one emitter says nothing
# about the other.
"$OLDPWD/$LHD" compile "lg:$T/lg" --top mem_mixed_rdclk --emit-dir "sim:$T/sim" \
  --workdir "$T/cgs" > "$T/cgen_sim.log" 2>&1
if [ $? -eq 0 ]; then
  echo "FAIL: cgen_sim accepted a mixed-timing memory; it would model every read as async"
  fails=$((fails+1))
elif grep -q "memory-mixed-read-timing" "$T/cgen_sim.log"; then
  echo "ok: cgen_sim refuses a mixed-timing memory by name"
else
  echo "FAIL: cgen_sim failed, but not with the mixed-read-timing diagnostic"
  grep -oP '"message":"\K[^"]{0,110}' "$T/cgen_sim.log" | head -1 | sed 's/^/      /'
  fails=$((fails+1))
fi

# A uniform memory must still work, or a tool that refused ALL memories would
# pass every check above.
cat > "$T/uniform.v" <<'EOF'
module mem_uniform_sync (
   input clk, input we, input [3:0] waddr, input [7:0] wdata
  ,input [3:0] ra0, ra1
  ,output reg [7:0] q0, q1
);
  reg [7:0] mem [0:15];
  always @(posedge clk) begin
    if (we) mem[waddr] <= wdata;
    q0 <= mem[ra0];
    q1 <= mem[ra1];
  end
endmodule
EOF
if "$OLDPWD/$LHD" compile verilog "$T/uniform.v" --top mem_uniform_sync --reader yosys-slang \
     --workdir "$T/uw" --emit "verilog:$T/uniform_out.v" > "$T/uniform.log" 2>&1; then
  echo "ok: an all-synchronous memory still emits (the refusal is not blanket)"
else
  echo "FAIL: a uniform all-sync memory was refused"; tail -2 "$T/uniform.log" | sed 's/^/      /'
  fails=$((fails+1))
fi

[ "$fails" -eq 0 ] || { echo "FAIL: $fails case(s) failed"; exit 1; }
echo "PASS: mem_mixed_rdclk_test"
