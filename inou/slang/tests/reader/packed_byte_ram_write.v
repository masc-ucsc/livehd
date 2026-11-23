// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Shared byte expressions feed both partial memory writes and bypass muxes.
// Two addresses and two byte lanes exercise independent reads and masked writes.
module packed_byte_ram_write(input clk, input [15:0] a, input [1:0] s, input wa, ra,
                       input we, output [15:0] q);
  wire [1:0][7:0] aa=a;
  reg [1:0][7:0] mem[2];
  always @(posedge clk) for (int j=0;j<2;j++) if (we && s[j]) mem[wa][j] <= aa[j];
  wire [1:0][7:0] b=mem[ra];
  for (genvar i=0; i<2; i++) begin : lane
    assign q[8*i+:8] = s[i] ? aa[i] : b[i];
  end
endmodule
