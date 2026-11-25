#!/usr/bin/env bash
set -euo pipefail
# `lhd_ckpt import-lhd`: the C++ simulator fast-forwards, a Lean route continues.
#
# A Pyrope design (an accumulator with a power-on value, a free-running counter, a 12-word RAM)
# and its testbench run under `lhd sim` with a checkpoint every 10 cycles, in both sim flows
# (cgen: LHD_SIM_LNAST=0; LNAST: the default). The same design is exported to a Lean
# certificate. Checks, per flow:
#   import     every checkpoint imports (names from the certificate's _io.json: flops by name,
#              the memory by its cell's instance name) and passes `lhd_ckpt check`
#   power-on   cert_trace from power-on for 10 cycles on the testbench's inputs ends in the
#              imported cycle-10 checkpoint
#   resume     from each imported checkpoint N, cert_trace (ir and vc) runs 10 cycles and ends
#              in the imported checkpoint N + 10
# Controls (cgen flow): a flipped register value resumes to a different state; a missing key
# is refused, and imported with `fill` (a note names it); a memory dump with one word changed
# resumes to a different state; a map file that names a missing dump is refused.
#
# CK_NAT=<dir with native lhd_ckpt and cert_trace> uses them; otherwise the drivers run
# interpreted against formal/lean/.olean-dev.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/lhd_ckpt_import_test}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
CK_NAT="${CK_NAT:-}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"

cat > "$OUT/ck.prp" <<'EOF'
/*
:name: ck
:type: simulation
*/
mod top(en:Bool, din:U8, wa:U4, we:Bool) -> (fout:U8@[0], cout:U8@[0], mout:U8@[0]) {
  reg acc:U8 = 5
  reg cnt:U8 = 0
  if en { wrap acc += din }
  wrap cnt += 1
  fout = acc
  cout = cnt
  reg arr:[12]U8 = nil
  if we { arr[wa] = din }
  mout = arr[3]
}
test top.run {
  mut t = top
  tick 45 {
    t.en = true; t.din = 3; t.wa = 3; t.we = true
    step
  }
}
EOF
# the testbench's inputs as a stim v1 file (ordinals: clock 0, din 1, en 2, reset 3, wa 4, we 5)
printf 'stim v1\nin 1 8 3\nin 2 1 1\nin 4 4 3\nin 5 1 1\n' > "$OUT/tb.stim"

(cd "$OUT" && "$LHD" compile ck.prp --top top --emit-dir lg:top_lg --workdir cw > compile.log 2>&1) \
  || { tail -5 "$OUT/compile.log"; echo "FAIL: compile"; exit 1; }
(cd "$OUT" && "$LHD" compile lg:top_lg --top top --workdir lw --emit-dir lean:top_lean \
   --set formal.lean.mode=verified_compiler > lean.log 2>&1) \
  || { grep -o '"message":"[^"]*' "$OUT/lean.log" | head -3; echo "FAIL: certificate"; exit 1; }
C="$OUT/top_lean/top.dcert"
grep -q '"inst": "arr"' "$OUT/top_lean/top_io.json" || { cat "$OUT/top_lean/top_io.json"; echo "FAIL: the sidecar does not name the memory"; exit 1; }

for flow in cgen lnast; do
  if [[ $flow == cgen ]]; then lnast=0; else lnast=1; fi
  (cd "$OUT" && LHD_SIM_LNAST=$lnast "$LHD" sim ck.prp --workdir "sim_$flow" \
     --set sim.checkpoint_every=10 --set sim.checkpoint_max=10 > "sim_$flow.log" 2>&1) \
    || { tail -5 "$OUT/sim_$flow.log"; echo "FAIL: lhd sim ($flow)"; exit 1; }
  for n in 10 20 30 40; do
    [[ -d "$OUT/sim_$flow/ckpt/top_run/ckp$n" ]] || { ls "$OUT/sim_$flow/ckpt/top_run"; echo "FAIL: no ckp$n ($flow)"; exit 1; }
  done
done

N=0
drv() { # <native name> <source> args... -> RC, LOG
  local name=$1 src=$2; shift 2
  LOG="$OUT/run_$(( ++N )).log"
  if [[ -n "$CK_NAT" ]]; then
    "$CK_NAT/$name" "$@" > "$LOG" 2>&1 && RC=0 || RC=$?
  else
    (cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean --run "$src" "$@") > "$LOG" 2>&1 && RC=0 || RC=$?
  fi
}
ck() { drv lhd_ckpt "$ROOT/pass/lean/tools/sim/lhd_ckpt.lean" "$@"; }
ct() { drv cert_trace "$ROOT/formal/lean/Futamura_Projection_Scripts/cert_trace.lean" "$@"; }
expect() { # <exit code> <text> <fn> args...
  local want=$1 text=$2 fn=$3; shift 3
  "$fn" "$@"
  if [[ "$RC" != "$want" ]] || { [[ -n "$text" ]] && ! grep -qF -- "$text" "$LOG"; }; then
    cat "$LOG"; echo "FAIL: $fn $* -- expected exit $want and '$text', got exit $RC"; exit 1
  fi
}

for flow in cgen lnast; do
  S="$OUT/sim_$flow/ckpt/top_run"
  I="$OUT/imp_$flow"; mkdir -p "$I"
  for n in 10 20 30 40; do
    expect 0 "OK: imported cycle $n: 2 flops, 1 memories (instance \`t\`)" ck import-lhd "$C" "$S/ckp$n" "$I/$n.ckpt"
    expect 0 "OK: cycle $n" ck check "$C" "$I/$n.ckpt"
  done
  expect 0 "" ct "$C" 10 "$OUT/tb.stim" ckpt-out:"$I/lean10.ckpt"
  expect 0 "SAME" ck diff "$I/lean10.ckpt" "$I/10.ckpt"
  for n in 10 20 30; do
    for rt in ir vc; do
      expect 0 "" ct "$C" 10 "$OUT/tb.stim" route:$rt ckpt-in:"$I/$n.ckpt" ckpt-out:"$I/r$n.$rt.ckpt"
      expect 0 "SAME" ck diff "$I/r$n.$rt.ckpt" "$I/$((n + 10)).ckpt"
    done
  done
done
cmp -s "$OUT/imp_cgen/20.ckpt" "$OUT/imp_lnast/20.ckpt" || { echo "FAIL: the two flows' checkpoints import differently"; exit 1; }

# controls
S="$OUT/sim_cgen/ckpt/top_run"
K="$OUT/ctl"; mkdir -p "$K"
cp -r "$S/ckp20" "$K/flip"; sed -i 's/"t.acc": "\(0x[0-9a-f]*\)"/"t.acc": "0x7"/' "$K/flip/regs.json"
grep -q '"t.acc": "0x7"' "$K/flip/regs.json" || { cat "$K/flip/regs.json"; echo "FAIL: the flip control was not made"; exit 1; }
expect 0 "OK: imported" ck import-lhd "$C" "$K/flip" "$K/flip.ckpt"
expect 0 "" ct "$C" 10 "$OUT/tb.stim" ckpt-in:"$K/flip.ckpt" ckpt-out:"$K/flip30.ckpt"
expect 1 "DIFFER: flop" ck diff "$K/flip30.ckpt" "$OUT/imp_cgen/30.ckpt"
cp -r "$S/ckp20" "$K/miss"; sed -i 's/"t.cnt"/"t.cnt_old"/' "$K/miss/regs.json"
expect 1 "REFUSED: flop 1 (cnt): regs.json has no key \`t.cnt\`" ck import-lhd "$C" "$K/miss" "$K/miss.ckpt"
expect 0 "note: flop 1 (cnt): no key \`t.cnt\`, took its power-on value" ck import-lhd "$C" "$K/miss" "$K/miss.ckpt" fill
# word 5, which no cycle of the testbench writes, set in the dump
cp -r "$S/ckp20" "$K/word"; sed -i '6s/.*/7/' "$K/word/t.arr.hex"
expect 0 "OK: imported" ck import-lhd "$C" "$K/word" "$K/word.ckpt"
expect 0 "" ct "$C" 10 "$OUT/tb.stim" ckpt-in:"$K/word.ckpt" ckpt-out:"$K/word30.ckpt"
expect 1 "DIFFER: memory 0 word 0x5" ck diff "$K/word30.ckpt" "$OUT/imp_cgen/30.ckpt"
printf 'mem 0 t.nosuch.hex\n' > "$K/bad.map"
expect 1 "REFUSED: memory 0 (arr_1): no file \`t.nosuch.hex\`" ck import-lhd "$C" "$S/ckp20" "$K/x.ckpt" map:"$K/bad.map"

echo "PASS lhd_ckpt_import_test: $N driver runs"
