module fz(
  input clock,
  input reset,
  input [15:0] i0,
  input [15:0] i1,
  input [15:0] i2,
  output [15:0] o0,
  output [7:0] o1
);
  wire signed [6:0] t0 = (((i0 + i1) ? (i1 >= i2) : i0) & 6'd63);
  reg [2:0] l0;
  always @(*) if (clock && (reset || (|(2'd2 ? i2 : i2)))) l0 = reset ? 0 : ((t0 + i0) > (1'd1 ? t0 : i2));
  reg [8:0] l1;
  always @(*) if (!clock && (reset || (|(t0 ^~ i2)))) l1 = reset ? 0 : ((l0 ? 33'd8085635084 : 1'b0) ? t0[1:0] : (12'b101110010101 ? t0 : i2));
  reg [2:0] l2;
  always @(*) if (!clock && (reset || (|(t0 << l0)))) l2 = reset ? 0 : i1[15:10];
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (|(-i0));
  wire gclk0 = clock & gen0;
  reg [6:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 125; else g0_0 <= i2[i2[2:0] +: 2];
  reg [2:0] g0_1;
  always @(posedge gclk0) if (reset) g0_1 <= 6; else g0_1 <= g0_0;
  reg gen1;
  always @(*) if (!clock) gen1 = reset || (|(i0 ? i2 : i2));
  wire gclk1 = clock & gen1;
  reg signed g1_0;
  always @(posedge gclk1) if (reset) g1_0 <= 1; else g1_0 <= ((i1 / (g0_1 | 1'b1)) + i2);
  wire [2:0] sub_o;
  fz_sub u_sub(.clock(clock), .reset(reset), .a(7'd117), .b(i0), .y(sub_o));
  reg [16:0] m1;
  reg signed m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if ((-g1_0)) begin m1 <= l2; m2 <= m1; end
      else if (7) m2 <= ((7 | i2) + (l2 - i2));
      else m1 <= m2;
    end
  end
  assign o0 = l2[2:0];
  assign o1 = (l2 >> 0);
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output reg [2:0] y);
  always @(posedge clock) if (reset) y <= 0; else y <= a - b;
endmodule
