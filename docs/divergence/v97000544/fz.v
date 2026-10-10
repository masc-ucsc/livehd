module fz(
  input clock,
  input reset,
  input [4:0] i0,
  input signed [47:0] i1,
  input [64:0] i2,
  input [2:0] i3,
  input signed [30:0] i4,
  output [2:0] o0,
  output [1:0] o1,
  output [4:0] o2
);
  function automatic signed [6:0] f0(input signed [32:0] a0, input signed [6:0] a1);
    f0 = (~^((6'b111000 >= a0) * a1));
  endfunction
  function automatic signed [15:0] f1(input [3:0] a0);
    f1 = ((^5) % (((3 || a0) * (2'd2 - a0)) | 1'b1));
  endfunction
  reg signed [6:0] t0;
  always @(*) begin
    t0 = (i2 & i4[28:25]);
    if ({3{4'd3}}) t0 = (i3[1:1] ^ 3);
  end
  wire [2:0] t1 = ((((12'sd1314 * 6'd36) | (33'b100000011000110100010110011110110 + 7)) >>> i3) & f0({1{i3}}, i2));
  wire [2:0] t2 = ((|i0) + $unsigned(t0));
  wire signed t3 = (~^(((i1 / (i2 | 1'b1)) - (i0 + t1)) / ((i3 >= {1'd1, i4, i4}) | 1'b1)));
  wire [6:0] t4 = (($unsigned((i4 | 8)) ? ((t1 * i0) + t2) : 5) > i3);
  reg signed [16:0] t5;
  always @(*) begin
    t5 = (~&((2 | i1) >>> i3));
    if (6'h30) t5 = t3;
  end
  wire [4:0] t6 = (t3 & t5[t4[3:0] +: 2]);
  reg [47:0] l0;
  always @(*) if (!clock && (reset || (i2))) l0 = reset ? 0 : i0;
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (i4);
  wire gclk0 = clock & gen0;
  reg [7:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 47; else g0_0 <= ((t0 < t0[4]) ^~ t5[12:4]);
  reg gen1;
  always @(*) if (!clock) gen1 = reset || (((t0 ^ t6) >> t1));
  wire gclk1 = clock & gen1;
  reg [69:0] g1_0;
  always @(posedge gclk1) if (reset) g1_0 <= 8870; else g1_0 <= t2;
  localparam [4:0] P0 = 4;
  assign o0 = (t4 / (((t2[1] + (^t4)) | l0) | 1'b1));
  assign o1 = ((((t2 > t1) < (6 ^~ l0)) ? ((t3 | t1) ? (^t0) : (^i1)) : ((g1_0 | i1) % (1'h0 | 1'b1))) - ((t0[2] & (i2 != 6'b001001)) ^ ((i0 % (2'h0 | 1'b1)) - (i4 ^ i1))));
  assign o2 = t3;
endmodule
