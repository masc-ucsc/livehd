module fz(
  input clock,
  input reset,
  input [3:0] i0,
  input [8:0] i1,
  input [30:0] i2,
  output [3:0] o0,
  output [8:0] o1,
  output [15:0] o2
);
  wire [8:0] t0 = i2;
  reg [3:0] t1;
  always @(*) begin
    t1 = ((i0 - i1) + t0);
    for (int k = 0; k < 4; k = k + 1) t1[k] = t1[k] ^ (i0[3:3]);
  end
  reg signed [2:0] l0;
  always @(*) if (!clock && (reset || ((t1 <<< 2)))) l0 = reset ? 0 : $unsigned((1'h1 ? t1 : t0));
  reg [2:0] l1;
  always_latch begin
    if (!clock && reset) l1 = 0;
    else if (!clock && ({i0, i1, i1})) l1 = 8;
  end
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (3'd1);
  wire gclk0 = clock & gen0;
  reg [8:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 397; else g0_0 <= t0;
  reg [7:0] g0_1;
  always @(posedge gclk0) if (reset) g0_1 <= 249; else g0_1 <= ((9 * i1) & (i2 ? i2 : g0_0));
  reg gen1;
  always @(*) if (!clock) gen1 = reset || (t1);
  wire gclk1 = clock & gen1;
  reg [8:0] g1_0;
  always @(posedge gclk1) if (reset) g1_0 <= 191; else g1_0 <= (2 << 2);
  reg [7:0] m1;
  reg [6:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if (l1) begin m1 <= ((&i1) | (^g0_1)); m2 <= m1; end
      else if (i0) m2 <= ((12'hc5c & l1) <<< i0);
      else m1 <= m2;
    end
  end
  assign o0 = m1[7 - i2[1:0] -: 1];
  assign o1 = (g0_1[6:2] & ((12'd3110 ? 9 : 1) % ((l0 <<< 8) | 1'b1)));
  assign o2 = (m2 ? {g0_0, 3'd2, i2[22:16]} : i2);
endmodule
