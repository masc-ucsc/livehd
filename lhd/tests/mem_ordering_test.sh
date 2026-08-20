#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Memory same-cycle ordering: `ordering="none"|"fwd"|"program"` (lhdsuite
# array_problem.md ruling, 2026-07-27).
#
# The equiv pairs (inou/prp/tests/equiv/mem_ordering_*) prove the BEHAVIOR
# against golden Verilog. This test pins the ENCODING those pairs depend on:
# the Memory cell's `fwd` sink is a per-(READ-port, WRITE-port) matrix, bit
# (r*n_wr + w) = "read port r sees write port w's new data" (graph/cell.cpp).
# Behavioral equivalence alone cannot catch a row/column transpose on a
# 1-read-port design (the matrix degenerates to the old per-write mask), which
# is exactly the shape almost every fixture has — so the canonical program
# below deliberately uses TWO read ports at DIFFERENT program positions.
#
#   o1 = mem[a1]     read BEFORE the write -> row 0
#   mem[a2] = d2
#   o4 = mem[a4]     read AFTER  the write -> row 1
#
# Expected FWD (1 write port, so row r is bit r):
# The array carries NO initializer on purpose: an array init is a reset value,
# and its restore sweep adds a second WRITE PORT — which shifts every row of the
# matrix by one column and would hide a transpose behind arithmetic.
#
#   program (default) -> 0b10 = 2   only the later read forwards
#   fwd               -> 0b11 = 3   position-blind: both forward
#   old               -> 0b00 = 0   nothing forwards; the read is defined OLD
#   none              -> 0b00 = 0   nothing forwards
#
# A transposed or position-blind lowering yields 1, 3 or 0 for ALL THREE modes,
# which no golden-Verilog pair with a single read port would notice.
#
# "old" and "none" share FWD(0), so FWD alone cannot tell them apart — that is
# exactly why the Memory cell grew the parallel `undef` matrix (graph/cell.cpp
# pid 15) and the wrappers grew the UNDEF parameter. Expected UNDEF:
#   program / fwd / old -> absent (the pin is only driven when non-zero)
#   none                -> 0b11 = 3   BOTH reads are undefined on a collision
# If `undef` were ever dropped again, "none" would silently collapse back onto
# "old" and the formal `?` half of the ruling would be gone with no test red.

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "${LHD}" ]; then
  LHD=./lhd/lhd
fi
if [ ! -x "${LHD}" ]; then
  echo "FAIL: no lhd binary"
  exit 3
fi

TMP=$(mktemp -d)
trap 'rm -rf "${TMP}"' EXIT
rc=0

gen() { # $1 = attribute text (may be empty)
  cat >"${TMP}/m.prp" <<EOF
pub mod m$1(clk:u1, a1:u2, a2:u2, d2:u2, a4:u2) -> (o1:u2@[], o4:u2@[]) {
  reg mem:[4]u2$2
  o1 = mem[a1]
  mem[a2] = d2
  o4 = mem[a4]
}
EOF
}

check() { # $1 = label, $2 = reg attribute text, $3 = expected FWD, $4 = expected UNDEF ("" = absent)
  gen "" "$2"
  rm -rf "${TMP}/v" "${TMP}/w"
  if ! "${LHD}" compile "${TMP}/m.prp" --top m --emit-dir "verilog:${TMP}/v/" \
       --workdir "${TMP}/w" -q >"${TMP}/log" 2>&1; then
    echo "FAIL: ${1}: compile failed"
    cat "${TMP}/log"
    rc=1
    return
  fi
  got=$(grep -ohE '\.FWD\([0-9]+\)' "${TMP}"/v/*.v | head -1 | tr -dc '0-9')
  if [ "${got}" != "$3" ]; then
    echo "FAIL: ${1}: expected FWD($3), got FWD(${got:-<none>})"
    grep -hE 'cgen_memory' "${TMP}"/v/*.v | head -2
    rc=1
    return
  fi
  # `undef` is driven only when non-zero, so the parameter is ABSENT for every
  # mode but "none" — which also keeps every pre-existing netlist byte-identical.
  ugot=$(grep -ohE '\.UNDEF\([0-9]+\)' "${TMP}"/v/*.v | head -1 | tr -dc '0-9')
  if [ "${ugot}" != "$4" ]; then
    echo "FAIL: ${1}: expected UNDEF(${4:-<absent>}), got UNDEF(${ugot:-<absent>})"
    grep -hE 'cgen_memory' "${TMP}"/v/*.v | head -2
    rc=1
    return
  fi
  echo "ok: ${1} -> FWD(${got}) UNDEF(${ugot:-<absent>})"
}

check "default (program)"   ""                      2 ""
check 'ordering="program"'  ':[ordering="program"]' 2 ""
check 'ordering="fwd"'      ':[ordering="fwd"]'     3 ""
check 'ordering="old"'      ':[ordering="old"]'     0 ""
check 'ordering="none"'     ':[ordering="none"]'    0 3

# The shipped wrapper must actually CARRY the UNDEF parameter — an override
# against a module that lacks it is a hard elaboration error in every reader, so
# a wrapper/cgen skew would surface far from here.
if ! grep -q 'UNDEF' ware/rtl/cgen_memory_2rd_1wr.v 2>/dev/null; then
  echo "FAIL: ware/rtl/cgen_memory_2rd_1wr.v has no UNDEF parameter but cgen emits .UNDEF()"
  rc=1
else
  echo "ok: shipped wrapper carries UNDEF"
fi

# A bogus value must be a hard error, never a silent accept-and-ignore (which
# is how `ordering` behaved before it was implemented).
gen "" ':[ordering="bogus"]'
rm -rf "${TMP}/w2"
if "${LHD}" compile "${TMP}/m.prp" --top m --workdir "${TMP}/w2" \
     >"${TMP}/log2" 2>&1; then
  echo "FAIL: ordering=\"bogus\" compiled cleanly (must be an error)"
  rc=1
elif ! grep -q "ordering" "${TMP}/log2"; then
  echo "FAIL: ordering=\"bogus\" errored without naming the attribute"
  cat "${TMP}/log2"
  rc=1
else
  echo "ok: bogus ordering rejected"
fi

# Same-address multi-write forwarding priority. Every model that resolves a
# collision must agree that the LAST program write wins: the ware/rtl wrappers
# (chain checks high->low), cgen_sim (ascending overwrite), the lec encoder
# (ascending STORE fold) and pass.abc/mem_lower (ascending mux fold, whose
# outermost mux is the winner). mem_lower disagreed until 2026-07-27 — it folded
# high->low, making write port 0 outermost, which contradicted its OWN storage
# fold. A 1-write memory cannot see this, so drive two writes at one address and
# LEC the tech-mapped netlist against the same design partitioned but unmapped.
LIB=inou/prp/tests/abc/test.lib
if [ -f "${LIB}" ]; then
  cat >"${TMP}/w.prp" <<'EOF'
pub mod w(clk:u1, a:u2, d2:u2, d3:u2, ra:u2) -> (o:u2@[]) {
  reg mem:[4]u2:[ordering="fwd"]
  mem[a] = d2
  mem[a] = d3
  o = mem[ra]
}
EOF
  D="${TMP}/abc"
  mkdir -p "${D}"
  ok=1
  "${LHD}" compile "${TMP}/w.prp" --top w.w --recipe O1 --emit-dir "lg:${D}/lg" --workdir "${D}/w1" -q >/dev/null 2>&1 || ok=0
  "${LHD}" pass color synth --top w.w "lg:${D}/lg" --workdir "${D}/w2" -q >/dev/null 2>&1 || ok=0
  "${LHD}" pass abc --top w.w "lg:${D}/lg" --emit-dir "lg:${D}/net" --set abc.library="${LIB}" \
      --set pass.abc.memory=true --workdir "${D}/w3" -q >/dev/null 2>&1 || ok=0
  "${LHD}" pass partition --top w.w "lg:${D}/lg" --emit-dir "lg:${D}/re" --workdir "${D}/w4" -q >/dev/null 2>&1 || ok=0
  "${LHD}" pass liberty gensim "${LIB}" --emit-dir "lg:${D}/models" --workdir "${D}/w5" -q >/dev/null 2>&1 || ok=0
  for x in net models re; do
    "${LHD}" compile "lg:${D}/${x}" --recipe O0 --emit-dir "verilog:${D}/${x}v" --workdir "${D}/w_${x}" -q >/dev/null 2>&1 || ok=0
  done
  cat "${D}"/netv/*.v "${D}"/modelsv/*.v >"${D}/impl.v" 2>/dev/null
  cat "${D}"/rev/*.v >"${D}/ref.v" 2>/dev/null
  if [ "${ok}" != "1" ] || [ ! -s "${D}/impl.v" ] || [ ! -s "${D}/ref.v" ]; then
    echo "FAIL: abc write-priority: could not build the netlist/reference pair"
    rc=1
  elif "${LHD}" lec --set formal.solver=lgyosys --impl "verilog:${D}/impl.v" \
         --ref "verilog:${D}/ref.v" --top w --workdir "${D}/wc" >"${D}/lec.log" 2>&1; then
    echo "ok: abc same-address multi-write forwards the LAST write"
  else
    echo "FAIL: abc forwarding priority disagrees with the memory model"
    echo "      (a same-address double write must forward the LAST one)"
    tail -3 "${D}/lec.log"
    rc=1
  fi
else
  echo "FAIL: abc write-priority: missing ${LIB} (data dep dropped?)"
  rc=1
fi

if [ ${rc} -eq 0 ]; then
  echo "PASS: memory ordering matrix"
fi
exit ${rc}
