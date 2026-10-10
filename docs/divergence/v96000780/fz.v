module fz(
  input clock,
  input reset,
  input [11:0] i0,
  output o0
);
  function automatic [8:0] f0(input [7:0] a0);
    f0 = a0;
  endfunction
  function automatic signed [6:0] f1(input [7:0] a0);
    f1 = {1{a0[6:0]}};
  endfunction
  wire [7:0] t0 = ((i0 & i0) - i0);
  reg [8:0] l0;
  always_latch begin
    if (!clock && reset) l0 = 0;
    else if (!clock && (t0)) l0 = (6'sd27 - t0);
  end
  reg signed [8:0] l1;
  always_latch begin
    if (!clock && reset) l1 = 0;
    else if (!clock && (|t0)) l1 = {8'd116, i0, i0[3:3]};
  end
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (|t0);
  wire gclk0 = clock & gen0;
  reg g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 0; else g0_0 <= f1(l1);
  reg signed [6:0] g0_1;
  always @(posedge gclk0) if (reset) g0_1 <= 38; else g0_1 <= (4'd3 - i0);
  reg gen1;
  always @(*) if (!clock) gen1 = reset || (6);
  wire gclk1 = clock & gen1;
  reg signed [7:0] g1_0;
  always @(posedge gclk1) if (reset) g1_0 <= 6; else g1_0 <= (i0 % (l0 | 1'b1));
  localparam [1:0] P0 = 6;
  reg fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + $unsigned(l0);
  reg [11:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if (t0) mem[g1_0[1:0]] <= 6'sd20;
  end
  wire [11:0] mrd = mem[{1'b0, fb}];
  reg signed [2:0] m1;
  reg [2:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if ((fb ** 1)) begin m1 <= (P0 ? t0 : g1_0); m2 <= m1; end
      else if (P0) m2 <= (P0 | 6'd59);
      else m1 <= m2;
    end
  end
  assign o0 = l0;
endmodule
