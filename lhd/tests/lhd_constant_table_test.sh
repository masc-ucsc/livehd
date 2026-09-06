#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# A procedurally constructed constant table must disappear in one compile.
set -euo pipefail
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_constant_table_$$}"
mkdir -p "$W"
cat > "$W/table.v" <<'RTL'
module table_columns(input [3:0] data, output reg [3:0] y);
  integer pcnt[0:15];
  reg [3:0] col[0:3];
  integer i, b, cnt, k;
  always @* begin
    for (i=0; i<16; i=i+1) begin
      pcnt[i]=0;
      for (b=0; b<4; b=b+1)
        pcnt[i]=pcnt[i]+((i >> b) & 1);
    end
    cnt=0;
    for (i=0; i<16; i=i+1)
      if (pcnt[i]==3 && cnt<4) begin
        col[cnt]=i;
        cnt=cnt+1;
      end
    y=0;
    for (k=0; k<4; k=k+1)
      if (data[k]) y=y^col[k];
  end
endmodule
RTL
cat > "$W/gold.v" <<'RTL'
module table_columns(input [3:0] data, output [3:0] y);
  assign y = (data[0] ? 4'd7 : 4'd0) ^ (data[1] ? 4'd11 : 4'd0)
           ^ (data[2] ? 4'd13 : 4'd0) ^ (data[3] ? 4'd14 : 4'd0);
endmodule
RTL
"$LHD" compile "$W/table.v" --top table_columns --emit-dir lg:"$W/lg" --workdir "$W/compile" -q
"$LHD" tool cat lg:"$W/lg" > "$W/graph.jsonl"
python3 - "$W/graph.jsonl" <<'PYCOUNT'
import json, sys
nodes = [row for line in open(sys.argv[1]) if (row := json.loads(line)).get("t") == "node"]
assert len(nodes) < 32, f"constant table still builds hardware: {len(nodes)} nodes"
assert not any(row["kind"] == "shl" for row in nodes), nodes
PYCOUNT
for engine in cvc5 lgyosys; do
  "$LHD" lec --impl lg:"$W/lg" --ref verilog:"$W/gold.v" --top table_columns \
    --set formal.solver="$engine" --set formal.timeout=60 --workdir "$W/$engine" \
    --result-json "$W/$engine.json" -q
  grep -q '"verdict":"proven"' "$W/$engine.json"
done
