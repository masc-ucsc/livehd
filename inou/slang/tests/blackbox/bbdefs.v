// Reference definitions of the modules bbtop.v instantiates as blackboxes;
// concatenated with bbtop.v they form the LEC golden.
module array_0_ext(
  input  [7:0]  RW0_addr,
  input         RW0_en,
  input         RW0_clk,
  input         RW0_wmode,
  input  [31:0] RW0_wdata,
  output [31:0] RW0_rdata
);
  assign RW0_rdata = (RW0_en ? RW0_wdata : 32'h0) ^ {24'h0, RW0_addr} ^ {31'h0, RW0_wmode};
endmodule

module bb2(
  input  [7:0] in,
  output [7:0] out1,
  output [7:0] out2
);
  assign out1 = in + 8'h3;
  assign out2 = ~in;
endmodule
