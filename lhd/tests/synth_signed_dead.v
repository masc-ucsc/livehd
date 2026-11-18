// A chain of signed 8-bit taps where only each tap's top four bits reach the output (the matched filter shape):
// the low four bits of every tap shift along the chain and fall off its end, so nothing observes them. A signed
// register must be narrowed like an unsigned one (its rebuilt value stays signed), and a sign extension or a
// product must not demand operand bits its observed result never reads.
module signed_tap(input clk, input rst, input signed [7:0] x_in, output signed [7:0] x_out);
  reg signed [7:0] x_r;
  always @(posedge clk) if (rst) x_r <= 8'sd0; else x_r <= x_in;
  assign x_out = x_r;
endmodule

module synth_signed_dead(input clk, input rst, input signed [7:0] x, output signed [7:0] y);
  wire signed [7:0] xs [0:4];
  wire [31:0] lvl;
  assign xs[0] = x;
  genvar i;
  generate
    for (i = 0; i < 4; i = i + 1) begin : taps
      signed_tap t(.clk(clk), .rst(rst), .x_in(xs[i]), .x_out(xs[i+1]));
      wire signed [3:0] m;
      wire signed [7:0] fp;
      wire signed [3:0] p;
      assign m  = xs[i+1][4 +: 4];
      assign fp = m * m;
      assign p  = fp[4 +: 4];
      assign lvl[8*i +: 8] = {{4{p[3]}}, p};
    end
  endgenerate
  assign y = lvl[7:0] + lvl[15:8] + lvl[23:16] + lvl[31:24];
endmodule
