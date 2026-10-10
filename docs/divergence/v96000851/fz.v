module fz(
  input clock,
  input reset,
  input [30:0] i0,
  input signed [2:0] i1,
  output signed [31:0] o0,
  output signed [7:0] o1
);
  function automatic [3:0] f0(input [16:0] a0);
    f0 = (~(2'h1 <<< 2));
  endfunction
  wire [2:0] t0 = i1;
  wire signed [15:0] t1 = t0;
  wire [1:0] t2 = (((i0 >> t0) & (i1 - 8)) >> 9);
  wire signed [4:0] t3 = (((33'd8173928063 ** 1) ? (i1 >= i1) : i1) - {t1, i1});
  wire signed [11:0] t4 = ({t0[2:1], t0, t3} & ((i1 == t1) == (&i0)));
  reg signed [16:0] l0;
  always_latch begin
    if (!clock && reset) l0 = 0;
    else if (!clock && (t2)) l0 = (t0 + (t4 * t3));
  end
  reg signed [1:0] l1;
  always_latch begin
    if (!clock && reset) l1 = 0;
    else if (!clock && (|t0)) l1 = (i0[10:5] + (t4 > t3));
  end
  reg signed [6:0] l2;
  always_latch begin
    if (clock && reset) l2 = 0;
    else if (clock && (l0[16 - i0[3:0] -: 2])) l2 = (t2 == (l1 & i0));
  end
  reg gen0;
  always @(*) if (!clock) gen0 = reset || ((i0 + t0));
  wire gclk0 = clock & gen0;
  reg [31:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 16506; else g0_0 <= t2[1:1];
  reg signed [2:0] g0_1;
  always @(posedge gclk0) if (reset) g0_1 <= 6; else g0_1 <= t0;
  reg gen1;
  always @(*) if (!clock) gen1 = reset || (|(&7));
  wire gclk1 = clock & gen1;
  reg [8:0] g1_0;
  always @(posedge gclk1) if (reset) g1_0 <= 335; else g1_0 <= t1;
  reg [31:0] g1_1;
  always @(posedge gclk1) if (reset) g1_1 <= 52897; else g1_1 <= ((g0_0 | l1) ? t1 : (t0 <<< 4));
  localparam [6:0] P0 = 6'b010011;
  reg [11:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + ((g0_1 % (2 | 1'b1)) % ((t2 ? g1_1 : t4) | 1'b1));
  assign o0 = (((l0 | l1) >>> g0_1) ? l2 : ((t1 / (g1_0 | 1'b1)) ? 1 : (8'd52 ? 8'hb6 : g0_0)));
  assign o1 = t2;
endmodule
