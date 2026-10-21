#!/usr/bin/env bash
set -euo pipefail
# Refinement-map self-test (pass/lean/tools/drivers/sym_refmap.lean).
#
# Small RTL fixtures, each through the real flow (yosys-slang -> LGraph ->
# DCERT2), then:
#   accS            spec: one channel `add`, response x, presents acc
#     spec facts                        SPEC FACTS PROVED (Moore)
#   acc1 refines accS                   an event every cycle: REFINEMENT PROVED
#   acc2 refines accS                   an event every other cycle (`adv`), a
#                                       stutter in between ranked by `rk`:
#                                       REFINEMENT PROVED
#     an event on every cycle           NOT A REFINEMENT: comm (the stutter
#                                       cycle does not add)
#     wrong map (acc <- nacc, init free) NOT A REFINEMENT: comm and obs
#     next-cycle observation            NOT A REFINEMENT: obs
#     rank stays 0                      NOT A REFINEMENT: rankLt
#     init acc = 1                      COUNTEREXAMPLE (base)
#   two             spec: channels `a`, `b` (select `sel`), each adds its
#                   response into its own register
#     spec facts                        SPEC FACTS PROVED (Moore, persist,
#                                       outC, comm for a/b)
#   ncon            a adds, b shifts the same register: SPEC FACTS FAIL
#                   (comm, outC)
#   nmoore          presents r + x, the response: SPEC FACTS FAIL (moore)
#   par refines two                     both events in every cycle, a then b
#   alt refines two                     b, then a on the next cycle; each
#                                       channel's stutter ranked separately
#     (par and alt order the two channels' events differently; with the
#     spec facts, Check.refmap_compose makes their streams agree)
#   par with the slots swapped          REFINEMENT PROVED (b then a: comm)
#   theorem files: par, alt and two's spec facts as F2 files (native_decide),
#     and Compose.lean, which derives in the kernel, from those three verdicts
#     (Check.start_ref, Check.refmap_compose_ref), that par and alt answered by
#     one environment show the same streams; acc2 as an F1 file (named axiom)
# Every PROVED is the verified check (Check.runRefMapOf / runSpecFactsOf)
# accepting LRAT proofs from the untrusted solver.
#
# SYM_NAT=<dir with a native sym_refmap> uses it; otherwise the driver runs
# interpreted against formal/lean/.olean-dev (build it with
# formal/lean/.build-proj.sh). The Lean toolchain pin lives in formal/lean.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/refmap_selftest}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
SYM_NAT="${SYM_NAT:-}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"

cat > "$OUT/accS.sv" <<'EOF'
module accS(input clk, input [7:0] x, output ok, output [7:0] acc_o);
  reg [7:0] acc;
  always @(posedge clk) acc <= acc + x;
  assign ok = 1'b1;
  assign acc_o = acc;
endmodule
EOF
cat > "$OUT/acc1.sv" <<'EOF'
module acc1(input clk, input [7:0] x, output [7:0] acc_o);
  reg [7:0] acc;
  always @(posedge clk) acc <= acc + x;
  assign acc_o = acc;
endmodule
EOF
# rk: cycles of internal work before the next event; sl: the selector (0 = internal)
cat > "$OUT/acc2.sv" <<'EOF'
module acc2(input clk, input [7:0] x, output [7:0] acc_o, output adv, output rk, output sl,
            output [7:0] nacc);
  reg [7:0] acc;
  reg phase;
  always @(posedge clk) begin
    phase <= ~phase;
    if (phase) acc <= acc + x;
  end
  assign acc_o = acc;
  assign adv = phase;
  assign rk = ~phase;
  assign sl = 1'b0;
  assign nacc = acc + x;
endmodule
EOF
cat > "$OUT/two.sv" <<'EOF2'
module two(input clk, input sel, input [7:0] x, output ok, output [7:0] ra_o, output [7:0] rb_o);
  reg [7:0] ra;
  reg [7:0] rb;
  always @(posedge clk) if (sel) rb <= rb + x; else ra <= ra + x;
  assign ok = 1'b1;
  assign ra_o = ra;
  assign rb_o = rb;
endmodule
EOF2
cat > "$OUT/ncon.sv" <<'EOF2'
module ncon(input clk, input sel, input [7:0] x, output ok, output [7:0] r_o);
  reg [7:0] r;
  always @(posedge clk) if (sel) r <= {r[6:0], 1'b0}; else r <= r + x;
  assign ok = 1'b1;
  assign r_o = r;
endmodule
EOF2
cat > "$OUT/nmoore.sv" <<'EOF2'
module nmoore(input clk, input [7:0] x, output ok, output [7:0] s_o);
  reg [7:0] r;
  always @(posedge clk) r <= r + x;
  assign ok = 1'b1;
  assign s_o = r + x;
endmodule
EOF2
cat > "$OUT/par.sv" <<'EOF2'
module par(input clk, input [7:0] xa, input [7:0] xb, output [7:0] ra_o, output [7:0] rb_o);
  reg [7:0] ra;
  reg [7:0] rb;
  always @(posedge clk) begin
    ra <= ra + xa;
    rb <= rb + xb;
  end
  assign ra_o = ra;
  assign rb_o = rb;
endmodule
EOF2
cat > "$OUT/alt.sv" <<'EOF2'
module alt(input clk, input [7:0] x, output [7:0] ra_o, output [7:0] rb_o, output ph_o,
           output rk_a, output rk_b, output sl);
  reg [7:0] ra;
  reg [7:0] rb;
  reg ph;
  always @(posedge clk) begin
    ph <= ~ph;
    if (ph) ra <= ra + x; else rb <= rb + x;
  end
  assign ra_o = ra;
  assign rb_o = rb;
  assign ph_o = ph;
  assign rk_a = ~ph;
  assign rk_b = ph;
  assign sl = 1'b0;
endmodule
EOF2
for d in accS acc1 acc2 two ncon nmoore par alt; do
  "$LHD" compile "$OUT/$d.sv" --reader yosys-slang --top "$d" --set yosys.setundef=zero \
    --emit-dir "lg:$OUT/${d}_lg" --workdir "$OUT/${d}_cw" > "$OUT/$d.compile.log" 2>&1 \
    || { tail -5 "$OUT/$d.compile.log"; echo "FAIL: compile $d"; exit 1; }
  "$LHD" compile "lg:$OUT/${d}_lg" --top "$d" --workdir "$OUT/${d}_lw" --emit-dir "lean:$OUT/${d}_lean" \
    --set formal.lean.mode=verified_compiler > "$OUT/$d.lean.log" 2>&1 \
    || { grep -o '"message":"[^"]*' "$OUT/$d.lean.log" | head -3; echo "FAIL: certificate $d"; exit 1; }
done

# <name> <impl> <slot kind> <alpha output> <obs> <rank> <init>
refmap() {
  {
    [[ -n "$2" ]] && echo "impl $2_lean/$2.dcert"
    echo "spec accS_lean/accS.dcert"
    echo "reset 0"
    echo "alpha acc $4"
    echo "init acc $7"
    echo "chan add allow:ok resp:x obs:acc_o${6:+ rank:$6}"
    echo "slot add $3 resp:in:x obs:$5"
  } > "$OUT/$1.refmap"
}
refmap accS     ""   1        acc_o out:acc_o      ""    0
refmap acc1     acc1 1        acc_o out:acc_o      ""    0
refmap acc2     acc2 out:adv  acc_o out:acc_o      rk/sl 0
refmap acc2_all acc2 1        acc_o out:acc_o      rk/sl 0
refmap acc2_map acc2 out:adv  nacc  out:acc_o      rk/sl free
refmap acc2_obs acc2 out:adv  acc_o out:acc_o@next rk/sl 0
refmap acc2_rk0 acc2 out:adv  acc_o out:acc_o      sl/sl 0
refmap acc2_ini acc2 out:adv  acc_o out:acc_o      rk/sl 1

run() { # <mode> <expect> <refmap> args...
  local mode=$1 expect=$2 rm=$3; shift 3
  local log="$OUT/run_$(( ++N )).log"
  if [[ -n "$SYM_NAT" ]]; then
    "$SYM_NAT/sym_refmap" "$mode" "$OUT/$rm.refmap" "$@" "work:$OUT/work_$N" > "$log" 2>&1 || true
  else
    (cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean --run "$ROOT/pass/lean/tools/drivers/sym_refmap.lean" \
      "$mode" "$OUT/$rm.refmap" "$@" "work:$OUT/work_$N") > "$log" 2>&1 || true
  fi
  local got
  got=$(grep -o 'RESULT: [A-Z -]*' "$log" | tail -1 | sed 's/RESULT: //; s/[ -]*$//')
  if [[ "$got" != "$expect"* ]]; then
    tail -6 "$log"; echo "FAIL: $mode $rm $* -- expected $expect, got '$got'"; exit 1
  fi
  echo "ok: $mode $rm -> $got"
}
printf '%s\n' 'spec two_lean/two.dcert' 'chan a sel:sel=0 resp:x obs:ra_o allow:ok' \
  'chan b sel:sel=1 resp:x obs:rb_o allow:ok' > "$OUT/two.refmap"
printf '%s\n' 'spec ncon_lean/ncon.dcert' 'chan a sel:sel=0 resp:x obs:r_o allow:ok' \
  'chan b sel:sel=1 obs:r_o allow:ok' > "$OUT/ncon.refmap"
printf '%s\n' 'spec nmoore_lean/nmoore.dcert' 'chan a resp:x obs:s_o allow:ok' > "$OUT/nmoore.refmap"
two_map() { # <name> <impl> <rank a> <rank b> <slot lines...>
  local nm=$1 im=$2 ra=$3 rb=$4; shift 4
  { echo "impl ${im}_lean/$im.dcert"; echo "spec two_lean/two.dcert"; echo "reset 0"
    echo "alpha ra ra_o"; echo "alpha rb rb_o"; echo "init ra 0"; echo "init rb 0"
    echo "chan a sel:sel=0 resp:x obs:ra_o allow:ok${ra:+ rank:$ra}"
    echo "chan b sel:sel=1 resp:x obs:rb_o allow:ok${rb:+ rank:$rb}"
    printf '%s\n' "$@"; } > "$OUT/$nm.refmap"
}
two_map par     par "" ""        'slot a 1 resp:in:xa obs:out:ra_o' 'slot b 1 resp:in:xb obs:out:rb_o'
two_map par_ba  par "" ""        'slot b 1 resp:in:xb obs:out:rb_o' 'slot a 1 resp:in:xa obs:out:ra_o'
two_map alt     alt rk_a/sl rk_b/sl 'slot a out:ph_o resp:in:x obs:out:ra_o' 'slot b !out:ph_o resp:in:x obs:out:rb_o'
N=0
run specfacts "SPEC FACTS PROVED"  accS
run refmap    "REFINEMENT PROVED"  acc1
run refmap    "REFINEMENT PROVED"  acc2
grep -q '\[rankLt add\] .*UNSAT' "$OUT/run_$N.log" || { echo "FAIL: rank obligation not reported"; exit 1; }
run refmap    "NOT A REFINEMENT"   acc2_all
grep -q 'NOT A REFINEMENT (step): \[comm acc\]' "$OUT/run_$N.log" || { tail -3 "$OUT/run_$N.log"; echo "FAIL: stutter event not caught by comm"; exit 1; }
grep -q 'acc: spec after the cycle' "$OUT/work_$N/cti_comm_acc.txt" || { cat "$OUT/work_$N/cti_comm_acc.txt"; echo "FAIL: comm counterexample report"; exit 1; }
run refmap    "NOT A REFINEMENT"   acc2_map
grep -q 'NOT A REFINEMENT (step): \[comm acc, obs add#0\]' "$OUT/run_$N.log" || { tail -3 "$OUT/run_$N.log"; echo "FAIL: wrong map not caught by comm"; exit 1; }
run refmap    "NOT A REFINEMENT"   acc2_obs
grep -q 'NOT A REFINEMENT (step): \[obs add#0\]' "$OUT/run_$N.log" || { tail -3 "$OUT/run_$N.log"; echo "FAIL: observation not caught"; exit 1; }
run refmap    "NOT A REFINEMENT"   acc2_rk0
grep -q 'NOT A REFINEMENT (step): \[rankLt add\]' "$OUT/run_$N.log" || { tail -3 "$OUT/run_$N.log"; echo "FAIL: flat rank not caught"; exit 1; }
run refmap    "COUNTEREXAMPLE"     acc2_ini
run specfacts "SPEC FACTS PROVED"  two
grep -q '\[comm a/b\] .*UNSAT' "$OUT/run_$N.log" || { echo "FAIL: the diamond obligation not reported"; exit 1; }
run specfacts "SPEC FACTS FAIL"    ncon
grep -q 'SPEC FACTS FAIL: .*comm a/b' "$OUT/run_$N.log" || { tail -3 "$OUT/run_$N.log"; echo "FAIL: non-commuting events not caught"; exit 1; }
run specfacts "SPEC FACTS FAIL"    nmoore
grep -q 'SPEC FACTS FAIL: \[moore a\]' "$OUT/run_$N.log" || { tail -3 "$OUT/run_$N.log"; echo "FAIL: non-Moore output not caught"; exit 1; }
run refmap    "REFINEMENT PROVED"  par
run refmap    "REFINEMENT PROVED"  par_ba
run refmap    "REFINEMENT PROVED"  alt
# theorem files and the composition, compiled against formal/lean/.olean-dev
T="$OUT/thm"
run refmap    "REFINEMENT PROVED"  par "emit:f2:$T/F2par.lean"
run refmap    "REFINEMENT PROVED"  alt "emit:f2:$T/F2alt.lean"
run specfacts "SPEC FACTS PROVED"  two "emit:f2:$T/F2two.lean"
run refmap    "REFINEMENT PROVED"  acc2 "emit:f1:$T/F1acc2.lean"
lean_c() { (cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev:$T" lean "$@"); }
for f in F2par F2alt F2two F1acc2; do
  lean_c -R "$T" -o "$T/$f.olean" "$T/$f.lean" > "$T/$f.out" 2>&1 \
    || { tail -5 "$T/$f.out"; echo "FAIL: theorem file $f"; exit 1; }
done
axioms() { tr '\n' ' ' < "$1" | grep -o "depends on axioms: \[[^]]*\]" | tr -s ' '; }
for f in F2par F2alt F2two; do
  [[ "$(axioms "$T/$f.out")" == "depends on axioms: [propext, Classical.choice, Quot.sound, verdict._native.native_decide.ax_1_1]" ]] \
    || { cat "$T/$f.out"; echo "FAIL: $f axioms"; exit 1; }
done
[[ "$(axioms "$T/F1acc2.out")" == "depends on axioms: [propext, Classical.choice, Quot.sound, checked]" ]] \
  || { cat "$T/F1acc2.out"; echo "FAIL: F1acc2 axioms"; exit 1; }
grep -q 'proofs: [0-9]* LRAT proofs, [0-9]* bytes, sha256 [0-9a-f]\{64\}' "$T/F1acc2.lean" || { echo "FAIL: F1 provenance"; exit 1; }
echo "ok: theorem files compile; F2 axioms: standard + one native_decide each; F1: the named verdict"
cat > "$T/Compose.lean" <<'EOF2'
/- par and alt order the events of channels a and b differently (both in one cycle, a then b;
or b, then a on the next cycle). Both refine the spec `two`, whose facts hold. So, answered by
the same environment, they show the same streams on both channels, in interpretDesign's
semantics. Kernel-checked from the three verdicts. -/
import F2par
import F2alt
import F2two
open Compiler Projection HDLProof HDLProof.Check

namespace RefMapDemo

abbrev A := RefMapF2.F2par.DI
abbrev B := RefMapF2.F2alt.DI
abbrev spA := RefMapF2.F2par.spec
abbrev spB := RefMapF2.F2alt.spec

theorem same_streams {insA insB : Nat → RuntimeInput}
    (hinsA : ∀ n, (insA n).toList.map BV.width = spA.inv.inW)
    (hinsB : ∀ n, (insB n).toList.map BV.width = spB.inv.inW)
    {E : Theory.Env Nat (List Int) (List Int)}
    (hEA : ImplObeysR A spA (StateM.denote RefMapF2.F2par.s0) insA E)
    (hEB : ImplObeysR B spB (StateM.denote RefMapF2.F2alt.s0) insB E) :
    ∀ nA nB x, Theory.PrefixAgree (implStreamR A spA (StateM.denote RefMapF2.F2par.s0) insA x nA)
      (implStreamR B spB (StateM.denote RefMapF2.F2alt.s0) insB x nB) := by
  have hsF : SameSpec RefMapF2.F2two.spec spA := ⟨rfl, rfl, rfl⟩
  have hs : SameSpec spA spB := ⟨rfl, rfl, rfl⟩
  -- no cycle assumptions
  have hgA : ∀ n, GoodR A spA (StateM.denote RefMapF2.F2par.s0) insA n := fun _ _ _ => by
    rw [show spA.inv.asmOut = none from rfl, show spA.inv.asmIn = [] from rfl]
    simp [HDLProof.Sym.asmBit, HDLProof.Sym.inAsmOK]
  have hgB : ∀ n, GoodR B spB (StateM.denote RefMapF2.F2alt.s0) insB n := fun _ _ _ => by
    rw [show spB.inv.asmOut = none from rfl, show spB.inv.asmIn = [] from rfl]
    simp [HDLProof.Sym.asmBit, HDLProof.Sym.inAsmOK]
  -- both maps start at ra = rb = 0
  have hstart := start_ref RefMapF2.F2par.checked RefMapF2.F2alt.checked hs hinsA hinsB (hgA _) (hgB _)
    (fun k hk => by
      have hk' : k < 2 := hk
      rcases (show k = 0 ∨ k = 1 by omega) with rfl | rfl <;> exact Or.inl ⟨0, rfl, rfl⟩)
  exact refmap_compose_ref RefMapF2.F2par.checked RefMapF2.F2alt.checked RefMapF2.F2two.checked
    hsF hs hinsA hinsB hgA hgB hstart hEA hEB

#print axioms same_streams

end RefMapDemo
EOF2
lean_c "$T/Compose.lean" > "$T/Compose.out" 2>&1 || { tail -5 "$T/Compose.out"; echo "FAIL: composition"; exit 1; }
[[ "$(axioms "$T/Compose.out")" == "depends on axioms: [propext, Classical.choice, Quot.sound, RefMapF2.F2alt.verdict._native.native_decide.ax_1_1, RefMapF2.F2par.verdict._native.native_decide.ax_1_1, RefMapF2.F2two.verdict._native.native_decide.ax_1_1]" ]] \
  || { cat "$T/Compose.out"; echo "FAIL: composition axioms"; exit 1; }
echo "ok: composition par/alt kernel-checked; axioms: standard + the three verdicts' native_decide"
echo "PASS: refmap_selftest"
