// z2 shape: constant-condition ternary RHS on a per-entry comb store
module arr_comb_mux_const(input logic sel, input logic d, output logic o);
  logic arr [2]; // array 1 and 2 are unknown -> anything is fine
  assign arr[0] =  d;
  assign o = arr[sel];
endmodule
