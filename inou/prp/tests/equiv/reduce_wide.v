module \reduce_wide.foo (
  input [128:0] a,
  input signed [64:0] b,
  output parity,
  output reg [7:0] population,
  output signed_parity,
  output reg [6:0] signed_population
);
  assign parity = ^a;
  assign signed_parity = ^b;
  integer i;
  always @* begin
    population = 0;
    for (i = 0; i < 129; i = i + 1)
      population = population + a[i];
    signed_population = 0;
    for (i = 0; i < 65; i = i + 1)
      signed_population = signed_population + b[i];
  end
endmodule
