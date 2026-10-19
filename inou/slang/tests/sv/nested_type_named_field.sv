// :test: roundtrip
// A nested struct field spelled like a Pyrope type (U0) is quoted in the
// callee's port list; the call must still bind `c.inner.U0` to it.
typedef struct packed {
  logic U0;
  logic b;
} inner_t;

typedef struct packed {
  logic [3:0] x;
  inner_t     inner;
} cap_t;

module nested_type_named_leaf (input cap_t c, output logic o);
  assign o = c.inner.U0 ^ c.inner.b ^ c.x[0];
endmodule

module nested_type_named_field (input logic [5:0] v, output logic o);
  cap_t c;
  assign c = v;
  nested_type_named_leaf u (.c(c), .o(o));
endmodule
