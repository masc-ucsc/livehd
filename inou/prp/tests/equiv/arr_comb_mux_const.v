// z2 shape: constant-condition ternary RHS on a per-entry comb store
module arr_comb_mux_const(input logic sel, input logic d, output logic o);
  logic arr [2];
  assign arr[0] = 1 ? d : ~d;
  assign o = arr[sel];
endmodule
