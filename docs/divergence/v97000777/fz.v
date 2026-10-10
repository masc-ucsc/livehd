module fz(
  input clock,
  input reset,
  input [16:0] i0,
  input signed [47:0] i1,
  input [7:0] i2,
  input [47:0] i3,
  input [6:0] i4,
  output [15:0] o0,
  output [7:0] o1
);
  wire [4:0] t0 = ((({i0[15:13], i1} % (i4[3] | 1'b1)) % (i4 | 1'b1)) != i4);
  reg [32:0] t1;
  always @(posedge clock) begin
    if (reset) t1 <= 53244;
    else if ((2'sd1 ^~ (i3[37:10] && i0))) t1 <= (((i3 - i4) ? i1[i0[4:0]] : $unsigned((t0 / (i2 | 1'b1)))) & (16'd35857 - {1{i4}}));
  end
  wire signed [2:0] t2 = i3;
  reg [4:0] t3;
  always @(posedge clock) if (reset) t3 <= 10; else t3 <= i2[7];
  wire [8:0] t4 = (({t3, i3[42:34]} >>> 7) >>> 9);
  wire t5 = i3[14];
  reg signed [30:0] l0;
  always_latch begin
    if (!clock && reset) l0 = 0;
    else if (!clock && (|(i0 / ((t3 || i2) | 1'b1)))) l0 = (2'h1 % (((i2 ^~ i2) != (i1 - i0)) | 1'b1));
  end
  reg l1;
  always_latch begin
    if (clock && reset) l1 = 0;
    else if (clock && ({i3, i1[47:41], i3})) l1 = ((i2[2] % (i2[7:5] | 1'b1)) ^ i0);
  end
  reg [11:0] l2;
  always_latch begin
    if (!clock && reset) l2 = 0;
    else if (!clock && ((i0[3] || (i0 - 1)))) l2 = (((~&i4) ^~ (t2 - i0)) >>> t3);
  end
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (((6'sd0 - t2) - (t0 - i1)));
  wire gclk0 = clock & gen0;
  reg g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 1; else g0_0 <= (~&((l2 % (i3 | 1'b1)) <= {1{1'd0}}));
  reg gen1;
  always @(*) if (!clock) gen1 = reset || (|3'h2);
  wire gclk1 = clock & gen1;
  reg [6:0] g1_0;
  always @(posedge gclk1) if (reset) g1_0 <= 113; else g1_0 <= $signed(g0_0);
  reg [64:0] g1_1;
  always @(posedge gclk1) if (reset) g1_1 <= 53945; else g1_1 <= (g0_0 - g1_0);
  localparam [11:0] P0 = 3'h7;
  reg [63:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + (P0 ** 3);
  wire [8:0] sub_o;
  fz_sub u_sub(.clock(clock), .reset(reset), .a(t5), .b(t0), .y(sub_o));
  assign o0 = (g1_1 / (t2 | 1'b1));
  assign o1 = (~&(((i3 ? 4'd10 : P0) >> 5) / ((t1 ? i1[29] : t5) | 1'b1)));
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output [8:0] y);
  assign y = a ^ b;
endmodule
