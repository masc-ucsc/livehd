// Cleanup can also prove that feedback cancels, without a constant selector.
module complement(input k, output q);
  assign q = ~k;
endmodule

module cancel(input i, j, output o);
  assign o = i ^ j;
endmodule

module module_cycle_context(input a, b, output x, y);
  wire feedback;
  complement source(.k(x), .q(feedback));
  cancel sink(.i(feedback), .j(feedback ^ a), .o(x));
  assign y = x;
endmodule
