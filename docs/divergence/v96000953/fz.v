module fz(
  input clock,
  input reset,
  input [11:0] i0,
  input [2:0] i1,
  output [16:0] o0,
  output [1:0] o1
);
  wire [6:0] t0 = $unsigned((8'sd50 ? (i0 / (i0 | 1'b1)) : (i1 ? 12'd1529 : i0)));
  wire [11:0] t1 = (8'b01000101 + 8'd203);
  wire signed [15:0] t2 = {i0[9:7], t0[4:3], 7'd0};
  reg signed [7:0] t3;
  always @(posedge clock) begin
    if (reset) t3 <= 210;
    else if ((i0[9] >>> 2)) t3 <= i1;
  end
  reg [7:0] t4;
  always @(*) begin
    t4 = (-{t0, t1[11:11], t1[8:3]});
    if (((t2 + i0) + t3)) t4 = t0;
  end
  reg [16:0] l0;
  always_latch begin
    if (!clock && reset) l0 = 0;
    else if (!clock && (|(t0 <<< 2))) l0 = ((2'd2 ? 6'b100011 : i1) ^ {1{t2}});
  end
  reg signed [8:0] l1;
  always_latch begin
    if (clock && reset) l1 = 0;
    else if (clock && (|l0)) l1 = ((t3 ? 4'd5 : t2) ? $unsigned(t3) : (i1 & t2));
  end
  reg gen0;
  always @(*) if (!clock) gen0 = reset || ((t3 <<< 4));
  wire gclk0 = clock & gen0;
  reg [2:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 7; else g0_0 <= ((t2 ? t0 : i1) ^~ l0[t3[3:0]]);
  reg gen1;
  always @(*) if (!clock) gen1 = reset || ((i1 ^~ i0));
  wire gclk1 = clock & gen1;
  reg [31:0] g1_0;
  always @(posedge gclk1) if (reset) g1_0 <= 56039; else g1_0 <= (t4 == (8'hbd >> 7));
  reg signed [15:0] g1_1;
  always @(posedge gclk1) if (reset) g1_1 <= 63920; else g1_1 <= (i1 >> 7);
  assign o0 = $signed(l0);
  assign o1 = ((i1[2:2] ? (i1 ? t4 : i0) : (l1 + g1_0)) - i0);
endmodule
