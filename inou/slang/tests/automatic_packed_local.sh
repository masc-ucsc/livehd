#!/bin/bash
# Automatic packed arrays in an edge process are scratch data, not state.
set -eu
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/automatic_packed_local_$$}"
mkdir -p "$W"
cat > "$W/automatic_packed_local.sv" <<'SV'
module automatic_packed_local(input clock,reset,en,input [5:0] addr,input [7:0] data,
                             input [33:0][7:0] table_in,output logic [7:0] y);
  always @(posedge clock or posedge reset) begin
    if (reset) y <= 0;
    else begin
      automatic logic [63:0][7:0] expanded;
      expanded = {{30{table_in[0]}}, table_in};
      y <= en ? data : expanded[addr];
    end
  end
endmodule
SV
# Flatten the golden bus and spell the replicated lanes independently.
cat > "$W/reference.sv" <<'SV'
module automatic_packed_local(input clock,reset,en,input [5:0] addr,input [7:0] data,
                             input [271:0] table_in,output logic [7:0] y);
  wire [7:0] lane = addr < 34 ? table_in >> {addr,3'b0} : table_in[7:0];
  always @(posedge clock or posedge reset)
    if (reset) y <= 0;
    else y <= en ? data : lane;
endmodule
SV
"$LHD" compile "$W/automatic_packed_local.sv" --top automatic_packed_local \
  --emit-dir "lg:$W/lg" --workdir "$W/compile"
"$LHD" lec --impl "lg:$W/lg" --ref "verilog:$W/reference.sv" --top automatic_packed_local \
  --workdir "$W/proof" --result-json "$W/proof.json"
grep -q '"verdict":"proven".*"bounded":false' "$W/proof.json"
echo 'PASS: automatic packed scratch arrays preserve clocked behavior'
