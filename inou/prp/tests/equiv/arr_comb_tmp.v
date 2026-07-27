// blocking temp then per-entry comb store (t = d; arr[0] = t)
module arr_comb_tmp(input logic sel, input logic d, output logic o);
  logic arr [2];
  logic t;
  always_comb begin
    t = d;
    arr[0] = t;
    arr[1] = ~d;
  end
  assign o = arr[sel];
endmodule
