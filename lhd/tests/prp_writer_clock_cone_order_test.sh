#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# REGRESSION: a `clock_pin=ref X` / `reset_pin=ref X` emitted by the Pyrope
# writer must bind X's REAL driver — never a hoisted seed — without the writer
# reordering any body statement to get there.
#
# Verilog `always_comb` is ORDER-INDEPENDENT; Pyrope assignment is SEQUENTIAL.
# The writer used to relocate the clock/reset-pin dependency cone ahead of the
# reg declares (a writer-side dependency graph + topological sort + cycle
# repair); that machinery produced two silent miscompiles (lhdsuite fixme.md
# issues 1f and 1f-bis) by hoisting enable-cone statements above the
# always_comb whose values they read. The writer now keeps every statement in
# body order and makes the pin net POSITION-INDEPENDENT instead: a pin net
# that is already a `wire`/`reg` needs nothing (a wire binds any ref to its
# single later driver; a flop Q is order-free state); a single-store `mut` pin
# net gets a bare `wire X` pre-declare; anything else gets a minted
# `wire X__pinw` alias assigned X's final value at the region end.
#
# Case 1 pins the wire-driven ICG shape END-TO-END: emit Pyrope from the
# Verilog, recompile it, and LEC it against the same Verilog compiled
# directly. In minion's `minion_dcache_cache_op_unit_l2` the old relocation
# turned `new_req = (state_q == IDLE) && (state_d != IDLE)` into a read of
# state_d's SEED value — the tautological constant 0 — silently deleting a
# term from the clock gate's enable. A writer that mis-schedules (or
# re-grows a scheduler around) the enable cone refutes here.
#
# Case 2 pins the reg-driven divided-clock shape (`always_ff @(posedge
# div_q)`): a reg used as a clock must keep `clock_pin=ref div_q`, the
# emission must recompile, and — when state_d is emitted as a positional
# `mut` — every read of it must land after its last write. (A lec here would
# be UNKNOWN for an unrelated reason: a reg-driven clock is a derived clock
# the encoder legitimately refuses to model.)

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

# An ICG whose enable cone reads `state_d`, which is computed by an always_comb
# placed AFTER the gate in source order. `clkgt` is a wire driving the flops'
# clock_pin — the shape that made the old cone walk start at a wire.
cat >"$W/icg_order.v" <<'EOF'
// A real ICG cell (latched enable, glitch-free), as designs actually
// instantiate it -- the shape the Clock_cell recognizer handles at the `Sub`
// boundary, and the shape that makes `clkgt` a wire in the emitted Pyrope.
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule

module icg_order (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       req_i,
  output logic [1:0] state_o,
  output logic       busy_o,
  output logic       new_req_o
);
  localparam logic [1:0] IDLE = 2'd0;
  localparam logic [1:0] RUN  = 2'd1;
  localparam logic [1:0] DONE = 2'd2;

  logic [1:0] state_q, state_d;
  logic       new_req;
  logic       clkgt;

  // The gate, and the enable cone that reads `new_req` -> `state_d`.
  clkgate u_cg (.clk_i(clk_i), .en_i(new_req | busy_o), .clk_o(clkgt));

  always_ff @(posedge clkgt or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q <= IDLE;
    end else begin
      state_q <= state_d;
    end
  end

  // `state_d`'s ONLY real writes live here, AFTER the gate above.
  always_comb begin
    state_d = state_q;
    case (state_q)
      IDLE:    if (req_i) state_d = RUN;
      RUN:     state_d = DONE;
      default: state_d = IDLE;
    endcase
  end

  // Reads the FINAL state_d — order-independent in Verilog, sequential in Pyrope.
  always_comb begin
    new_req = (state_q == IDLE) && (state_d != IDLE);
  end

  assign busy_o   = (state_q != IDLE);
  assign state_o  = state_q;
  assign new_req_o = new_req;
endmodule
EOF

TOP=icg_order

"$LHD" compile verilog --top "$TOP" --emit-dir "pyrope:$W/tree" --workdir "$W/wp" \
  -- "$W/icg_order.v" >"$W/emit.log" 2>&1 \
  || { tail -20 "$W/emit.log"; fail "Verilog -> Pyrope emission failed"; }

[ -f "$W/tree/$TOP.prp" ] || fail "no $TOP.prp emitted"

"$LHD" compile verilog --top "$TOP" --emit-dir "lg:$W/ref" --workdir "$W/wr" \
  -- "$W/icg_order.v" >"$W/ref.log" 2>&1 \
  || { tail -20 "$W/ref.log"; fail "Verilog -> lg (reference) failed"; }

"$LHD" compile "$W/tree/$TOP.prp" --top "$TOP" --emit-dir "lg:$W/impl" --workdir "$W/wi" \
  >"$W/impl.log" 2>&1 \
  || { tail -20 "$W/impl.log"; fail "recompile of the emitted Pyrope failed"; }

"$LHD" lec --impl "lg:$W/impl" --ref "lg:$W/ref" --top "$TOP" --workdir "$W/LW" \
  >"$W/lec.log" 2>&1
rc=$?

if [ $rc -ne 0 ]; then
  echo "---- lec output ----"
  grep -E "^lec|REFUTED|counterexample" "$W/lec.log" | head -20
  echo "---- emitted Pyrope ----"
  cat "$W/tree/$TOP.prp"
  fail "round trip is NOT equivalent (lec exit $rc): the writer mis-scheduled the clock cone"
fi

# Whole-word verdict checks: `-w` so UNPROVEN / NOT-PROVEN-style text cannot
# satisfy the PROVEN requirement, plus an explicit rejection of an exit-0
# inconclusive run (the witness-free Unknown branch warns but exits 0).
if grep -qw "REFUTED" "$W/lec.log" || grep -qw "INCONCLUSIVE" "$W/lec.log"; then
  grep -E "^lec" "$W/lec.log" | head -10
  fail "lec exited 0 but the log carries a REFUTED/INCONCLUSIVE verdict"
fi
grep -qw "PROVEN" "$W/lec.log" || {
  grep -E "^lec" "$W/lec.log" | head -10
  fail "lec exited 0 without a PROVEN verdict"
}

# Guard against a VACUOUS pass: the emitted design must still contain the
# enable cone. Two structural tripwires, both immune to the io header:
#  - the internal `new_req` net is used somewhere OTHER than the port list /
#    the `new_req_o` port assignment (its own def or a read),
#  - `state_d` is READ somewhere beyond its own writes (the enable term or
#    the flop next-state both qualify; a design that lost the comb block has
#    neither).
grep -E 'new_req' "$W/tree/$TOP.prp" | grep -v 'new_req_o' | grep -q . \
  || fail "emitted Pyrope has no internal 'new_req' use -- the enable term vanished"
grep -v '^[[:space:]]*state_d = ' "$W/tree/$TOP.prp" | grep -q 'state_d' \
  || fail "emitted Pyrope never reads 'state_d' -- the enable/flop cone vanished"

echo "ok: case 1 (wire-driven ICG clock) round-trips equivalent"

# ---------------------------------------------------------------------------
# Case 2 — the SECOND position-independent class: a `*_pin=ref` naming a REG.
# ---------------------------------------------------------------------------
# A divided clock (`always_ff @(posedge div_q)`) puts a reg name in the pin
# set. The reg is position-independent (its Q is order-free state), so the
# writer must leave everything in body order and keep `clock_pin=ref div_q`
# on the state flops.
cat >"$W/divclk.v" <<'EOF'
module divclk (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       req_i,
  output logic [1:0] state_o,
  output logic       div_o
);
  logic       div_q;
  logic [1:0] state_q, state_d;
  logic       new_req;

  // div_q is a REG used as a clock -> `clock_pin=ref div_q`.
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) div_q <= 1'b0; else div_q <= ~div_q & (new_req | req_i);
  end

  always_ff @(posedge div_q or negedge rst_ni) begin
    if (!rst_ni) state_q <= 2'd0; else state_q <= state_d;
  end

  always_comb begin
    state_d = state_q;
    case (state_q)
      2'd0:    if (req_i) state_d = 2'd1;
      2'd1:    state_d = 2'd2;
      default: state_d = 2'd0;
    endcase
  end

  always_comb new_req = (state_q == 2'd0) && (state_d != 2'd0);

  assign state_o = state_q;
  assign div_o   = div_q;
endmodule
EOF

"$LHD" compile verilog --top divclk --emit-dir "pyrope:$W/dtree" --workdir "$W/dwp" \
  -- "$W/divclk.v" >"$W/demit.log" 2>&1 \
  || { tail -20 "$W/demit.log"; fail "case 2: Verilog -> Pyrope emission failed"; }

P="$W/dtree/divclk.prp"
[ -f "$P" ] || fail "case 2: no divclk.prp emitted"

# The fixture must actually exercise the reg-as-clock path: without this the
# rest of case 2 is a permanent no-op if derived-clock handling ever changes.
grep -q 'clock_pin=ref div_q' "$P" || {
  cat "$P"
  fail "case 2: emitted Pyrope has no 'clock_pin=ref div_q' -- the reg-driven clock shape is gone, the case no longer tests the reg pin path"
}

# The emission must be valid Pyrope: recompile it.
"$LHD" compile "$P" --top divclk --emit-dir "lg:$W/dimpl" --workdir "$W/dwi" \
  >"$W/dimpl.log" 2>&1 \
  || { tail -20 "$W/dimpl.log"; echo "---- emitted Pyrope ----"; cat "$P"; \
       fail "case 2: recompile of the emitted Pyrope failed"; }

# Ordering property, asserted only where order is MEANINGFUL: if `state_d` is
# emitted as a positional `mut` (not a position-independent `wire`), then
# every read of it must land after its last write — Pyrope assignment is
# sequential, so an earlier read would observe the seed, the exact stale-read
# class the deleted relocation machinery used to introduce.
if ! grep -Eq '^[[:space:]]*wire[[:space:]]+state_d' "$P"; then
  last_write=$(grep -n '^[[:space:]]*state_d = ' "$P" | tail -1 | cut -d: -f1)
  first_read=$(grep -n 'state_d' "$P" \
    | grep -v ':[[:space:]]*state_d = ' \
    | grep -v ':[[:space:]]*mut[[:space:]]state_d' \
    | head -1 | cut -d: -f1)
  [ -n "$last_write" ] || { cat "$P"; fail "case 2: no 'state_d =' write found"; }
  [ -n "$first_read" ] || { cat "$P"; fail "case 2: state_d is never read -- the enable/flop cone vanished"; }
  if [ "$first_read" -lt "$last_write" ]; then
    echo "---- emitted Pyrope ----"
    cat "$P"
    fail "case 2: state_d is read (line $first_read) BEFORE its last write (line $last_write) while declared positional -- a stale-read emission"
  fi
fi

echo "ok: case 2 (reg-driven divided clock) keeps its clock_pin and recompiles"

# ---------------------------------------------------------------------------
# Case 3 — a LOCAL net named exactly `clock` is not the implicit clock input.
# ---------------------------------------------------------------------------
# Minion's intpipe_top aliases clk_i through a local `logic clock`. The slang
# reader used to classify clocks by spelling alone, omit clock_pin on every
# state declaration, and thereby make tolg mint an unrelated top-level input
# named `clock`. The local `clock = clk_i` driver then disappeared on reread.
cat >"$W/local_clock_alias.sv" <<'EOF'
module local_clock_alias (
  input  logic clk_i,
  input  logic d,
  output logic q
);
  logic clock;
  assign clock = clk_i;
  always_ff @(posedge clock) q <= d;
endmodule
EOF

"$LHD" compile verilog --top local_clock_alias --emit-dir "pyrope:$W/atree" --workdir "$W/awp" \
  -- "$W/local_clock_alias.sv" >"$W/aemit.log" 2>&1 \
  || { tail -20 "$W/aemit.log"; fail "case 3: Verilog -> Pyrope emission failed"; }

AP="$W/atree/local_clock_alias.prp"
[ -f "$AP" ] || fail "case 3: no local_clock_alias.prp emitted"
grep -q 'clock_pin=ref clock' "$AP" || {
  cat "$AP"
  fail "case 3: a local net named clock was mistaken for the implicit clock input"
}

"$LHD" compile "$AP" --top local_clock_alias --emit "verilog:$W/local_clock_alias.v" --workdir "$W/awi" \
  >"$W/aimpl.log" 2>&1 \
  || { tail -20 "$W/aimpl.log"; cat "$AP"; fail "case 3: emitted Pyrope did not recompile"; }

if grep -Eq '(^|,)[[:space:]]*input[[:space:]]+clock([[:space:],)]|$)' "$W/local_clock_alias.v"; then
  cat "$W/local_clock_alias.v"
  fail "case 3: reread promoted the local clock alias to an unrelated module input"
fi
grep -q 'posedge clk_i' "$W/local_clock_alias.v" || {
  cat "$W/local_clock_alias.v"
  fail "case 3: reread state is not driven by clk_i through the local alias"
}

echo "ok: case 3 (local net named clock) remains bound to clk_i"

echo "PASS: pin nets stay position-independent and local clock aliases remain explicit"
exit 0
