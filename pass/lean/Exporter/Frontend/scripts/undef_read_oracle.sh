#!/usr/bin/env bash
set -euo pipefail
# ordering="none" memories through the Lean routes (decision #8).
#
# A memory with `ordering="none"` leaves a same-step read of an address being
# written UNDEFINED on the bits that write drives (graph/cell.cpp pid 15, the
# `undef` matrix): the RTL reads x, pass/lec a don't-care, `lhd sim` a random
# value. The certificate cannot pick one value without describing a different
# design, so each such read gets a FREE INPUT (`<memory>__undef_read<r>`,
# marked `free_value` in the io sidecar; not a port of the RTL) and takes the
# undefined bits from it. The certificate then covers every legal answer.
#
# The designs are the two such rows of the Pyrope equivalence corpus:
#   mem_ordering_none               two reads of a 4 x 2 memory, one write
#   spec_memory_partial_merge_none  a 4 x 16 memory, a whole write and two
#                                   partial writes on 4-bit lanes; only the
#                                   lanes a colliding write drives are undefined
#
# The check is BIT-EXACT. Lean runs twice on one stimulus, once with every free
# input 0 and once with every free input all ones; the golden runs under iverilog
# and prints each output in binary, x included. On every cycle and output bit:
#   golden bit x       -> the 0-run bit is 0 and the 1-run bit is 1 (the bit
#                         comes from the free input, and only from it);
#   golden bit 0 or 1  -> both runs equal it (a defined bit never moves).
# Routes: IR and TV (cert_trace), legacy (with its proof bridge), FP (equal to
# IR under FP's schedule), D3 (every gate, the proof included).
# Control: the free inputs never reaching the read (the certificate with each
# free-input source turned into a constant 0) leaves the 1-run's x bits at 0.
#
# The Lean toolchain pin lives in formal/lean; lean runs from there.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/undef_read_oracle}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
CYCLES="${CYCLES:-64}"
ROWS="${ROWS:-mem_ordering_none spec_memory_partial_merge_none}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
command -v iverilog >/dev/null && command -v vvp >/dev/null || { echo "iverilog/vvp not on PATH" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"
cd "$ROOT/formal/lean"
LP=$(lake env printenv LEAN_PATH)
EQ="$ROOT/inou/prp/tests/equiv"

ct() { # <dcert> <stim> <out> <cert_trace options...>
  local dc=$1 st=$2 o=$3; shift 3
  (ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run Futamura_Projection_Scripts/cert_trace.lean \
     "$dc" "$CYCLES" "$st" "$@" > "$o" 2> "$o.err") || { cat "$o.err"; echo "FAIL: cert_trace $*"; exit 1; }
}

py() {
  python3 -I - "$@" <<'PY'
import json, random, re, sys
verb = sys.argv[1]
def ident(n):
    return n if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_$]*", n) else "\\" + n + " "

if verb == "stim":   # stim <io.json> <cycles> <dir> <row> <verilog_top> <golden.v>
    io, cycles, d, row, vtop, gold = sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5], sys.argv[6], sys.argv[7]
    meta = json.load(open(io))
    ins = [(i["name"], i["width"], i["ordinal"], bool(i.get("free_value"))) for i in meta["inputs"]]
    if not any(f for *_, f in ins):
        sys.exit("FAIL: the sidecar marks no input free_value")
    outs = [(o["name"], o["width"]) for o in sorted(meta["outputs"], key=lambda o: o["ordinal"])]
    clocks = {c["name"] for c in meta.get("clocks", [])} & {n for n, *_ in ins}
    rng = random.Random("undef_read:" + row)
    rows = []
    for k in range(cycles):
        r = {}
        for n, w, _, free in ins:
            if n in clocks or free:
                continue
            # few addresses, so reads and writes collide often
            r[n] = rng.getrandbits(w)
        rows.append(r)
    for tag, fill in (("zero", lambda w: 0), ("ones", lambda w: (1 << w) - 1)):
        with open(f"{d}/stim_{tag}.txt", "w") as f:
            f.write("stim v1\n")
            for k, r in enumerate(rows):
                for n, w, o, free in ins:
                    if n in clocks:
                        continue
                    f.write(f"at {k} in {o} {w} {fill(w) if free else r[n]}\n")
    # The golden's arrays start at x in iverilog; Lean starts each memory at 0.
    # Zero every entry still x at time 0.5, after the golden's own initial blocks.
    text = open(gold, errors="replace").read()
    body = ""
    for m in re.split(r"^\s*module\s+", text, flags=re.M)[1:]:
        nm = re.match(r"(\\\S+|[A-Za-z_][\w$]*)", m)
        if nm and nm.group(1).lstrip("\\") == vtop.lstrip("\\"):
            body = m.split("endmodule")[0]
    arrays = []
    for m in re.finditer(r"^\s*(?:reg|logic)\s+(?:(?:signed|unsigned)\s+)?(?:\[[^\]]+\]\s*)?(?!(?:signed|unsigned)\b)([A-Za-z_][\w$]*)\s*\[\s*(\d+)\s*(?::\s*(\d+)\s*)?\]", body, re.M):
        lo, hi = (0, int(m.group(2)) - 1) if m.group(3) is None else sorted((int(m.group(2)), int(m.group(3))))
        arrays.append((m.group(1), lo, hi))
    if not arrays:
        sys.exit("FAIL: no array found in the golden")
    L = ["`timescale 1ns/1ps", "module tb;", "  integer zk;"]
    rtl_ins = [(n, w) for n, w, _, free in ins if not free]
    for n, w in rtl_ins:
        L.append(f"  reg {'' if w == 1 else f'[{w-1}:0] '}{ident(n)};")
    for n, w in outs:
        L.append(f"  wire {'' if w == 1 else f'[{w-1}:0] '}{ident(n)};")
    L.append(f"  {ident(vtop)} dut({', '.join(f'.{ident(n)}({ident(n)})' for n, _ in rtl_ins + outs)});")
    for c in clocks:
        L += [f"  initial {ident(c)} = 1'b0;", f"  always #5 {ident(c)} = ~{ident(c)};"]
    L += ["  initial begin", "    #0.5;"]
    for n, lo, hi in arrays:
        L.append(f"    for (zk = {lo}; zk <= {hi}; zk = zk + 1) if (^dut.{n}[zk] === 1'bx) dut.{n}[zk] = 0;")
    L.append("  end")
    L.append("  initial begin")
    data = [n for n, _ in rtl_ins if n not in clocks]
    for n in data:
        L.append(f"    {ident(n)} = 0;")
    L.append("    #1;")
    fmt = " ".join(["%b"] * len(outs))
    args = ", ".join(ident(n) for n, _ in outs)
    for k, r in enumerate(rows):
        for n in data:
            L.append(f"    {ident(n)} <= {r[n]};")
        L.append(f"    #3; $display(\"STEP {k} {fmt}\", {args});")
        L.append("    #7;")
    L += ["    $finish;", "  end", "endmodule"]
    open(f"{d}/tb.v", "w").write("\n".join(L) + "\n")
    json.dump([w for _, w in outs], open(f"{d}/widths.json", "w"))

elif verb == "compare":   # compare <dir> <zero trace> <ones trace> <what>
    d, zf, of, what = sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
    widths = json.load(open(f"{d}/widths.json"))
    def lean(path):
        rows = {}
        for line in open(path):
            p = line.split()
            if p and p[0] == "STEP":
                rows[int(p[1])] = [format(int(v), f"0{w}b") for v, w in zip(p[2:], widths)]
        return rows
    gold = {}
    for line in open(f"{d}/rtl.txt"):
        p = line.split()
        gold[int(p[1])] = p[2:]
    z, o = lean(zf), lean(of)
    if not (len(gold) == len(z) == len(o)) or not gold:
        sys.exit(f"FAIL: {what}: {len(gold)} golden, {len(z)} zero-run, {len(o)} ones-run steps")
    xbits = 0
    for k in sorted(gold):
        for j, w in enumerate(widths):
            g = gold[k][j].rjust(w, "0")
            for b in range(w):
                gb, zb, ob = g[b], z[k][j][b], o[k][j][b]
                if gb in "xXzZ":
                    xbits += 1
                    if (zb, ob) != ("0", "1"):
                        sys.exit(f"FAIL: {what}: step {k} output {j} bit {w - 1 - b}: golden x, Lean {zb}/{ob} (want 0/1)")
                elif zb != gb or ob != gb:
                    sys.exit(f"FAIL: {what}: step {k} output {j} bit {w - 1 - b}: golden {gb}, Lean {zb}/{ob}")
    if xbits == 0:
        sys.exit(f"FAIL: {what}: no undefined bit in {len(gold)} steps -- the stimulus never collided")
    print(f"ok: {what}: bit-exact at all {len(gold)} steps; {xbits} undefined bits come from the free input and only from it")

elif verb == "freeze":   # freeze <in.dcert> <out.dcert> <ordinals...>: free-input sources -> constant 0
    src, dst, ords = sys.argv[2], sys.argv[3], {int(x) for x in sys.argv[4:]}
    lines = open(src).read().split("\n")
    n = int(lines[1])
    hit = 0
    for i in range(2, 2 + n):
        f = lines[i].split()
        if f[0] == "0" and int(f[1]) in ords:   # input <ordinal> <width>
            lines[i] = f"1 {f[2]} 0"            # const <width> 0
            hit += 1
    if hit == 0:
        sys.exit("no free-input source found")
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
  D=$(ls "$R"/vc/*.dcert); IO=$(ls "$R"/vc/*_io.json)
  py stim "$IO" "$CYCLES" "$R" "$row" "$vtop" "$EQ/$row.v"
  iverilog -g2012 -o "$R/rtl.vvp" "$EQ/$row.v" "$R/tb.v" > "$R/iverilog.log" 2>&1 \
    || { cat "$R/iverilog.log"; echo "FAIL: $row iverilog"; exit 1; }
  vvp "$R/rtl.vvp" | grep '^STEP' > "$R/rtl.txt"
  for route in ir tv; do
    opts=(edges:all); [[ $route == tv ]] && opts+=(route:tv)
    for t in zero ones; do ct "$D" "$R/stim_$t.txt" "$R/${route}_$t.txt" "${opts[@]}"; done
    py compare "$R" "$R/${route}_zero.txt" "$R/${route}_ones.txt" "$row $route"
  done
  # control: the free inputs never reach the read
  frees=$(python3 -I -c "import json,sys; print(' '.join(str(i['ordinal']) for i in json.load(open(sys.argv[1]))['inputs'] if i.get('free_value')))" "$IO")
  py freeze "$D" "$R/frozen.dcert" $frees
  ct "$R/frozen.dcert" "$R/stim_ones.txt" "$R/ctl_ones.txt" edges:all
  if py compare "$R" "$R/ir_zero.txt" "$R/ctl_ones.txt" "$row control" > "$R/ctl.log" 2>&1; then
    echo "FAIL: $row: the control (free inputs cut off) still passed"; exit 1
  fi
  echo "ok: $row control -- with the free inputs cut off the undefined bits no longer follow them ($(tail -1 "$R/ctl.log" | cut -c1-90))"
  # legacy, with its proof bridge
  L="$R/legacy"; mod="LegacyUr$(echo "$row" | tr -cd 'a-z0-9')"
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
  for t in zero ones; do
    (ulimit -s unlimited && LEAN_PATH="$LP:$L/olean" lean --run "$L/lroot/$mod/${top}_LegacySim.lean" "$R/stim_$t.txt" \
       > "$L/legacy_$t.txt" 2> "$L/legacy_$t.err") || { cat "$L/legacy_$t.err"; echo "FAIL: $row legacy run"; exit 1; }
  done
  py compare "$R" "$L/legacy_zero.txt" "$L/legacy_ones.txt" "$row legacy (bridge proofs elaborated)"
  # FP: admitted, run, equal to IR under FP's schedule
  for t in zero ones; do
    (ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run Futamura_Projection_Scripts/dcert2_memroute.lean \
       "$D" 20000 200 "$CYCLES" zero edges:all "stim:$R/stim_$t.txt" outputs > "$R/fp_$t.log" 2> "$R/fp_$t.err") \
      || { tail -5 "$R/fp_$t.log"; cat "$R/fp_$t.err"; echo "FAIL: $row FP runner"; exit 1; }
    grep -q 'RESULT: MEMROUTE OK' "$R/fp_$t.log" || { grep RESULT "$R/fp_$t.log"; echo "FAIL: $row FP did not run the memory"; exit 1; }
    grep '^STEP' "$R/fp_$t.log" > "$R/fp_$t.txt"
    ct "$D" "$R/stim_$t.txt" "$R/ir_third_$t.txt" edges:third
    cmp -s "$R/ir_third_$t.txt" "$R/fp_$t.txt" || { diff "$R/ir_third_$t.txt" "$R/fp_$t.txt" | head -4; echo "FAIL: $row FP differs from IR"; exit 1; }
  done
  echo "ok: $row FP (MEMROUTE OK) equals IR at all $CYCLES steps of FP's schedule, free inputs 0 and all ones"
done

# ---- D3: translation validation, with its proof ----------------------------------
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
echo "PASS: undef_read_oracle"
