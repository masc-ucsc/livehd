module fz(
  input clock,
  input reset,
  input [11:0] i0,
  input [11:0] i1,
  input signed [15:0] i2,
  input [8:0] i3,
  output [63:0] o0,
  output [47:0] o1,
  output [3:0] o2
);
  reg signed [30:0] t0;
  always @(posedge clock) begin
    if (reset) t0 <= 30627;
    else if (((8 + (3'h2 / (i3 | 1'b1))) | {i1, i3, 1'd0})) t0 <= (i2[0] | i1);
  end
  wire [7:0] t1 = i2[13];
  reg [47:0] t2;
  always @(*) begin
    t2 = t0;
    if (1'd0) t2 = (t1 ^ {1{i1}});
  end
  reg [11:0] t3;
  always @(*) begin
    t3 = t2;
    if (i0[10:2]) t3 = t1;
  end
  reg [11:0] t4;
  always @(*) begin
    t4 = 3'b010;
    if (33'sd3323521527) t4 = 8'd225;
  end
  reg [16:0] t5;
  always @(*) begin
    t5 = i1[4:2];
    if ({2'd3, t4[9:6], t2}) t5 = (-t0);
  end
  wire [6:0] sub_o;
  fz_sub u_sub(.clock(clock), .reset(reset), .a(t4), .b(t1), .y(sub_o));
  assign o0 = t4;
  assign o1 = ((t2 ? t3 : t0) | $unsigned((sub_o % (i3 | 1'b1))));
  assign o2 = i1;
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output [6:0] y);
  assign y = a & b;
endmodule
