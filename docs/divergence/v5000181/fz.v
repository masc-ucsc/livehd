module fz(
  input clock,
  input reset,
  input signed [6:0] i0,
  output signed [1:0] o0
);
  reg signed [11:0] t0;
  always @(*) begin
    t0 = (!1'b0);
    if ((5 >>> 9)) t0 = (i0 + i0);
  end
  reg [1:0] t1;
  always @(posedge clock) if (reset) t1 <= 2; else t1 <= t0;
  localparam [1:0] P0 = 8'b00110110;
  assign o0 = t1[0:0];
endmodule
