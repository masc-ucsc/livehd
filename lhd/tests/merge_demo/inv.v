// A Verilog leaf, elaborated through yosys and instantiated as a black box.
module inv(input signed [7:0] a, output signed [7:0] z);
  assign z = -a;
endmodule
