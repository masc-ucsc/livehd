#!/usr/bin/env bash
set -euo pipefail
# A memory written on TWO clocks through the Lean routes (decision #9).
#
# The design is one 16 x 8 array with two ports: port a on clk_a and port b on
# clk_b, each a write and a registered read. The certificate gives the memory a
# commit table (DCERT3 `MemoryDesc.commits`): `nextImg` is the image after both
# ports' writes, taken when both clocks rise; the entry for {clk_a} holds only
# port a's write and the entry for {clk_b} only port b's.
#
# The stimulus raises each clock on its own random schedule, so a step can have
# both clocks, one or none. When both rise and both write, the addresses differ:
# two always blocks writing one entry at the same instant is a Verilog race.
#
# Checks:
#   export   the certificate carries the commit table (Lean literal and DCERT3).
#   IR, TV   cert_trace equals iverilog on every step, under the random
#            schedule and with both clocks every step.
#   legacy   the legacy model, with its proof bridge (`_next_refines_fast`,
#            elaborated without sorry), equals iverilog under the random
#            schedule.
#   FP       admits the memory, executes it and compares it with the reference
#            itself (RESULT MEMROUTE OK), and its trace equals IR under its own
#            independent schedule (`edges:indep`: clock j fires on step k unless
#            (k + j) is a multiple of (j + 2)), given to cert_trace as a file.
#   D3       translation validation (d3_sweep.py, its default generator) passes
#            every gate, the proof included, and rejects a memory mutation.
# Controls:
#   the random schedule's trace differs from the both-clocks trace;
#   a certificate whose two entries are swapped differs from iverilog;
#   a certificate with the table removed (the one-clock rule) differs.
#
# The Lean toolchain pin lives in formal/lean; lean runs from there.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/two_clock_mem_oracle}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
command -v iverilog >/dev/null && command -v vvp >/dev/null || { echo "iverilog/vvp not on PATH" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"
STEPS=60
LP=$(cd "$ROOT/formal/lean" && lake env printenv LEAN_PATH)

cat > "$OUT/ram2c.v" <<'EOF'
module ram2c(input clk_a, input clk_b,
             input a_we, input [3:0] a_addr, input [7:0] a_wdata, output reg [7:0] a_rdata,
             input b_we, input [3:0] b_addr, input [7:0] b_wdata, output reg [7:0] b_rdata);
  reg [7:0] m [0:15];
  always @(posedge clk_a) begin
    if (a_we) m[a_addr] <= a_wdata;
    a_rdata <= m[a_addr];
  end
  always @(posedge clk_b) begin
    if (b_we) m[b_addr] <= b_wdata;
    b_rdata <= m[b_addr];
  end
endmodule
EOF

# ---- export ---------------------------------------------------------------------
(cd "$OUT" && "$LHD" compile ram2c.v --reader slang --top ram2c --emit-dir lg:lg --workdir cw > compile.log 2>&1) \
  || { tail -3 "$OUT/compile.log"; echo "FAIL: compile"; exit 1; }
(cd "$OUT" && "$LHD" compile lg:lg --top ram2c --emit-dir lean:vc --set formal.lean.mode=verified_compiler \
   --workdir vw > vc.log 2>&1) || { grep -o '"message":"[^"]*' "$OUT/vc.log" | head -2; echo "FAIL: the export refused the RAM"; exit 1; }
D="$OUT/vc/ram2c.dcert"
grep -q 'commits := #\[{ mask := 1, nextImg := [0-9]* }, { mask := 2, nextImg := [0-9]* }\]' "$OUT/vc/ram2c_Lgraph.lean" \
  || { echo "FAIL: the Lean literal carries no two-entry commit table"; exit 1; }
# The DCERT3 memory record: aw dw nextImg clock nInit [init..] nCommits (mask img).. hasAsync asyncImg.
rec=$(python3 -I - "$D" <<'PY'
import sys
lines = open(sys.argv[1], "rb").read()[7:].split(b"\n")
i = 0
for sec in range(4):                     # sources, nodes, outputs, flops
    n = int(lines[i]); i += 1 + n
assert int(lines[i]) == 1, "expected one memory"
print(lines[i + 1].decode())
PY
)
read -r -a f <<< "$rec"
(( ${#f[@]} == 12 && f[4] == 0 && f[5] == 2 && f[6] == 1 && f[8] == 2 && f[10] == 0 )) \
  || { echo "FAIL: unexpected DCERT3 memory record: $rec"; exit 1; }
echo "ok: export -- one memory with a two-entry commit table ($rec)"

# ---- stimulus, schedules and the source testbenches -------------------------------
python3 -I - "$OUT" "$STEPS" "$OUT/vc/ram2c_io.json" <<'PY'
import json, random, sys
out, steps, io = sys.argv[1], int(sys.argv[2]), json.load(open(sys.argv[3]))
ins = {i["name"]: (i["ordinal"], i["width"]) for i in io["inputs"]}
clocks = sorted(io["clocks"], key=lambda c: c["ordinal"])
assert [c["name"] for c in clocks] == ["clk_a", "clk_b"], clocks   # the edge columns, in ordinal order
random.seed(11)
rows, sched = [], []
for k in range(steps):
    ea, eb = random.randrange(2), random.randrange(2)
    r = {n: random.randrange(1 << w) for n, (o, w) in ins.items() if not n.startswith("clk")}
    r["a_we"] = 1 if random.random() < 0.6 else 0
    r["b_we"] = 1 if random.random() < 0.6 else 0
    if k < 16:  # fill every entry through port a first, both clocks rising
        ea, eb = 1, 1
        r.update(a_we=1, a_addr=k, b_we=0)
    if ea and eb and r["a_we"] and r["b_we"] and r["a_addr"] == r["b_addr"]:
        r["b_addr"] = (r["a_addr"] + 1) % 16
    rows.append(r); sched.append((ea, eb))
with open(f"{out}/stim.txt", "w") as f:
    f.write("stim v1\n")
    for k, r in enumerate(rows):
        for n, v in r.items():
            o, w = ins[n]
            f.write(f"at {k} in {o} {w} {v}\n")
for name, s in (("edges.txt", sched), ("edges_all.txt", [(1, 1)] * steps)):
    with open(f"{out}/{name}", "w") as f:
        f.write("edges v1\n")
        for k, (ea, eb) in enumerate(s):
            f.write(f"at {k} {ea} {eb}\n")
outs = sorted(io["outputs"], key=lambda o: o["ordinal"])
for name, s in (("tb.v", sched), ("tb_all.v", [(1, 1)] * steps)):
    with open(f"{out}/{name}", "w") as f:
        f.write("`timescale 1ns/1ps\nmodule tb;\n integer i;\n reg clk_a = 0, clk_b = 0;\n")
        for n, (o, w) in ins.items():
            if not n.startswith("clk"):
                f.write(f" reg [{w-1}:0] {n} = 0;\n")
        for o in outs:
            f.write(f" wire [{o['width']-1}:0] {o['name']};\n")
        f.write(" ram2c u(" + ", ".join(f".{n}({n})" for n in list(ins) + [o["name"] for o in outs]) + ");\n")
        f.write(" initial begin\n  for (i = 0; i < 16; i = i + 1) u.m[i] = 0;\n  u.a_rdata = 0; u.b_rdata = 0;\n")
        fmt, names = " ".join("%0d" for _ in outs), ", ".join(o["name"] for o in outs)
        for k, r in enumerate(rows):
            t = 10 * k
            f.write(f"  #({t+1} - $time); " + " ".join(f"{n} = {v};" for n, v in r.items()) + "\n")
            f.write(f'  #({t+4} - $time); $display("STEP {k} {fmt}", {names});\n')
            f.write(f"  #({t+5} - $time); clk_a = {s[k][0]}; clk_b = {s[k][1]};\n")
            f.write(f"  #({t+8} - $time); clk_a = 0; clk_b = 0;\n")
        f.write("  $finish;\n end\nendmodule\n")
both = sum(1 for e in sched[16:] if e == (1, 1)); one = sum(1 for e in sched[16:] if sum(e) == 1)
print(f"ok: {steps} steps; after the fill: {both} with both clocks, {one} with one, {steps-16-both-one} with none")
PY
for tb in tb tb_all; do
  (cd "$OUT" && iverilog -g2012 -o $tb.vvp ram2c.v $tb.v > $tb.iverilog.log 2>&1 && vvp $tb.vvp | grep '^STEP' > rtl_$tb.txt) \
    || { cat "$OUT/$tb.iverilog.log"; echo "FAIL: iverilog $tb"; exit 1; }
done
grep -q x "$OUT/rtl_tb.txt" && { echo "FAIL: the source read x"; exit 1; }

ct() { # <dcert> <name> <opts...>
  local d=$1 n=$2; shift 2
  (cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run \
     Futamura_Projection_Scripts/cert_trace.lean "$d" "$STEPS" "$OUT/stim.txt" "$@" \
     > "$OUT/$n.txt" 2> "$OUT/$n.err") || { cat "$OUT/$n.err"; echo "FAIL: cert_trace $n"; exit 1; }
}

# ---- IR and TV ------------------------------------------------------------------
ct "$D" ir edges:file:"$OUT/edges.txt"
ct "$D" tv edges:file:"$OUT/edges.txt" route:tv
ct "$D" ir_all edges:file:"$OUT/edges_all.txt"
ct "$D" tv_all edges:file:"$OUT/edges_all.txt" route:tv
for n in ir tv; do
  diff "$OUT/rtl_tb.txt" "$OUT/$n.txt" > "$OUT/diff_$n.txt" \
    || { head -4 "$OUT/diff_$n.txt"; echo "FAIL: $n differs from the source under the random schedule"; exit 1; }
  diff "$OUT/rtl_tb_all.txt" "$OUT/${n}_all.txt" > "$OUT/diff_${n}_all.txt" \
    || { head -4 "$OUT/diff_${n}_all.txt"; echo "FAIL: $n differs from the source with both clocks every step"; exit 1; }
done
echo "ok: IR and TV equal iverilog on all $STEPS steps, under the random schedule and with both clocks every step"
n=$(diff "$OUT/ir.txt" "$OUT/ir_all.txt" | grep -c '^<' || true)
[[ $n -gt 0 ]] || { echo "FAIL: the random schedule gave the same trace as both clocks every step"; exit 1; }
echo "ok: control -- the random schedule differs from both clocks every step on $n steps"

# ---- controls: the commit table changed -------------------------------------------
# Record fields: 0 aw, 1 dw, 2 nextImg, 3 clock, 4 nInit, 5 nCommits, 6 mask, 7 image,
# 8 mask, 9 image, 10 hasAsync, 11 asyncImg.
for c in swap none; do
  case $c in
    swap) new="${f[*]:0:7} ${f[9]} ${f[8]} ${f[7]} ${f[10]} ${f[11]}"; what="its two entries' images swapped" ;;
    none) new="${f[*]:0:5} 0 0 0"; what="no commit table (the one-clock rule)" ;;
  esac
  python3 -I - "$D" "$OUT/$c.dcert" "$rec" "$new" <<'PY'
import sys
src, dst, old, new = sys.argv[1:5]
b = open(src, "rb").read().split(b"\n")
hits = [i for i, l in enumerate(b) if l == old.encode()]
assert len(hits) == 1, hits
b[hits[0]] = new.encode()
open(dst, "wb").write(b"\n".join(b))
PY
  ct "$OUT/$c.dcert" "ctl_$c" edges:file:"$OUT/edges.txt"
  n=$(diff "$OUT/rtl_tb.txt" "$OUT/ctl_$c.txt" | grep -c '^<' || true)
  [[ $n -gt 0 ]] || { echo "FAIL: the $c certificate matched the source"; exit 1; }
  echo "ok: control -- the certificate with $what differs from the source on $n steps"
done

# ---- legacy: the model with its proof bridge ----------------------------------------
L="$OUT/legacy"
mkdir -p "$L"
(cd "$L" && "$LHD" compile lg:../lg --top ram2c --emit-dir lean:model --set formal.lean.emit_cert=true \
   --set formal.lean.emit_fast_bridge=true --workdir lw > model.log 2>&1) \
  || { grep -o '"message":"[^"]*' "$L/model.log" | head -2; echo "FAIL: legacy emission"; exit 1; }
LR="$L/lroot"; OL="$L/olean"
mkdir -p "$LR/LegacyRam2c" "$OL/LegacyRam2c"
cp "$L/model/ram2c_Lgraph.lean" "$LR/LegacyRam2c/"
python3 -I "$ROOT/pass/lean/Legacy_Fast_Model_Simulator/scripts/make_legacy_sim.py" "$LR/LegacyRam2c/ram2c_Lgraph.lean" \
  --cycles "$STEPS" --stim --out "$LR/LegacyRam2c" --module-prefix LegacyRam2c > "$L/gen.log" 2>&1 \
  || { tail -3 "$L/gen.log"; echo "FAIL: make_legacy_sim"; exit 1; }
(cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP:$OL" lean -R "$LR" -o "$OL/LegacyRam2c/ram2c_Lgraph.olean" \
   "$LR/LegacyRam2c/ram2c_Lgraph.lean" > "$L/build.log" 2>&1) \
  || { grep -m5 error "$L/build.log"; echo "FAIL: legacy elaboration (bridge proofs)"; exit 1; }
grep -q sorry "$L/build.log" && { echo "FAIL: sorry in the legacy model"; exit 1; }
grep -q 'theorem ram2c_next_refines_fast' "$LR/LegacyRam2c/ram2c_Lgraph.lean" \
  || { echo "FAIL: the bridge theorem for the next state was not emitted"; exit 1; }
(cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP:$OL" lean --run "$LR/LegacyRam2c/ram2c_LegacySim.lean" \
   "$OUT/stim.txt" "$OUT/edges.txt" > "$L/legacy.txt" 2> "$L/legacy.err") || { cat "$L/legacy.err"; echo "FAIL: legacy run"; exit 1; }
diff "$OUT/rtl_tb.txt" "$L/legacy.txt" > "$L/diff.txt" || { head -4 "$L/diff.txt"; echo "FAIL: legacy differs from the source"; exit 1; }
echo "ok: the legacy model (bridge proofs elaborated) equals iverilog on all $STEPS steps under the random schedule"

# ---- FP: admitted, executed, equal to IR ---------------------------------------------
python3 -I - "$OUT/edges_indep.txt" "$STEPS" <<'PY'
import sys
out, n = sys.argv[1], int(sys.argv[2])
with open(out, "w") as f:
    f.write("edges v1\n")
    for k in range(n):
        f.write(f"at {k} {int((k + 0) % 2 != 0)} {int((k + 1) % 3 != 0)}\n")
PY
(cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run \
   Futamura_Projection_Scripts/dcert2_memroute.lean "$D" 20000 200 "$STEPS" zero edges:indep \
   "stim:$OUT/stim.txt" outputs > "$OUT/fp.log" 2> "$OUT/fp.err") \
  || { tail -5 "$OUT/fp.log"; cat "$OUT/fp.err"; echo "FAIL: FP runner"; exit 1; }
grep -q 'RESULT: MEMROUTE OK' "$OUT/fp.log" || { grep RESULT "$OUT/fp.log"; echo "FAIL: FP did not run the memory"; exit 1; }
grep '^STEP' "$OUT/fp.log" > "$OUT/fp.txt"
ct "$D" ir_indep edges:file:"$OUT/edges_indep.txt"
diff "$OUT/ir_indep.txt" "$OUT/fp.txt" > "$OUT/diff_fp.txt" \
  || { head -4 "$OUT/diff_fp.txt"; echo "FAIL: FP differs from IR"; exit 1; }
echo "ok: FP admits and runs the memory (MEMROUTE OK) and equals IR on all $STEPS steps"

# ---- D3: translation validation, with its proof -------------------------------------
D3="$OUT/d3"
mkdir -p "$D3/certs"
cp "$OUT/vc/ram2c_Lgraph.lean" "$D3/certs/"
(cd "$ROOT" && env -u OUT -u TMPDIR python3 -I pass/lean/Translation_Validation/scripts/d3_sweep.py \
   --certs "$D3/certs" --out "$D3/out" --jobs 1 --timeout 900 --prove > "$D3/sweep.log" 2>&1) \
  || { tail -5 "$D3/sweep.log"; echo "FAIL: D3 sweep"; exit 1; }
python3 -I - "$D3/out" <<'PY'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1]), delimiter="\t"))
if len(rows) != 1:
    sys.exit(f"FAIL: D3 wrote {len(rows)} rows")
r = rows[0]
gates = "".join(r[k] for k in ("cert", "compile", "reify", "typecheck", "sim", "checker", "agree", "proof"))
if gates != "11111111" or r["mut_mem"] != "rejected" or r["drift"]:
    sys.exit(f"FAIL: D3 {r['verdict']}: gates {gates}, mut_mem {r['mut_mem']}, drift {r['drift'] or '-'}")
print("ok: D3 -- every gate passes, the proof included; a memory mutation is rejected")
PY
echo "PASS: two_clock_mem_oracle"
