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

"$OLDPWD/$LHD" compile verilog "$RTL" --top mem_mixed_rdclk --reader yosys-slang \
  --workdir "$T/w" --emit-dir "lg:$T/lg" > "$T/compile.log" 2>&1 \
  || { echo "FAIL: the fixture did not import"; tail -3 "$T/compile.log"; exit 1; }
echo "ok: the mixed-read fixture imports"

# The input shape itself, so a yosys change that stops producing mask 24 is
# visible as such rather than as a mysterious pass.
if [ -r "$T/pp-mem.il" ]; then
  grep -q "RD_CLK_ENABLE 5'11000" "$T/pp-mem.il" \
    && echo "ok: yosys reports RD_CLK_ENABLE = 5'11000 (mask 24)" \
    || { echo "FAIL: the fixture no longer produces the 3-async/2-sync mask"; fails=$((fails+1)); }
fi

# ---- the representation -----------------------------------------------------
# `type` must be the MIXED SENTINEL (3), never the raw mask (24) and never a
# scalar that claims the memory is uniform.
TYPE="$(grep -oP 'type=\K[0-9]+' "$T/lean.log" 2>/dev/null | head -1)"
if [ -z "$TYPE" ]; then
  "$OLDPWD/$LHD" compile "lg:$T/lg" --top mem_mixed_rdclk --workdir "$T/lw" \
    --emit-dir "lean:$T/lean" --set formal.lean.mode=verified_compiler \
    > "$T/lean.log" 2>&1
  TYPE="$(grep -oP 'type=\K[0-9]+' "$T/lean.log" | head -1)"
fi
case "$TYPE" in
  3)  echo "ok: the mixed memory records type=3 (Memory_type_mixed)" ;;
  24) echo "FAIL: type=24 -- the RD_CLK_ENABLE bitmask is being written into the scalar \`type\` again"; fails=$((fails+1)) ;;
  2)  echo "FAIL: type=2 -- a mixed memory is being recorded as a COMBINATIONAL ARRAY"; fails=$((fails+1)) ;;
  "") echo "FAIL: could not determine the memory type from the emitter diagnostics"; fails=$((fails+1)) ;;
  *)  echo "FAIL: type=$TYPE -- expected the mixed sentinel 3"; fails=$((fails+1)) ;;
esac

# Every read port must carry an explicit, unambiguous timing: async(1) or
# sync(2). A port left at the old boolean 1 for a SYNC read would read as
# "async" and lose its register.
RD="$(grep -oP 'rdports=\K[0-9]+' "$T/lean.log" | head -1)"
[ "$RD" = "5" ] && echo "ok: all 5 read ports are present" \
  || { echo "FAIL: rdports=$RD, expected 5"; fails=$((fails+1)); }

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
