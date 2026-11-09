#!/usr/bin/env bash
set -euo pipefail
# Judge self-test (`sym_refmap judge`, HDLProof.Kit.Judge) and the product search
# (scripts/mk_product.py + `sym_verify ... base`), on small RTL fixtures through the real flow
# (yosys-slang -> LGraph [-> single_edge] -> DCERT2):
#   accS / acc2   the spec adds x every cycle; acc2 adds x every other cycle (adv) and takes x only
#                 then (xfer out:adv), so D0 must hold in between (maxstall 1)
#     random runs, acc2                     MATCH FOUND
#     random runs, acc2b (adds 1 more when x[1:0] = 3)  NO MATCH
#     acc2 with maxstall 0                  NO MATCH (a hold is needed)
#     acc2 with the slot firing always      MATCH FOUND, the slot's stall choice fails (STALL CHOICE WRONG)
#     product search, acc2b                 COUNTEREXAMPLE (base), its trace -> judge NO MATCH
#     product search, acc2                  NO COUNTEREXAMPLE UP TO k
#   pv0 / pv1     a falling-edge capture feeding a rising-edge register (single_edge: P = 2);
#                 pv1 shows pv0's output one clock later (one more register); pv0's observer
#                 `dly_o` delays its own output by one clock, compared lockstep (maxstall 0)
#     random runs, pv1                      MATCH FOUND
#     random runs, pv1b (adds 2, not 1)     NO MATCH
#     product search, pv1b                  COUNTEREXAMPLE (base), its trace -> judge NO MATCH
#   rS / rD / rDb  a result channel with a valid and no ready: rS (D0) shows the result in the
#                 cycle the request comes; rD (a right D1) registers it, so it leaves one cycle
#                 later; rDb registers it with the payload one transfer behind the valid (a bug)
#     rD, result read in the same cycle     NO MATCH, CLASS DELAY? (read 1 step later); the
#                                           transaction view shows D1's same words one step later;
#                                           the run's inputs as a trace give the same NO MATCH
#     rD, result read late (@next)          MATCH FOUND
#     rDb, result read late                 NO MATCH, CLASS VALUE DIFFERENCE
#     acc2 with maxstall 0                  CLASS STUTTER? (maxstall 1)
#     timing lints                          a payload and its guard at different cycles; an xfer
#                                           that reads a late-read output in the same cycle
#     asm on random runs                    a step that breaks asm is drawn again (MATCH FOUND);
#                                           tries:0 ends the run (ENVIRONMENT VIOLATED); a trace
#                                           that breaks asm is reported as not a legal witness
#     controls (D0 as the implementation)   positive with the change's delay: PASS; positive on the
#                                           same-cycle contract or without the delay: FAIL (CONTRACT
#                                           TOO STRICT); negative: PASS; a guard that never opens:
#                                           FAIL (NOT SEEN); an output no word reads: noted
#     coverage                              transactions per channel are counted; runs with no
#                                           transaction warn (search) and fail (controls)
#     product search, rD read in the same   COUNTEREXAMPLE (its guard is bit 0 of the 1-bit v),
#       cycle                               its trace -> judge NO MATCH
#     product search, rD read late (@next)  NO COUNTEREXAMPLE UP TO k
#     product search, rDb read late         COUNTEREXAMPLE at cycle m (D0 one step behind), its
#                                           trace -> judge NO MATCH at step m-1, the payload
#                                           differs; the classification reads on past the
#                                           trace's end (random inputs keeping asm): VALUE
#                                           DIFFERENCE (the trace alone cannot tell it from a delay)
#     product, @next with period 2          refused
#
# The judge's default route is vc (fp for a certificate with memories); two cases also run on fp.
# SYM_NAT=<dir with native sym_refmap and sym_verify> uses them; otherwise the drivers run
# interpreted against formal/lean/.olean-dev (formal/lean/.build-proj.sh).

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/judge_selftest}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
SYM_NAT="${SYM_NAT:-}"
PY="${PYTHON:-python3}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"

cat > "$OUT/accS.v" <<'EOF'
module accS(clk, x, ok, acc_o);
  input clk; input [7:0] x; output ok; output [7:0] acc_o;
  reg [7:0] acc;
  always @(posedge clk) acc <= acc + x;
  assign ok = 1'b1;
  assign acc_o = acc;
endmodule
EOF
for v in acc2 acc2b; do
  inc='x'; [[ $v = acc2b ]] && inc='x + {7'"'"'d0, x[0] & x[1]}'
  cat > "$OUT/$v.v" <<EOF
module $v(clk, x, acc_o, adv);
  input clk; input [7:0] x; output [7:0] acc_o; output adv;
  reg [7:0] acc;
  reg phase;
  always @(posedge clk) begin
    phase <= ~phase;
    if (phase) acc <= acc + $inc;
  end
  assign acc_o = acc;
  assign adv = phase;
endmodule
EOF
done
cat > "$OUT/pv0.v" <<'EOF'
module pv0(clk, x, ok, y, dly_o);
  input clk; input [7:0] x; output ok; output [7:0] y; output [7:0] dly_o;
  reg [7:0] n;
  always @(negedge clk) n <= x;
  reg [7:0] r;
  always @(posedge clk) r <= n + 8'd1;
  reg [7:0] dly;
  always @(posedge clk) dly <= r;
  assign ok = 1'b1;
  assign y = r;
  assign dly_o = dly;
endmodule
EOF
for v in pv1 pv1b; do
  k=1; [[ $v = pv1b ]] && k=2
  cat > "$OUT/$v.v" <<EOF
module $v(clk, x, y);
  input clk; input [7:0] x; output [7:0] y;
  reg [7:0] n;
  always @(negedge clk) n <= x;
  reg [7:0] s;
  always @(posedge clk) s <= n + 8'd$k;
  reg [7:0] r;
  always @(posedge clk) r <= s;
  assign y = r;
endmodule
EOF
done

cat > "$OUT/rS.v" <<'EOF'
module rS(clk, vin, x, ok, v, y, yg, z, req);
  input clk; input vin; input [7:0] x; output ok; output v; output [7:0] y; output [7:0] yg;
  output [7:0] z; output req;
  reg [7:0] cnt;
  always @(posedge clk) if (vin) cnt <= cnt + 8'd1;
  assign ok = 1'b1;
  assign v = vin;
  assign y = x + cnt;
  assign yg = vin ? x + cnt : 8'd0;
  assign z = 8'd0;
  assign req = vin & x[0];
endmodule
EOF
for v in rD rDb; do
  if [[ $v = rD ]]; then upd='yq <= x + cnt;'; else upd='begin y1 <= x + cnt; yq <= y1; end'; fi
  cat > "$OUT/$v.v" <<EOF
module $v(clk, vin, x, v, y, req, env_ok);
  input clk; input vin; input [7:0] x; output v; output [7:0] y; output req; output env_ok;
  reg [7:0] cnt;
  reg vq;
  reg [7:0] yq;
  reg [7:0] y1;
  always @(posedge clk) begin
    if (vin) cnt <= cnt + 8'd1;
    vq <= vin;
    if (vin) $upd
  end
  assign v = vq;
  assign y = yq;
  assign req = vin & x[0];
  assign env_ok = !(vin & (x[1:0] == 2'b11));
endmodule
EOF
done

cert() { # <design> <top> <src> : compile [-> single_edge] -> certificate in <design>_lean/
  local d=$1 top=$2 src=$3
  "$LHD" compile "$src" --reader yosys-slang --top "$top" --set compile.yosys.setundef=zero \
    --emit-dir "lg:$OUT/${d}_lg" --workdir "$OUT/${d}_cw" > "$OUT/$d.compile.log" 2>&1 \
    || { tail -5 "$OUT/$d.compile.log"; echo "FAIL: compile $d"; exit 1; }
  "$LHD" pass single_edge --top "$top" "lg:$OUT/${d}_lg" --emit-dir "lg:$OUT/${d}_se" \
    --workdir "$OUT/${d}_sw" > "$OUT/$d.se.log" 2>&1 || { tail -5 "$OUT/$d.se.log"; echo "FAIL: single_edge $d"; exit 1; }
  "$LHD" compile "lg:$OUT/${d}_se" --top "$top" --workdir "$OUT/${d}_lw" --emit-dir "lean:$OUT/${d}_lean" \
    --set formal.lean.mode=verified_compiler > "$OUT/$d.lean.log" 2>&1 \
    || { grep -o '"message":"[^"]*' "$OUT/$d.lean.log" | head -3; echo "FAIL: certificate $d"; exit 1; }
  if [[ "$top" != "$d" ]]; then
    mv "$OUT/${d}_lean/$top.dcert" "$OUT/${d}_lean/$d.dcert"
    mv "$OUT/${d}_lean/${top}_io.json" "$OUT/${d}_lean/${d}_io.json"
  fi
}
for d in accS acc2 acc2b pv0 pv1 pv1b rS rD rDb; do cert "$d" "$d" "$OUT/$d.v"; done
# the falling-edge capture makes pv* two slots per clock
grep -q 'P=2 slots' "$OUT/pv0.se.log" || { cat "$OUT/pv0.se.log" | tail -5; echo "FAIL: pv0 is not P = 2"; exit 1; }

acc_map() { # <name> <impl> <slot kind> <maxstall>
  printf '%s\n' "impl $2_lean/$2.dcert" "spec accS_lean/accS.dcert" \
    "chan add allow:ok resp:x obs:acc_o" "slot add $3 resp:in:x obs:out:acc_o" \
    "xfer out:adv" "maxstall $4" > "$OUT/$1.refmap"
}
acc_map acc2      acc2  out:adv 1
acc_map acc2b     acc2b out:adv 1
acc_map acc2_r0   acc2  out:adv 0
acc_map acc2_slot acc2  1       1
pv_map() { # <name> <impl>
  printf '%s\n' "impl $2_lean/$2.dcert" "spec pv0_lean/pv0.dcert" \
    "chan step allow:ok resp:x obs:dly_o" "slot step 1 resp:in:x obs:out:y" \
    "maxstall 0" "period 2" > "$OUT/$1.refmap"
}
pv_map pv1  pv1
pv_map pv1b pv1b
r_map() { # <name> <impl> <spec obs> <slot obs> <extra lines...>
  local nm=$1 impl=$2 sobs=$3 iobs=$4; shift 4
  printf '%s\n' "impl ${impl}_lean/$impl.dcert" "spec rS_lean/rS.dcert" \
    "chan step allow:ok resp:vin,x obs:$sobs" "slot step 1 resp:in:vin,in:x obs:$iobs" \
    "xfer in:vin" "maxstall 0" "$@" > "$OUT/$nm.refmap"
}
LATE='out:v@next,out:y@next?out:v@next.0,out:req'
r_map r_same    rD  v,yg,req 'out:v,out:y?out:v.0,out:req'
r_map r_late    rD  v,yg,req "$LATE" 'xfer out:v@next'
r_map rb_late   rDb v,yg,req "$LATE" 'xfer out:v@next'
r_map r_lint    rD  v,yg,req 'out:v@next,out:y@next?out:v.0,out:req' 'xfer out:v@next'
r_map r_lint2   rD  v,yg,req "$LATE" 'xfer out:v'
r_map r_env     rD  v,yg,req "$LATE" 'xfer out:v@next' 'asm env_ok'
r_map r_same_env rD v,yg,req 'out:v,out:y?out:v.0,out:req' 'asm env_ok'
r_map r_dead    rD  v,z,req  'out:v@next,out:y@next?out:v@next.1,out:req' 'xfer out:v@next'
r_map r_blind   rD  v,req    'out:v@next,out:req' 'xfer out:v@next'
r_map r_quiet   rD  v,yg,req "$LATE" 'xfer out:v@next' 'drive vin 0 0 0'

drv() { # <driver> args...
  local d=$1; shift
  if [[ -n "$SYM_NAT" ]]; then "$SYM_NAT/$d" "$@"
  else (cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean --run "$ROOT/pass/lean/tools/drivers/$d.lean" "$@"); fi
}
N=0
judge() { # <expect> <refmap> args...
  local expect=$1 rm=$2; shift 2
  local log="$OUT/judge_$(( ++N )).log"
  drv sym_refmap judge "$OUT/$rm.refmap" "$@" "work:$OUT/jw_$N" > "$log" 2>&1 || true
  local got
  got=$(grep -o 'RESULT: [A-Z -]*' "$log" | tail -1 | sed 's/RESULT: //; s/[ -]*$//')
  if [[ "$got" != "$expect"* ]]; then tail -8 "$log"; echo "FAIL: judge $rm $* -- expected $expect, got '$got'"; exit 1; fi
  echo "ok: judge $rm $* -> $got"
}
judge "MATCH FOUND" acc2      random:40,8
grep -q 'route sim:vc (the default)' "$OUT/judge_$N.log" || { tail -4 "$OUT/judge_$N.log"; echo "FAIL: the judge's default route is not vc"; exit 1; }
judge "NO MATCH"    acc2b     random:40,8
# the same two on fp, named
judge "MATCH FOUND" acc2      random:40,8 sim:fp
judge "NO MATCH"    acc2b     random:40,8 sim:fp
judge "NO MATCH"    acc2_r0   random:10,1
# the same run, 2000 steps long: the search must stop at the NO MATCH, not simulate the whole run
judge "NO MATCH"    acc2_r0   random:2000,1
grep -qE '\] [0-9]+ of 2000 steps searched' "$OUT/judge_$N.log" && ! grep -q '\] 2000 of 2000 steps' "$OUT/judge_$N.log" \
  || { tail -4 "$OUT/judge_$N.log"; echo "FAIL: the judge did not stop at the NO MATCH"; exit 1; }
judge "MATCH FOUND" acc2_slot random:20,2
grep -q 'STALL CHOICE WRONG' "$OUT/judge_$N.log" || { tail -4 "$OUT/judge_$N.log"; echo "FAIL: wrong stall choice not reported"; exit 1; }
judge "MATCH FOUND" pv1       random:40,8
judge "NO MATCH"    pv1b      random:40,8

has() { # <pattern> <what>: in the last judge log
  grep -qE -- "$1" "$OUT/judge_$N.log" || { tail -12 "$OUT/judge_$N.log"; echo "FAIL: $2"; exit 1; }
  echo "ok:   $2"
}
# classification: a delay, a value difference, a stutter
judge "NO MATCH"    r_same    random:40,4
has 'CLASS: DELAY\? \(out:v and the 1 word it guards read 1 step later' "a registered result read in the same cycle is classed DELAY?"
has 'D1 shows the same words at step [0-9]+, 1 step later' "the transaction view shows D1's words one step later"
TSV=$(grep -o 'as a trace: .*' "$OUT/judge_$N.log" | sed 's/as a trace: //')
[[ -s "$TSV" ]] || { echo "FAIL: no trace file for the NO MATCH"; exit 1; }
STEP=$(grep -o 'RESULT: NO MATCH at step [0-9]*' "$OUT/judge_$N.log" | grep -o '[0-9]*$')
judge "NO MATCH"    r_same    "trace:$TSV"
has "RESULT: NO MATCH at step $STEP:" "the NO MATCH's own trace gives the NO MATCH at the same step"
judge "MATCH FOUND" r_late    random:40,8
judge "NO MATCH"    rb_late   random:40,8
has 'CLASS: VALUE DIFFERENCE' "a payload one transfer behind is classed VALUE DIFFERENCE"
has 'differs: out:y@next\?out:v@next.0' "the transaction view names the differing word"
judge "NO MATCH"    acc2_r0   random:10,1
has 'CLASS: STUTTER\? \(maxstall 1' "a missing hold is classed STUTTER?"
judge "NO MATCH"    r_same    random:40,4 classify:0
! grep -q 'CLASS:' "$OUT/judge_$N.log" || { echo "FAIL: classify:0 still classifies"; exit 1; }
echo "ok:   classify:0 turns the classification off"
# timing lints
judge "MATCH FOUND" r_late    random:4,1
! grep -q 'warning:' "$OUT/judge_$N.log" || { grep warning "$OUT/judge_$N.log"; echo "FAIL: a lint on a consistent contract"; exit 1; }
judge "NO MATCH"    r_lint    random:20,2
has 'warning: observed word out:y@next\?out:v.0 reads its payload at cycle n\+1 and its guard at cycle n' "lint: payload and guard at different cycles"
judge "MATCH FOUND" r_lint2   random:20,2
has 'warning: xfer out:v reads v at cycle n, but the slot observes v only at cycle n\+1' "lint: an xfer read at the wrong cycle"
# the environment on random runs and on traces
judge "MATCH FOUND" r_env     random:40,8
has 'drawn again' "random runs draw a step that breaks asm again"
judge "ENVIRONMENT VIOLATED" r_env random:40,8 tries:0
printf 'cycle\tvin\tx\n0\t1\t3\n1\t0\t0\n' > "$OUT/env_bad.tsv"
judge "NO MATCH"    r_same_env "trace:$OUT/env_bad.tsv"
has 'environment: asm env_ok FAILS at step 0: this run is not a legal witness' "a trace that breaks asm is not a legal witness"
# the controls: D0 as the implementation
judge "CONTROLS PASS" r_late  random:40,4 control:pos delay:v=1,y=1
judge "CONTROLS FAIL" r_same  random:40,4 control:pos delay:v=1,y=1
has 'CONTRACT TOO STRICT' "the same-cycle contract rejects D0 with the change's delay"
judge "CONTROLS FAIL" r_late  random:40,4 control:pos
judge "CONTROLS PASS" r_late  random:40,4 control:neg delay:v=1,y=1
has 'y bit 0, every cycle: seen' "negative control: a flipped result is seen"
judge "CONTROLS PASS" r_late  random:40,4 control:neg delay:v=1,y=1 flip:y@v
judge "MATCH FOUND"   r_dead  random:40,4
judge "CONTROLS FAIL" r_dead  random:40,4 control:neg delay:v=1,y=1 flip:y@v
has 'y bit 0 while v = 1: NOT SEEN' "negative control: a guard that never opens hides the result"
judge "CONTROLS PASS" r_blind random:40,4 control:neg delay:v=1
has 'note: outputs both certificates have that no observed word reads \(the contract does not compare them\): y' "an output no word reads is noted"
# coverage: transactions per channel
judge "MATCH FOUND"   r_late  random:40,4
has 'transactions searched, per channel \(as the contract reads D1.s ports\): out:v@next.0 [1-9]' "the search counts its transactions"
judge "MATCH FOUND"   r_quiet random:40,4
has 'warning: channel out:v@next.0 carried no transaction on these runs' "a search with no transaction warns"
judge "CONTROLS FAIL" r_quiet random:40,4 control:pos delay:v=1,y=1
has 'not exercised: no transaction on out:v@next.0' "a control with no transaction fails"
printf 'cycle\tvin\tx\n0\t0\t0\n1\t0\t5\n' > "$OUT/quiet.tsv"
judge "CONTROLS PASS" r_late  "trace:$OUT/quiet.tsv" control:pos delay:v=1,y=1
has 'transactions on the positive control.s runs, per channel: out:v@next.0 0' "a control on a trace reports its counts and does not fail on them"

product() { # <name> <refmap> <d0> <d1> <k list> <expect>
  local nm=$1 rm=$2 d0=$3 d1=$4 ks=$5 expect=$6
  "$PY" "$ROOT/pass/lean/tools/scripts/mk_product.py" "$OUT/$rm.refmap" "$OUT/$d0.v" "$OUT/$d1.v" \
    "$OUT/$nm.sv" "$d0" "$d1" > "$OUT/$nm.gen.log" 2>&1 || { cat "$OUT/$nm.gen.log"; echo "FAIL: mk_product $nm"; exit 1; }
  cert "$nm" prod "$OUT/$nm.sv"
  local log="$OUT/$nm.search.log"
  drv sym_verify "$OUT/${nm}_lean/$nm.dcert" ok asm:env_ok base "k:$ks" "work:$OUT/${nm}_sw2" > "$log" 2>&1 || true
  local got
  got=$(grep -o 'RESULT: [A-Z -]*' "$log" | tail -1 | sed 's/RESULT: //; s/[ -]*$//')
  if [[ "$got" != "$expect"* ]]; then tail -8 "$log"; echo "FAIL: product search $nm -- expected $expect, got '$got'"; exit 1; fi
  echo "ok: product search $nm -> $got"
}
product prod_acc2b acc2b accS acc2b 2,4,8 "COUNTEREXAMPLE"
judge "NO MATCH" acc2b "trace:$(ls "$OUT"/prod_acc2b_sw2/*cex_inputs.tsv | head -1)"
product prod_acc2 acc2 accS acc2 2,4,8 "NO COUNTEREXAMPLE"
product prod_pv1b pv1b pv0 pv1b 4,8,16 "COUNTEREXAMPLE"
judge "NO MATCH" pv1b "trace:$(ls "$OUT"/prod_pv1b_sw2/*cex_inputs.tsv | head -1)"
# a guard on a 1-bit port (out:v.0) reads the port itself
product prod_r_same r_same rS rD 2,4,8 "COUNTEREXAMPLE"
grep -q 'd1_v ? d1_y' "$OUT/prod_r_same.sv" || { echo "FAIL: the 1-bit guard is not read as the port"; exit 1; }
judge "NO MATCH" r_same "trace:$(ls "$OUT"/prod_r_same_sw2/*cex_inputs.tsv | head -1)"
judge "CONTROLS FAIL" r_same "trace:$(ls "$OUT"/prod_r_same_sw2/*cex_inputs.tsv | head -1)" control:pos delay:v=1,y=1
has 'the trace ends at step [0-9]+: the classification reads on past it with random inputs drawn on D1' "a control on a short trace is re-read past its end"
# @next: the product runs D0 one step behind; a counterexample at cycle m is the judge's step m-1
product prod_r_late r_late rS rD 2,4,8 "NO COUNTEREXAMPLE"
grep -q 'D0 one step behind' "$OUT/prod_r_late.gen.log" || { cat "$OUT/prod_r_late.gen.log"; echo "FAIL: @next product mode not reported"; exit 1; }
product prod_rb_late rb_late rS rDb 2,4,8 "COUNTEREXAMPLE"
m=$(grep -o 'fails the property at cycle [0-9]*' "$OUT/prod_rb_late.search.log" | tail -1 | grep -o '[0-9]*$' || true)
[[ -n "$m" && "$m" -ge 1 ]] || { tail -4 "$OUT/prod_rb_late.search.log"; echo "FAIL: no counterexample cycle >= 1"; exit 1; }
judge "NO MATCH" rb_late "trace:$(ls "$OUT"/prod_rb_late_sw2/*cex_inputs.tsv | head -1)"
has "NO MATCH at step $((m - 1))" "the product's counterexample at cycle $m is the judge's NO MATCH at step $((m - 1))"
has "D1.s transaction #[0-9]+ \\(step [0-9]+\\) differs: out:y@next" "the @next product's counterexample shows the payload that differs"
has 'the trace ends at step [0-9]+: the classification reads on to step [0-9]+, past it with random inputs' "a short trace is re-read past its end"
has 'CLASS: VALUE DIFFERENCE' "past the trace's end, the skew is told apart from a delay"
printf '%s\n' "impl pv1b_lean/pv1b.dcert" "spec pv0_lean/pv0.dcert" "chan step allow:ok resp:x obs:dly_o" \
  "slot step 1 resp:in:x obs:out:y@next" "maxstall 0" "period 2" > "$OUT/pv_next.refmap"
if "$PY" "$ROOT/pass/lean/tools/scripts/mk_product.py" "$OUT/pv_next.refmap" "$OUT/pv0.v" "$OUT/pv1b.v" \
     "$OUT/prod_pv_next.sv" pv0 pv1b > "$OUT/prod_pv_next.gen.log" 2>&1 \
   || ! grep -q '@next with period 2' "$OUT/prod_pv_next.gen.log"; then
  cat "$OUT/prod_pv_next.gen.log"; echo "FAIL: mk_product accepted @next with period 2"; exit 1
fi
echo "ok: product, @next with period 2 -> refused"
echo "judge self-test: all passed"
