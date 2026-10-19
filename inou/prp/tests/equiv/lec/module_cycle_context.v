/*
:lec_top: module_cycle_context
:lec_set: formal.effort=proof
:lec_sweep: compile.specialize_modules=false,true
*/
// Flat reference, independent of the mux/instance representation.
module module_cycle_context(input a, b, output x, y);
  assign x = a;
  assign y = a;
endmodule
