/*
:top: reductions
:readers: slang yosys-slang yosys-verilog
*/
module reductions(input [8:0] a, input signed [3:0] b, output [6:0] y);
  assign y[0] = &a;
  assign y[1] = ~(&a);
  assign y[2] = &b;
  assign y[3] = &(a[3:0]);
  nand gate_nand(y[4], a[0], a[1], a[2], a[3]);
  assign y[5] = &(a[0]);
  assign y[6] = &{a[3:0], b};
endmodule
