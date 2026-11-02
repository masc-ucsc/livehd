module top(input a, b, c, d, h, output [6:0] o);
  child dut(a, b, c, d, h, 4'b0, 4'b0, o);
endmodule

module child(input a, b, c, d, h, input [3:0] pad1, pad2, output [6:0] o);
  wire [5:0] p = {a, pad1, b};
  wire [5:0] q = {c, pad2, d};
  wire [5:0] mixed = p ^ q;
  wire [4:0] low = mixed[4:0];
  assign o = {low, h, low[0]};
endmodule
