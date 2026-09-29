module packed_array_wire_init_sroa
    (input logic [2 : 0]    idx_i,
     input logic [63 : 0]   a_i,
     input logic [63 : 0]   b_i,
     output logic [63 : 0]  pick_o,
     output logic [15 : 0]  const_pick_o,
     output logic [511 : 0] whole_o);
  wire [7 : 0][63 : 0] lanes     = {64'h0, 64'h0, 64'h0, b_i, a_i, 64'h3333, 64'h2222, 64'h1111};
  wire [1 : 0][15 : 0] constants = {16'hBEEF, 16'h1234};

  assign pick_o       = lanes[idx_i];
  assign const_pick_o = constants[idx_i[0]];
  assign whole_o      = lanes;
endmodule
