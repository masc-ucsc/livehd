// Golden for fn_name_value_port.prp: every output is a plain port copy.
module fn_name_value_port (
  input  [7:0] sel,
  input  [7:0] inc,
  output [7:0] o,
  output [7:0] p,
  output [7:0] q
);
  assign o = sel;
  assign p = inc;
  assign q = sel;
endmodule
