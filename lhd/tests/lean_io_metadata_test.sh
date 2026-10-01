#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The <Top>_io.json sidecar emitted beside a verified_compiler certificate.
#
# DesignCert is POSITIONAL by design: RuntimeInput[k], RuntimeResult.outputs[k],
# ClockEdges[k]. An external driver (a testbench, a differential harness, a
# simulator front-end) needs names to build a stimulus, and the obvious way to
# get them -- re-walk GraphIO and assume the order agrees -- is exactly how a
# sidecar silently drifts from the model it claims to describe. The ordinals are
# therefore recorded in the SAME loops that assign them.
#
# This checks the two can't disagree: the sidecar's counts and widths must match
# the certificate's own input arity, output list and clock list, and the mixed
# fixture's specific names must land on specific ordinals.
#
# NON-SEMANTIC: the sidecar is not a DesignCert field and is no part of
# compileDesign_correct. A missing sidecar must never be read as a proof gap.
set -u

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; }
LHD="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RTL="$HERE/mem_mixed_rdclk_negedge.v"
[ -r "$RTL" ] || { echo "FAIL: missing fixture $RTL"; exit 1; }

T="${TEST_TMPDIR:-$(cd "$HERE/../.." && pwd)/generated/tests}/lean_io_metadata"
rm -rf "$T"; mkdir -p "$T"; cd "$T" || exit 1

"$LHD" compile verilog "$RTL" --top mem_mixed_rdclk_negedge --reader yosys-slang \
  --workdir "$T/w" --emit-dir "lg:$T/lg" > "$T/compile.log" 2>&1 \
  || { echo "FAIL: the fixture did not import"; tail -3 "$T/compile.log"; exit 1; }
"$LHD" pass single_edge --top mem_mixed_rdclk_negedge "lg:$T/lg" --emit-dir "lg:$T/lg_norm" \
  --set multi_clock=true --workdir "$T/se" > "$T/se.log" 2>&1 \
  || { echo "FAIL: single_edge refused the fixture"; exit 1; }
"$LHD" compile "lg:$T/lg_norm" --top mem_mixed_rdclk_negedge --workdir "$T/lw" \
  --emit-dir "lean:$T/lean" --set formal.lean.mode=verified_compiler \
  --set formal.lean.strict=true > "$T/lean.log" 2>&1 \
  || { echo "FAIL: the certificate was not emitted"; tail -3 "$T/lean.log"; exit 1; }
echo "ok: certificate emitted from the NORMALIZED graph"

CERT="$T/lean/mem_mixed_rdclk_negedge_Lgraph.lean"
IOJ="$T/lean/mem_mixed_rdclk_negedge_io.json"
[ -r "$IOJ" ] || { echo "FAIL: no IO sidecar beside the certificate"; exit 1; }
echo "ok: the sidecar is emitted beside the certificate"

CERT="$CERT" IOJ="$IOJ" python3 - <<'PYIO'
import json, os, re, sys
cert = open(os.environ["CERT"]).read()
io = json.load(open(os.environ["IOJ"]))
fails = 0
def bad(m):
    global fails
    print(f"FAIL: {m}"); fails += 1

# --- inputs ----------------------------------------------------------------
# The contract is NOT "the two sets are equal". A primary input that the design
# consumes only as a CLOCK still occupies a RuntimeInput ordinal -- nothing
# reads it, but a driver must supply a value there or every later ordinal
# shifts by one. So:
#   * every `SourceDesc.input <ordinal> <width>` must be named by the sidecar
#     at that ordinal and width (certificate is a SUBSET of the sidecar);
#   * the sidecar's ordinals must be dense 0..n-1, or they cannot index
#     RuntimeInput at all;
#   * an ordinal the certificate never reads is reported, not hidden.
cert_in = {int(o): int(w) for o, w in re.findall(r"SourceDesc\.input\s+(\d+)\s+(\d+)", cert)}
side_in = {i["ordinal"]: i["width"] for i in io["inputs"]}
name_of = {i["ordinal"]: i["name"] for i in io["inputs"]}
if len(side_in) != len(io["inputs"]):
    bad("the sidecar repeats an input ordinal")
if sorted(side_in) != list(range(len(side_in))):
    bad(f"sidecar input ordinals are not dense 0..n-1: {sorted(side_in)}")
unnamed = sorted(set(cert_in) - set(side_in))
if unnamed:
    bad(f"the certificate reads input ordinal(s) {unnamed} that the sidecar does not name")
mism = [k for k in cert_in if k in side_in and cert_in[k] != side_in[k]]
if mism:
    bad("input width mismatch at ordinal(s) " + ",".join(str(k) for k in sorted(mism)))
if not unnamed and not mism:
    print(f"ok: all {len(cert_in)} certificate inputs are named at the right ordinal and width")
reserved = sorted(set(side_in) - set(cert_in))
print("ok: reserved-but-unread input ordinal(s): "
      + (", ".join(f"{k}={name_of[k]}" for k in reserved) if reserved else "none"))
# Pinned concretely: this fixture's clock is consumed as a clock and read by no
# source. If that ever changes the stimulus layout changes with it.
if reserved != [0] or name_of.get(0) != "clk":
    bad(f"expected exactly the clock (ordinal 0, 'clk') to be reserved-but-unread, got "
        + str([(k, name_of[k]) for k in reserved]))

# --- outputs: one sidecar entry per DesignCert output, in order -------------
m = re.search(r"outputs\s*:=\s*#\[(.*?)\]", cert, re.S)
cert_out = [int(w) for w in re.findall(r"width\s*:=\s*(\d+)", m.group(1))] if m else []
side_out = [o["width"] for o in sorted(io["outputs"], key=lambda o: o["ordinal"])]
if len(cert_out) != len(side_out):
    bad(f"{len(side_out)} sidecar outputs vs {len(cert_out)} in DesignCert.outputs "
        "(an undriven output must be skipped by both or neither)")
elif cert_out != side_out:
    bad(f"output widths differ in order: certificate {cert_out}, sidecar {side_out}")
else:
    print(f"ok: {len(cert_out)} outputs agree positionally with DesignCert.outputs")
if [o["ordinal"] for o in sorted(io["outputs"], key=lambda o: o["ordinal"])] != list(range(len(side_out))):
    bad("output ordinals are not dense 0..n-1, so they cannot index RuntimeResult.outputs")

# --- clocks -----------------------------------------------------------------
m = re.search(r"clocks\s*:=\s*#\[(.*?)\]", cert, re.S)
cert_clk = len(re.findall(r"name\s*:=", m.group(1))) if m else 0
if cert_clk != len(io["clocks"]):
    bad(f"{len(io['clocks'])} sidecar clocks vs {cert_clk} in DesignCert.clocks")
else:
    print(f"ok: {cert_clk} clock(s) agree")

# --- the fixture's own mapping ---------------------------------------------
# Names to ordinals, concretely: a sidecar that is internally consistent but
# maps the wrong name to an ordinal would drive the wrong port.
want_in = ["clk", "nd", "ra0", "ra1", "ra2", "ra3", "ra4", "waddr", "wdata", "we"]
got_in = [i["name"] for i in sorted(io["inputs"], key=lambda i: i["ordinal"])]
if got_in != want_in:
    bad(f"input name order changed\n      want: {want_in}\n      got:  {got_in}")
else:
    print("ok: the fixture's inputs map to the expected ordinals")
want_out = ["nq", "q0", "q1", "q2", "q3", "q4"]
got_out = [o["name"] for o in sorted(io["outputs"], key=lambda o: o["ordinal"])]
if got_out != want_out:
    bad(f"output name order changed\n      want: {want_out}\n      got:  {got_out}")
else:
    print("ok: the fixture's outputs map to the expected ordinals")
for nm, w in (("clk", 1), ("we", 1), ("wdata", 8), ("ra3", 4)):
    e = next((i for i in io["inputs"] if i["name"] == nm), None)
    if e is None or e["width"] != w:
        bad(f"input {nm} should be {w} bit(s), sidecar says {e and e['width']}")
print("ok: spot-checked input widths")

# --- runtime_input_arity and used_by_model ----------------------------------
# inputArity is the highest READ ordinal plus one; DirectSim ignores anything
# the model does not read. A driver needs both facts, so both are asserted
# against the certificate rather than taken on trust.
arity = io.get("runtime_input_arity")
if arity is None:
    bad("the sidecar does not report runtime_input_arity")
else:
    want = (max(cert_in) + 1) if cert_in else 0
    if arity != want:
        bad(f"runtime_input_arity is {arity}, but the highest certificate input ordinal "
            f"plus one is {want}")
    else:
        print(f"ok: runtime_input_arity {arity} matches the certificate")
for i in io["inputs"]:
    used = i.get("used_by_model")
    if used is None:
        bad(f"input {i['name']} has no used_by_model flag"); break
    if used != (i["ordinal"] in cert_in):
        bad(f"input {i['name']} (ordinal {i['ordinal']}) is marked used_by_model={used}, "
            f"but the certificate {'reads' if i['ordinal'] in cert_in else 'never reads'} it")
else:
    print("ok: used_by_model agrees with the certificate for every input")
sys.exit(1 if fails else 0)
PYIO
rc=$?

# ---- a refused REGENERATION must not leave the previous run's metadata ----
# Checking a fresh directory only proves a failed run creates no NEW sidecar.
# The dangerous case is the same output directory: succeed, then fail, and the
# old io.json sits beside a stale certificate looking current.
[ -r "$IOJ" ] || { echo "FAIL: expected a sidecar from the successful run"; exit 1; }
"$LHD" compile "lg:$T/lg_norm" --top mem_mixed_rdclk_negedge --workdir "$T/lw2" \
  --emit-dir "lean:$T/lean" --set formal.lean.mode=verified_compiler \
  --set formal.lean.max_nodes=1 > "$T/lean_refuse.log" 2>&1
if [ $? -eq 0 ]; then
  # Not SKIPPED: if the probe stops refusing, the stale-file property was never
  # exercised at all, and a silent skip would report that as a pass forever.
  echo "FAIL: the max_nodes=1 refusal probe SUCCEEDED, so the stale-sidecar"
  echo "      property was not tested. Pick a probe that still refuses."
  rc=1
elif [ -r "$IOJ" ]; then
  echo "FAIL: a refused REGENERATION left the previous run's sidecar in place"
  echo "      (a driver would read it as describing the current certificate)"
  rc=1
elif ls "$T/lean"/*_io.json.tmp >/dev/null 2>&1; then
  echo "FAIL: a refused regeneration left a .tmp sidecar behind"; rc=1
else
  echo "ok: a refused regeneration removes the previous sidecar"
fi

# ---- a TRAILING unused input, not just the interior clock hole ------------
# `runtime_input_arity` is the highest read ordinal plus one, so an unused port
# at the END sits outside it entirely -- a driver padding to the port count
# would feed values DirectSim ignores, and one padding to the arity would be
# right. The interior clock hole does not exercise that.
cat > "$T/trailing.v" <<'EOF'
module trailing_unused (
   input        clk
  ,input  [3:0] a
  ,output reg [3:0] y
  ,input  [7:0] zz_unused   // never read: highest ordinal, outside inputArity
);
  always @(posedge clk) y <= a;
endmodule
EOF
"$LHD" compile verilog "$T/trailing.v" --top trailing_unused --reader yosys-slang   --workdir "$T/tw" --emit-dir "lg:$T/tlg" > "$T/t_compile.log" 2>&1   && "$LHD" compile "lg:$T/tlg" --top trailing_unused --workdir "$T/tlw"        --emit-dir "lean:$T/tlean" --set formal.lean.mode=verified_compiler        > "$T/t_lean.log" 2>&1
if [ $? -ne 0 ]; then
  # Not SKIPPED: this fixture is the ONLY coverage of a trailing unused input,
  # which is the case the interior clock hole cannot exercise. Losing it
  # silently would leave runtime_input_arity untested at its boundary.
  echo "FAIL: the trailing-unused fixture did not emit, so runtime_input_arity"
  echo "      coverage for a TRAILING unused port was lost"
  grep -oP '"message":"\K[^"]{0,120}' "$T/t_lean.log" | head -1 | sed 's/^/      /'
  rc=1
else
  TJ="$T/tlean/trailing_unused_io.json"
  TJ="$TJ" python3 - <<'PYTRAIL'
import json, os, sys
d = json.load(open(os.environ["TJ"]))
ins = sorted(d["inputs"], key=lambda i: i["ordinal"])
arity = d["runtime_input_arity"]
last = ins[-1]
fails = 0
if last["name"] != "zz_unused":
    print(f"FAIL: expected zz_unused to hold the highest ordinal, got {last['name']}"); fails += 1
elif last["used_by_model"]:
    print("FAIL: zz_unused is never read but is marked used_by_model"); fails += 1
elif arity > last["ordinal"]:
    print(f"FAIL: runtime_input_arity {arity} includes the unread trailing ordinal "
          f"{last['ordinal']}"); fails += 1
else:
    print(f"ok: a trailing unused input sits outside runtime_input_arity "
          f"({arity} <= ordinal {last['ordinal']}), and is marked used_by_model=false")
if arity > len(ins):
    print(f"FAIL: runtime_input_arity {arity} exceeds the {len(ins)} declared ports"); fails += 1
sys.exit(1 if fails else 0)
PYTRAIL
  [ $? -eq 0 ] || rc=1
fi

# ---- the pair must publish together, or not at all -----------------------
# The certificate used to be renamed final BEFORE the sidecar was written, so a
# sidecar failure aborted the command having already published a new
# _Lgraph.lean with no metadata beside it -- a half generation a file scanner
# reads as success. Obstruct the sidecar's destination with a DIRECTORY, which
# rename() cannot overwrite, and require: nonzero exit, and no certificate left
# behind by this invocation.
OB="$T/obstruct"
rm -rf "$OB"; mkdir -p "$OB"
# A NON-EMPTY directory. An empty one is not an obstruction: the generation
# transaction's std::remove() succeeds on it (POSIX remove() rmdir's an empty
# directory), so the probe quietly cleared its own obstacle and reported
# success. A non-empty directory resists both remove() and rename().
mkdir -p "$OB/mem_mixed_rdclk_negedge_io.json"
: > "$OB/mem_mixed_rdclk_negedge_io.json/keep"
"$LHD" compile "lg:$T/lg_norm" --top mem_mixed_rdclk_negedge --workdir "$T/lw_ob" \
  --emit-dir "lean:$OB" --set formal.lean.mode=verified_compiler \
  --set formal.lean.strict=true > "$T/obstruct.log" 2>&1
obrc=$?
if [ "$obrc" -eq 0 ]; then
  echo "FAIL: an unwritable sidecar destination still reported success"
  rc=1
elif [ -e "$OB/mem_mixed_rdclk_negedge_Lgraph.lean" ]; then
  echo "FAIL: the command failed but left a certificate with no sidecar beside it"
  echo "      (a scanner would read that half-pair as a successful generation)"
  rc=1
elif ls "$OB"/*.tmp >/dev/null 2>&1; then
  echo "FAIL: temp files were left behind after the failed publish"; rc=1
else
  echo "ok: an unwritable sidecar destination fails and leaves no half-pair"
fi
rm -rf "$OB"

[ "$rc" -eq 0 ] || { echo "FAIL: sidecar/certificate disagreement"; exit 1; }
echo "PASS: lean_io_metadata_test"
