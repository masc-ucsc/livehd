// Top for the unknown-module (blackbox) reader test: array_0_ext and bb2 have
// NO definition in this file. Under a user --ignore-unknown-modules they must
// lower as blackbox sub-instances (port directions inferred: an lvalue-shaped
// connection to an otherwise-undriven net is an output) and the pyrope
// emission must import them; without the flag the compile must fail cleanly.
module bbtop(
  input         clock,
  input  [7:0]  addr,
  input         wen,
  input  [31:0] wdata,
  output [31:0] rdata_out,
  output [7:0]  y
);
  wire [31:0] rdata;
  array_0_ext array_0_ext (      // single-output blackbox (SRAM-macro shape)
    .RW0_addr(addr),
    .RW0_en(1'h1),
    .RW0_clk(clock),
    .RW0_wmode(wen),
    .RW0_wdata(wdata),
    .RW0_rdata(rdata)
  );
  assign rdata_out = rdata + 32'h1;

  wire [7:0] m;
  wire [7:0] xw;
  bb2 u1 (                       // multi-output blackbox
    .in(addr),
    .out1(m),
    .out2(xw)
  );
  assign y = m ^ xw;
endmodule
