#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# ONE EDGE PER MEMORY -- a mixed-edge memory must PARSE but be refused by FORMAL
# by name, and a single-edge memory must take its edge from EVERY clocked port.
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
# User ruling 2026-08-02: a memory whose ports do not all commit on the same
# edge is a shape LiveHD does not model, but the LANGUAGE allows it -- so it is
# a FORMAL error, not a read error. It parses and regenerates (blackbox: fine to
# do strange things), `lhd lec` refuses it BY NAME, and the user opts back in
# per memory with `--set formal.ignore_memory=<name>`, which blackboxes it: the
# reads become one shared free symbol per (port, cycle) across the two designs
# and the contents are never compared. (A latch may mix phases -- that is what
# the formal phase schedule is for; see todo/livehd/2f-latch M10.)
#
# Every "it is refused" case is paired with a case that must still pass, or a
# tool that simply rejected all memories would pass the negative half.

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
compile_emit() { # <name> <src> <top> -> $W/<name>.log, $W/emit_<name>/ (Verilog), $W/lg_<name>/ (IR)
  rm -rf "$W/emit_$1" "$W/lg_$1"
  "$LHD" compile "$2" --reader yosys-verilog --top "$3" \
    --emit-dir "verilog:$W/emit_$1" --emit-dir "lg:$W/lg_$1" --workdir "$W/w_$1" >"$W/$1.log" 2>&1
  return $?
}

# ---------------------------------------------------------------------------
# 1. Mixed edges: posedge write, negedge read. MUST be refused, BY NAME.
# ---------------------------------------------------------------------------
# `dbg` is deliberate: it is logic OUTSIDE the memory, so case 1d can introduce a
# real difference that the ignored memory must NOT swallow.
cat > "$W/mixed.v" <<'EOF'
module mixed(input clk, input [3:0] wa, input we, input [7:0] d,
             input [3:0] ra, output reg [7:0] q, output [7:0] dbg);
   reg [7:0] m[15:0];
   always @(posedge clk) if (we) m[wa] <= d;
   always @(negedge clk) q <= m[ra];
   assign dbg = d ^ 8'hFF;
endmodule
EOF
compile_emit mixed "$W/mixed.v" mixed \
  || { tail -8 "$W/mixed.log"; fail "case 1: a mixed-edge memory must PARSE (the language allows it); only FORMAL refuses it"; }
grep -qa "mixes clock edges" "$W/mixed.log" \
  || { tail -8 "$W/mixed.log"; fail "case 1: parsed, but SILENTLY -- the lost per-port edges must be warned about where they are lost"; }
grep -qa "read port 0" "$W/mixed.log" \
  || { tail -8 "$W/mixed.log"; fail "case 1: the warning does not name the offending port"; }
echo "ok: a mixed-edge memory parses, with a warning naming the disagreeing ports"

# ---------------------------------------------------------------------------
# 1b. ...and FORMAL refuses it by name, at exit 7 (could-not-decide), never 10.
# ---------------------------------------------------------------------------
sed 's/dbg = d ^ 8.hFF/dbg = ~(~(d ^ 8\x27hFF))/' "$W/mixed.v" > "$W/mixed_eq.v"
compile_emit mixed_eq "$W/mixed_eq.v" mixed \
  || { tail -8 "$W/mixed_eq.log"; fail "case 1b: the equivalent variant must compile"; }
LOUT=$("$LHD" lec --ref "lg:$W/lg_mixed" --impl "lg:$W/lg_mixed_eq" --top mixed \
       --workdir "$W/lw1" 2>&1); LRC=$?
if [ "$LRC" -eq 0 ]; then
  fail "case 1b: formal PASSED a memory whose per-port clock edges it cannot model"
elif [ "$LRC" -eq 10 ]; then
  fail "case 1b: formal reported exit 10 (a counterexample) for a shape it merely cannot MODEL; rc 7 and rc 10 must never be conflated"
fi
grep -qa "PER-PORT clock edge polarity" <<<"$LOUT" \
  || { tail -8 <<<"$LOUT"; fail "case 1b: refused, but not BY NAME"; }
echo "ok: formal refuses a mixed-edge memory by name at rc=$LRC (not a counterexample)"

# ---------------------------------------------------------------------------
# 1c. The diagnostic's own suggestion must WORK VERBATIM, the run must then
#     PROVE, and the pass must DISCLOSE that a memory was blackboxed.
# ---------------------------------------------------------------------------
SUG=$(grep -oaE "formal.ignore_memory=[A-Za-z0-9_:.]+" <<<"$LOUT" | head -1)
[ -n "$SUG" ] || fail "case 1c: the refusal does not tell the user how to proceed"
IOUT=$("$LHD" lec --ref "lg:$W/lg_mixed" --impl "lg:$W/lg_mixed_eq" --top mixed \
       --workdir "$W/lw2" --set "$SUG" 2>&1); IRC=$?
[ "$IRC" -eq 0 ] \
  || { tail -8 <<<"$IOUT"; fail "case 1c: '--set $SUG' -- the tool's OWN suggestion -- did not let the run proceed (rc=$IRC)"; }
grep -qa "memory(ies) IGNORED" <<<"$IOUT" \
  || { tail -8 <<<"$IOUT"; fail "case 1c: an ignore-assisted PASS was not DISCLOSED; it reads as an unconditional proof"; }
echo "ok: the suggested ignore works verbatim, proves, and the pass is disclosed"

# ---------------------------------------------------------------------------
# 1d. SOUNDNESS: ignoring a memory must not swallow a difference OUTSIDE it.
#     Without this, "exclude from formal" would be indistinguishable from
#     "disable formal".
# ---------------------------------------------------------------------------
sed 's/dbg = d ^ 8.hFF/dbg = d ^ 8\x27hF0/' "$W/mixed.v" > "$W/mixed_bad.v"
compile_emit mixed_bad "$W/mixed_bad.v" mixed \
  || { tail -8 "$W/mixed_bad.log"; fail "case 1d: the differing variant must compile"; }
BOUT=$("$LHD" lec --ref "lg:$W/lg_mixed" --impl "lg:$W/lg_mixed_bad" --top mixed \
       --workdir "$W/lw3" --set "$SUG" 2>&1); BRC=$?
if [ "$BRC" -eq 0 ]; then
  tail -8 <<<"$BOUT"; fail "case 1d: an ignored memory SWALLOWED a real difference in the logic around it"
fi
grep -qaiE "refut|not equivalent|equiv_fail" <<<"$BOUT" \
  || { tail -8 <<<"$BOUT"; fail "case 1d: expected a REFUTATION outside the ignored memory"; }
echo "ok: an ignored memory does not hide a difference in the logic around it"

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
