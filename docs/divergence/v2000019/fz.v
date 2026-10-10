module fz(
  input clock,
  input reset,
  input signed [4:0] i0,
  input signed [30:0] i1,
  input i2,
  output [6:0] o0,
  output signed [7:0] o1,
  output [4:0] o2
);
  wire [30:0] t0 = i1[24:2];
  wire [2:0] t1 = (((16'd51751 || i1) < 4'sd2) + (i0 + (i1 >> 6)));
  reg [8:0] t2;
  always @(*) begin
    t2 = 6'd20;
    if (((t1 + i2) >> 9)) t2 = t1;
  end
  wire signed [30:0] t3 = (t1 | (^(i1 & t2)));
  wire signed [1:0] t4 = $unsigned({i0, t1[1:0], t1[1:1]});
  assign o0 = i0;
  assign o1 = ((t3[28:28] ^~ (t4 + 3)) * i1[26:18]);
  assign o2 = $signed((i1 | t2));
endmodule
