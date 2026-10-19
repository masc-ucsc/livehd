/*
:lec_expect: refuted
*/
// Mutation control: cleanup must not turn a wrong result into equivalence.
module module_cycle_context(input a, b, output x, y);
  assign x = a;
  assign y = ~a;
endmodule
