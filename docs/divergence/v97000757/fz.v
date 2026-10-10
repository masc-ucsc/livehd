module fz(
  input clock,
  input reset,
  input [2:0] i0,
  output [2:0] o0,
  output [6:0] o1
);
  reg [4:0] t0;
  always @(*) begin
    t0 = {i0[1:0], i0, i0};
    case (i0)
      2: t0 = (i0 / (i0 | 1'b1));
      3: t0 = (i0 == 2);
      default: ;
    endcase
  end
  wire signed [3:0] t1 = t0;
  reg [8:0] l0;
  always @(*) if (!clock && (reset || (t1))) l0 = reset ? 0 : (t0 ? i0 : t0);
  reg signed l1;
  always @(*) if (!clock && (reset || (|12'sd1403))) l1 = reset ? 0 : (t1 + t0);
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (5);
  wire gclk0 = clock & gen0;
  reg [4:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 5; else g0_0 <= (l0 + t0);
  reg [3:0] g0_1;
  always @(posedge gclk0) if (reset) g0_1 <= 0; else g0_1 <= (t1 + t0);
  reg gen1;
  always @(*) if (!clock) gen1 = reset || (i0);
  wire gclk1 = clock & gen1;
  reg signed [1:0] g1_0;
  always @(posedge gclk1) if (reset) g1_0 <= 3; else g1_0 <= (g0_1 ^~ 6);
  reg [11:0] g1_1;
  always @(posedge gclk1) if (reset) g1_1 <= 3190; else g1_1 <= g0_0;
  reg signed [4:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + g0_1[3:3];
  assign o0 = (t0 ^ (l0 << 0));
  assign o1 = l1;
endmodule
