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
sys.exit(1 if fails else 0)
PYIO
rc=$?

# A REFUSED design must not leave metadata behind that looks valid.
"$LHD" compile "lg:$T/lg" --top mem_mixed_rdclk_negedge --workdir "$T/lw_raw" \
  --emit-dir "lean:$T/lean_raw" --set formal.lean.mode=verified_compiler \
  --set formal.lean.max_nodes=1 > "$T/lean_raw.log" 2>&1
if [ $? -eq 0 ]; then
  echo "note: the refusal probe did not refuse; skipping the stale-metadata check"
else
  if ls "$T/lean_raw"/*_io.json >/dev/null 2>&1; then
    echo "FAIL: a REFUSED design left an IO sidecar behind"; rc=1
  else
    echo "ok: a refused design leaves no sidecar"
  fi
fi

[ "$rc" -eq 0 ] || { echo "FAIL: sidecar/certificate disagreement"; exit 1; }
echo "PASS: lean_io_metadata_test"
