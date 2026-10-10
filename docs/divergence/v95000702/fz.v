module fz(
  input clock,
  input reset,
  input [8:0] i0,
  input signed i1,
  output signed o0,
  output [7:0] o1,
  output signed [2:0] o2
);
  wire signed t0 = ((3'sd2 ? (2'h2 ? i0 : 6'b110101) : (i0 - i0)) ? ((i1 + i0) ^ $unsigned(i0)) : 33'h1721cc84c);
  wire [30:0] t1 = (t0 ^~ $unsigned({i0[6:4], i1, i0[6:6]}));
  wire [2:0] t2 = (($signed(1'd0) << 3) / ((~^(t0 ? t1 : 8'b01110011)) | 1'b1));
  reg signed t3;
  always @(*) begin
    t3 = t2;
    case (i1)
      2: t3 = ((i1 * t0) * (4 % (t1 | 1'b1)));
      3: t3 = i1;
      default: ;
    endcase
  end
  reg [2:0] l0;
  always_latch begin
    if (!clock && reset) l0 = 0;
    else if (!clock && (t0)) l0 = ((5 + 16'sd27669) - (i0 >> t3));
  end
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (|$unsigned(3));
  wire gclk0 = clock & gen0;
  reg signed [31:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 3639; else g0_0 <= 1'b1;
  reg signed [30:0] g0_1;
  always @(posedge gclk0) if (reset) g0_1 <= 47501; else g0_1 <= (l0[0:0] - 12'b010101011111);
  reg gen1;
  always @(*) if (!clock) gen1 = reset || (|t0);
  wire gclk1 = clock & gen1;
  reg [30:0] g1_0;
  always @(posedge gclk1) if (reset) g1_0 <= 43229; else g1_0 <= ((33'd6415007089 * t0) >> 9);
  reg signed [1:0] g1_1;
  always @(posedge gclk1) if (reset) g1_1 <= 3; else g1_1 <= t3;
  reg signed [7:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + t3;
  reg [15:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if (((t0 ^ i1) ? (g0_0 % (g0_0 | 1'b1)) : (g1_0 % (l0 | 1'b1)))) mem[g1_1[1:0]] <= ($unsigned(g1_1) >> 6);
  end
  wire [15:0] mrd = mem[{1'b0, i1}];
  assign o0 = ((g1_1[1:0] ? l0 : (i0 ** 2)) >> t0);
  assign o1 = fb;
  assign o2 = (((|33'd6516067747) ^~ (t1 <= i0)) * ((g1_0 - t3) ? (3 - t0) : t1));
endmodule
