module unpacked_array_port_order(
  input  logic [1:0] idx_i,
  input  logic [7:0] lane0_i,
  input  logic [7:0] lane1_i,
  input  logic [7:0] lane2_i,
  input  logic [7:0] lane3_i,
  input  logic [7:0] asc_i [0:3],
  input  logic [7:0] desc_i [3:0],
  output logic [7:0] asc_o [0:3],
  output logic [7:0] desc_o [3:0],
  output logic [7:0] asc_pick_o,
  output logic [7:0] desc_pick_o,
  output logic [31:0] asc_flat_o,
  output logic [31:0] desc_flat_o
);
  assign asc_o[0] = lane0_i;
  assign asc_o[1] = lane1_i;
  assign asc_o[2] = lane2_i;
  assign asc_o[3] = lane3_i;

  assign desc_o[3] = lane3_i;
  assign desc_o[2] = lane2_i;
  assign desc_o[1] = lane1_i;
  assign desc_o[0] = lane0_i;

  assign asc_pick_o = asc_i[idx_i];
  assign desc_pick_o = desc_i[idx_i];
  assign asc_flat_o = {asc_i[0], asc_i[1], asc_i[2], asc_i[3]};
  assign desc_flat_o = {desc_i[3], desc_i[2], desc_i[1], desc_i[0]};
endmodule
