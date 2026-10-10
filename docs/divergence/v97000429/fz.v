module fz(
  input clock,
  input reset,
  input signed [15:0] i0,
  input signed [8:0] i1,
  output signed o0,
  output signed [3:0] o1
);
  wire signed [4:0] t0 = i1;
  reg signed [11:0] t1;
  always @(*) begin
    t1 = $signed(t0[4:3]);
    if (((~^i1) != (t0 ^~ i0))) t1 = ((i0 > i1) - (4'b1011 ? t0 : t0));
  end
  wire [3:0] t2 = (i0 < ((!t1) ^ i0));
  reg [30:0] t3;
  always @(*) begin
    t3 = 4;
    for (int k = 0; k < 4; k = k + 1) t3[k] = t3[k] ^ ((t2 >>> 9));
  end
  reg [2:0] l0;
  always_latch begin
    if (clock && reset) l0 = 0;
    else if (clock && ((i1 ^~ i0))) l0 = ((i1 & i1) - 12'b011100011101);
  end
  reg [15:0] l1;
  always_latch begin
    if (clock && reset) l1 = 0;
    else if (clock && (|i0)) l1 = {3'd1, t0[0:0], 1'd0};
  end
  reg [31:0] l2;
  always @(*) if (!clock && (reset || ((t3 ? l0 : l0)))) l2 = reset ? 0 : t1[1:0];
  reg fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + t2;
  reg [7:0] m1;
  reg [3:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if (((fb >>> 9) + {t2, l2[31:4], t0[2:1]})) begin m1 <= $unsigned((i0 ^~ l2)); m2 <= m1; end
      else if (i0) m2 <= (t2[2] | $signed(t1));
      else m1 <= m2;
    end
  end
  assign o0 = i1;
  assign o1 = (((l0 - l0) && (!fb)) | $signed((i1 ? t3 : t3)));
endmodule
