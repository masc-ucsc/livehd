module fz(
  input clock,
  input reset,
  input signed [2:0] i0,
  input signed [16:0] i1,
  input [4:0] i2,
  input signed [4:0] i3,
  output signed [16:0] o0,
  output signed [16:0] o1
);
  wire [32:0] t0 = 1;
  wire [64:0] t1 = i1;
  reg [32:0] l0;
  always_latch begin
    if (!clock && reset) l0 = 0;
    else if (!clock && (({1{t1}} ^ (i2 + i3)))) l0 = (t1 ? (i3[3] > $signed(t0)) : (|i1));
  end
  reg [64:0] l1;
  always_latch begin
    if (!clock && reset) l1 = 0;
    else if (!clock && (|((i3 | t0) + $signed(12'b001110000000)))) l1 = i1[15:14];
  end
  reg signed [15:0] l2;
  always @(*) if (!clock && (reset || (|((t0 ? 4'd11 : 16'd3674) && i1)))) l2 = reset ? 0 : i3[4];
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (((t0 ? t1 : i1) & (16'd11795 == t1)));
  wire gclk0 = clock & gen0;
  reg [32:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 12674; else g0_0 <= i2;
  reg gen1;
  always @(*) if (!clock) gen1 = reset || (((i2 ? i3 : t1) & (16'sd6940 ^~ i3)));
  wire gclk1 = clock & gen1;
  reg [69:0] g1_0;
  always @(posedge gclk1) if (reset) g1_0 <= 158; else g1_0 <= $signed((l1 ^ {1{i0}}));
  reg [4:0] g1_1;
  always @(posedge gclk1) if (reset) g1_1 <= 29; else g1_1 <= ((i2 & (16'sd20949 - 6'sd16)) | (^(l1 >>> 1)));
  localparam [7:0] P0 = 2;
  reg [2:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + (t0[15:8] >> 1);
  wire [0:0] sub_o;
  fz_sub u_sub(.clock(clock), .reset(reset), .a(i1), .b(fb), .y(sub_o));
  assign o0 = ((((t1 - P0) ^~ (1'b0 == 33'b011011111100100101000110111011110)) + 16'sd23417) + $unsigned(({i2[4:4], l1, i3[3:2]} / (l2 | 1'b1))));
  assign o1 = ((g1_1 ? (t0[26] & (l1 + sub_o)) : g1_0[5]) >> g1_1);
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output [0:0] y);
  assign y = a & b;
endmodule
