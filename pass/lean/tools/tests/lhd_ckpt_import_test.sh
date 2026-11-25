#!/usr/bin/env bash
set -euo pipefail
# `lhd_ckpt import-lhd`: the C++ simulator fast-forwards, a Lean route continues.
#
# A Pyrope design (an accumulator with a power-on value, a free-running counter, a 16-word RAM:
# every 4-bit address names a word)
# and its testbench run under `lhd sim` with a checkpoint every 10 cycles (`c<N>/`: state.bin,
# frame.bin, meta.json; the layout from sim/drv.tb.json and sim/<stem>.dut.json). The same design
# is exported to a Lean certificate. Each checkpoint is also written in the earlier layout
# (`ckp<N>/`: regs.json, one .hex per memory, meta.json) by an independent converter below.
# Checks:
#   import     every checkpoint imports, in both layouts, to byte-identical checkpoints (names from
#              the certificate's _io.json: flops by name, the memory by its cell's instance name)
#              that pass `lhd_ckpt check`
#   power-on   cert_trace from power-on for 10 cycles on the testbench's inputs ends in the
#              imported cycle-10 checkpoint
#   resume     from each imported checkpoint N, cert_trace (ir and vc) runs 10 cycles and ends
#              in the imported checkpoint N + 10
# Controls: a flipped register value resumes to a different state; a key no field has is refused,
# and imported with `fill` (a note names it); a memory word changed in state.bin resumes to a
# different state; a map file that names a missing memory is refused; the same two refusals in
# the earlier layout.
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
  reg arr:[16]U8 = nil
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

(cd "$OUT" && "$LHD" sim ck.prp --workdir sim --set sim.checkpoint_every=10 --set sim.checkpoint_max=10 \
   --set sim.checkpoint_min_secs=0 > sim.log 2>&1) \
  || { tail -5 "$OUT/sim.log"; echo "FAIL: lhd sim"; exit 1; }
S="$OUT/sim/ckpt/top_run"; SIM="$OUT/sim/sim"
for n in 10 20 30 40; do
  [[ -f "$S/c$n/state.bin" && -f "$S/c$n/meta.json" ]] || { ls "$S"; echo "FAIL: no checkpoint c$n"; exit 1; }
done

# the earlier layout, written from the current one: regs.json (flops; inputs as <inst>.__in.<name>),
# <inst>.<memory>.hex, meta.json (the cycle as a string)
cat > "$OUT/old_layout.py" <<'EOF'
import json, os, sys
ck, sim, out = sys.argv[1:4]
test = os.path.basename(os.path.dirname(ck.rstrip('/')))
man = json.load(open(f'{sim}/drv.tb.json'))
t = next(t for t in man['tests'] if ''.join(c if c.isalnum() else '_' for c in t['name']) == test)
whole = int.from_bytes(open(f'{ck}/state.bin', 'rb').read(), 'little')
bits = lambda off, w: (whole >> off) & ((1 << w) - 1)
store = lambda b: 8 if b <= 8 else 16 if b <= 16 else 32 if b <= 32 else (b + 63) // 64 * 64
os.makedirs(out, exist_ok=True)
regs = {}
for i in t['instances']:
    for f in json.load(open(f"{sim}/{i['stem']}.dut.json"))['fields']:
        off, b, key = (i['base'] + f['word']) * 64, f['bits'], f"{i['var']}.{f['name']}"
        if b == 0:
            continue
        if f['kind'] == 2:
            regs[key] = hex(bits(off, b))
        elif f['kind'] == 0:
            regs[f"{i['var']}.__in.{f['name']}"] = hex(bits(off, b))
        elif f['kind'] == 3:
            stride = b if store(b) != b and b * f['entries'] <= 128 else store(b)
            with open(f'{out}/{key}.hex', 'w') as h:
                for e in range(f['entries']):
                    h.write('%x\n' % bits(off + e * stride, b))
json.dump(regs, open(f'{out}/regs.json', 'w'), indent=1)
json.dump({'cycle': str(json.load(open(f'{ck}/meta.json'))['cycle'])}, open(f'{out}/meta.json', 'w'))
EOF
# set <key> (entry <e> of a memory, or - for a flop) to <value> in a copy's state.bin
cat > "$OUT/patch_state.py" <<'EOF'
import json, os, sys
ck, sim, key, entry, value = sys.argv[1:6]
test = os.path.basename(os.path.dirname(ck.rstrip('/')))
man = json.load(open(f'{sim}/drv.tb.json'))
t = next(t for t in man['tests'] if ''.join(c if c.isalnum() else '_' for c in t['name']) == test)
raw = open(f'{ck}/state.bin', 'rb').read()
whole = int.from_bytes(raw, 'little')
store = lambda b: 8 if b <= 8 else 16 if b <= 16 else 32 if b <= 32 else (b + 63) // 64 * 64
for i in t['instances']:
    for f in json.load(open(f"{sim}/{i['stem']}.dut.json"))['fields']:
        if f"{i['var']}.{f['name']}" != key:
            continue
        b, off = f['bits'], (i['base'] + f['word']) * 64
        if entry != '-':
            off += int(entry) * (b if store(b) != b and b * f['entries'] <= 128 else store(b))
        whole = (whole & ~(((1 << b) - 1) << off)) | ((int(value) & ((1 << b) - 1)) << off)
        open(f'{ck}/state.bin', 'wb').write(whole.to_bytes(len(raw), 'little'))
        sys.exit(0)
sys.exit(f'no field {key}')
EOF
for n in 10 20 30 40; do
  python3 "$OUT/old_layout.py" "$S/c$n" "$SIM" "$OUT/old/top_run/ckp$n" || { echo "FAIL: old layout of c$n"; exit 1; }
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

I="$OUT/imp"; mkdir -p "$I"
for n in 10 20 30 40; do
  expect 0 "OK: imported cycle $n: 2 flops, 1 memories (instance \`t\`)" ck import-lhd "$C" "$S/c$n" "$I/$n.ckpt"
  expect 0 "OK: cycle $n" ck check "$C" "$I/$n.ckpt"
  expect 0 "OK: imported cycle $n: 2 flops, 1 memories (instance \`t\`)" ck import-lhd "$C" "$OUT/old/top_run/ckp$n" "$I/old$n.ckpt"
  cmp -s "$I/$n.ckpt" "$I/old$n.ckpt" || { echo "FAIL: c$n and ckp$n import differently"; exit 1; }
done
expect 0 "" ct "$C" 10 "$OUT/tb.stim" ckpt-out:"$I/lean10.ckpt"
expect 0 "SAME" ck diff "$I/lean10.ckpt" "$I/10.ckpt"
for n in 10 20 30; do
  for rt in ir vc; do
    expect 0 "" ct "$C" 10 "$OUT/tb.stim" route:$rt ckpt-in:"$I/$n.ckpt" ckpt-out:"$I/r$n.$rt.ckpt"
    expect 0 "SAME" ck diff "$I/r$n.$rt.ckpt" "$I/$((n + 10)).ckpt"
  done
done

# controls, on copies of c20 (a copy keeps its test's folder name; sim: names the layout files)
K="$OUT/ctl"; mkdir -p "$K"
cpy() { mkdir -p "$K/$1/top_run"; cp -r "$S/c20" "$K/$1/top_run/c20"; }
cpy flip; python3 "$OUT/patch_state.py" "$K/flip/top_run/c20" "$SIM" t.acc - 7 || { echo "FAIL: the flip control was not made"; exit 1; }
expect 0 "OK: imported" ck import-lhd "$C" "$K/flip/top_run/c20" "$K/flip.ckpt" sim:"$SIM"
expect 0 "" ct "$C" 10 "$OUT/tb.stim" ckpt-in:"$K/flip.ckpt" ckpt-out:"$K/flip30.ckpt"
expect 1 "DIFFER: flop" ck diff "$K/flip30.ckpt" "$I/30.ckpt"
printf 'flop 1 t.cnt_old\n' > "$K/miss.map"
expect 1 "REFUSED: flop 1 (cnt): state.bin has no key \`t.cnt_old\`" ck import-lhd "$C" "$S/c20" "$K/miss.ckpt" map:"$K/miss.map"
expect 0 "note: flop 1 (cnt): no key \`t.cnt_old\`, took its power-on value" ck import-lhd "$C" "$S/c20" "$K/miss.ckpt" map:"$K/miss.map" fill
# word 5, which no cycle of the testbench writes, set in state.bin
cpy word; python3 "$OUT/patch_state.py" "$K/word/top_run/c20" "$SIM" t.arr 5 7 || { echo "FAIL: the word control was not made"; exit 1; }
expect 0 "OK: imported" ck import-lhd "$C" "$K/word/top_run/c20" "$K/word.ckpt" sim:"$SIM"
expect 0 "" ct "$C" 10 "$OUT/tb.stim" ckpt-in:"$K/word.ckpt" ckpt-out:"$K/word30.ckpt"
expect 1 "DIFFER: memory 0 word 0x5" ck diff "$K/word30.ckpt" "$I/30.ckpt"
printf 'mem 0 t.nosuch\n' > "$K/bad.map"
expect 1 "REFUSED: memory 0 (arr_1): no memory \`t.nosuch\`" ck import-lhd "$C" "$S/c20" "$K/x.ckpt" map:"$K/bad.map"
# the earlier layout's refusals
O="$OUT/old/top_run/ckp20"
mkdir -p "$K/oldmiss"; cp -r "$O" "$K/oldmiss/ckp20"; sed -i 's/"t.cnt"/"t.cnt_old"/' "$K/oldmiss/ckp20/regs.json"
expect 1 "REFUSED: flop 1 (cnt): regs.json has no key \`t.cnt\`" ck import-lhd "$C" "$K/oldmiss/ckp20" "$K/oldmiss.ckpt"
printf 'mem 0 t.nosuch.hex\n' > "$K/oldbad.map"
expect 1 "REFUSED: memory 0 (arr_1): no file \`t.nosuch.hex\`" ck import-lhd "$C" "$O" "$K/x.ckpt" map:"$K/oldbad.map"

echo "PASS lhd_ckpt_import_test: $N driver runs"
