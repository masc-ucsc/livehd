typedef struct packed {
  logic        interrupt_x;
  logic [57:0] reserved;
  logic [4:0]  cause;
} cause_t;

module struct_pattern_named_default_child(
  input  cause_t      cause_i,
  input  logic        aux_i,
  output logic [63:0] packed_o,
  output logic        aux_o
);
  assign packed_o = cause_i;
  assign aux_o = aux_i;
endmodule

module struct_pattern_named_default(
  input  logic [4:0] cause_i,
  input  logic       interrupt_i,
  input  logic       aux_i,
  output logic [63:0] packed_o,
  output logic       aux_o
);
  cause_t cause;
  logic child_aux;

  // Keep the instance before its combinational input driver. Its independent
  // aux output feeds the same always_comb block, which makes a coarse
  // instance/process dependency cycle even though the field-level logic is
  // acyclic. The cause leaves must therefore remain position-independent.
  struct_pattern_named_default_child child (
    .cause_i(cause),
    .aux_i(aux_i),
    .packed_o(packed_o),
    .aux_o(child_aux)
  );

  always_comb begin
    cause = '{cause: cause_i, interrupt_x: interrupt_i, default: '0};
    aux_o = child_aux;
  end
endmodule
