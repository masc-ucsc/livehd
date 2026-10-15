#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A packed struct muxed through an instance and sliced back, where one field
# of the mux output feeds ANOTHER field of the mux input (br_amba_axi_shrinker:
# `shift` -> shifted data -> `data`). It is a loop per WORD and none per BIT,
# so it must prove -- through both ways it can reach the encoder:
#   legalize  the mux is state-free, so pass.legalize inlines it; the packed
#             loop it leaves in the top shares a component with a stateful
#             instance (`dff`) and must still be split there.
#   splice    the mux holds state, so legalize keeps the instance; the impl has
#             no `pmux`, so lhd lec splices it into the ref top and must re-apply
#             legalize's packed-slice repair to that host.
# A wrong impl is still a counterexample, and a REAL comb loop is never proven.
set -eu
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi
W="${TEST_TMPDIR:-/tmp/lec_packed_false_loop_$$}"
mkdir -p "$W"

TOP_PORTS='
    input  logic       clk,
    input  logic       sel,
    input  logic [3:0] a,
    input  logic [1:0] sh0,
    input  logic [1:0] sh1,
    output logic [3:0] y,
    output logic       last'

cat > "$W/legalize_ref.sv" <<RTL
module pmux (
    input  logic            sel,
    input  logic [1:0][6:0] in,
    output logic [6:0]      out
);
  always_comb begin
    out = '0;
    for (int i = 0; i < 2; i++) begin
      out |= ({7{sel == i}} & in[i]);
    end
  end
endmodule

module dff (
    input  logic       clk,
    input  logic [3:0] d,
    output logic [3:0] q
);
  always_ff @(posedge clk) q <= d;
endmodule

module top ($TOP_PORTS
);
  typedef struct packed {
    logic [1:0] shift;
    logic [3:0] data;
    logic       last;
  } payload_t;

  payload_t [1:0] pin;
  payload_t       pout;
  logic [3:0]     q;
  logic [3:0]     shifted;

  pmux m (.sel(sel), .in(pin), .out(pout));
  dff r (.clk(clk), .d(pout.data), .q(q));

  assign shifted = q << pout.shift;
  assign pin[0]  = '{shift: sh0, data: shifted, last: sh0[0]};
  assign pin[1]  = '{shift: sh1, data: shifted ^ a, last: sh1[1]};
  assign y       = pout.data;
  assign last    = pout.last;
endmodule
RTL

# The same function, flat except for the shared register module.
legalize_impl() {
  cat <<RTL
module dff (
    input  logic       clk,
    input  logic [3:0] d,
    output logic [3:0] q
);
  always_ff @(posedge clk) q <= d;
endmodule

module top ($TOP_PORTS
);
  logic [3:0] q;
  logic [3:0] shifted;
  logic [3:0] data;

  assign shifted = q << (sel ? sh1 : sh0);
  assign data    = sel ? (shifted ^ a) : $1;
  dff r (.clk(clk), .d(data), .q(q));
  assign y    = data;
  assign last = sel ? sh1[1] : sh0[0];
endmodule
RTL
}
legalize_impl shifted > "$W/legalize_impl.sv"
legalize_impl '~shifted' > "$W/legalize_bad.sv"

cat > "$W/splice_ref.sv" <<RTL
module pmux (
    input  logic            clk,
    input  logic            sel,
    input  logic [1:0][6:0] in,
    output logic [6:0]      out,
    output logic            seen
);
  always_comb begin
    out = '0;
    for (int i = 0; i < 2; i++) begin
      out |= ({7{sel == i}} & in[i]);
    end
  end
  always_ff @(posedge clk) seen <= sel;
endmodule

module top ($TOP_PORTS,
    output logic       seen
);
  typedef struct packed {
    logic [1:0] shift;
    logic [3:0] data;
    logic       last;
  } payload_t;

  payload_t [1:0] pin;
  payload_t       pout;
  logic [3:0]     shifted;

  pmux m (.clk(clk), .sel(sel), .in(pin), .out(pout), .seen(seen));

  assign shifted = a << pout.shift;
  assign pin[0]  = '{shift: sh0, data: shifted, last: sh0[0]};
  assign pin[1]  = '{shift: sh1, data: ~shifted, last: sh1[1]};
  assign y       = pout.data;
  assign last    = pout.last;
endmodule
RTL

splice_impl() {
  cat <<RTL
module top ($TOP_PORTS,
    output logic       seen
);
  logic [3:0] shifted;
  logic       m_seen;

  assign shifted = a << (sel ? sh1 : sh0);
  assign y       = sel ? ~shifted : $1;
  assign last    = sel ? sh1[1] : sh0[0];
  always_ff @(posedge clk) m_seen <= sel;
  assign seen = m_seen;
endmodule
RTL
}
splice_impl shifted > "$W/splice_impl.sv"
splice_impl '(shifted ^ a)' > "$W/splice_bad.sv"

# A REAL loop through an instance: w[0] = ~(w[0] ^ b[0]).
cat > "$W/real_ref.sv" <<'RTL'
module inv (
    input  logic [1:0] a,
    output logic [1:0] y
);
  assign y = ~a;
endmodule

module top (
    input  logic [1:0] b,
    output logic [1:0] y
);
  logic [1:0] w;
  inv u (.a({b[1], w[0] ^ b[0]}), .y(w));
  assign y = w;
endmodule
RTL
cat > "$W/real_impl.sv" <<'RTL'
module top (
    input  logic [1:0] b,
    output logic [1:0] y
);
  assign y = ~b;
endmodule
RTL

lec() {  # lec <name> <ref> <impl>
  local rc=0
  # simfail_run=false: the verdict is the point, not the replayed waveform.
  "$LHD" lec --set formal.effort=proof --set formal.simfail_run=false --ref "$W/$2" --impl "$W/$3" --top top \
    --workdir "$W/w_$1" --result-json "$W/$1.json" > "$W/$1.log" 2>&1 || rc=$?
  echo "$rc"
}

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

for flow in legalize splice; do
  rc=$(lec "$flow" "${flow}_ref.sv" "${flow}_impl.sv")
  if [ "$rc" -ne 0 ] || ! grep -q '"verdict":"proven-full"' "$W/$flow.json"; then
    cat "$W/$flow.log" >&2
    fail "$flow: a word-level-only packed loop must be PROVEN-full (rc=$rc)"
  fi
  rc=$(lec "${flow}_bad" "${flow}_ref.sv" "${flow}_bad.sv")
  if [ "$rc" -ne 10 ] || ! grep -q '"verdict":"refuted-cex"' "$W/${flow}_bad.json"; then
    cat "$W/${flow}_bad.log" >&2
    fail "$flow: a wrong impl must be REFUTED-cex (rc=$rc, want 10)"
  fi
done

# The real loop is never a verdict: pass.legalize refuses it before any engine.
rc=$(lec real real_ref.sv real_impl.sv)
if [ "$rc" -eq 0 ] || grep -q '"verdict":"proven' "$W/real.json" || ! grep -q '"code":"comb-loop"' "$W/real.log"; then
  cat "$W/real.log" >&2
  fail "a real combinational loop must be refused as comb-loop, never proven (rc=$rc)"
fi

echo 'PASS: packed false loops prove through legalize and the absent-def splice; a real loop is refused'
