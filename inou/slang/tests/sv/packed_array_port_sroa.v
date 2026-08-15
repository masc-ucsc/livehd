module packed_array_port_sroa_child(
  input  logic [3:0][7:0] lanes_i,
  input  logic [2:0]      idx_i,
  output logic [3:0][7:0] lanes_o,
  output logic [7:0]      pick_o
);
  assign lanes_o = lanes_i;
  assign pick_o  = lanes_i[idx_i];
endmodule

module packed_array_port_sroa(
  input  logic [31:0] flat_i,
  input  logic [2:0]  idx_i,
  output logic [31:0] flat_o,
  output logic [7:0]  pick_o
);
  packed_array_port_sroa_child child (
    .lanes_i(flat_i),
    .idx_i(idx_i),
    .lanes_o(flat_o),
    .pick_o(pick_o)
  );
endmodule
