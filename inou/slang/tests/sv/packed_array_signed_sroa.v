typedef logic signed [7:0] signed_lane_t;

module packed_array_signed_sroa(
  input  logic [2:0]        idx_i,
  input  logic signed [7:0] data_i,
  output logic signed [7:0] pick_o,
  output logic signed [15:0] extended_o,
  output logic [31:0]       whole_o
);
  signed_lane_t [3:0] lanes;

  always_comb begin
    lanes = '0;
    lanes[0] = -8'sd2;
    lanes[1] = 8'sd3;
    lanes[2] = -8'sd4;
    lanes[3] = 8'sd5;
    lanes[idx_i] = data_i;
  end

  assign pick_o     = lanes[idx_i];
  assign extended_o = $signed(lanes[idx_i]);
  assign whole_o    = lanes;
endmodule
