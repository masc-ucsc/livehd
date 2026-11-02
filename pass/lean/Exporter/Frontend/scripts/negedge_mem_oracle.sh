#!/usr/bin/env bash
set -euo pipefail
# Memories written on the FALLING clock edge, end to end (decision #4).
#
# pass.single_edge lowers a falling memory as it lowers a negedge flop: the cell
# is retyped to posedge, its write clock rebound to the reference root, and its
# write gated to the fall slot (P = 2). This checks the certificate built from
# the lowered graph against iverilog on the ORIGINAL RTL, for both Verilog
# readers (yosys-slang and the native slang reader).
#
# Lean step 2p is the RISE sub-step of source period p and step 2p+1 its FALL
# sub-step; both take period p's inputs. single_edge promises the state at
# PERIOD BOUNDARIES (step 2p, just before a rise), so those must match; the
# mid-period steps are reported. IR is cert_trace (interpretDesign), TV is
# cert_trace route:tv (compileDesign), every clock firing.
#
# Designs:
#   negmem   a 4 x 8 memory written on `negedge clk`, read asynchronously
#            (inline). Control: the same memory written on posedge must differ.
#   ramdp    NVDLA's RAMDP_16X256_GL_M1_E2, its EMULATION model (16 x 256,
#            written on negedge CLK_W from posedge-captured address/data, read
#            through a clock-gate latch and a data latch), with CLK_R and CLK_W
#            tied to one clock. Run only when NVDLA_RAMDP names the model file
#            (chipyard generators/nvdla/.../vmod/rams/model/RAMDP_16X256_GL_M1_E2.v);
#            its EMULATION block is copied from that file at run time. Control:
#            the same RAM with the write removed must differ at a boundary.
#
# The Lean toolchain pin lives in formal/lean; lean runs from there.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/negedge_mem_oracle}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
PERIODS="${PERIODS:-24}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
command -v iverilog >/dev/null && command -v vvp >/dev/null || { echo "iverilog/vvp not on PATH" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"
cd "$ROOT/formal/lean"
LP=$(lake env printenv LEAN_PATH)

# ---- the designs --------------------------------------------------------------
cat > "$OUT/negmem.v" <<'EOF'
module negmem(input clk, input we, input [1:0] wa, input [1:0] ra, input [7:0] d, output [7:0] q);
  reg [7:0] m [0:3];
  always @(negedge clk) if (we) m[wa] <= d;
  assign q = m[ra];
endmodule
EOF
sed 's/negedge clk/posedge clk/' "$OUT/negmem.v" > "$OUT/negmem_ctl.v"
# top:source:control:where the control must differ (any step, or a boundary):start state
DESIGNS=("negmem:negmem.v:negmem_ctl.v:any:m:4")
if [[ -n "${NVDLA_RAMDP:-}" ]]; then
  [[ -s "$NVDLA_RAMDP" ]] || { echo "NVDLA_RAMDP=$NVDLA_RAMDP is not a file" >&2; exit 2; }
  # The EMULATION block: from its `else //def EMULATION` to `endif //def EMULATION`.
  awk '/^`else \/\/def EMULATION/{on=1; next} /^`endif \/\/def EMULATION/{on=0} on' "$NVDLA_RAMDP" > "$OUT/ramdp_body.v"
  grep -q 'always @(negedge CLK_W )' "$OUT/ramdp_body.v" || { echo "FAIL: the EMULATION block has no negedge write" >&2; exit 1; }
  {
    echo "// NVDLA RAMDP_16X256_GL_M1_E2, its EMULATION block copied from $(basename "$NVDLA_RAMDP"),"
    echo "// with CLK_R and CLK_W tied to one clock."
    echo 'module ramdp(input clk, input RE, input WE, input [3:0] RA, input [3:0] WA,'
    echo '             input [255:0] WD, output [255:0] RD);'
    echo '  wire CLK_R = clk;'
    echo '  wire CLK_W = clk;'
    cat "$OUT/ramdp_body.v"
    echo 'endmodule'
  } > "$OUT/ramdp.v"
  sed 's/            if (we_ff) begin/            if (1'"'"'b0) begin/' "$OUT/ramdp.v" > "$OUT/ramdp_ctl.v"
  cmp -s "$OUT/ramdp.v" "$OUT/ramdp_ctl.v" && { echo "FAIL: the no-write control was not derived" >&2; exit 1; }
  DESIGNS+=("ramdp:ramdp.v:ramdp_ctl.v:boundary:we_ff,wa_ff,wd_ff,re_lat,ra_ff,dout,array:16")
else
  echo "note: NVDLA_RAMDP not set -- the NVDLA RAM benchmark is skipped"
fi

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
ra = [i["name"] for i in ins if i["name"].lower() == "ra"]
wa = [i["name"] for i in ins if i["name"].lower() == "wa"]
for p in range(1, periods):  # read what is written this period, or what was written last period
    if ra and wa and p % 3 == 0:
        rows[p][ra[0]] = rows[p][wa[0]]
    elif ra and wa and p % 3 == 1:
        rows[p][ra[0]] = rows[p - 1][wa[0]]
with open(f"{out}/stim.txt", "w") as f:
    f.write("stim v1\n")
    for p, r in enumerate(rows):
        for k in (2 * p, 2 * p + 1):
            for i in ins:
                f.write(f"at {k} in {i['ordinal']} {i['width']} {r[i['name']]}\n")
with open(f"{out}/tb.v", "w") as f:
    f.write(f"`timescale 1ns/1ps\nmodule tb;\n integer i;\n reg {clk} = 0;\n")
    for i in ins:
        f.write(f" reg [{i['width'] - 1}:0] {i['name']} = 0;\n")
    for o in outs:
        f.write(f" wire [{o['width'] - 1}:0] {o['name']};\n")
    ports = [f".{clk}({clk})"] + [f".{i['name']}({i['name']})" for i in ins] + [f".{o['name']}({o['name']})" for o in outs]
    f.write(f" {top} u({', '.join(ports)});\n initial begin\n")
    # The Lean start state: every register and memory entry 0.
    for it in inits.split(","):
        if ":" in it:
            name, n = it.split(":")
            f.write(f"  for (i = 0; i < {n}; i = i + 1) u.{name}[i] = 0;\n")
        else:
            f.write(f"  u.{it} = 0;\n")
    fmt = " ".join("%0d" for _ in outs)
    names = ", ".join(o["name"] for o in outs)
    for p, r in enumerate(rows):
        t = 20 * p
        f.write(f"  #({t + 1} - $time); " + " ".join(f"{n} = {v};" for n, v in r.items()) + "\n")
        f.write(f'  #({t + 4} - $time); $display("STEP {2 * p} {fmt}", {names});\n')
        f.write(f"  #({t + 5} - $time); {clk} = 1;\n")
        f.write(f'  #({t + 9} - $time); $display("STEP {2 * p + 1} {fmt}", {names});\n')
        f.write(f"  #({t + 15} - $time); {clk} = 0;\n")
    f.write("  $finish;\n end\nendmodule\n")
PY
}
sim() { # <dir> <rtl> <name>
  iverilog -g2012 -o "$1/$3.vvp" "$2" "$1/tb.v" > "$1/$3.iverilog.log" 2>&1 || { cat "$1/$3.iverilog.log"; echo "FAIL: iverilog $2"; exit 1; }
  vvp "$1/$3.vvp" | grep '^STEP' > "$1/$3.txt"
}
even() { awk '$2 % 2 == 0' "$1"; }

STEPS=$((2 * PERIODS))
for d in "${DESIGNS[@]}"; do
  IFS=: read -r top src ctl need inits <<< "$d"  # `read` keeps the colons of the last field
  for reader in yosys-slang slang; do
    R="$OUT/$top/$reader"
    mkdir -p "$R"
    "$LHD" compile "$OUT/$src" --top "$top" --reader "$reader" --set yosys.setundef=zero \
      --workdir "$R/cw" --emit-dir "lg:$R/s1" > "$R/compile.log" 2>&1 \
      || { grep -o '"message":"[^"]*' "$R/compile.log" | head -3; echo "FAIL: $top ($reader) compile"; exit 1; }
    "$LHD" pass single_edge --top "$top" "lg:$R/s1" --emit-dir "lg:$R/s2" --workdir "$R/sw" > "$R/single_edge.log" 2>&1 \
      || { grep -o '"message":"[^"]*' "$R/single_edge.log" | head -3; echo "FAIL: $top ($reader) single_edge"; exit 1; }
    grep -q 'falling-edge memory(ies) moved to the fall slot' "$R/single_edge.log" \
      || { grep -o 'P=[0-9][^"]*' "$R/single_edge.log" | head -1; echo "FAIL: $top ($reader): the falling memory was not lowered"; exit 1; }
    "$LHD" compile "lg:$R/s2" --top "$top" --workdir "$R/vw" --emit-dir "lean:$R/vc" \
      --set formal.lean.mode=verified_compiler > "$R/vc.log" 2>&1 \
      || { grep -o '"message":"[^"]*' "$R/vc.log" | head -3; echo "FAIL: $top ($reader) certificate"; exit 1; }
    gen "$R/vc/${top}_io.json" "$top" "$R" "$inits"
    sim "$R" "$OUT/$src" rtl
    grep -q x "$R/rtl.txt" && { grep -m3 x "$R/rtl.txt"; echo "FAIL: the source produced x"; exit 1; }
    [[ $(wc -l < "$R/rtl.txt") -eq $STEPS ]] || { echo "FAIL: the source run did not produce $STEPS steps"; exit 1; }
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
    sim "$R" "$OUT/$ctl" ctl
    n=$(diff "$R/ctl.txt" "$R/ir.txt" | grep -c '^<' || true)
    nb=$(diff <(even "$R/ctl.txt") <(even "$R/ir.txt") | grep -c '^<' || true)
    [[ $n -gt 0 ]] || { echo "FAIL: $top ($reader): the control agreed on every step"; exit 1; }
    [[ $need != boundary || $nb -gt 0 ]] || { echo "FAIL: $top ($reader): the control agreed at every period boundary"; exit 1; }
    echo "ok: $top ($reader) control $ctl differs on $n of $STEPS steps ($nb period boundaries)"
  done
done
echo "PASS: negedge_mem_oracle"
