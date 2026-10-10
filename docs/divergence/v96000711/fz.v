module fz(
  input clock,
  input reset,
  input [11:0] i0,
  output [3:0] o0
);
  wire signed [1:0] t0 = i0[11 - i0[2:0] -: 3];
  reg [11:0] l0;
  always @(*) if (!clock && (reset || (|t0))) l0 = reset ? 0 : $unsigned(t0);
  reg l1;
  always @(*) if (!clock && (reset || (t0))) l1 = reset ? 0 : t0;
  reg [4:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if (i0) mem[l0[1:0]] <= l1;
  end
  wire [4:0] mrd = mem[t0[1:0]];
  assign o0 = ((8'sd29 % (l0 | 1'b1)) + (0 << mrd));
endmodule
