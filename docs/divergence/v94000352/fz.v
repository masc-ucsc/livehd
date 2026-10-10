module fz(
  input clock,
  input reset,
  input [6:0] i0,
  output signed [7:0] o0
);
  wire [1:0] t0 = ((!i0) == i0[0:0]);
  reg [3:0] l0;
  always @(*) if (!clock && (reset || (t0))) l0 = reset ? 0 : i0;
  reg l1;
  always_latch begin
    if (clock && reset) l1 = 0;
    else if (clock && (i0)) l1 = 4'd7;
  end
  reg [6:0] l2;
  always @(*) if (!clock && (reset || (t0))) l2 = reset ? 0 : (~^i0);
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (|i0);
  wire gclk0 = clock & gen0;
  reg signed [6:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 106; else g0_0 <= {l0, 3'd0, l0[0:0]};
  reg mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if ($signed(4)) mem[t0[1:0]] <= (l0 == t0);
  end
  wire mrd = mem[i0[1:0]];
  assign o0 = l2;
endmodule
