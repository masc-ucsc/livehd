module packed_vector_port_sroa_child(
  input  logic [7:0] bits_i,
  output logic [1:0] picks_o
);
  assign picks_o = {bits_i[7], bits_i[3]};
endmodule

module packed_vector_port_sroa(
  input  logic [7:0] bits_i,
  output logic [1:0] picks_o
);
  packed_vector_port_sroa_child child (
    .bits_i  (bits_i),
    .picks_o (picks_o)
  );
endmodule
