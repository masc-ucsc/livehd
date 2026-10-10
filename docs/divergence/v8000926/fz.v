module fz(
  input clock,
  input reset,
  input signed [31:0] i0,
  input signed [16:0] i1,
  output signed [1:0] o0,
  output [1:0] o1,
  output [3:0] o2
);
  reg signed [6:0] t0;
  always @(*) begin
    t0 = $signed(8'd125);
    if (((~|12'd2604) * 4'sd4)) t0 = ((i1 << 2) | 12'he60);
  end
  wire [3:0] t1 = (((t0 != 6'd28) - (i1 >>> 2)) ^~ 8'b01110001);
  wire [15:0] t2 = (t0[2:0] ? 8'h5b : ((t0 / (i1 | 1'b1)) & i1[14:4]));
  reg [30:0] t3;
  always @(posedge clock) begin
    if (reset) t3 <= 56179;
    else if (t1) t3 <= (({i1[13:12], t0, t2[15:14]} * i1) + (^1'h1));
  end
  wire [4:0] t4 = i1;
  assign o0 = ($unsigned((4 >>> 0)) + ((t0 >> t1) <= t1));
  assign o1 = t1;
  assign o2 = i0;
endmodule
