module fz(
  input clock,
  input reset,
  input [2:0] i0,
  input signed [69:0] i1,
  input [69:0] i2,
  input signed [47:0] i3,
  input i4,
  input signed [2:0] i5,
  output [63:0] o0,
  output signed o1
);
  wire [1:0] t0 = ((i2[22:14] + ((i4 % (i2 | 1'b1)) - {3{i1}})) ^~ $signed(i2));
  reg [11:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if ((((i4 * i3) / ({2{t0[0:0]}} | 1'b1)) == ($signed(i0) ? (i1 - i5) : i3))) mem[i1[1:0]] <= (((i4 * 5) && (1'h0 + t0)) / ((~|(i5 & i0)) | 1'b1));
  end
  wire [11:0] mrd = mem[i1[1:0]];
  reg signed [30:0] m1;
  reg [6:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if ((((t0 + 16'd18135) & (-i2)) ^ ((1'd1 ^ i3) & (8'sd32 ? mrd : t0)))) begin m1 <= (i4 | i3); m2 <= m1; end
      else if (i1) m2 <= i3;
      else m1 <= m2;
    end
  end
  assign o0 = (m2 & {2{i3[23:23]}});
  assign o1 = (i2 | (((6 >>> 5) - 7) / (mrd | 1'b1)));
endmodule
