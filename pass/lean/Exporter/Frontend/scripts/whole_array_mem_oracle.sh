#!/usr/bin/env bash
set -euo pipefail
# Whole-array memories through the Lean routes (decision #2).
#
# A WHOLE-ARRAY Memory cell (graph/cell.cpp pins 11-14) has a bulk `update` bus
# loaded when `update_enable` is set, per-port writes over it, and a `reset`
# that restores `init` (a runtime bus or a constant; absent, 0) over both; a
# `read_all` output reads the whole committed array as one bus. The certificate
# spells that with two image operators, `Op_MemLoad n` and `Op_MemToBus n`
# (LGraphModel.lean): the next image is MemLoad(update) -> the port writes ->
# MemLoad(reset), as cgen and pass/lec order the cell.
#
# The designs are rows of the Pyrope equivalence corpus
# (inou/prp/tests/equiv/<row>.prp with its <row>.v golden). For each row:
#   export   lhd compile -> single_edge -> the verified_compiler certificate,
#            which must contain Op_MemLoad or Op_MemToBus;
#   IR, TV   cert_trace (interpretDesign; route:tv the compiled program) equals
#            iverilog on the golden at every cycle, on a seeded random stimulus.
#            A reset input named `reset` is held high for two cycles and then
#            low; any other input (including a memory's own reset pin) is
#            random on every cycle.
#   legacy   the legacy model, with its proof bridge (`_next_refines_fast`,
#            elaborated without sorry; Translation/WholeArrayBridge), equals
#            iverilog at every cycle.
#   FP       admits the certificate, specializes and executes it and compares
#            it with the reference itself (RESULT MEMROUTE OK), and its trace
#            equals IR's under FP's own schedule (FP `edges:all` = cert_trace
#            `edges:third`: every third cycle quiet; FP has no every-cycle mode).
#   D3       translation validation (d3_sweep.py, its default generator) passes
#            every gate on every row's certificate, the proof included, and
#            rejects a memory mutation.
# Controls (a wrong certificate must differ):
#   a MemLoad that loads entry 0 only (its count set to 1);
#   a MemToBus that packs one entry fewer.
#   IR on each must differ from the golden, and FP on each from IR on the
#   unchanged certificate.
#
# Rows: mem_whole_reg (a bulk update and read_all, no enable, no reset),
# mem_bulk_vs_entry_order (update, update enable, port writes and reset on seven
# memories), spec_memory_reset_priority and spec_memory_poweron_not_reset (a
# reset restoring a constant that is also the power-on contents),
# spec_memory_bulk_read_visibility (reads before and after a bulk clear, with
# three forwarding policies), clocked_array_flags (read_all of a per-port
# memory).
#
# The Lean toolchain pin lives in formal/lean; lean runs from there.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/whole_array_mem_oracle}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
CYCLES="${CYCLES:-40}"
ROWS="${ROWS:-mem_whole_reg mem_bulk_vs_entry_order spec_memory_reset_priority spec_memory_poweron_not_reset spec_memory_bulk_read_visibility clocked_array_flags}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
command -v iverilog >/dev/null && command -v vvp >/dev/null || { echo "iverilog/vvp not on PATH" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"
cd "$ROOT/formal/lean"
LP=$(lake env printenv LEAN_PATH)
EQ="$ROOT/inou/prp/tests/equiv"
# the legacy bridge's lemmas are not in the package root
lake build LeanSemanticPrimitives.Translation.WholeArrayBridge > "$OUT/lake.log" 2>&1 \
  || { tail -5 "$OUT/lake.log"; echo "FAIL: lake build WholeArrayBridge"; exit 1; }

ct() { # <dcert> <stim> <out> <cert_trace options...>
  local dc=$1 st=$2 o=$3; shift 3
  (ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run Futamura_Projection_Scripts/cert_trace.lean \
     "$dc" "$CYCLES" "$st" "$@" > "$o" 2> "$o.err") || { cat "$o.err"; echo "FAIL: cert_trace $*"; exit 1; }
}

# Stimulus, testbench and the dcert controls. One python, three verbs.
py() {
  python3 -I - "$@" <<'PY'
import json, os, random, re, sys
verb = sys.argv[1]

def ident(n):
    return n if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_$]*", n) else "\\" + n + " "

if verb == "stim":   # stim <io.json> <cycles> <dir> <row> <golden.v> <verilog_top> <lean literal>
    io, cycles, d, row, gold, vtop, lit = sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5], sys.argv[6], sys.argv[7], sys.argv[8]
    meta = json.load(open(io))
    ins = [(i["name"], i["width"], i["ordinal"]) for i in meta["inputs"]]
    outs = [(o["name"], o["width"]) for o in sorted(meta["outputs"], key=lambda o: o["ordinal"])]
    clocks = {c["name"] for c in meta.get("clocks", [])} & {n for n, _, _ in ins}
    rng = random.Random("whole_array:" + row)
    rows = [{n: ((1 if k < 2 else 0) if n == "reset" else rng.getrandbits(w)) for n, w, _ in ins if n not in clocks}
            for k in range(cycles)]
    with open(f"{d}/stim.txt", "w") as f:
        f.write("stim v1\n")
        for k, r in enumerate(rows):
            for n, w, o in ins:
                if n in r:
                    f.write(f"at {k} in {o} {w} {r[n]}\n")
    # The golden's unpacked arrays start at X in iverilog; Lean starts each
    # memory at its power-on words (DCERT3 `init`, else 0). Set every entry
    # still X at time 0.5 -- after the golden's own initial blocks -- to the
    # certificate's word for it.
    text = open(gold, errors="replace").read()
    body = ""
    for m in re.split(r"^\s*module\s+", text, flags=re.M)[1:]:
        nm = re.match(r"(\\\S+|[A-Za-z_][\w$]*)", m)
        if nm and nm.group(1).lstrip("\\") == vtop.lstrip("\\"):
            body = m.split("endmodule")[0]
    arrays = []
    # `name [lo:hi]` or the C-style `name [n]` (entries 0..n-1)
    for m in re.finditer(r"^\s*(?:reg|logic)\s+(?:(?:signed|unsigned)\s+)?(?:\[[^\]]+\]\s*)?(?!(?:signed|unsigned)\b)([A-Za-z_][\w$]*)\s*\[\s*(\d+)\s*(?::\s*(\d+)\s*)?\]", body, re.M):
        if m.group(3) is None:
            lo, hi = 0, int(m.group(2)) - 1
        else:
            lo, hi = sorted((int(m.group(2)), int(m.group(3))))
        arrays.append((m.group(1), lo, hi))
    inits = [[int(v) for v in g.split(",") if v.strip()] for g in re.findall(r"init := #\[([0-9, ]*)\]", open(lit).read())]
    power = dict(zip([a[0] for a in arrays], inits)) if len(inits) == len(arrays) else {}
    L = ["`timescale 1ns/1ps", "module tb;", "  integer zk;"]
    for n, w, _ in ins:
        L.append(f"  reg {'' if w == 1 else f'[{w-1}:0] '}{ident(n)};")
    for n, w in outs:
        L.append(f"  wire {'' if w == 1 else f'[{w-1}:0] '}{ident(n)};")
    L.append(f"  {ident(vtop)} dut({', '.join(f'.{ident(n)}({ident(n)})' for n, _ in [(a, b) for a, b, _ in ins] + outs)});")
    for c in clocks:
        L += [f"  initial {ident(c)} = 1'b0;", f"  always #5 {ident(c)} = ~{ident(c)};"]
    L.append("  initial begin")
    L.append("    #0.5;")
    for n, lo, hi in arrays:
        words = power.get(n, [])
        for k in range(lo, hi + 1):
            v = words[k - lo] if k - lo < len(words) else 0
            L.append(f"    if (^dut.{n}[{k}] === 1'bx) dut.{n}[{k}] = {v};")
    L.append("  end")
    L.append("  initial begin")
    data = [n for n, _, _ in ins if n not in clocks]
    for n in data:
        L.append(f"    {ident(n)} = 0;")
    L.append("    #1;")
    fmt = " ".join(["%0d"] * len(outs))
    args = ", ".join(f"$unsigned({ident(n)})" for n, _ in outs)
    for k, r in enumerate(rows):
        for n in data:
            L.append(f"    {ident(n)} <= {r[n]};")
        L.append(f"    #3; $display(\"STEP {k} {fmt}\"{', ' + args if outs else ''});")
        L.append("    #7;")
    L += ["    $finish;", "  end", "endmodule"]
    open(f"{d}/tb.v", "w").write("\n".join(L) + "\n")

elif verb == "mutate":   # mutate <in.dcert> <out.dcert> <opcode> <new count>
    src, dst, opc, new = sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
    lines = open(src).read().split("\n")
    assert lines[0] == "DCERT3", lines[0]
    nsrc = int(lines[1])
    i = 2 + nsrc
    nnodes = int(lines[i])
    hits = [j for j in range(i + 1, i + 1 + nnodes) if lines[j].split()[0] == opc]
    if not hits:
        sys.exit(f"no node with opcode {opc}")
    j = hits[-1]   # the LAST one: for MemLoad, the reset load when there is one
    f = lines[j].split()
    f[1] = new if new != "minus1" else str(int(f[1]) - 1)
    lines[j] = " ".join(f)
    open(dst, "w").write("\n".join(lines))
PY
}

for row in $ROWS; do
  R="$OUT/$row"
  mkdir -p "$R/tmp"
  ptop=$(sed -n 's/^:pyrope_top: *//p' "$EQ/$row.prp" | head -1)
  vtop=$(sed -n 's/^:verilog_top: *//p' "$EQ/$row.prp" | head -1)
  TMPDIR="$R/tmp" "$LHD" compile "$EQ/$row.prp" --top "$ptop" --set compile.flatten=true --set compile.unknown_zero=true \
    --emit-dir "lg:$R/s1" --workdir "$R/w1" > "$R/compile.log" 2>&1 \
    || { grep -o '"message":"[^"]*' "$R/compile.log" | head -2; echo "FAIL: $row compile"; exit 1; }
  TMPDIR="$R/tmp" "$LHD" pass single_edge --top "$ptop" "lg:$R/s1" --emit-dir "lg:$R/s2" --workdir "$R/w2" > "$R/se.log" 2>&1 \
    || { grep -o '"message":"[^"]*' "$R/se.log" | head -2; echo "FAIL: $row single_edge"; exit 1; }
  TMPDIR="$R/tmp" "$LHD" compile "lg:$R/s2" --top "$ptop" --emit-dir "lean:$R/vc" --workdir "$R/w3" \
    --set formal.lean.mode=verified_compiler > "$R/vc.log" 2>&1 \
    || { grep -o '"message":"[^"]*' "$R/vc.log" | head -2; echo "FAIL: $row certificate"; exit 1; }
  D=$(ls "$R"/vc/*.dcert); LIT=$(ls "$R"/vc/*_Lgraph.lean); IO=$(ls "$R"/vc/*_io.json)
  nload=$(grep -c 'Op_MemLoad' "$LIT" || true); nbus=$(grep -c 'Op_MemToBus' "$LIT" || true)
  [[ $((nload + nbus)) -gt 0 ]] || { echo "FAIL: $row: the certificate has no whole-array operator"; exit 1; }
  py stim "$IO" "$CYCLES" "$R" "$row" "$EQ/$row.v" "$vtop" "$LIT"
  iverilog -g2012 -o "$R/rtl.vvp" "$EQ/$row.v" "$R/tb.v" > "$R/iverilog.log" 2>&1 \
    || { cat "$R/iverilog.log"; echo "FAIL: $row iverilog"; exit 1; }
  vvp "$R/rtl.vvp" | grep '^STEP' > "$R/rtl.txt"
  [[ $(wc -l < "$R/rtl.txt") -eq $CYCLES ]] || { echo "FAIL: $row: the golden run did not produce $CYCLES steps"; exit 1; }
  grep -q 'x' "$R/rtl.txt" && { grep -m2 x "$R/rtl.txt"; echo "FAIL: $row: the golden produced x"; exit 1; }
  ct "$D" "$R/stim.txt" "$R/ir.txt" edges:all
  ct "$D" "$R/stim.txt" "$R/tv.txt" edges:all route:tv
  for n in ir tv; do
    diff "$R/rtl.txt" "$R/$n.txt" > "$R/diff_$n.txt" \
      || { head -4 "$R/diff_$n.txt"; echo "FAIL: $row: $n differs from the golden"; exit 1; }
  done
  echo "ok: $row ($nload MemLoad, $nbus MemToBus): IR and TV equal iverilog at all $CYCLES cycles"
  # legacy: the model with its proof bridge
  L="$R/legacy"; mod="LegacyWa$(echo "$row" | tr -cd 'a-z0-9')"
  mkdir -p "$L/lroot/$mod" "$L/olean/$mod"
  (cd "$L" && TMPDIR="$R/tmp" "$LHD" compile "lg:$R/s2" --top "$ptop" --emit-dir lean:model --set formal.lean.emit_cert=true \
     --set formal.lean.emit_fast_bridge=true --workdir lw > model.log 2>&1) \
    || { grep -o '"message":"[^"]*' "$L/model.log" | head -2; echo "FAIL: $row legacy emission"; exit 1; }
  LF=$(basename "$(ls "$L"/model/*_Lgraph.lean)"); top=${LF%_Lgraph.lean}
  cp "$L/model/$LF" "$L/lroot/$mod/"
  python3 -I "$ROOT/pass/lean/Legacy_Fast_Model_Simulator/scripts/make_legacy_sim.py" "$L/lroot/$mod/$LF" \
    --cycles "$CYCLES" --stim --out "$L/lroot/$mod" --module-prefix "$mod" > "$L/gen.log" 2>&1 \
    || { tail -3 "$L/gen.log"; echo "FAIL: $row make_legacy_sim"; exit 1; }
  (ulimit -s unlimited && LEAN_PATH="$LP:$L/olean" lean -R "$L/lroot" -o "$L/olean/$mod/${top}_Lgraph.olean" \
     "$L/lroot/$mod/$LF" > "$L/build.log" 2>&1) \
    || { grep -m5 error "$L/build.log"; echo "FAIL: $row legacy elaboration (bridge proofs)"; exit 1; }
  grep -q sorry "$L/build.log" && { echo "FAIL: $row sorry in the legacy model"; exit 1; }
  grep -q "theorem ${top}_next_refines_fast" "$L/lroot/$mod/$LF" \
    || { echo "FAIL: $row the bridge theorem for the next state was not emitted"; exit 1; }
  (ulimit -s unlimited && LEAN_PATH="$LP:$L/olean" lean --run "$L/lroot/$mod/${top}_LegacySim.lean" "$R/stim.txt" \
     > "$L/legacy.txt" 2> "$L/legacy.err") || { cat "$L/legacy.err"; echo "FAIL: $row legacy run"; exit 1; }
  diff "$R/rtl.txt" "$L/legacy.txt" > "$L/diff.txt" \
    || { head -4 "$L/diff.txt"; echo "FAIL: $row legacy differs from the golden"; exit 1; }
  echo "ok: $row legacy (bridge proofs elaborated) equals iverilog at all $CYCLES cycles"
  # FP: admitted, specialized, executed and compared with its own reference
  fp() { # <dcert> <out prefix>
    (ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run Futamura_Projection_Scripts/dcert2_memroute.lean \
       "$1" 20000 200 "$CYCLES" zero edges:all "stim:$R/stim.txt" outputs > "$2.log" 2> "$2.err") \
      || { tail -5 "$2.log"; cat "$2.err"; echo "FAIL: $row FP runner on $1"; exit 1; }
    grep -q 'RESULT: MEMROUTE OK' "$2.log" || { grep RESULT "$2.log"; echo "FAIL: $row FP did not run the memory"; exit 1; }
    grep '^STEP' "$2.log" > "$2.txt"
  }
  fp "$D" "$R/fp"
  ct "$D" "$R/stim.txt" "$R/ir_third.txt" edges:third
  diff "$R/ir_third.txt" "$R/fp.txt" > "$R/diff_fp.txt" \
    || { head -4 "$R/diff_fp.txt"; echo "FAIL: $row FP differs from IR"; exit 1; }
  echo "ok: $row FP (MEMROUTE OK) equals IR at all $CYCLES steps of FP's schedule"
  # controls
  for c in "29 1 a MemLoad that loads entry 0 only" "30 minus1 a MemToBus that packs one entry fewer"; do
    set -- $c; opc=$1; new=$2; shift 2; what="$*"
    [[ $opc == 29 && $nload -eq 0 ]] && continue
    [[ $opc == 30 && $nbus -eq 0 ]] && continue
    py mutate "$D" "$R/ctl_$opc.dcert" "$opc" "$new"
    ct "$R/ctl_$opc.dcert" "$R/stim.txt" "$R/ctl_$opc.txt" edges:all
    nd=$(diff "$R/rtl.txt" "$R/ctl_$opc.txt" | grep -c '^<' || true)
    [[ $nd -gt 0 ]] || { echo "FAIL: $row: control ($what) matched the golden"; exit 1; }
    echo "ok: $row control -- $what differs from the golden on $nd cycles"
    fp "$R/ctl_$opc.dcert" "$R/fp_ctl_$opc"
    nf=$(diff "$R/ir_third.txt" "$R/fp_ctl_$opc.txt" | grep -c '^<' || true)
    [[ $nf -gt 0 ]] || { echo "FAIL: $row: FP on the control ($what) matched IR on the design"; exit 1; }
    echo "ok: $row control -- FP on it differs from IR on the design on $nf steps"
  done
done

# ---- D3: translation validation, with its proof, on every row's certificate ------
D3="$OUT/d3"
mkdir -p "$D3/certs"
for row in $ROWS; do cp "$OUT/$row"/vc/*_Lgraph.lean "$D3/certs/"; done
(cd "$ROOT" && env -u OUT -u TMPDIR python3 -I pass/lean/Translation_Validation/scripts/d3_sweep.py \
   --certs "$D3/certs" --out "$D3/out" --jobs 1 --timeout 900 --prove > "$D3/sweep.log" 2>&1) \
  || { tail -5 "$D3/sweep.log"; echo "FAIL: D3 sweep"; exit 1; }
python3 -I - "$D3/out" $ROWS <<'PY'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1]), delimiter="\t"))
if len(rows) != len(sys.argv) - 2:
    sys.exit(f"FAIL: D3 wrote {len(rows)} rows for {len(sys.argv) - 2} certificates")
for r in rows:
    gates = "".join(r[k] for k in ("cert", "compile", "reify", "typecheck", "sim", "checker", "agree", "proof"))
    if gates != "11111111" or r["mut_mem"] != "rejected" or r["drift"]:
        sys.exit(f"FAIL: D3 {r['verdict']}: gates {gates}, mut_mem {r['mut_mem']}, drift {r['drift'] or '-'}")
print(f"ok: D3 -- every gate passes on all {len(rows)} certificates, the proof included; memory mutations are rejected")
PY
echo "PASS: whole_array_mem_oracle"
