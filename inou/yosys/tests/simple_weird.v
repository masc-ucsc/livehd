module simple_weird(input [7:0] a, input [7:0] b,
  output signed [2:0] h
);

  signed wire [7:0] as = a;
  signed wire [7:0] bs = b;

  wire [7:0] f = as + bs;
  assign h[0] = 1 | {|{as + bs - as}};
  assign h[1] = ^{as + bs - as};
  assign h[2] = &{as + bs - as};

endmodule

