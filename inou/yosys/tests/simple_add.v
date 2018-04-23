module simple_add(input [7:0] a, input [7:0] b,
  output signed [7:0] h
);

  signed wire [7:0] as = a;
  signed wire [7:0] bs = b;

  wire [7:0] f = as + bs;
  assign h = as + bs - as;

endmodule

