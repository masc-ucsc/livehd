
module offset(a, b, c, d, e, f, g, h);

  input [31:0] a;
  input [15:0] b;
  input [32:1] d;
  output [7:0] c;
  output [31:0] e;
  output [10:1] f;
  output [10:1] g;
  output [10:1] h;

  wire [32:1] a_sdw;
  wire [31:16] b_sdw;

  assign a_sdw[32:1] = a[31:0];
  assign b_sdw[31:16] = b[15:0];

  assign c = a_sdw[8:1] & b_sdw[24:17];
  assign e = d;

  assign f = a_sdw[8:1] | b_sdw[31:21];

  assign g[10:2] = a_sdw[10:2] | b_sdw[26:18];
  wire aWire;
  assign { h[10:2], aWire } = a_sdw[10:1] | b_sdw[26:17];
  assign h[1] = 1'b1;

  //expected: h = { a[9:1] | b[10:2], 1'h1 };
endmodule

