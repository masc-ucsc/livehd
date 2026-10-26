// :test: roundtrip
// Fixed unpacked structs cross module boundaries and contain nonzero-based
// arrays in both directions. Output is also checked against scalar RTL below.
package unpacked_struct_types;
  typedef struct packed { logic signed [3:0] x; logic [2:0] y; } lane_t;
  typedef struct { lane_t lanes[2:1]; logic flag; } inner_t;
  typedef struct { inner_t inner; logic [4:0] values[3:4]; } record_t;
endpackage

module unpacked_struct_leaf(
  input unpacked_struct_types::record_t a,
  input logic pick,
  input logic signed [2:0] idx,
  output unpacked_struct_types::record_t b
);
  always_comb begin
    b = a;
    b.inner.lanes[1].x = a.inner.lanes[2].x;
    b.values[3 + pick] = a.values[4] ^ 5'h15;
    b.inner.lanes[idx].y = 3'd6;
  end
endmodule

module unpacked_struct_nested(
  input logic [3:0] x,
  input logic [4:0] v,
  input logic pick,
  input logic signed [2:0] idx,
  output logic [7:0] selected,
  output logic [5:0] lanes,
  output logic [15:0] y
);
  import unpacked_struct_types::*;
  record_t a, b;
  always_comb begin
    a.inner.lanes[2] = '{x: x, y: 3'd5};
    a.inner.lanes[1] = '{x: 4'd3, y: 3'd2};
    a.inner.flag = pick;
    a.values[3] = 5'd7;
    a.values[4] = v;
  end
  unpacked_struct_leaf u(a, pick, idx, b);
  assign selected = b.inner.lanes[idx].x;
  assign lanes = {b.inner.lanes[2].y, b.inner.lanes[1].y};
  assign y = {b.inner.lanes[1].x, b.inner.flag, b.values[3], b.values[4], 1'b0};
endmodule
