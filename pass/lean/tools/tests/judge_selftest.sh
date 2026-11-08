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
for d in accS acc2 acc2b pv0 pv1 pv1b; do cert "$d" "$d" "$OUT/$d.v"; done
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
echo "judge self-test: all passed"
