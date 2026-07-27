#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# TRACKER — a TRUSTED def whose struct ports are a FLAT BUS on one side and
# PER-LEAF on the other cannot be paired, so its outputs become unrelated free
# symbols and the parent comparison degrades to Unknown (or a false REFUTED).
#
# This is the root cause of lhdsuite `//bench:minion_lec`'s refutation
# (`prim_mul_div`, resp_dest ref=126 impl=127). Minimised from it by
# divide-and-conquer; see lhdsuite array_problem.md.
#
# WHY THE TWO SHAPES EXIST — by design, not by accident.
# lhd/lhd_kernel_compile.cpp gates `compile.slang.struct_port_bundles` on the
# emission: a graphs flow (lg/verilog) keeps FLAT struct ports ("that flat
# lgraph is the LEC reference"), while the pyrope flow turns a qualifying
# packed-struct port into a Pyrope TUPLE port, which compiles to per-leaf graph
# ports `base.field`. So the bench legitimately compares a bus-port reference
# against a leaf-port implementation.
#
# pass/lec ALREADY bridges that for TOP-LEVEL IO — query.cpp's
# `detect_port_bundles` / "Tuple-leaf <-> flat-bus port bundles" (~:1725), whose
# layout convention is "the FIRST leaf in decl order is the MOST SIGNIFICANT
# bits". Case 1 below proves that bridge works.
#
# THE GAP: an internal TRUSTED instance is a BOX, not top-level IO, and the box
# machinery keys ports purely BY NAME (encode.hpp `Comb_box`: `in_ports` is a
# NAME-SORTED concat layout, `out_fn`/`out_w` are keyed by port name). The two
# sides' name sets are then disjoint (`din` vs `din.fp`/`din.addr`/
# `din.thread_id`), so nothing pairs. Case 3 is that gap.
#
# TO FIX: apply the same leaf<->bus normalisation to box instance ports when
# building the box correspondence, so a bundled `base` bus and its `base.field`
# leaves present one identical signature to the UF.
#
# WHAT IS NOT WRONG (case 2 pins this down, so a future fix does not go
# hunting in the front end): the emitted Pyrope and the netlist regenerated
# from it are CORRECT — lgcheck proves the regenerated netlist against the
# original through an internal struct-port child.

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "${LHD}" ]; then LHD=./lhd/lhd; fi
if [ ! -x "${LHD}" ]; then echo "FAIL: no lhd binary"; exit 3; fi

TMP=$(mktemp -d)
trap 'rm -rf "${TMP}"' EXIT
rc=0

# A child with packed-struct ports, and two parents: one whose top ports are
# also structs, one whose top ports are flat (so lgcheck can compare directly).
cat >"${TMP}/d.sv" <<'EOF'
package dp;
  typedef struct packed { logic fp; logic [4:0] addr; logic thread_id; } dest_t;
endpackage
module kid(input logic clk, input logic rst_ni, input dp::dest_t din, output dp::dest_t dout);
  always_ff @(posedge clk) begin
    if (!rst_ni) dout <= '0;
    else         dout <= din;
  end
endmodule
module flat_top(input logic clk, input logic rst_ni,
                input logic [6:0] in_bus, output logic [6:0] out_bus);
  dp::dest_t a, b;
  assign a = in_bus;
  kid u(.clk(clk), .rst_ni(rst_ni), .din(a), .dout(b));
  assign out_bus = b;
endmodule
EOF

run() { "${LHD}" "$@" >"${TMP}/log" 2>&1; }
verdict() { grep -oE "(PROVEN|REFUTED|INCONCLUSIVE)" "${TMP}/log" | head -1; }

# Build the two sides of flat_top: reference from Verilog (flat struct ports on
# `kid`), implementation via the emitted Pyrope (per-leaf ports on `kid`).
run compile "${TMP}/d.sv" --top flat_top --emit-dir "lg:${TMP}/ref.lg"   --workdir "${TMP}/w1" -q || { echo "FAIL: ref compile"; exit 1; }
run compile "${TMP}/d.sv" --top flat_top --emit-dir "pyrope:${TMP}/prp/" --workdir "${TMP}/w2" -q || { echo "FAIL: pyrope emit"; exit 1; }
run compile "${TMP}"/prp/*.prp --top flat_top --emit-dir "lg:${TMP}/impl.lg" --workdir "${TMP}/w3" -q || { echo "FAIL: pyrope compile"; exit 1; }

# --- case 1: NOT trusted. The structural comparison (with top-level leaf<->bus
# bundling where it applies) must PROVE. This is the control: everything about
# the two designs agrees when the child is actually compared.
run lec --impl "lg:${TMP}/impl.lg" --ref "lg:${TMP}/ref.lg" --top flat_top --workdir "${TMP}/wl1"
v=$(verdict)
if [ "${v}" = "PROVEN" ]; then
  echo "ok: untrusted child proves (bus-vs-leaf handled when the child is compared)"
else
  echo "FAIL: untrusted child gave ${v:-<none>}, expected PROVEN"
  rc=1
fi

# --- case 2: the FRONT END is innocent. lgcheck the netlist regenerated from
# the emitted Pyrope against the reference netlist. flat_top's own ports are
# flat, so the port lists match and yosys can miter them directly.
run compile "lg:${TMP}/ref.lg"  --top flat_top --emit-dir "verilog:${TMP}/rv/" --workdir "${TMP}/w4" -q
run compile "lg:${TMP}/impl.lg" --top flat_top --emit-dir "verilog:${TMP}/iv/" --workdir "${TMP}/w5" -q
cat "${TMP}"/rv/*.v >"${TMP}/ref.v" 2>/dev/null
cat "${TMP}"/iv/*.v >"${TMP}/gen.v" 2>/dev/null
if [ ! -s "${TMP}/ref.v" ] || [ ! -s "${TMP}/gen.v" ]; then
  echo "FAIL: could not emit the two netlists"
  rc=1
elif "${LHD}" lec --set formal.solver=lgyosys --impl "verilog:${TMP}/gen.v" \
       --ref "verilog:${TMP}/ref.v" --top flat_top --workdir "${TMP}/wl2" >"${TMP}/log" 2>&1; then
  echo "ok: netlist regenerated via Pyrope proves against the original"
else
  echo "FAIL: the Pyrope round-trip netlist is NOT equivalent — the front end"
  echo "      (slang read / pyrope emission / pyrope read) has a real bug"
  tail -3 "${TMP}/log"
  rc=1
fi

# --- case 3: THE TRACKED GAP. Trust the child. "Assume equal" must make the
# parent EASIER, so this has to PROVE; today the two sides' box port names do
# not pair (bus vs leaf) and it does not.
run lec --impl "lg:${TMP}/impl.lg" --ref "lg:${TMP}/ref.lg" --top flat_top \
    --workdir "${TMP}/wl3" --set formal.lec.trust=kid
v=$(verdict)
if [ "${v}" = "PROVEN" ]; then
  echo "ok: TRUSTED child with bus-vs-leaf struct ports proves"
else
  echo "FAIL: trusted child gave ${v:-<none>}, expected PROVEN."
  echo "      A trusted box's ports pair BY NAME (encode.hpp Comb_box), and the"
  echo "      two sides spell them 'din'/'dout' vs 'din.fp'/'din.addr'/..., so"
  echo "      nothing pairs and the outputs become unrelated free symbols."
  rc=1
fi

if [ ${rc} -eq 0 ]; then
  echo "PASS: trusted-box struct port pairing"
fi
exit ${rc}
