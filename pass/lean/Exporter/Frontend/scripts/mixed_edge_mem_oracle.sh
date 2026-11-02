#!/usr/bin/env bash
set -euo pipefail
# Memories whose ports use BOTH clock edges, end to end (decision #9).
#
# Both readers used to mark such a memory `posclk = 2` ("mixed"), which lost
# every port's edge, so single_edge and pass.lean refused it. Now the cell is
# marked rising and each falling port's clock pin is the inverted clock
# (`posedge ~clk`). single_edge then lowers it as it lowers a falling memory
# (decision #4): every port rebound to the reference clock, a rising port's
# write gated to the rise slot and a falling port's to the fall slot (P = 2).
#
# Checks, for both Verilog readers (yosys-slang and native slang):
#   reader   the graph's memory is marked rising (posclk 1, never 2).
#   cgen     the Verilog lhd emits from that graph equals iverilog on the source
#            at EVERY step (before and after each rise), so the reader kept
#            each port's edge.
#   IR, TV   the certificate built after single_edge equals iverilog on the
#            source at every period boundary (what single_edge promises).
#   control  the same design with every port on the rising edge differs.
#
# Lean step 2p is the RISE sub-step of source period p and step 2p+1 its FALL
# sub-step; both take period p's inputs (as negedge_mem_oracle.sh).
#
# Designs:
#   mixwr   two write ports on opposite edges of one clock: port 0 writes `d0`
#           at the rise; port 1 writes at the fall, from a register loaded at
#           the rise of the same period. Read asynchronously.
#   mixrd   written at the rise, read into a register at the fall. Both readers
#           keep a read register OUTSIDE the memory cell (the yosys flow runs
#           memory -nordff), so this memory is not mixed: it is the neighbouring
#           shape, a rising memory whose reader is a negedge register.
#
# No real design was found that writes one array on both edges (searched:
# chipyard's generators, CORE-ET, CVA6, picorv32, NVDLA and the vendor
# simulation libraries on this machine). The closest, Altera's dcfifo models in
# altera_mf.v, pick ONE write edge by a constant parameter, and neither reader
# accepts that file (native slang: `defparam`; yosys-slang aborts).
#
# The Lean toolchain pin lives in formal/lean; lean runs from there.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/mixed_edge_mem_oracle}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
PERIODS="${PERIODS:-24}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
command -v iverilog >/dev/null && command -v vvp >/dev/null || { echo "iverilog/vvp not on PATH" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"
cd "$ROOT/formal/lean"
LP=$(lake env printenv LEAN_PATH)

# ---- the designs --------------------------------------------------------------
cat > "$OUT/mixwr.v" <<'EOF'
module mixwr(input clk, input we0, input we1, input [1:0] a0, input [1:0] a1,
             input [7:0] d0, input [1:0] ra, output [7:0] q);
  reg [7:0] mem [0:3];
  reg [7:0] r;
  always @(posedge clk) begin
    r <= d0;
    if (we0) mem[a0] <= d0;
  end
  always @(negedge clk) if (we1) mem[a1] <= r;
  assign q = mem[ra];
endmodule
EOF
sed 's/negedge clk/posedge clk/' "$OUT/mixwr.v" > "$OUT/mixwr_ctl.v"
cat > "$OUT/mixrd.v" <<'EOF'
module mixrd(input clk, input we, input [1:0] wa, input [1:0] ra, input [7:0] d, output reg [7:0] q);
  reg [7:0] mem [0:3];
  always @(posedge clk) if (we) mem[wa] <= d;
  always @(negedge clk) q <= mem[ra];
endmodule
EOF
sed 's/negedge clk/posedge clk/' "$OUT/mixrd.v" > "$OUT/mixrd_ctl.v"
# top:source:control:start state (register or name:entries)
DESIGNS=("mixwr:mixwr.v:mixwr_ctl.v:mem:4,r" "mixrd:mixrd.v:mixrd_ctl.v:mem:4,q")

# ---- stimulus and a source testbench in cert_trace's STEP format ----------------
gen() { # <io.json> <top> <dir> <inits>
  python3 -I - "$1" "$2" "$PERIODS" "$3" "$4" <<'PY'
import json, random, sys
io_path, top, periods, out, inits = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5]
io = json.load(open(io_path))
clocks = {c["name"] for c in io.get("clocks", [])}
ins = [i for i in io["inputs"] if i["name"] not in clocks]
outs = sorted(io["outputs"], key=lambda o: o["ordinal"])
clk = sorted(clocks)[0]
random.seed(7)
rows = [{i["name"]: random.randrange(1 << i["width"]) for i in ins} for _ in range(periods)]
names = {i["name"] for i in ins}
for p in range(min(4, periods)):  # fill every entry through a rising write port first
    if "we0" in names:
        rows[p].update(we0=1, a0=p, we1=0)
    elif "we" in names and "wa" in names:
        rows[p].update(we=1, wa=p)
for p in range(periods):  # read an entry a port just wrote, or wrote last period
    if "ra" in names:
        src = [n for n in ("a1", "a0", "wa") if n in names]
        if src and p % 3 == 0:
            rows[p]["ra"] = rows[p][src[0]]
        elif src and p % 3 == 1 and p > 0:
            rows[p]["ra"] = rows[p - 1][src[-1]]
    if "a0" in names and "a1" in names and rows[p]["a0"] == rows[p]["a1"]:
        rows[p]["a1"] = (rows[p]["a0"] + 1) % 4  # distinct entries: the two writes never race
with open(f"{out}/stim.txt", "w") as f:
    f.write("stim v1\n")
    for p, r in enumerate(rows):
        for k in (2 * p, 2 * p + 1):
            for i in ins:
                f.write(f"at {k} in {i['ordinal']} {i['width']} {r[i['name']]}\n")
for tb, with_init in (("tb.v", True), ("tb_noinit.v", False)):
  with open(f"{out}/{tb}", "w") as f:
    f.write(f"`timescale 1ns/1ps\nmodule tb;\n integer i;\n reg {clk} = 0;\n")
    for i in ins:
        f.write(f" reg [{i['width'] - 1}:0] {i['name']} = 0;\n")
    for o in outs:
        f.write(f" wire [{o['width'] - 1}:0] {o['name']};\n")
    ports = [f".{clk}({clk})"] + [f".{i['name']}({i['name']})" for i in ins] + [f".{o['name']}({o['name']})" for o in outs]
    f.write(f" {top} u({', '.join(ports)});\n initial begin\n")
    for it in (inits.split(",") if with_init else []):  # the Lean start state: every register and entry 0
        if ":" in it:
            name, n = it.split(":")
            f.write(f"  for (i = 0; i < {n}; i = i + 1) u.{name}[i] = 0;\n")
        elif it:
            f.write(f"  u.{it} = 0;\n")
    fmt = " ".join("%0d" for _ in outs)
    names_ = ", ".join(o["name"] for o in outs)
    for p, r in enumerate(rows):
        t = 20 * p
        f.write(f"  #({t + 1} - $time); " + " ".join(f"{n} = {v};" for n, v in r.items()) + "\n")
        f.write(f'  #({t + 4} - $time); $display("STEP {2 * p} {fmt}", {names_});\n')
        f.write(f"  #({t + 5} - $time); {clk} = 1;\n")
        f.write(f'  #({t + 9} - $time); $display("STEP {2 * p + 1} {fmt}", {names_});\n')
        f.write(f"  #({t + 15} - $time); {clk} = 0;\n")
    f.write("  $finish;\n end\nendmodule\n")
PY
}
sim() { # <dir> <name> <testbench> <rtl files...>
  local d=$1 n=$2 tb=$3; shift 3
  iverilog -g2012 -o "$d/$n.vvp" "$@" "$d/$tb" > "$d/$n.iverilog.log" 2>&1 || { cat "$d/$n.iverilog.log"; echo "FAIL: iverilog $n"; exit 1; }
  vvp "$d/$n.vvp" | grep '^STEP' > "$d/$n.txt"
}
even() { awk '$2 % 2 == 0' "$1"; }

STEPS=$((2 * PERIODS))
for d in "${DESIGNS[@]}"; do
  IFS=: read -r top src ctl inits <<< "$d"  # `read` keeps the colons of the last field
  for reader in yosys-slang slang; do
    R="$OUT/$top/$reader"
    mkdir -p "$R"
    "$LHD" compile "$OUT/$src" --top "$top" --reader "$reader" --set yosys.setundef=zero \
      --workdir "$R/cw" --emit-dir "lg:$R/s1" --emit-dir "verilog:$R/v1" > "$R/compile.log" 2>&1 \
      || { grep -o '"message":"[^"]*' "$R/compile.log" | head -3; echo "FAIL: $top ($reader) compile"; exit 1; }
    "$LHD" tool grep "lg:$R/s1" kind=memory > "$R/mem.json" 2>/dev/null || true
    grep -q '"kind":"memory"' "$R/mem.json" || { echo "FAIL: $top ($reader): no memory cell"; exit 1; }
    grep -q 'p6=2[,"]' "$R/mem.json" && { echo "FAIL: $top ($reader): the memory is still marked mixed (posclk 2)"; exit 1; }
    if [[ $top == mixwr ]]; then
      grep -q 'mixes clock edges' "$R/compile.log" || { echo "FAIL: $top ($reader): the reader did not see a mixed memory"; exit 1; }
    fi
    "$LHD" pass single_edge --top "$top" "lg:$R/s1" --emit-dir "lg:$R/s2" --workdir "$R/sw" > "$R/single_edge.log" 2>&1 \
      || { grep -o '"message":"[^"]*' "$R/single_edge.log" | head -3; echo "FAIL: $top ($reader) single_edge"; exit 1; }
    "$LHD" compile "lg:$R/s2" --top "$top" --workdir "$R/vw" --emit-dir "lean:$R/vc" \
      --set formal.lean.mode=verified_compiler > "$R/vc.log" 2>&1 \
      || { grep -o '"message":"[^"]*' "$R/vc.log" | head -3; echo "FAIL: $top ($reader) certificate"; exit 1; }
    gen "$R/vc/${top}_io.json" "$top" "$R" "$inits"
    sim "$R" rtl tb.v "$OUT/$src"
    grep -q x "$R/rtl.txt" && { grep -m3 x "$R/rtl.txt"; echo "FAIL: the source produced x"; exit 1; }
    [[ $(wc -l < "$R/rtl.txt") -eq $STEPS ]] || { echo "FAIL: the source run did not produce $STEPS steps"; exit 1; }
    # cgen: lhd's own Verilog of the READ graph, at every step. Its memory sits
    # in a wrapper, so neither run is initialized by name: the stimulus fills
    # every entry first, and any x left must match exactly.
    sim "$R" rtl0 tb_noinit.v "$OUT/$src"
    sim "$R" cgen tb_noinit.v -I "$ROOT/ware/rtl" "$R"/v1/*.v
    diff "$R/rtl0.txt" "$R/cgen.txt" > "$R/diff_cgen.txt" \
      || { head -4 "$R/diff_cgen.txt"; echo "FAIL: $top ($reader): lhd's emitted Verilog differs from the source"; exit 1; }
    nx=$(grep -c x "$R/rtl0.txt" || true)
    echo "ok: $top ($reader) lhd's emitted Verilog == source at all $STEPS steps ($nx steps still show x)"
    for route in ir tv; do
      opts=(edges:all); [[ $route == tv ]] && opts+=(route:tv)
      (ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run Futamura_Projection_Scripts/cert_trace.lean \
         "$R/vc/$top.dcert" "$STEPS" "$R/stim.txt" "${opts[@]}" > "$R/$route.txt" 2> "$R/$route.err") \
        || { cat "$R/$route.err"; echo "FAIL: cert_trace $route"; exit 1; }
      diff <(even "$R/rtl.txt") <(even "$R/$route.txt") > "$R/diff_$route.txt" \
        || { head -4 "$R/diff_$route.txt"; echo "FAIL: $top ($reader) $route differs from the source at a period boundary"; exit 1; }
      mid=$(diff "$R/rtl.txt" "$R/$route.txt" | grep -c '^<' || true)
      echo "ok: $top ($reader) $route == source at all $PERIODS period boundaries ($mid of $PERIODS mid-period steps differ)"
    done
    cmp -s "$R/ir.txt" "$R/tv.txt" || { echo "FAIL: $top ($reader) TV != IR"; exit 1; }
    sim "$R" ctl tb.v "$OUT/$ctl"
    nb=$(diff <(even "$R/ctl.txt") <(even "$R/ir.txt") | grep -c '^<' || true)
    [[ $nb -gt 0 ]] || { echo "FAIL: $top ($reader): the all-rising control agreed at every period boundary"; exit 1; }
    echo "ok: $top ($reader) control $ctl differs at $nb of $PERIODS period boundaries"
  done
done
echo "PASS: mixed_edge_mem_oracle"
