// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Shared byte expressions feed both partial memory writes and bypass muxes.
// Four words suffice to exercise dynamic addressing and independent byte writes.
module packed_byte_ram_write(input clk, input [31:0] a, input [3:0] s, input [1:0] wa, ra,
                       input we, output [31:0] q);
  wire [3:0][7:0] aa=a;
  reg [3:0][7:0] mem[4];
  always @(posedge clk) for (int j=0;j<4;j++) if (we && s[j]) mem[wa][j] <= aa[j];
  wire [3:0][7:0] b=mem[ra];
  for (genvar i=0; i<4; i++) begin : lane
    assign q[8*i+:8] = s[i] ? aa[i] : b[i];
  end
endmodule
