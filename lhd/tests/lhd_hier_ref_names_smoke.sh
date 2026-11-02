#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Nested paths, literal underscores, and real ports must keep distinct values
# when the slang reader exports signals used by hierarchical references.
set -eu
LHD="${LHD:-lhd/lhd}"
case "$LHD" in /*) ;; *) LHD="$PWD/$LHD" ;; esac
W="${TEST_TMPDIR:-/tmp/lhd_hier_ref_names_$$}"
mkdir -p "$W"

cat >"$W/impl.sv" <<'EOF'
module leaf(input logic a, output logic q);
  assign q = a;
endmodule
module mid(input logic a, input logic b, input logic c,
           output logic xmr__a_5finst_2eq,
           output logic xmr__a_5finst_2eq_);
  leaf a_inst(.a(a), .q());
  wire a_inst_q = b;
  assign xmr__a_5finst_2eq = c;
  assign xmr__a_5finst_2eq_ = !c;
endmodule
module top(input logic a, input logic b, input logic c,
           output logic y0, output logic y1, output logic y2, output logic y3);
  mid m(.a(a), .b(b), .c(c),
        .xmr__a_5finst_2eq(y2), .xmr__a_5finst_2eq_(y3));
  assign y0 = m.a_inst.q;
  assign y1 = m.a_inst_q;
endmodule
EOF
cat >"$W/ref.sv" <<'EOF'
module top(input logic a, input logic b, input logic c,
           output logic y0, output logic y1, output logic y2, output logic y3);
  assign y0 = a;
  assign y1 = b;
  assign y2 = c;
  assign y3 = !c;
endmodule
EOF

"$LHD" lec --set formal.effort=proof --set formal.simfail_run=false \
  --impl "$W/impl.sv" --ref "$W/ref.sv" --top top --workdir "$W/wd" \
  --result-json "$W/result.json" >"$W/lec.log" 2>&1 || {
    cat "$W/lec.log"
    exit 1
  }
python3 - "$W/result.json" <<'PY'
import json
import sys
with open(sys.argv[1]) as stream:
    result = json.load(stream)
assert result["lec"]["verdict"] == "proven-full", result
PY
echo "PASS: hierarchical exports preserve distinct paths and real ports"
