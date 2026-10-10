module fz(
  input clock,
  input reset,
  input [1:0] i0,
  input [2:0] i1,
  output signed [4:0] o0,
  output signed [4:0] o1
);
  reg signed [31:0] t0;
  always @(*) begin
    t0 = 0;
    if (((i1 * 2'd1) << 6)) t0 = ((i1 * i1) && (i0 | i1));
  end
  wire signed [2:0] t1 = (i0 < i0[1:0]);
  reg [7:0] l0;
  always @(*) if (!clock && (reset || (|(33'b111011111111010101111111010011110 & i0)))) l0 = reset ? 0 : (i1 - (t1 << 4));
  reg [11:0] l1;
  always @(*) if (clock && (reset || (|l0[6:5]))) l1 = reset ? 0 : ((l0 >>> 1) ^ t1);
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (i1);
  wire gclk0 = clock & gen0;
  reg [8:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 169; else g0_0 <= ((7 | t1) | t0);
  reg signed [1:0] g0_1;
  always @(posedge gclk0) if (reset) g0_1 <= 0; else g0_1 <= ((i0 + l0) ? (i1 * l0) : (i1 ? t0 : i1));
  reg [15:0] m1;
  reg signed [2:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if (((g0_0 * i0) << 8)) begin m1 <= l0[5:2]; m2 <= m1; end
      else if (((l0 & g0_1) != (12'h7b6 & i1))) m2 <= i0[0];
      else m1 <= m2;
    end
  end
  assign o0 = ({l1, 1'd1, i0[0:0]} + (i1 + (m2 ? t1 : t1)));
  assign o1 = m2;
endmodule
