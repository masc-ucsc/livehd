module fz(
  input clock,
  input reset,
  input signed [8:0] i0,
  input signed i1,
  input signed [1:0] i2,
  output [30:0] o0,
  output o1,
  output [30:0] o2
);
  wire signed [1:0] t0 = i1;
  wire signed [4:0] t1 = (&i2[1]);
  wire signed [31:0] t2 = (((t0 <<< 5) - (t1 ? i0 : 3'b010)) + ((t1 ^ t1) % (t1 | 1'b1)));
  wire signed [3:0] t3 = {t2[12:10], i0, t1};
  localparam P0 = 8'd224;
  reg [15:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if (P0) mem[t3[1:0]] <= i1;
  end
  wire [15:0] mrd = mem[{1'b0, P0}];
  assign o0 = (i2[0] ^~ mrd);
  assign o1 = $signed(i1);
  assign o2 = ((4'h8 | (i0 >>> 1)) < ((P0 ? t2 : t3) - (5 ** 2)));
endmodule
