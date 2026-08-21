module deferred_for_runtime (
  input  logic [3:0] n_i,
  input  logic [7:0] x_i,
  output logic       y_o
);
  always_comb begin
    y_o = 1'b0;
    for (int unsigned k = 0; k < n_i; k++) y_o |= x_i[k];
  end
endmodule
