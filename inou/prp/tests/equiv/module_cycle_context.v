// Independent flat reference: x = a; y = x.
module module_cycle_context(input a, b, output x, y);
  assign x = a;
  assign y = a;
endmodule
