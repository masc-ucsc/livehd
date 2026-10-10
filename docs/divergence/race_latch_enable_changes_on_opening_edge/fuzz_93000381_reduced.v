module fz(
  input clock,
  input reset,
  input [4:0] i0,
  input [2:0] i1,
  output [11:0] o1
);
  wire [30:0] t0 = (((i0 == i1) ? i0 : i1) ^ i1);
  reg signed [3:0] t1;
  always @(posedge clock) begin
    if (reset) t1 <= 9;
    else if ({2'd2, i0}) t1 <= i0;
  end
  always @(posedge clock) begin
  end
  reg signed [2:0] l0;
  always @(*) if (clock && (reset || (t1))) l0 = reset ? 0 : i1[2];
  reg [1:0] l1;
  always_latch begin
    if (clock && reset) l1 = 0;
  end
  reg signed [16:0] l2;
  always @(*) if (clock && (reset || ((4'h4 < t0)))) l2 = reset ? 0 : i0[3:2];
  assign o1 = (($unsigned(i1) ? 3'b001 : (l0 ^~ l2)) << 4);
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output [7:0] y);
endmodule

