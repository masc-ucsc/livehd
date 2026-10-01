// Golden for generic_width_tuple_field.prp: N = 8, so the whole 8-bit address
// passes when `w` is set (else 0), and `hit` compares all 8 bits against 5.
module generic_width_tuple_field(input [7:0] a, input w, output [7:0] y, output h);
  assign y = w ? a : 8'd0;
  assign h = w && (a == 8'd5);
endmodule
