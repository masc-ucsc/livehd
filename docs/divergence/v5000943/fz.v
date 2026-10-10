module fz(
  input clock,
  input reset,
  input [69:0] i0,
  input [4:0] i1,
  input [15:0] i2,
  input [63:0] i3,
  output [11:0] o0,
  output [3:0] o1,
  output [63:0] o2
);
  reg signed t0;
  always @(*) begin
    t0 = $signed((i1 != i2));
    if ((i1[4:4] > {i1[4:4], i0[27:15]})) t0 = ((i2 - i2) + ({i2[12:11], i0[38:15], 2'd1} >>> 4));
  end
  wire [3:0] t1 = (i0 << t0);
  wire [1:0] t2 = i1;
  wire [2:0] t3 = (i1 || ((~^(t1 ? t2 : i1)) != t1));
  reg signed [6:0] t4;
  always @(posedge clock) begin
    if (reset) t4 <= 21;
    else if ((((t1 >> i1) ? (|t1) : t1[2:2]) < i3[56])) t4 <= t1;
  end
  localparam P0 = 8;
  reg [6:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if (i3) mem[{1'b0, t0}] <= i3;
  end
  wire [6:0] mrd = mem[{1'b0, P0}];
  assign o0 = $unsigned(t0);
  assign o1 = ((P0 * ({P0, i1} ? t1[1:1] : t3)) + (1'sd0 ? t2 : mrd));
  assign o2 = 2;
endmodule
