module fz(
  input clock,
  input reset,
  input signed i0,
  input signed [30:0] i1,
  input [6:0] i2,
  input [8:0] i3,
  output [63:0] o0
);
  reg [64:0] t0;
  always @(*) begin
    t0 = (~4'd3);
    if (1'd1) t0 = {2{8'd94}};
  end
  assign o0 = (({t0, i1, i3[7:6]} && ((i0 | i2) ? {i1, t0, t0} : (i0 | 2'sd1))) ^ t0);
endmodule
