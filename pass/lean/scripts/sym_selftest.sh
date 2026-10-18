#!/usr/bin/env bash
set -euo pipefail
# M3 self-test: unbounded invariants and refinement with stuttering, proved on
# the projection residual (formal/lean/scripts/sym_verify.lean, sym_refine.lean).
#
# Small RTL fixtures, each through the real flow (yosys-slang -> LGraph ->
# DCERT2), then:
#   cnt10  mod-10 counter
#     c <= 9                      PROVED (k=1); also with the check re-building
#                                 the obligations (`rebuild`), closed (`closed`),
#                                 on the plain memory interpreter (`route:plain`),
#                                 and as the F2 theorem file (`emit:`), compiled
#     c != 9                      NOT K-INDUCTIVE at k=1; COUNTEREXAMPLE at k=10
#     c == 0 while reset held     PROVED (k=1); NOT K-INDUCTIVE without the assumption
#     candidates c != 9, c <= 9   one specialization; the first is not
#                                 1-inductive, the second PROVED
#   cnt10 refines cnt10           PROVED (identity refinement, lockstep, k=1),
#                                 also with equal next states (`stateq`)
#   acc2 refines acc1             a two-phase accumulator adds every other cycle
#                                 (witness `adv`): NOT K-INDUCTIVE at k=1,
#                                 PROVED at k=2 (also as one run over k=1,2);
#                                 in LOCKSTEP a COUNTEREXAMPLE, decoded and
#                                 replayed on both simulators
# Every PROVED is the verified check (Sym.runInvKOf / Sym.runRefKOf, by
# default through *_of_obls over the obligations already built)
# accepting LRAT proofs from the untrusted solver. A run with several
# attempts is judged by its LAST verdict line.
#
# SYM_NAT=<dir with native sym_verify and sym_refine> uses those; otherwise the
# drivers run interpreted against formal/lean/.olean-dev (build it with
# formal/lean/.build-proj.sh). The Lean toolchain pin lives in formal/lean.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
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
cat > "$OUT/acc1.sv" <<'EOF'
module acc1(input clk, input [7:0] x, output [7:0] acc_o);
  reg [7:0] acc;
  always @(posedge clk) acc <= acc + x;
  assign acc_o = acc;
endmodule
EOF
cat > "$OUT/acc2.sv" <<'EOF'
module acc2(input clk, input [7:0] x, output [7:0] acc_o, output adv);
  reg [7:0] acc;
  reg phase;
  always @(posedge clk) begin
    phase <= ~phase;
    if (phase) acc <= acc + x;
  end
  assign acc_o = acc;
  assign adv = phase;
endmodule
EOF
for d in cnt10 acc1 acc2; do
  "$LHD" compile "$OUT/$d.sv" --reader yosys-slang --top "$d" --set yosys.setundef=zero \
    --emit-dir "lg:$OUT/${d}_lg" --workdir "$OUT/${d}_cw" > "$OUT/$d.compile.log" 2>&1 \
    || { tail -5 "$OUT/$d.compile.log"; echo "FAIL: compile $d"; exit 1; }
  "$LHD" compile "lg:$OUT/${d}_lg" --top "$d" --workdir "$OUT/${d}_lw" --emit-dir "lean:$OUT/${d}_lean" \
    --set formal.lean.mode=verified_compiler > "$OUT/$d.lean.log" 2>&1 \
    || { grep -o '"message":"[^"]*' "$OUT/$d.lean.log" | head -3; echo "FAIL: certificate $d"; exit 1; }
done

run() { # <driver> <expect> args...
  local drv=$1 expect=$2; shift 2
  local log="$OUT/run_$(( ++N )).log"
  if [[ -n "$SYM_NAT" ]]; then
    "$SYM_NAT/$drv" "$@" "work:$OUT/work" > "$log" 2>&1 || true
  else
    (cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean --run "scripts/$drv.lean" "$@" "work:$OUT/work") \
      > "$log" 2>&1 || true
  fi
  local got
  got=$(grep -o 'RESULT: [A-Z -]*' "$log" | tail -1 | sed 's/RESULT: //; s/ *$//')
  if [[ "$got" != "$expect"* ]]; then
    tail -6 "$log"; echo "FAIL: $drv $* -- expected $expect, got '$got'"; exit 1
  fi
  echo "ok: $drv ${*##*/} -> $got"
}
N=0
C="$OUT/cnt10_lean/cnt10.dcert"
A1="$OUT/acc1_lean/acc1.dcert"
A2="$OUT/acc2_lean/acc2.dcert"
run sym_verify "INVARIANT PROVED"    "$C" ok_good k:1
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
run sym_verify "NOT K-INDUCTIVE"     "$C" ok_bad k:1
run sym_verify "COUNTEREXAMPLE"      "$C" ok_bad k:10
run sym_verify "INVARIANT PROVED"    "$C" '!c_o' in:rst=1 k:1
run sym_verify "NOT K-INDUCTIVE"     "$C" '!c_o' k:1
run sym_verify "INVARIANT PROVED"    "$C" ok_bad,ok_good k:1
grep -q 'SUMMARY: ok_good PROVED at k=1 after 2 attempt' "$OUT/run_$N.log" || { echo "FAIL: candidate summary"; exit 1; }
run sym_refine "REFINEMENT PROVED"   "$C" "$C" pair:c_o=c_o pair:ok_good=ok_good pair:ok_bad=ok_bad k:1
run sym_refine "REFINEMENT PROVED"   "$C" "$C" pair:c_o=c_o stateq k:1
run sym_refine "REFINEMENT PROVED"   "$C" "$C" pair:c_o=c_o stateq k:1 route:plain
run sym_refine "NOT K-INDUCTIVE"     "$A2" "$A1" pair:acc_o=acc_o adv:adv k:1
run sym_refine "REFINEMENT PROVED"   "$A2" "$A1" pair:acc_o=acc_o adv:adv k:2
run sym_refine "REFINEMENT PROVED"   "$A2" "$A1" pair:acc_o=acc_o adv:adv k:2 rebuild
run sym_refine "REFINEMENT PROVED"   "$A2" "$A1" pair:acc_o=acc_o adv:adv k:1,2
grep -q '\[k=1\] RESULT: NOT K-INDUCTIVE' "$OUT/run_$N.log" || { echo "FAIL: k list"; exit 1; }
run sym_refine "COUNTEREXAMPLE"      "$A2" "$A1" pair:acc_o=acc_o k:4
grep -q "COUNTEREXAMPLE (base, replayed)" "$OUT/run_$N.log" || { tail -5 "$OUT/run_$N.log"; echo "FAIL: refinement counterexample not replayed"; exit 1; }
echo "PASS: sym_selftest"
