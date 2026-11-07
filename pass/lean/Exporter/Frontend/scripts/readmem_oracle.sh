#!/usr/bin/env bash
set -euo pipefail
# Memories loaded from a file (`$readmemh`/`$readmemb`) through the Lean route
# (decision #3).
#
# The native slang reader keeps `$readmemh("rom.hex", m)` as a COMMAND on the
# memory's `initial` pin; the exporter now reads the file at export, as `lhd sim`
# reads it at power-on (fill with 0, then load; hlop/readmem.hpp parses). A
# relative path opens from the directory `lhd` runs in (SV keeps runtime-cwd
# semantics), so every lhd call below runs in $OUT.
#
# Checks:
#   rom      a 16 x 8 ROM, registered and asynchronous read, loaded from an
#            image with comments and OUT-OF-ORDER `@` blocks: IR and VC
#            (cert_trace) equal iverilog on the source for every step.
#            Control: iverilog with an image that differs in one entry.
#   partial  an image that names entries 0-11 only: those match iverilog, and
#            12-15 read 0 (lhd sim's fill; iverilog has x there).
#   x digit  refused at export, like X/Z bits in a literal initializer.
#   missing  refused at export, naming the file.
#   ram      a WRITABLE memory loaded from the same image (written back with
#            its own registered read every cycle): the file's words are its
#            POWER-ON image (DCERT3 `MemoryDesc.init`, decision #5), and IR and
#            VC equal iverilog on every step, FP (`initStateOf`) equals IR, and
#            the legacy model (`initState`) equals iverilog. Control: the RAM
#            loaded from the one-entry-changed image differs.
#
# The Lean toolchain pin lives in formal/lean; lean runs from there.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/readmem_oracle}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
command -v iverilog >/dev/null && command -v vvp >/dev/null || { echo "iverilog/vvp not on PATH" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"
STEPS=40
LP=$(cd "$ROOT/formal/lean" && lake env printenv LEAN_PATH)

cat > "$OUT/rom_file.v" <<'EOF'
module rom_file(input clk, input [3:0] ra, output reg [7:0] q, output [7:0] qa);
  reg [7:0] m [0:15];
  initial $readmemh("rom.hex", m);
  always @(posedge clk) q <= m[ra];   // registered read
  assign qa = m[ra];                  // asynchronous read
endmodule
EOF
sed -e 's/rom_file(/ram_file(/' -e 's|  assign qa = m\[ra\];.*|  assign qa = m[ra];\n  always @(posedge clk) m[ra] <= q;|' \
  "$OUT/rom_file.v" > "$OUT/ram_file.v"
grep -q 'm\[ra\] <= q' "$OUT/ram_file.v" || { echo "FAIL: the writable variant was not derived"; exit 1; }
cat > "$OUT/full.hex" <<'EOF'
// every entry named; an @address jump back and a comment on the way
3a c5 07 ff
10 22 34 46
@c
9e 81 /* block comment */ 5d 6f
@8
a0 b1 c2 d3
EOF
sed 's/c5 07 ff/c5 07 fe/' "$OUT/full.hex" > "$OUT/alt.hex"
printf '3a c5 07 ff\n10 22 34 46\na0 b1 c2 d3\n' > "$OUT/part.hex"
printf '3a c5 0x ff\n' > "$OUT/x.hex"

run() { # <dir> <hex> <top> <src> -> exports into <dir>/vc, log <dir>/vc.log; returns the export's status
  local d=$1
  mkdir -p "$d"
  cp "$OUT/$2" "$d/rom.hex"
  cp "$OUT/$4" "$d/"
  (cd "$d" && "$LHD" compile "$4" --reader slang --top "$3" --emit-dir lg:lg --workdir cw > compile.log 2>&1) \
    || { tail -3 "$d/compile.log"; echo "FAIL: compile in $d"; exit 1; }
  grep -q "readmemh" <("$LHD" tool grep "lg:$d/lg" kind=memory 2>/dev/null) \
    || { echo "FAIL: the reader did not keep the file command on the memory"; exit 1; }
  (cd "$d" && "$LHD" compile lg:lg --top "$3" --emit-dir lean:vc --set formal.lean.mode=verified_compiler \
     --workdir vw > vc.log 2>&1)
}

sim() { # <dir> -> <dir>/rtl.txt (iverilog run IN <dir>, so it reads <dir>/rom.hex)
  (cd "$1" && iverilog -g2012 -o rtl.vvp rom_file.v "$OUT/tb.v" > iverilog.log 2>&1 && vvp rtl.vvp | grep '^STEP' > rtl.txt) \
    || { cat "$1/iverilog.log"; echo "FAIL: iverilog in $1"; exit 1; }
}
ct() { # <dir> <name> <opts...>
  local d=$1 n=$2; shift 2
  (cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run \
     Futamura_Projection_Scripts/cert_trace.lean "$d/vc/rom_file.dcert" "$STEPS" "$OUT/stim.txt" "$@" \
     > "$d/$n.txt" 2> "$d/$n.err") || { cat "$d/$n.err"; echo "FAIL: cert_trace $n"; exit 1; }
}

# ---- rom: the full image ------------------------------------------------------
R="$OUT/rom"
run "$R" full.hex rom_file rom_file.v || { grep -o '"message":"[^"]*' "$R/vc.log" | head -2; echo "FAIL: the export refused the ROM"; exit 1; }
# ---- stimulus and the source testbench ----------------------------------------
python3 -I - "$OUT" "$STEPS" "$OUT/rom/vc/rom_file_io.json" <<'PY'
import json, random, sys
out, steps = sys.argv[1], int(sys.argv[2])
ord_ra = next(i["ordinal"] for i in json.load(open(sys.argv[3]))["inputs"] if i["name"] == "ra")
random.seed(5)
ra = [k % 16 if k < 16 else random.randrange(16) for k in range(steps)]  # every entry, then random
open(f"{out}/ra.txt", "w").write("\n".join(map(str, ra)) + "\n")
with open(f"{out}/stim.txt", "w") as f:
    f.write("stim v1\n")
    for k, a in enumerate(ra):
        f.write(f"at {k} in {ord_ra} 4 {a}\n")
with open(f"{out}/tb.v", "w") as f:
    f.write("`timescale 1ns/1ps\nmodule tb;\n reg clk = 0; reg [3:0] ra = 0; wire [7:0] q, qa;\n")
    f.write(" rom_file u(.clk(clk), .ra(ra), .q(q), .qa(qa));\n initial begin\n  u.q = 0;\n")
    for k, a in enumerate(ra):
        f.write(f"  #({10*k+1} - $time); ra = {a};\n")
        f.write(f'  #({10*k+4} - $time); $display("STEP {k} %0d %0d", q, qa);\n')
        f.write(f"  #({10*k+5} - $time); clk = 1;\n  #({10*k+8} - $time); clk = 0;\n")
    f.write("  $finish;\n end\nendmodule\n")
PY
sim "$R"
grep -q x "$R/rtl.txt" && { echo "FAIL: the source read x"; exit 1; }
ct "$R" ir edges:all
ct "$R" vc edges:all route:vc
for n in ir vc; do
  diff "$R/rtl.txt" "$R/$n.txt" > "$R/diff_$n.txt" || { head -4 "$R/diff_$n.txt"; echo "FAIL: $n differs from the source"; exit 1; }
done
echo "ok: rom -- IR and VC equal iverilog on all $STEPS steps (registered and async read, out-of-order @ blocks)"
C="$OUT/rom_alt"
mkdir -p "$C"; cp "$OUT/alt.hex" "$C/rom.hex"; cp "$OUT/rom_file.v" "$C/"
sim "$C"
n=$(diff "$C/rtl.txt" "$R/ir.txt" | grep -c '^<' || true)
[[ $n -gt 0 ]] || { echo "FAIL: an image that differs in one entry gave the same trace"; exit 1; }
echo "ok: control -- the source with one entry changed differs on $n steps"

# ---- partial: entries 12-15 are 0 ---------------------------------------------
P="$OUT/partial"
run "$P" part.hex rom_file rom_file.v || { grep -o '"message":"[^"]*' "$P/vc.log" | head -2; echo "FAIL: the export refused a partial image"; exit 1; }
sim "$P"
ct "$P" ir edges:all
# Step k shows qa = m[ra_k] and q = m[ra_{k-1}]: compare entries the file names
# with iverilog, and require 0 where it names none.
paste -d' ' "$OUT/ra.txt" "$P/ir.txt" "$P/rtl.txt" | awk -v prev=-1 '
  { ra = $1; iq = $4; iqa = $5; rq = $8; rqa = $9
    if (ra < 12 && iqa != rqa) { bad = bad " async@" NR-1 }
    if (ra >= 12 && iqa != 0) { bad = bad " async0@" NR-1 }
    if (prev >= 0 && prev < 12 && iq != rq) { bad = bad " reg@" NR-1 }
    if (prev >= 12 && iq != 0) { bad = bad " reg0@" NR-1 }
    prev = ra }
  END { if (bad != "") { print "FAIL: partial image:" bad; exit 1 } }'
echo "ok: partial -- named entries equal iverilog; unnamed entries read 0 (lhd sim's fill)"

# ---- refusals -----------------------------------------------------------------
X="$OUT/xdigit"
if run "$X" x.hex rom_file rom_file.v; then echo "FAIL: an image with an x digit was exported"; exit 1; fi
grep -q 'X/Z digits' "$X/vc.log" || { grep -o '"message":"[^"]*' "$X/vc.log" | head -2; echo "FAIL: the x-digit refusal does not say why"; exit 1; }
echo "ok: an x digit is refused"
M="$OUT/missing"
run "$M" full.hex rom_file rom_file.v > /dev/null || true
rm -rf "$M/vc" "$M/vw"; rm -f "$M/rom.hex"
if (cd "$M" && "$LHD" compile lg:lg --top rom_file --emit-dir lean:vc --set formal.lean.mode=verified_compiler \
      --workdir vw > vc_missing.log 2>&1); then
  echo "FAIL: the export succeeded with the image file missing"; exit 1
fi
grep -q 'cannot be read' "$M/vc_missing.log" || { grep -o '"message":"[^"]*' "$M/vc_missing.log" | head -2; echo "FAIL: the missing-file refusal does not say why"; exit 1; }
echo "ok: a missing image file is refused"
W="$OUT/ram"
run "$W" full.hex ram_file ram_file.v \
  || { grep -o '"message":"[^"]*' "$W/vc.log" | head -2; echo "FAIL: the export refused a writable memory with a file preload"; exit 1; }
grep -q 'init := #\[' "$W/vc/ram_file_Lgraph.lean" || { echo "FAIL: the certificate carries no power-on words"; exit 1; }
cmp -s <(python3 -I -c 'import json,sys; print([(i["name"],i["ordinal"]) for i in json.load(open(sys.argv[1]))["inputs"]])' "$W/vc/ram_file_io.json") \
       <(python3 -I -c 'import json,sys; print([(i["name"],i["ordinal"]) for i in json.load(open(sys.argv[1]))["inputs"]])' "$R/vc/rom_file_io.json") \
  || { echo "FAIL: the RAM's input ordinals differ from the ROM's; the shared stimulus does not apply"; exit 1; }
sed 's/ rom_file u(/ ram_file u(/' "$OUT/tb.v" > "$OUT/tb_ram.v"
simram() { # <dir> -> <dir>/rtl.txt
  (cd "$1" && iverilog -g2012 -o rtl.vvp ram_file.v "$OUT/tb_ram.v" > iverilog.log 2>&1 && vvp rtl.vvp | grep '^STEP' > rtl.txt) \
    || { cat "$1/iverilog.log"; echo "FAIL: iverilog in $1"; exit 1; }
}
simram "$W"
grep -q x "$W/rtl.txt" && { echo "FAIL: the RAM source read x"; exit 1; }
for route in ir vc; do
  opts=(edges:all); [[ $route == vc ]] && opts+=(route:vc)
  (cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run \
     Futamura_Projection_Scripts/cert_trace.lean "$W/vc/ram_file.dcert" "$STEPS" "$OUT/stim.txt" "${opts[@]}" \
     > "$W/$route.txt" 2> "$W/$route.err") || { cat "$W/$route.err"; echo "FAIL: cert_trace $route (ram)"; exit 1; }
  diff "$W/rtl.txt" "$W/$route.txt" > "$W/diff_$route.txt" \
    || { head -4 "$W/diff_$route.txt"; echo "FAIL: ram $route differs from the source"; exit 1; }
done
echo "ok: ram -- a writable memory starts from the file's words: IR and VC equal iverilog on all $STEPS steps"
# FP starts from `initStateOf`; its `edges:all` leaves every third step quiet,
# so it is compared with cert_trace `edges:third`.
(cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run \
   Futamura_Projection_Scripts/cert_trace.lean "$W/vc/ram_file.dcert" "$STEPS" "$OUT/stim.txt" edges:third \
   > "$W/ir_third.txt" 2> "$W/ir_third.err") || { cat "$W/ir_third.err"; echo "FAIL: cert_trace edges:third (ram)"; exit 1; }
(cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP" lean --tstack=1048576 --run \
   Futamura_Projection_Scripts/dcert2_memroute.lean "$W/vc/ram_file.dcert" 20000 200 "$STEPS" zero edges:all \
   "stim:$OUT/stim.txt" outputs nocompare > "$W/fp.log" 2> "$W/fp.err") \
  || { tail -3 "$W/fp.log"; cat "$W/fp.err"; echo "FAIL: FP runner (ram)"; exit 1; }
grep '^STEP' "$W/fp.log" > "$W/fp.txt"
diff "$W/ir_third.txt" "$W/fp.txt" > "$W/diff_fp.txt" || { head -4 "$W/diff_fp.txt"; echo "FAIL: ram FP differs from IR"; exit 1; }
echo "ok: ram -- FP (initStateOf) equals IR with every third step quiet"
# The legacy model starts from its emitted `initState`; built with `lean -R`
# into this run's directory, so no `lake build` runs in the shared workspace.
(cd "$W" && "$LHD" compile lg:lg --top ram_file --emit-dir lean:legacy --set formal.lean.emit_cert=true \
   --set formal.lean.emit_fast_bridge=true --workdir lw > legacy.log 2>&1) \
  || { grep -o '"message":"[^"]*' "$W/legacy.log" | head -2; echo "FAIL: legacy emission (ram)"; exit 1; }
LR="$W/lroot"; OL="$W/olean"
mkdir -p "$LR/LegacyRam" "$OL/LegacyRam"
cp "$W/legacy/ram_file_Lgraph.lean" "$LR/LegacyRam/"
python3 -I "$ROOT/pass/lean/Legacy_Fast_Model_Simulator/scripts/make_legacy_sim.py" "$LR/LegacyRam/ram_file_Lgraph.lean" \
  --cycles "$STEPS" --stim --out "$LR/LegacyRam" --module-prefix LegacyRam > "$W/legacy_gen.log" 2>&1 \
  || { tail -3 "$W/legacy_gen.log"; echo "FAIL: make_legacy_sim (ram)"; exit 1; }
(cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP:$OL" lean -R "$LR" -o "$OL/LegacyRam/ram_file_Lgraph.olean" \
   "$LR/LegacyRam/ram_file_Lgraph.lean" > "$W/legacy_build.log" 2>&1) \
  || { grep -m5 error "$W/legacy_build.log"; echo "FAIL: legacy elaboration (ram, bridge proofs)"; exit 1; }
grep -q sorry "$W/legacy_build.log" && { echo "FAIL: sorry in the legacy proofs (ram)"; exit 1; }
(cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$LP:$OL" lean --run "$LR/LegacyRam/ram_file_LegacySim.lean" \
   "$OUT/stim.txt" > "$W/legacy.txt" 2> "$W/legacy.err") || { cat "$W/legacy.err"; echo "FAIL: legacy run (ram)"; exit 1; }
diff "$W/rtl.txt" "$W/legacy.txt" > "$W/diff_legacy.txt" || { head -4 "$W/diff_legacy.txt"; echo "FAIL: ram legacy differs from the source"; exit 1; }
echo "ok: ram -- the legacy model (initState, bridge proofs) equals iverilog on all $STEPS steps"
WC="$OUT/ram_alt"
mkdir -p "$WC"; cp "$OUT/alt.hex" "$WC/rom.hex"; cp "$OUT/ram_file.v" "$WC/"
simram "$WC"
n=$(diff "$WC/rtl.txt" "$W/ir.txt" | grep -c '^<' || true)
[[ $n -gt 0 ]] || { echo "FAIL: the RAM from a different image gave the same trace"; exit 1; }
echo "ok: control -- the RAM loaded from the one-entry-changed image differs on $n steps"
echo "PASS: readmem_oracle"
