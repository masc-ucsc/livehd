#!/usr/bin/env bash
set -euo pipefail
# M3 self-test: unbounded invariants, proved by sym_verify
# (pass/lean/tools/drivers/sym_verify.lean). Refinement with stuttering is
# tested by refmap_selftest.sh (sym_refmap).
#
# Small RTL fixtures, each through the real flow (yosys-slang -> LGraph ->
# DCERT2), then:
#   cnt10  mod-10 counter
#     c <= 9                      PROVED (k=1); also with the check re-building
#                                 the obligations (`rebuild`), closed (`closed`),
#                                 on the plain memory interpreter (`route:plain`),
#                                 and as the F2 theorem file (`emit:`), compiled;
#                                 closed and emitted also on ir and vc (`runInvKOn`)
#     c != 9                      NOT K-INDUCTIVE at k=1; COUNTEREXAMPLE at k=10
#     c == 0 while reset held     PROVED (k=1); NOT K-INDUCTIVE without the assumption
#     candidates c != 9, c <= 9   one specialization; the first is not
#                                 1-inductive, the second PROVED
# Every PROVED is the verified check (Sym.runInvKOf / Sym.runConjTOf, by
# default through *_of_obls over the obligations already built)
# accepting LRAT proofs from the untrusted solver. A run with several
# attempts is judged by its LAST verdict line.
#
#   ram4 / ram4b  a 4-word RAM; writes never store 0xff (ram4b: except into word 0)
#     word 0 != 0xff              PROVED (k=1) / COUNTEREXAMPLE (base); closed on every
#                                 route, and emitted on vc, compiled
#
# The symbolic semantics of operators the routes used to refuse (fp's mulI / divI / modI;
# Op_GetMask / Op_SetMask with a mask that is an input, on every route). Each has a PROVED case
# and a mutation that must fail:
#   mul4   q = a*b: q[0] == a[0]&b[0] PROVED, q[0] == a[0]|b[0] COUNTEREXAMPLE;
#          the registered product <= 225 PROVED
#   udiv4  unsigned q = a/b, r = a - q*b: when b != 0, q <= a and r < b PROVED;
#          q < a COUNTEREXAMPLE
#   sdiv4  signed q = a/b: the sign of a nonzero q is sign(a) xor sign(b), except -8/-1, PROVED;
#          without the exception COUNTEREXAMPLE
#   rem3   signed a % 3 (truncated): a negative a gives a negative or zero remainder, -2 <= r <= 2,
#          -1 % 3 == -1, and the registered remainder read signed is > -3, PROVED; the sign bit
#          is never set, r >= 0, and -1 % 3 == 2 (Euclidean), each COUNTEREXAMPLE
#   getmask_sym / setmask_sym (built by tests/gen_gap_certs.lean): when the input mask equals
#          0xA5, the result equals that of the constant mask 0xA5 (output 0) PROVED; compared
#          with 0xA4 (output 1) COUNTEREXAMPLE
#   bulk_ok / bulk_bad: a whole-memory load from a bus, read back onto a bus, equals the bus
#          PROVED; with the load enable an input COUNTEREXAMPLE (on fp: mulI / divI / modI by
#          powers of two)
#   mul4 bad_lsb on ir with sf:1 wf:1: fp cannot specialize the replay simulator, so the run
#          is COUNTEREXAMPLE NOT REPLAYED and names that reason (not "did not reproduce")
#   tests/fp_symbolic.lean checks fp's symbolic primitives and terms against evalPrim / evalFuel.
# The default route is vc (`sim:` absent); a run naming closed / emit: / route: and no sim: uses fp.
# ROUTES (default "fp ir vc"): every sym_verify case that names no `emit:` or `route:` also
# runs on the IR and VC symbolic routes (`sim:ir`, `sim:vc`) and must give the same verdict
# (`closed` included: each route's closed check). `emit:` on ir and vc is tested by name.
#
# SYM_NAT=<dir with a native sym_verify> uses it; otherwise the
# drivers run interpreted against formal/lean/.olean-dev (build it with
# formal/lean/.build-proj.sh). The Lean toolchain pin lives in formal/lean.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/sym_selftest}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
SYM_NAT="${SYM_NAT:-}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"

cat > "$OUT/cnt10.sv" <<'EOF'
module cnt10(input clk, input rst, output ok_good, output ok_bad, output [3:0] c_o);
  reg [3:0] c;
  always @(posedge clk) if (rst || c == 4'd9) c <= 4'd0; else c <= c + 4'd1;
  assign ok_good = (c <= 4'd9);
  assign ok_bad  = (c != 4'd9);
  assign c_o = c;
endmodule
EOF
# M8 conjunctive induction: two counters kept in step, and a delayed copy.
# eq_ok holds only with a_ok in range; lag_ok also needs itself (en = 0 holds).
cat > "$OUT/pair.sv" <<'EOF'
module pair(input clk, input rst, input en,
            output a_ok, output b_ok, output eq_ok, output lag_ok, output [3:0] a_o);
  reg [3:0] a;    // 0..9, counting up
  reg [3:0] b;    // (10 - a) mod 10, counting down
  reg [3:0] a_d;  // a, one enabled step late
  always @(posedge clk) begin
    if (rst) begin a <= 4'd0; b <= 4'd0; a_d <= 4'd0; end
    else if (en) begin
      a   <= (a == 4'd9) ? 4'd0 : a + 4'd1;
      b   <= (b == 4'd0) ? 4'd9 : b - 4'd1;
      a_d <= a;
    end
  end
  assign a_ok   = a <= 4'd9;
  assign b_ok   = b <= 4'd9;
  assign eq_ok  = (a == 4'd0) ? (b == 4'd0) : ({1'b0, a} + {1'b0, b} == 5'd10);
  assign lag_ok = a_d <= 4'd9;
  assign a_o = a;
endmodule
EOF
printf '%s\n' 'a_ok a_ok' 'b_ok b_ok' 'eq_ok eq_ok' 'lag_ok lag_ok' > "$OUT/pair_all.spec"
printf '%s\n' 'a_ok a_ok hyps: a_ok' 'b_ok b_ok hyps: b_ok' 'eq_ok eq_ok hyps: eq_ok a_ok' \
  'lag_ok lag_ok hyps: lag_ok a_ok' > "$OUT/pair_min.spec"
printf '%s\n' 'a_ok a_ok hyps: a_ok' 'b_ok b_ok hyps: b_ok' 'eq_ok eq_ok hyps: eq_ok' \
  'lag_ok lag_ok hyps: lag_ok a_ok' > "$OUT/pair_missing.spec"
printf '%s\n' 'a_ok a_ok' 'never !a_ok' > "$OUT/pair_false.spec"
# M8 register facts and case splits: two cores running one FSM, core B's states
# encoded one higher; their data registers are equal, checked per joint state.
cat > "$OUT/twocore.sv" <<'EOF'
module twocore(input clk, input rst, input go, input [7:0] x, output [7:0] dA_o, output [7:0] dB_o);
  reg [1:0] sA; reg [7:0] dA;
  always @(posedge clk) begin
    if (rst) begin sA <= 2'd0; dA <= 8'd0; end
    else begin
      sA <= (sA == 2'd0) ? (go ? 2'd1 : 2'd0) : (sA == 2'd1) ? 2'd2 : 2'd0;
      if (sA == 2'd1) dA <= dA + x;
      else if (sA == 2'd2) dA <= dA ^ x;
    end
  end
  reg [1:0] sB; reg [7:0] dB;
  always @(posedge clk) begin
    if (rst) begin sB <= 2'd1; dB <= 8'd0; end
    else begin
      case (sB)
        2'd2: sB <= 2'd3;
        2'd3: sB <= 2'd1;
        default: sB <= go ? 2'd2 : 2'd1;
      endcase
      dB <= (sB == 2'd2) ? x + dB : (sB == 2'd3) ? x ^ dB : dB;
    end
  end
  assign dA_o = dA;
  assign dB_o = dB;
endmodule
EOF
printf '%s\n' 's_dom oneof sA=0,sB=0;sA=0,sB=1;sA=1,sB=2;sA=2,sB=3' 'd_eq eq dA dB split: s_dom' > "$OUT/tc_split.spec"
printf '%s\n' 's_dom oneof sA=0,sB=0;sA=0,sB=1;sA=1,sB=2;sA=2,sB=2;sA=2,sB=3' 'd_eq eq dA dB split: s_dom' > "$OUT/tc_bad.spec"
cat > "$OUT/ram4.sv" <<'EOF'
module ram4(input clk, input we, input [1:0] wa, input [7:0] wd, output ok, output [7:0] w0_o);
  reg [7:0] mem [0:3];
  always @(posedge clk) if (we) mem[wa] <= (wd == 8'hff) ? 8'hfe : wd;
  assign w0_o = mem[0];
  assign ok = mem[0] != 8'hff;
endmodule
EOF
cat > "$OUT/ram4b.sv" <<'EOF'
module ram4b(input clk, input we, input [1:0] wa, input [7:0] wd, output ok, output [7:0] w0_o);
  reg [7:0] mem [0:3];
  always @(posedge clk) if (we) mem[wa] <= (wd == 8'hff && wa != 2'd0) ? 8'hfe : wd;
  assign w0_o = mem[0];
  assign ok = mem[0] != 8'hff;
endmodule
EOF
cat > "$OUT/mul4.sv" <<'EOF'
module mul4(input clk, input [3:0] a, input [3:0] b, output ok_lsb, output bad_lsb, output ok_max,
            output [7:0] p_o);
  wire [7:0] q = a * b;
  reg [7:0] p;
  always @(posedge clk) p <= a * b;
  assign ok_lsb = q[0] == (a[0] & b[0]);
  assign bad_lsb = q[0] == (a[0] | b[0]);
  assign ok_max = p <= 8'd225;
  assign p_o = p;
endmodule
EOF
cat > "$OUT/udiv4.sv" <<'EOF'
module udiv4(input clk, input [3:0] a, input [3:0] b, output ok_q, output ok_r, output bad_q,
             output [3:0] q_o);
  wire [3:0] q = a / b;
  wire [3:0] r = a - q * b;  // the exporter refuses `%` by a divisor that can be zero
  reg [3:0] qr;
  always @(posedge clk) qr <= q;
  assign ok_q = (b == 4'd0) | (q <= a);
  assign ok_r = (b == 4'd0) | (r < b);
  assign bad_q = (b == 4'd0) | (q < a);
  assign q_o = qr;
endmodule
EOF
cat > "$OUT/sdiv4.sv" <<'EOF'
module sdiv4(input clk, input signed [3:0] a, input signed [3:0] b, output ok_sign,
             output bad_sign, output [3:0] q_o);
  wire signed [3:0] q = a / b;
  reg [3:0] qr;
  always @(posedge clk) qr <= q;
  assign ok_sign = (b == 4'sb0000) | (q == 4'sb0000) | ((a == 4'sb1000) & (b == 4'sb1111))
                   | (q[3] == (a[3] ^ b[3]));
  assign bad_sign = (b == 4'sb0000) | (q == 4'sb0000) | (q[3] == (a[3] ^ b[3]));
  assign q_o = qr;
endmodule
EOF
# r is also compared signed (r >= 0, r == -1): the yosys reader keeps the sign of a signed `%`
# (inou/yosys yosys_rem_sign_test; it used to compare r unsigned, so bad_pos was PROVED).
cat > "$OUT/rem3.sv" <<'EOF'
module rem3(input clk, input signed [3:0] a, output ok_neg, output ok_rng, output ok_tr,
            output ok_reg, output bad_pos, output bad_sign, output bad_eu, output [3:0] r_o);
  wire signed [3:0] r = a % 4'sd3;
  reg [3:0] rr;
  always @(posedge clk) rr <= r;
  assign ok_neg = ~a[3] | r[3] | (r == 4'd0);
  assign ok_rng = (r > -4'sd3) & (r < 4'sd3);
  assign ok_tr = (a != 4'sb1111) | (r == 4'sb1111);
  assign ok_reg = $signed(rr) > -4'sd3;
  assign bad_pos = r >= 4'sd0;
  assign bad_sign = ~r[3];
  assign bad_eu = (a != 4'sb1111) | (r == 4'sd2);
  assign r_o = rr;
endmodule
EOF

for d in cnt10 pair twocore ram4 ram4b mul4 udiv4 sdiv4 rem3; do
  "$LHD" compile "$OUT/$d.sv" --reader yosys-slang --top "$d" --set yosys.setundef=zero \
    --emit-dir "lg:$OUT/${d}_lg" --workdir "$OUT/${d}_cw" > "$OUT/$d.compile.log" 2>&1 \
    || { tail -5 "$OUT/$d.compile.log"; echo "FAIL: compile $d"; exit 1; }
  "$LHD" compile "lg:$OUT/${d}_lg" --top "$d" --workdir "$OUT/${d}_lw" --emit-dir "lean:$OUT/${d}_lean" \
    --set formal.lean.mode=verified_compiler > "$OUT/$d.lean.log" 2>&1 \
    || { grep -o '"message":"[^"]*' "$OUT/$d.lean.log" | head -3; echo "FAIL: certificate $d"; exit 1; }
done

ROUTES="${ROUTES:-fp ir vc}"
# the IR and VC symbolic steps against eval_op and interpretDesign, operator by operator and
# step by step (tests/native_symbolic.lean)
(cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean --run "$ROOT/pass/lean/tools/tests/native_symbolic.lean") \
  > "$OUT/native_symbolic.log" 2>&1 && grep -q '^PASS native symbolic' "$OUT/native_symbolic.log" \
  || { tail -5 "$OUT/native_symbolic.log"; echo "FAIL: native_symbolic"; exit 1; }
echo "ok: $(tail -1 "$OUT/native_symbolic.log")"
# fp's symbolic primitives and terms against the concrete semantics (tests/fp_symbolic.lean)
(cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean --run "$ROOT/pass/lean/tools/tests/fp_symbolic.lean") \
  > "$OUT/fp_symbolic.log" 2>&1 && grep -q '^PASS fp symbolic' "$OUT/fp_symbolic.log" \
  || { tail -5 "$OUT/fp_symbolic.log"; echo "FAIL: fp_symbolic"; exit 1; }
echo "ok: $(tail -1 "$OUT/fp_symbolic.log")"
# certificates no Verilog front end emits (symbolic masks, whole-memory load)
(cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean --run "$ROOT/pass/lean/tools/tests/gen_gap_certs.lean" "$OUT/gap") \
  > "$OUT/gen_gap_certs.log" 2>&1 || { tail -5 "$OUT/gen_gap_certs.log"; echo "FAIL: gen_gap_certs"; exit 1; }
run1() { # <driver> <expect> args...
  local drv=$1 expect=$2; shift 2
  local log="$OUT/run_$(( ++N )).log"
  if [[ -n "$SYM_NAT" ]]; then
    "$SYM_NAT/$drv" "$@" "work:$OUT/work" > "$log" 2>&1 || true
  else
    (cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean --run "$ROOT/pass/lean/tools/drivers/$drv.lean" "$@" "work:$OUT/work") \
      > "$log" 2>&1 || true
  fi
  local got
  got=$(grep -o 'RESULT: [A-Z -]*' "$log" | tail -1 | sed 's/RESULT: //; s/ *$//')
  if [[ "$got" != "$expect"* ]]; then
    tail -6 "$log"; echo "FAIL: $drv $* -- expected $expect, got '$got'"; exit 1
  fi
  echo "ok: $drv ${*##*/} -> $got"
}
fponly() { [[ " $* " == *" emit:"* || " $* " == *" route:"* ]]; }
run() { # <driver> <expect> args... : on fp, and on the other ROUTES where the case allows
  local drv=$1 expect=$2; shift 2
  local sim
  for sim in $ROUTES; do
    if [[ "$sim" == fp ]]; then
      # fp is named; a case using emit: / route: names no sim: and so checks that such a
      # run falls back to fp (the default is vc)
      if [[ "$drv" == sym_verify ]] && ! fponly "$@"; then run1 "$drv" "$expect" "$@" sim:fp
      else run1 "$drv" "$expect" "$@"; fi
      continue
    fi
    [[ "$drv" == sym_verify ]] || continue
    fponly "$@" && continue
    run1 "$drv" "$expect" "$@" "sim:$sim"
  done
}
N=0
C="$OUT/cnt10_lean/cnt10.dcert"
P="$OUT/pair_lean/pair.dcert"
run sym_verify "INVARIANT PROVED"    "$C" ok_good k:1
# no sim: given -> vc; closed (fp only) and no sim: -> fp
run1 sym_verify "INVARIANT PROVED" "$C" ok_good k:1
grep -q 'route sim:vc (the default)' "$OUT/run_$N.log" || { tail -4 "$OUT/run_$N.log"; echo "FAIL: the default route is not vc"; exit 1; }
run1 sym_verify "INVARIANT PROVED" "$C" ok_good k:1 closed
grep -q 'route sim:fp (no sim: given' "$OUT/run_$N.log" || { tail -4 "$OUT/run_$N.log"; echo "FAIL: closed did not fall back to fp"; exit 1; }
echo "ok: default route vc; closed without sim: uses fp"
run sym_verify "INVARIANT PROVED"    "$C" ok_good k:1 rebuild
run sym_verify "INVARIANT PROVED"    "$C" ok_good k:1 closed
run sym_verify "INVARIANT PROVED"    "$C" ok_good k:1 route:plain
run sym_verify "INVARIANT PROVED"    "$C" ok_good k:1 "emit:$OUT/F2cnt10.lean"
# the theorem form compiles, and its only non-standard axiom is the native one
(cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean "$OUT/F2cnt10.lean") > "$OUT/F2cnt10.out" 2>&1 \
  || { tail -5 "$OUT/F2cnt10.out"; echo "FAIL: F2 theorem file"; exit 1; }
tr '\n' ' ' < "$OUT/F2cnt10.out" | grep -Eq "'SymF2.F2cnt10.invariant' depends on axioms: \[propext, +Classical.choice, +Quot.sound, +[^],]*native_decide[^],]*\]" \
  || { cat "$OUT/F2cnt10.out"; echo "FAIL: F2 axioms"; exit 1; }
echo "ok: F2 theorem file compiles; axioms: $(tr '\n' ' ' < "$OUT/F2cnt10.out" | grep -o "invariant' depends on axioms: [^]]*]" | tr -s ' ')"
# the theorem form on ir and vc: stated over the closed runInvKOn of that route
f2_on() { # <sim> <cert> <name> args...
  local sim=$1 cert=$2 name=$3; shift 3
  run1 sym_verify "INVARIANT PROVED" "$cert" "$@" "sim:$sim" "emit:$OUT/$name.lean"
  grep -q "runInvKOn_sound via native_decide" "$OUT/run_$N.log" || { tail -3 "$OUT/run_$N.log"; echo "FAIL: $name not stated over runInvKOn"; exit 1; }
  grep -q "^theorem verdict : runInvKOn \.$sim D spec s0 pfB pfS = true := by native_decide" "$OUT/$name.lean" \
    || { echo "FAIL: $name verdict line"; exit 1; }
  (cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean "$OUT/$name.lean") > "$OUT/$name.out" 2>&1 \
    || { tail -5 "$OUT/$name.out"; echo "FAIL: theorem file $name"; exit 1; }
  tr '\n' ' ' < "$OUT/$name.out" | grep -Eq "'SymF2.$name.invariant' depends on axioms: \[propext, +Classical.choice, +Quot.sound, +[^],]*native_decide[^],]*\]" \
    || { cat "$OUT/$name.out"; echo "FAIL: $name axioms"; exit 1; }
  echo "ok: $name (sim:$sim) theorem file compiles; axioms: standard + one native_decide"
}
f2_on vc "$C" V2cnt10 ok_good k:1
f2_on ir "$C" I2cnt10 ok_good k:1
run sym_verify "NOT K-INDUCTIVE"     "$C" ok_bad k:1
run sym_verify "COUNTEREXAMPLE"      "$C" ok_bad k:10
run sym_verify "INVARIANT PROVED"    "$C" '!c_o' in:rst=1 k:1
run sym_verify "NOT K-INDUCTIVE"     "$C" '!c_o' k:1
run sym_verify "INVARIANT PROVED"    "$C" ok_bad,ok_good k:1
grep -q 'SUMMARY: ok_good PROVED at k=1 after 2 attempt' "$OUT/run_$N.log" || { echo "FAIL: candidate summary"; exit 1; }
run sym_verify "INVARIANT PROVED"    "$P" "conj:$OUT/pair_all.spec" jobs:4
run sym_verify "INVARIANT PROVED"    "$P" "conj:$OUT/pair_min.spec" jobs:4
run sym_verify "NOT INDUCTIVE"       "$P" "conj:$OUT/pair_missing.spec" jobs:4 replay
# the CTI is evaluated on every conjunct: eq_ok's hypotheses hold at cycle 0
# (only the left-out a_ok fails there) and eq_ok fails at cycle 1
grep -q '\[eq_ok\] counterexample: false at cycle 0 \[a_ok\], at cycle 1 \[.*eq_ok' "$OUT/run_$N.log" || { tail -5 "$OUT/run_$N.log"; echo "FAIL: step counterexample not evaluated"; exit 1; }
grep -q '\[eq_ok\] replay: hypotheses at cycle 0 true, broken at cycle 1 true' "$OUT/run_$N.log" || { tail -5 "$OUT/run_$N.log"; echo "FAIL: step counterexample not replayed"; exit 1; }
grep -q '^conjuncts FALSE at cycle 1: .*eq_ok' "$OUT/work/cti_eq_ok.txt" || { cat "$OUT/work/cti_eq_ok.txt"; echo "FAIL: counterexample report"; exit 1; }
grep -q 'not preserved under their hypotheses: \[eq_ok\]' "$OUT/run_$N.log" || { echo "FAIL: the failing conjunct is not named"; exit 1; }
grep -q '^  a = 4.ha$' "$OUT/work/cti_eq_ok.txt" || { cat "$OUT/work/cti_eq_ok.txt"; echo "FAIL: counterexample state by register name"; exit 1; }
run sym_verify "COUNTEREXAMPLE"      "$P" "conj:$OUT/pair_false.spec"
# the monolithic check of eq_ok alone is not inductive: it needs a_ok
run sym_verify "NOT K-INDUCTIVE"     "$P" eq_ok k:1
TC="$OUT/twocore_lean/twocore.dcert"
run sym_verify "INVARIANT PROVED"    "$TC" "conj:$OUT/tc_split.spec" jobs:4
grep -q '\[d_eq | sA=2,sB=3\] .*UNSAT' "$OUT/run_$N.log" || { echo "FAIL: per-case obligation not reported"; exit 1; }
run sym_verify "NOT INDUCTIVE"       "$TC" "conj:$OUT/tc_bad.spec" jobs:4
grep -q '\[d_eq | sA=2,sB=2\] counterexample: false at cycle 0 \[\], at cycle 1 \[.*d_eq' "$OUT/run_$N.log" || { tail -5 "$OUT/run_$N.log"; echo "FAIL: case counterexample not evaluated"; exit 1; }
M4="$OUT/ram4_lean/ram4.dcert"
M4B="$OUT/ram4b_lean/ram4b.dcert"
run sym_verify "INVARIANT PROVED"    "$M4" ok k:1
run sym_verify "INVARIANT PROVED"    "$M4" ok k:1 closed
f2_on vc "$M4" V2ram4 ok k:1
run sym_verify "COUNTEREXAMPLE"      "$M4B" ok k:4
# operators the routes used to refuse (see the header)
MU="$OUT/mul4_lean/mul4.dcert"
run sym_verify "INVARIANT PROVED"    "$MU" ok_lsb k:1
run sym_verify "COUNTEREXAMPLE"      "$MU" bad_lsb k:1
run sym_verify "INVARIANT PROVED"    "$MU" ok_max k:1
run1 sym_verify "COUNTEREXAMPLE NOT REPLAYED" "$MU" bad_lsb k:1 base sim:ir sf:1 wf:1
grep -q 'the replay simulator could not be built (fp specialization failed' "$OUT/run_$N.log" \
  || { tail -3 "$OUT/run_$N.log"; echo "FAIL: the not-replayed result does not name its reason"; exit 1; }
UD="$OUT/udiv4_lean/udiv4.dcert"
run sym_verify "INVARIANT PROVED"    "$UD" ok_q k:1
run sym_verify "INVARIANT PROVED"    "$UD" ok_r k:1
run sym_verify "COUNTEREXAMPLE"      "$UD" bad_q k:1
SD="$OUT/sdiv4_lean/sdiv4.dcert"
run sym_verify "INVARIANT PROVED"    "$SD" ok_sign k:1
run sym_verify "COUNTEREXAMPLE"      "$SD" bad_sign k:1
R3="$OUT/rem3_lean/rem3.dcert"
run sym_verify "INVARIANT PROVED"    "$R3" ok_neg k:1
run sym_verify "INVARIANT PROVED"    "$R3" ok_rng k:1
run sym_verify "INVARIANT PROVED"    "$R3" ok_tr k:1
run sym_verify "INVARIANT PROVED"    "$R3" ok_reg k:1
run sym_verify "COUNTEREXAMPLE"      "$R3" bad_pos k:1
run sym_verify "COUNTEREXAMPLE"      "$R3" bad_sign k:1
run sym_verify "COUNTEREXAMPLE"      "$R3" bad_eu k:1
G="$OUT/gap"
run sym_verify "INVARIANT PROVED"    "$G/getmask_sym.dcert" 0 k:1
run sym_verify "COUNTEREXAMPLE"      "$G/getmask_sym.dcert" 1 k:1
run sym_verify "INVARIANT PROVED"    "$G/setmask_sym.dcert" 0 k:1
run sym_verify "COUNTEREXAMPLE"      "$G/setmask_sym.dcert" 1 k:1
run sym_verify "INVARIANT PROVED"    "$G/bulk_ok.dcert" 0 k:1
run sym_verify "COUNTEREXAMPLE"      "$G/bulk_bad.dcert" 0 k:1
echo "PASS: sym_selftest"
