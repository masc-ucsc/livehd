typedef struct packed {
  logic [7:0] data;
  logic       valid;
} state_lane_t;

module packed_array_struct_reg_sroa(
  input  logic        clk,
  input  logic        rst_n,
  input  logic        en_i,
  input  logic        field_only_i,
  input  logic [2:0]  idx_i,
  input  logic [7:0]  data_i,
  input  logic        valid_i,
  output logic [7:0]  pick_data_o,
  output logic        pick_valid_o,
  output logic [35:0] whole_o
);
  state_lane_t [3:0] lanes_q;
  state_lane_t       pick;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      lanes_q <= '0;
    end else if (en_i) begin
      if (field_only_i)
        lanes_q[idx_i].valid <= valid_i;
      else
        lanes_q[idx_i] <= '{data: data_i, valid: valid_i};
    end
  end

  assign pick         = lanes_q[idx_i];
  assign pick_data_o  = pick.data;
  assign pick_valid_o = pick.valid;
  assign whole_o      = lanes_q;
endmodule
