#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A register array of structs written field by field (SoomRV's pipeline arrays,
# `pl[i] <= 'x; pl[i].valid <= 0;`) becomes ONE wide memory word whose ports
# memory_share merges. Two bugs on that path:
#   * the merged write port's never-written lanes get 'x DATA (WR_EN 0 there),
#     made AFTER the reader's first `setundef -zero`; with yosys.setundef=zero
#     the reader now runs it again after `memory -nomap` (inou_yosys_read.ys).
#     Before, pass.lean refused: "Const node n_12 has X/Z bits" (SoomRV
#     Multiply, CSR, IssueQueue, CacheLineManager, Core);
#   * the read port's data is a concatenation (`{x[35:27], \q, \v, ...}`); a
#     piece that is an already-known wire (here the outputs q and v) was wired
#     to the WHOLE port instead of its slice (lgyosys_tolg.cpp resolve_memory),
#     so q read another entry's lanes: (d<<1|1)&0xff, two cycles early.
# The fixture writes '0, not 'x, so the source has no X and lec is exact.

set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then
    LHD=./bazel-bin/lhd/lhd
  else
    echo "FAIL: could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi

# absolute: the compiles below run inside $W, where the reader also drops its
# pp-mem.il / pp.v debug dumps
LHD="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
fail() {
  echo "FAIL: $*"
  exit 1
}

cat > "$W/ms.sv" <<'VEOF'
module ms(input logic clk, input logic rst, input logic en, input logic [7:0] d,
          output logic [7:0] q, output logic v);
  typedef struct packed { logic [7:0] res; logic valid; } PS;
  PS pl [2:0];
  always_ff @(posedge clk) begin
    for (integer i = 0; i < 3; i = i + 1) begin
      pl[i] <= '0;
      pl[i].valid <= 0;
    end
    if (!rst && en) begin
      pl[0].valid <= 1;
      pl[0].res <= d;
    end
    for (integer i = 1; i < 3; i = i + 1)
      if (pl[i-1].valid) pl[i] <= pl[i-1];
  end
  assign q = pl[2].res;
  assign v = pl[2].valid;
endmodule
VEOF

( cd "$W" && "$LHD" compile ms.sv --reader yosys-slang --top ms --set yosys.setundef=zero --emit-dir "lg:$W/lg" \
  --workdir "$W/cw" >"$W/c.log" 2>&1 ) || { tail -6 "$W/c.log"; fail "compile of ms.sv failed"; }
"$LHD" pass single_edge --top ms "lg:$W/lg" --emit-dir "lg:$W/lg_n" --workdir "$W/pw" >"$W/p.log" 2>&1 \
  || { grep -o '"message":"[^"]*' "$W/p.log" | head -2; fail "pass single_edge refused ms.sv"; }
"$LHD" compile "lg:$W/lg_n" --top ms --workdir "$W/lw" --emit-dir "lean:$W/lean" \
  --set formal.lean.mode=verified_compiler >"$W/lean.log" 2>&1 \
  || { grep -o '"message":"[^"]*' "$W/lean.log" | grep -v DEFERRED | head -2; fail "pass.lean refused the design (an X constant left by memory_share?)"; }
[ -s "$W/lean/ms.dcert" ] || fail "no certificate was written"
echo "ok: the merged write port has no X lanes left; the certificate is written"

# The netlist computes the source: the zeroed lanes are never written, and q/v read their own slice.
( cd "$W" && "$LHD" compile ms.sv --reader yosys-slang --top ms --set yosys.setundef=zero --emit "verilog:$W/out.v" \
  --workdir "$W/vw" -q >"$W/v.log" 2>&1 ) || { tail -4 "$W/v.log"; fail "verilog emission failed"; }
"$LHD" lec --impl "verilog:$W/out.v" --ref "verilog:$W/ms.sv" --top ms --workdir "$W/lec" -q >"$W/lec.log" 2>&1 \
  || { tail -3 "$W/lec.log"; fail "the netlist is not equivalent to the source"; }
echo "PASS: yosys_memory_share_undef_test"
