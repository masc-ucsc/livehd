typedef struct packed {
  logic [7:0] data;
  logic       valid;
} lane_t;

module packed_array_struct_sroa(
  input  logic [1:0] idx_i,
  input  logic [7:0] a_i,
  input  logic [7:0] b_i,
  output logic [7:0] pick_data_o,
  output logic       pick_valid_o,
  output logic [7:0] first_data_o,
  output logic [35:0] whole_o
);
  lane_t [3:0] lanes;
  lane_t       pick;

  always_comb begin
    lanes[0] = '{data: a_i, valid: 1'b1};
    lanes[1] = '{data: b_i, valid: 1'b0};
    lanes[2] = '{data: a_i ^ b_i, valid: 1'b1};
    lanes[3] = '{data: a_i + b_i, valid: 1'b0};
  end

  assign pick         = lanes[idx_i];
  assign pick_data_o  = pick.data;
  assign pick_valid_o = pick.valid;
  assign first_data_o = lanes[0].data;
  assign whole_o      = lanes;
endmodule
