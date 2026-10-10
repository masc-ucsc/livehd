module fz(
  input clock,
  input reset,
  input [32:0] i0,
  input [30:0] i1,
  input signed [30:0] i2,
  input [1:0] i3,
  input signed [16:0] i4,
  output signed [7:0] o0
);
  function automatic signed [63:0] f0(input [6:0] a0);
    f0 = (~&1'd0);
  endfunction
  wire [2:0] t0 = (^(i1[5] >> i3));
  reg [3:0] t1;
  always @(*) begin
    t1 = i1;
    case (i1)
      2: t1 = (i4[16:15] + ((t0 || i0) ? {t0, i3} : i2[5:5]));
      default: ;
    endcase
  end
  reg [69:0] t2;
  always @(*) begin
    t2 = i1;
    if ((i2 ^ (i0 - t0))) t2 = 7;
  end
  wire [32:0] t3 = {i3, 3'd6};
  reg signed [30:0] l0;
  always_latch begin
    if (clock && reset) l0 = 0;
    else if (clock && (((12'h10b ? i1 : i0) - (i1 & i0)))) l0 = {i3[1:1], i3, t1};
  end
  reg [63:0] l1;
  always_latch begin
    if (!clock && reset) l1 = 0;
    else if (!clock && (((i3 >> i3) - (i0 % (t1 | 1'b1))))) l1 = i0;
  end
  reg [1:0] l2;
  always_latch begin
    if (clock && reset) l2 = 0;
    else if (clock && (($unsigned(1'd0) >= (i2 * i0)))) l2 = t2;
  end
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (i3);
  wire gclk0 = clock & gen0;
  reg [16:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 1758; else g0_0 <= i1;
  reg gen1;
  always @(*) if (!clock) gen1 = reset || (((t3 * 16'sd24116) ^ (i2 - i0)));
  wire gclk1 = clock & gen1;
  reg [3:0] g1_0;
  always @(posedge gclk1) if (reset) g1_0 <= 8; else g1_0 <= i4;
  wire [6:0] sub_o;
  fz_sub u_sub(.clock(clock), .reset(reset), .a(i1), .b(i1), .y(sub_o));
  assign o0 = (t2 ^~ $unsigned(((5 - t0) / (l1 | 1'b1))));
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output reg [6:0] y);
  always @(posedge clock) if (reset) y <= 0; else y <= a ^ b;
endmodule
