#!/bin/bash
# An implicit exhaustive-case default must write struct members, not a scalar root.
set -eu
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/packed_struct_fullcase_$$}"
mkdir -p "$W"
cat > "$W/packed_struct_fullcase.sv" <<'SV'
module packed_struct_fullcase(input logic clk, reset, ack,
                             input logic [1:0] state, input logic [7:0] data,
                             output wire [9:0] out, qout);
  typedef struct packed {logic ack; logic v; logic [7:0] data;} link_t;
  link_t link, qlink;
  assign out = link;
  assign qout = qlink;
  // These fields belong to separate processes and must survive the case.
  always_comb link.ack = ack;
  always_ff @(posedge clk) qlink.ack <= reset ? 0 : ack;
  always_comb begin
    link.v = 0;
    link.data = 0;
    case (state)
      0: link.v = 0;
      1: link.data = data;
      2: begin link.v = 1; link.data = 8'ha5; end
      3: begin link.v = ack; link.data = data; end
    endcase
  end
  always_ff @(posedge clk) begin
    qlink.v <= 0;
    qlink.data <= 0;
    if (!reset) begin
      case (state)
        0: qlink.v <= 0;
        1: qlink.data <= data;
        2: begin qlink.v <= 1; qlink.data <= 8'ha5; end
        3: begin qlink.v <= ack; qlink.data <= data; end
      endcase
    end
  end
endmodule
SV
cat > "$W/reference.sv" <<'SV'
module packed_struct_fullcase(input clk, reset, ack,
                             input [1:0] state, input [7:0] data,
                             output [9:0] out, output reg [9:0] qout);
  wire valid = state == 2 || (state == 3 && ack);
  wire [7:0] payload = state == 0 ? 8'h00 : state == 2 ? 8'ha5 : data;
  assign out = {ack, valid, payload};
  always @(posedge clk) qout <= reset ? 10'b0 : out;
endmodule
SV
"$LHD" compile "$W/packed_struct_fullcase.sv" --top packed_struct_fullcase \
  --emit-dir "lg:$W/lg" --workdir "$W/compile"
"$LHD" lec --impl "lg:$W/lg" --ref "verilog:$W/reference.sv" --top packed_struct_fullcase \
  --set formal.effort=proof --workdir "$W/proof" --result-json "$W/proof.json"
grep -q '"verdict":"proven-full"' "$W/proof.json"
echo 'PASS: exhaustive packed-struct cases preserve independent fields and clocked writes'
