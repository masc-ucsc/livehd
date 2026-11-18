// Constant-0 bits that cross instance boundaries (pass/synth propagate_constant_bits), with synth.flat_sweep off.
// `stage` registers a 32-bit flit, but its instances feed it a flit whose upper 30 bits are 0, so after the state sweep
// cuts those flop bits its output port is PART constant (30 of 32 bits read 0). `thru` passes the flit on unchanged,
// and `wmux` selects one of two flits. Without the propagation `wmux` sees two unknown 32-bit inputs and maps a
// 32-bit mux; with it, both instances' inputs read 0 above bit 2 and the mux keeps two bits.
module stage(input clk, input rst, input [31:0] d_i, output reg [31:0] d_o);
  always @(posedge clk) begin
    if (rst) d_o <= 32'd0;
    else d_o <= d_i;
  end
endmodule

module thru(input [31:0] i, output [31:0] o);
  assign o = i;
endmodule

module wmux(input s, input [31:0] a, input [31:0] b, output [31:0] o);
  assign o = s ? a : b;
endmodule

module synth_const_bits(input clk, input rst, input s, input [1:0] x, input [1:0] y, output [31:0] o);
  wire [31:0] q0, q1, p0, p1;
  stage s0(.clk(clk), .rst(rst), .d_i({30'd0, x}), .d_o(q0));
  stage s1(.clk(clk), .rst(rst), .d_i({30'd0, y}), .d_o(q1));
  thru  t0(.i(q0), .o(p0));
  thru  t1(.i(q1), .o(p1));
  wmux  m(.s(s), .a(p0), .b(p1), .o(o));
endmodule
