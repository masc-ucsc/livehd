// A register chain across three instances of one module. The first instance is fed only by constants, so its
// registers can only ever hold their reset value; the constant must cross the register into the second instance,
// and from there into the third, with the hierarchy kept (no compile.flatten).
module sweep_leaf (
    input  clk,
    input  rst,
    input  v_i,
    input  [7:0] d_i,
    output v_o,
    output [7:0] d_o
);
  reg       v_q;
  reg [7:0] d_q;
  always @(posedge clk or posedge rst) begin
    if (rst) begin
      v_q <= 1'b0;
      d_q <= 8'h00;
    end else begin
      v_q <= v_i;
      if (v_i) d_q <= d_i;
    end
  end
  assign v_o = v_q;
  assign d_o = d_q;
endmodule

module sweep_hier_const (
    input  clk,
    input  rst,
    output [7:0] y,
    output vv
);
  wire       v1, v2;
  wire [7:0] d1, d2;
  sweep_leaf a (.clk(clk), .rst(rst), .v_i(1'b0), .d_i(8'h00), .v_o(v1), .d_o(d1));
  sweep_leaf b (.clk(clk), .rst(rst), .v_i(v1),   .d_i(d1),    .v_o(v2), .d_o(d2));
  sweep_leaf c (.clk(clk), .rst(rst), .v_i(v2),   .d_i(d2),    .v_o(vv), .d_o(y));
endmodule
