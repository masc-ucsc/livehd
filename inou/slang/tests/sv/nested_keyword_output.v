typedef struct packed {
  logic [7:0] sat;
  logic [7:0] ordinary;
} keyword_leaf_t;

typedef struct packed {
  keyword_leaf_t toS1;
} keyword_outer_t;

module nested_keyword_output(
  input  logic [7:0]   a,
  input  logic [7:0]   b,
  output keyword_outer_t io_out
);
  assign io_out.toS1.sat = a;
  assign io_out.toS1.ordinary = b;
endmodule
