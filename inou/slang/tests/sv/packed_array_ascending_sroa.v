module packed_array_ascending_sroa(
  input  logic [2:0] idx_i,
  input  logic [7:0] data_i,
  output logic [7:0] pick_o,
  output logic [31:0] whole_o
);
  logic [0:3][7:0] lanes;

  always_comb begin
    lanes = 32'h11223344;
    lanes[idx_i] = data_i;
  end

  assign pick_o  = lanes[idx_i];
  assign whole_o = lanes;
endmodule
