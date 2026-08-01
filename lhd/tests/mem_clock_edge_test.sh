#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# ONE EDGE PER MEMORY -- the yosys reader must refuse a mixed-edge memory by
# name, and must take the surviving edge from EVERY clocked port.
#
# The Memory cell carries a SINGLE global `posclk`. Yosys keeps a bit per port
# (WR_CLK_POLARITY / RD_CLK_POLARITY), and the reader used to take the write
# vector whole -- `getParam(WR_CLK_POLARITY).as_int()` -- while dropping
# RD_CLK_POLARITY outright. Two silent miscompiles fell out of that:
#
#   * mixed edges collapsed to "nonzero == posedge", so a negedge synchronous
#     READ imported as posedge with no diagnostic at all;
#   * a memory with NO write ports (a clocked ROM) has an EMPTY write vector, so
#     `as_int()` returned 0 and every such ROM imported as NEGEDGE.
#
# User ruling 2026-08-01: a memory whose ports do not all commit on the same
# edge is NOT A VALID MEMORY, so this is a named refusal rather than a
# modelling gap. (A latch may mix phases -- that is what the formal phase
# schedule is for; see todo/livehd/2f-latch M10 -- a memory may not.)
#
# Every "it is refused" case is paired with a case that must still COMPILE, or
# a reader that simply rejected all memories would pass the negative half.

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

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() {
  echo "FAIL: $*"
  exit 1
}

# Compile through the YOSYS reader (the only front end that can express per-port
# polarity at all) and emit Verilog, so the surviving edge is observable.
compile_emit() { # <name> <src> <top> -> $W/<name>.log, $W/emit_<name>/
  rm -rf "$W/emit_$1"
  "$LHD" compile "$2" --reader yosys-verilog --top "$3" \
    --emit-dir "verilog:$W/emit_$1" --workdir "$W/w_$1" >"$W/$1.log" 2>&1
  return $?
}

# ---------------------------------------------------------------------------
# 1. Mixed edges: posedge write, negedge read. MUST be refused, BY NAME.
# ---------------------------------------------------------------------------
cat > "$W/mixed.v" <<'EOF'
module mixed(input clk, input [3:0] wa, input we, input [7:0] d,
             input [3:0] ra, output reg [7:0] q);
   reg [7:0] m[15:0];
   always @(posedge clk) if (we) m[wa] <= d;
   always @(negedge clk) q <= m[ra];
endmodule
EOF
if compile_emit mixed "$W/mixed.v" mixed; then
  tail -5 "$W/mixed.log"
  fail "case 1: a memory written on posedge and read on negedge COMPILED -- the read edge was silently dropped"
fi
grep -qa "mixes clock edges" "$W/mixed.log" \
  || { tail -8 "$W/mixed.log"; fail "case 1: refused, but not by NAME -- the diagnostic must say which ports disagree"; }
grep -qa "read port 0" "$W/mixed.log" \
  || { tail -8 "$W/mixed.log"; fail "case 1: the diagnostic does not name the offending port"; }
echo "ok: a mixed-edge memory is refused by name, naming the disagreeing ports"

# ---------------------------------------------------------------------------
# 2. The vacuity guard: the SAME design with both ports on posedge must build.
#    Without this, a reader that refused every memory would pass case 1.
# ---------------------------------------------------------------------------
sed 's/always @(negedge clk) q/always @(posedge clk) q/' "$W/mixed.v" > "$W/same.v"
sed -i.bak 's/module mixed/module same/' "$W/same.v"
compile_emit same "$W/same.v" same \
  || { tail -8 "$W/same.log"; fail "case 2: a single-edge memory must still compile"; }
echo "ok: the same memory with both ports on one edge still compiles"

# ---------------------------------------------------------------------------
# 3. A clocked ROM has NO write ports, so its edge can only come from the READ
#    port. Both polarities must survive the round trip.
# ---------------------------------------------------------------------------
cat > "$W/rom_p.v" <<'EOF'
module rom_p(input clk, input [3:0] ra, output reg [7:0] q);
   reg [7:0] m[15:0];
   initial begin m[0] = 8'd7; m[1] = 8'd9; end
   always @(posedge clk) q <= m[ra];
endmodule
EOF
sed -e 's/rom_p/rom_n/' -e 's/posedge clk) q/negedge clk) q/' "$W/rom_p.v" > "$W/rom_n.v"

compile_emit rom_p "$W/rom_p.v" rom_p \
  || { tail -8 "$W/rom_p.log"; fail "case 3: a posedge-read ROM must compile"; }
grep -qa "posedge" "$W/emit_rom_p"/*.v \
  || { fail "case 3: a posedge-read ROM lost its edge -- with no write ports the polarity must come from the READ port"; }
grep -qa "negedge" "$W/emit_rom_p"/*.v \
  && { fail "case 3: a POSEDGE-read ROM emitted a negedge -- the empty write-polarity vector was read as the memory's edge"; }

compile_emit rom_n "$W/rom_n.v" rom_n \
  || { tail -8 "$W/rom_n.log"; fail "case 3: a negedge-read ROM must compile"; }
grep -qa "negedge" "$W/emit_rom_n"/*.v \
  || { fail "case 3: a negedge-read ROM lost its edge"; }
echo "ok: a clocked ROM takes its edge from the read port, in both polarities"

echo "PASS: mem_clock_edge_test"
exit 0
