module packed_array_sroa
    (input logic [1 : 0]   idx_i,
     input logic [7 : 0]   a_i,
     input logic [7 : 0]   b_i,
     input logic [7 : 0]   c_i,
     input logic [7 : 0]   d_i,
     output logic [7 : 0]  pick_o,
     output logic [7 : 0]  first_o,
     output logic [31 : 0] whole_o);
  logic [3 : 0][7 : 0] lanes;

  always_comb begin
    lanes           = '0;
    lanes[0]        = a_i;
    lanes[1]        = b_i;
    lanes[2]        = c_i;
    lanes[2][3 : 0] = c_i[3 : 0];
    lanes[3]        = d_i;
  end

  assign pick_o  = lanes[idx_i];
  assign first_o = lanes[0];
  assign whole_o = lanes;
endmodule
