// Independent specification: four and twelve bits, with no state or reset.
module top(
    input wire [3:0] a4, b4,
    input wire [11:0] a12, b12,
    output wire [3:0] y4,
    output wire [11:0] y12
);
  assign y4 = a4 ^ b4;
  assign y12 = a12 ^ b12;
endmodule
