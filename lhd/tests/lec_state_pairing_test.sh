#!/bin/bash
# 2f-lec tier-2 uncertain state correspondence: semdiff full-match pairs are
# injected as UNCERTAIN, PROVEN via the self-certifying inductive proof or a
# reset-established pair-free BMC retry, REFUTED confirmed pair-free (drop-all
# + retry once), bounded bmc PASS never claimed directly with pairs applied,
# and a PASS persists entity-keyed pair hints that warm runs replay without the
# signature pass.
LHD=./bazel-bin/lhd/lhd

[ -x "$LHD" ] || LHD=./lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: lhd binary not found"; exit 1; }
W="${TEST_TMPDIR:-/tmp/lec_state_pairing_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*"; exit 1; }

# Two-stage pipeline; the impl clone renames every register.
cat > "$W/ref.prp" <<'EOF'
mod dut(d:u8) -> (q:u8@[1]) {
  reg ra:u8 = 0
  reg rb:u8 = 0
  q = rb
  rb = ra
  ra = d
}
EOF
sed 's/ra/xa/g; s/rb/xb/g' "$W/ref.prp" > "$W/impl.prp"

# ---------------------------------------------------------------------------
# 1. Renamed pipeline, NO match file: tier-2 pairs both flops, the inductive
#    proof self-certifies, and the disclosure names the uncertain pairs.
# ---------------------------------------------------------------------------
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/impl.prp" --workdir "$W/wd1" 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#1 renamed pipeline should be PROVEN (rc=$RC): $OUT"
echo "$OUT" | grep -q "tier-2 state pairing: 2 uncertain pair(s) injected" || fail "#1 missing injection line: $OUT"
echo "$OUT" | grep -q "PROVEN with 2 uncertain tier-2 pair(s) applied" || fail "#1 missing self-certifying disclosure: $OUT"
grep -q '"pair_hints"' "$W/wd1/formal_cache.json" || fail "#1 pair hint not persisted"
grep -q '"ra", "xa"' "$W/wd1/formal_cache.json" || fail "#1 pair hint content wrong: $(cat "$W/wd1/formal_cache.json")"
echo "PASS: renamed pipeline PROVEN via uncertain tier-2 pairs; pair hint persisted"

# ---------------------------------------------------------------------------
# 2. Warm re-run in the same workdir: the pair hint re-injects the same pair
#    set (same um=[...] key), the verdict cache hits, and the signature pass
#    never runs.
# ---------------------------------------------------------------------------
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/impl.prp" --workdir "$W/wd1" 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#2 warm re-run should be PROVEN (rc=$RC): $OUT"
echo "$OUT" | grep -q "PROVEN (cache)" || fail "#2 warm run should hit the verdict cache: $OUT"
echo "$OUT" | grep -q "tier-2 state pairing" && fail "#2 warm run must skip the signature pass: $OUT"
echo "PASS: warm run replays the pair hint and hits the cache (no signature pass)"

# ---------------------------------------------------------------------------
# 3. Genuinely different renamed design: pairs apply, BMC refutes, the
#    drop-all pair-free re-solve refutes on its own -> a REAL FAIL.
# ---------------------------------------------------------------------------
sed 's/xb = xa/xb = xa ^ 1/' "$W/impl.prp" > "$W/bad.prp"
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/bad.prp" --workdir "$W/wd3" 2>&1)
RC=$?
[ "$RC" -ne 0 ] || fail "#3 a real difference must FAIL: $OUT"
echo "$OUT" | grep -q "tier-2 confirm (REFUTED under 2 uncertain tier-2 pair(s); dropped all, re-solved pair-free)" \
  || fail "#3 missing the pair-free confirmation disclosure: $OUT"
echo "PASS: real difference still FAILs through the pair-free confirming re-solve"

# ---------------------------------------------------------------------------
# 4. Planted BOGUS (crossed) pair hint on the EQUIVALENT pair: ind goes SAT
#    (distrusted), and bmc bounded-proves under the speculative aliases. That
#    result is never trusted directly. The recovery drops every speculative
#    alias and reruns pair-free; with no detected reset, reference power-on state
#    is tracked '?' and implementation power-on remains arbitrary. Only that
#    stronger pair-free bounded proof is accepted.
# ---------------------------------------------------------------------------
plant_crossed_hint() {
  mkdir -p "$1"
  cat > "$1/formal_cache.json" <<'EOF'
{
  "schema": 1,
  "salt": "0000000000000000",
  "verdicts": {},
  "unknowns": {},
  "hints": {},
  "pair_hints": {
    "dut": {"pairs": [["ra", "xb"], ["rb", "xa"]]}
  }
}
EOF
}

# Explicitly name no real reset so this pins the no-reset '?' leg even if a
# frontend injects a conventional reset port into the graph.
plant_crossed_hint "$W/wd4"
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/impl.prp" --workdir "$W/wd4" \
      --set formal.reset=missing_reset 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#4 crossed hint should recover through pair-free no-reset BMC (rc=$RC): $OUT"
echo "$OUT" | grep -q "pair-free BMC from reset/no-reset initialization (dropped 2 uncertain tier-2 pair(s)" \
  || fail "#4 missing pair-free recovery disclosure: $OUT"
echo "$OUT" | grep -q "synthetic ? initialization (no reset)" || fail "#4 missing tracked-? initialization disclosure: $OUT"
echo "$OUT" | grep -q "PROVEN with 2 uncertain" && fail "#4 must not trust the crossed speculative pairs directly: $OUT"
echo "PASS: bogus crossed pairs are discarded; pair-free BMC proves from no-reset '?' initialization"

# ---------------------------------------------------------------------------
# 5. Init-value mismatch: the tier-2 precondition refuses the pair and the
#    report says why; the real divergence (the differing reset) still FAILs
#    through the pair-free confirmation.
# ---------------------------------------------------------------------------
sed 's/reg xa:u8 = 0/reg xa:u8 = 1/' "$W/impl.prp" > "$W/init.prp"
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/init.prp" --workdir "$W/wd5" 2>&1)
RC=$?
[ "$RC" -ne 0 ] || fail "#5 differing reset value is a real difference, must FAIL: $OUT"
echo "$OUT" | grep -q "kind/init mismatch" || fail "#5 missing the init-mismatch unpaired reason: $OUT"
echo "PASS: init-mismatch pair refused with reason; the reset difference still FAILs"

# ---------------------------------------------------------------------------
# 6. All names match: zero tier-2 work (the signature pass never runs).
# ---------------------------------------------------------------------------
cp "$W/ref.prp" "$W/same.prp"
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/same.prp" --workdir "$W/wd6" 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#6 identical design should be PROVEN (rc=$RC): $OUT"
echo "$OUT" | grep -q "tier-2" && fail "#6 all-names-match must do zero tier-2 work: $OUT"
echo "PASS: all-names-match design does zero tier-2 work"

# ---------------------------------------------------------------------------
# 7. formal.lec.state_pairing=false: the pre-tier-2 behavior. Under `auto` the
#    renamed pair still passes, but only BOUNDED (bmc; ind is gated Unknown by
#    the unmatched cut points) — the unbounded ind PROVEN of #1 is exactly
#    what tier-2 buys. Forcing engine=ind shows the gate directly: UNKNOWN.
# ---------------------------------------------------------------------------
#    A bounded result is INCONCLUSIVE by default now, so this case -- whose whole
#    point is that the fallback is BOUNDED -- opts out of strict explicitly. That
#    is the honest shape: it asserts the bounded pass EXISTS, not that a bounded
#    pass is equivalence.
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/impl.prp" --set formal.lec.state_pairing=false \
      --set formal.strict=false 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#7 pairing-off auto run should still bounded-pass (rc=$RC): $OUT"
echo "$OUT" | grep -q "tier-2 state pairing" && fail "#7 pairing ran despite formal.lec.state_pairing=false: $OUT"
echo "$OUT" | grep -q "BOUNDED-Proven" || fail "#7 pairing-off verdict should be the bounded bmc PASS: $OUT"
# The witness-carrying Unknown (matched portion differs through the unmatched
# cuts) escalates in the exit policy — a nonzero exit, but NOT a REFUTED.
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/impl.prp" --set formal.lec.state_pairing=false --set formal.engine=ind 2>&1)
RC=$?
[ "$RC" -ne 0 ] || fail "#7 ind witness-carrying UNKNOWN escalates (rc=$RC): $OUT"
echo "$OUT" | grep -q "UNKNOWN" || fail "#7 ind with pairing off should gate to UNKNOWN: $OUT"
echo "$OUT" | grep -q "REFUTED (not equivalent)" && fail "#7 must not claim REFUTED through unmatched cuts: $OUT"
echo "$OUT" | grep -q "cut point" || fail "#7 the unmatched cut points should be named: $OUT"
echo "PASS: formal.lec.state_pairing=false keeps the pre-tier-2 behavior (bounded auto / ind-UNKNOWN)"

# ---------------------------------------------------------------------------
# 8. Flat path (formal.lec.hier=false): same pairing + proof, and the PASS
#    stores an entity-keyed pair hint there too.
# ---------------------------------------------------------------------------
OUT=$("$LHD" lec --ref "$W/ref.prp" --impl "$W/impl.prp" --set formal.lec.hier=false --workdir "$W/wd8" 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#8 flat path should be PROVEN (rc=$RC): $OUT"
echo "$OUT" | grep -q "tier-2 state pairing: 2 uncertain pair(s) injected" || fail "#8 missing flat-path injection: $OUT"
echo "$OUT" | grep -q "PROVEN with 2 uncertain tier-2 pair(s) applied" || fail "#8 missing flat-path disclosure: $OUT"
grep -q '"ra", "xa"' "$W/wd8/formal_cache.json" || fail "#8 flat-path pair hint not persisted: $(cat "$W/wd8/formal_cache.json")"
echo "PASS: flat (non-hier) path pairs, proves, and persists the hint"

# ---------------------------------------------------------------------------
# 9. The inferred control-state pair is FUNCTIONALLY corresponding but not
#    bit-vector equal: ref stores sel=1 as 1'b1, impl stores it as 2'b11. The
#    speculative ind step therefore cannot close nxt:sd, while BMC under the
#    speculative aliases bounded-proves and would normally be suppressed.
#
#    Both designs have a real reset. The recovery leg drops the speculative
#    aliases, starts renamed state independently, drives reset through the
#    ordinary after_reset prologue, and proves the checked post-reset window.
# ---------------------------------------------------------------------------
cat > "$W/reset_ref.v" <<'EOF'
module dut(
  input clock,
  input reset,
  input sel,
  input [7:0] a,
  input [7:0] b,
  output [8:0] out
);
  reg sd;
  reg [7:0] ad;
  reg [7:0] bd;
  always @(posedge clock) begin
    if (reset) begin
      sd <= 1'b0;
      ad <= 8'b0;
      bd <= 8'b0;
    end else begin
      sd <= sel;
      ad <= a;
      bd <= b;
    end
  end
  assign out = sd ? (ad + 9'd1) : (bd + 9'd2);
endmodule
EOF
cat > "$W/reset_impl.v" <<'EOF'
module dut(
  input signed clock,
  input signed reset,
  input signed sel,
  input signed [7:0] a,
  input signed [7:0] b,
  output [8:0] out
);
  reg [1:0] xs;
  reg [8:0] xa;
  reg [8:0] xb;
  always @(posedge clock) begin
    if (reset) begin
      xs <= 2'b0;
      xa <= 9'b0;
      xb <= 9'b0;
    end else begin
      xs <= sel;
      xa <= {1'b0, a};
      xb <= {1'b0, b};
    end
  end
  assign out = xs ? (xa + 9'd1) : (xb + 9'd2);
endmodule
EOF
OUT=$("$LHD" lec --ref "$W/reset_ref.v" --impl "$W/reset_impl.v" --workdir "$W/wd9" 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#9 reset-established pair-free BMC should PASS (rc=$RC): $OUT"
echo "$OUT" | grep -q "tier-2 state pairing: 3 uncertain pair(s) injected" \
  || fail "#9 missing speculative-pair injection: $OUT"
echo "$OUT" | grep -q "pair-free BMC from reset/no-reset initialization (dropped 3 uncertain tier-2 pair(s)" \
  || fail "#9 missing reset-backed pair-free retry disclosure: $OUT"
echo "$OUT" | grep -q "BOUNDED-Proven" || fail "#9 reset-backed retry should be a bounded BMC proof: $OUT"
echo "$OUT" | grep -q "no primary reset input found" && fail "#9 reset fallback failed to detect reset: $OUT"
echo "PASS: speculative ind failure recovers through pair-free BMC from detected reset"

# ---------------------------------------------------------------------------
# 10. Exact reset-less mod_mux_aligned shape. Generated fallback names
#     flop_24/flop_28/flop_32 look like a numeric register bank, but without an
#     ACTUAL reset they must advance during the prologue rather than be held.
#     Initial reference state is tracked '?' and becomes defined on the first
#     unconditional write; the pair-free BMC then proves the checked window.
# ---------------------------------------------------------------------------
cat > "$W/noreset_ref.v" <<'EOF'
module dut(
  input clock,
  input sel,
  input [7:0] a,
  input [7:0] b,
  output [8:0] out
);
  reg sd;
  reg [7:0] ad;
  reg [7:0] bd;
  always @(posedge clock) begin
    sd <= sel;
    ad <= a;
    bd <= b;
  end
  assign out = sd ? (ad + 9'd1) : (bd + 9'd2);
endmodule
EOF
cat > "$W/noreset_impl.v" <<'EOF'
module dut(
  input signed sel,
  input signed [7:0] a,
  input signed [7:0] b,
  output [8:0] out,
  input signed clock
);
  reg [8:0] flop_24;
  reg [8:0] flop_28;
  reg [1:0] flop_32;
  always @(posedge clock) begin
    flop_24 <= {1'b0, a};
    flop_28 <= {1'b0, b};
    flop_32 <= sel;
  end
  assign out = flop_32 ? (flop_24 + 9'd1) : (flop_28 + 9'd2);
endmodule
EOF
OUT=$("$LHD" lec --ref "$W/noreset_ref.v" --impl "$W/noreset_impl.v" --workdir "$W/wd10" 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#10 reset-less recoded state should PASS through tracked-? BMC (rc=$RC): $OUT"
echo "$OUT" | grep -q "tier-2 state pairing: 3 uncertain pair(s) injected" \
  || fail "#10 missing speculative-pair injection: $OUT"
echo "$OUT" | grep -q "pair-free BMC from reset/no-reset initialization (dropped 3 uncertain tier-2 pair(s)" \
  || fail "#10 missing pair-free no-reset retry disclosure: $OUT"
echo "$OUT" | grep -q "synthetic ? initialization (no reset)" || fail "#10 missing tracked-? initialization: $OUT"
echo "PASS: reset-less mod_mux_aligned shape proves via pair-free tracked-? BMC"

# ---------------------------------------------------------------------------
# 11. Cross-front-end aggregate recoding. The Verilog side keeps validity in
#     one packed shift register and instantiates one parameterized registered
#     leaf definition. Pyrope emits scalar loop replicas (`__liN`) and a
#     primitive-width generic specialization (`pipe_cell__u4`). A full proof
#     requires BOTH contracts: pair/collapse the unique specialization with the
#     elaborated Verilog leaf, then prove the packed/scalar transition relation
#     and establish its base from reset. Neither ind-only nor bounded BMC alone
#     is sufficient.
# ---------------------------------------------------------------------------
cat > "$W/aggregate_ref.v" <<'EOF'
module pipe_cell #(parameter W = 4)(
  input clock, input reset, input [W-1:0] d, output [W-1:0] q
);
  reg [W-1:0] q_r;
  always @(posedge clock) begin
    if (reset) q_r <= {W{1'b0}};
    else       q_r <= d + 1'b1;
  end
  assign q = q_r;
endmodule

module dut(input clock, input reset, input [3:0] d,
           output [3:0] q, output valid);
  wire [3:0] q0;
  pipe_cell #(.W(4)) c0(.clock(clock), .reset(reset), .d(d),  .q(q0));
  pipe_cell #(.W(4)) c1(.clock(clock), .reset(reset), .d(q0), .q(q));
  reg [2:0] valid_pipe;
  always @(posedge clock) begin
    if (reset) valid_pipe <= 3'b000;
    else       valid_pipe <= (valid_pipe << 1) | 3'b001;
  end
  assign valid = valid_pipe[2];
endmodule
EOF
cat > "$W/aggregate_impl.prp" <<'EOF'
pub mod pipe_cell<T>::[timecheck=false](d:T) -> (q:T@[0]) {
  reg q_r = 0
  q = q_r
  const full = d + 1
  q_r = full#[0..=(d.[bits] - 1)]
}

mod valid_stage::[timecheck=false](in_valid:bool, clear:bool) -> (out_valid:bool@[0]) {
  reg valid_r:bool = false
  out_valid = valid_r
  valid_r = if clear { false } else { in_valid }
}

pub mod dut::[timecheck=false](d:u4, reset:bool) -> (q:u4@[0], valid:bool@[0]) {
  mut x:u4 = d
  for i in 0..<2 {
    x = pipe_cell<u4>(d=x).q
  }
  mut v:bool = not reset
  for i in 0..<3 {
    v = valid_stage(in_valid=v, clear=reset)
  }
  q = x
  valid = v
}
EOF
OUT=$("$LHD" lec --ref "$W/aggregate_ref.v" --impl "$W/aggregate_impl.prp" --top dut --workdir "$W/wd11" 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "#11 aggregate recoding should be PROVEN (rc=$RC): $OUT"
echo "$OUT" | grep -q "lec block 'pipe_cell' pass" || fail "#11 primitive specialization leaf was not paired/proven: $OUT"
echo "$OUT" | grep -q "PROVEN by packed/scalar relation" || fail "#11 missing combined base+step proof: $OUT"
echo "$OUT" | grep -q "packed/scalar relation reached from reset" || fail "#11 missing reset-reachable base disclosure: $OUT"
echo "$OUT" | grep -q "PROVEN equivalent" || fail "#11 did not report unbounded equivalence: $OUT"
echo "PASS: primitive generic leaf collapse + reset-established packed/scalar induction"

echo "ALL PASS: lec tier-2 uncertain state correspondence"
exit 0
