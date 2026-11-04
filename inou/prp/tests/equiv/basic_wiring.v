// Canonical paired regression; original parser forms are retained below.
module basic_wiring(
  input [63:0] a, input [3:0] low, input b, output alias_bit, output [4:0] scattered, output
  [31:0] offset_word, output [1:0] joined, output [5:0] truncated, output [31:0] mask16,
  identity32, output [63:0] low32, doubled, widened
);
  assigns alias_assignment(a[0], alias_bit);
  multiassign scattered_assignment(scattered);
  offset_input nonzero_port_index(a[31:0], a[15:0], a[31:0], offset_word);
  trivial_join bit_assignments(a[0], b, joined);
  concat_truncated_not narrow_temporaries(a, low, truncated);
  consts wide_masks(a[31:0], mask16, low32, identity32, doubled, widened);
endmodule

// Former fixture: inou/yosys/tests/assigns.v

module assigns(input a, output b);

wire [1:0] c;
assign b = c[0];
assign c[0] = a;
assign c[1] = a;

endmodule


// Former fixture: inou/yosys/tests/multiassign.v

module multiassign(output [4:0] out);

  assign { out[4], out[2:1] } = { 2'b11, out[3] }; //Also, isn't this tuple assignment inverted!?
  assign { out[3], out[0] } = 2'b01;

endmodule


// Former fixture: inou/yosys/tests/offset_input.v

module offset_input(a, b, d, e);

  input [31:0] a;
  input [15:0] b;
  input [32:1] d;
  output [31:0] e;

  assign e = d;

endmodule


// Former fixture: inou/yosys/tests/trivial_join.v
module trivial_join(input a, input b, output [1:0] c);
  assign c[0] = a;
  assign c[1] = ~b;
endmodule


// Former fixture: inou/yosys/tests/concat_truncated_not.v
module concat_truncated_not (
  input [63:0] a,
  input [3:0] low,
  output [5:0] y
);
  wire bit_not = ~a;
  wire bit_add = a + 1'b1;
  assign y = {bit_not, bit_add, low};
endmodule

// Former fixture: inou/yosys/tests/consts.v

module consts(a,b,c,d,e,f);

input [31:0] a;
output [31:0] b,d;
output [63:0] c,e,f;

assign c = 64'hFFFFFFFF & {a,a};
assign b = 32'hFFFF & a;

assign e = 64'hFFFFFFFFFFFFFFFF & {a,a};
assign d = 32'hFFFFFFFF & a;

assign f = 32'hFFFFFFFF & a;

endmodule
