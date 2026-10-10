module fz(
  input clock,
  input reset,
  input signed [64:0] i0,
  input signed [2:0] i1,
  input [7:0] i2,
  input [32:0] i3,
  output signed [15:0] o0,
  output [4:0] o1
);
  wire signed [2:0] t0 = i3[21];
  wire [64:0] t1 = ((((i2 ? i2 : i0) >= i0) | $signed(t0)) + (i1 * ((i1 * i3) ^~ (t0 ? i1 : i3))));
  wire [2:0] t2 = ((((i3 ? t1 : i3) + (t0 << i1)) ? (~^(~^t0)) : ({1{t1[60:37]}} ? t1 : (~2))) ? (i1[1] > i2[2]) : t0);
  reg [69:0] t3;
  always @(*) begin
    t3 = i2;
    for (int k = 0; k < 4; k = k + 1) t3[k] = t3[k] ^ (((t1 >> t2) / ((t1 + i2) | 1'b1)));
  end
  reg signed [11:0] t4;
  always @(posedge clock) if (reset) t4 <= 3490; else t4 <= t2[1:0];
  wire [15:0] t5 = t2;
  reg [15:0] t6;
  always @(posedge clock) begin
    if (reset) t6 <= 39387;
    else if (i2) t6 <= (i0[63:57] % (({3{t5}} ^ ((i2 & i0) >>> i1)) | 1'b1));
  end
  wire signed [31:0] t7 = (t6 - ((t0 ? (t4 - t3) : t5) ^~ i2));
  assign o0 = t2[2:2];
  assign o1 = ((((3 ? t0 : 8'b11000011) ^~ (t3 - 4)) / (((3'sd0 + t2) <<< t0) | 1'b1)) | (~&t6));
endmodule
