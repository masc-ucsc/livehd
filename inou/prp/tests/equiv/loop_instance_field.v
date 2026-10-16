// Hand-written golden for loop_instance_field.prp: o ORs byte i of din for
// every i whose sel bit is set and whose mirrored bit (7 - i) is set too (inv
// is ~sel); p is (a ^ 4'b0101) bit-reversed; n[f] = sel_in[f] | sel_in[2 + f].
module top(
  input  [7:0]  sel_in,
  input  [63:0] din,
  input  [3:0]  a,
  output [7:0]  o,
  output [3:0]  p,
  output [1:0]  n
);
  reg [7:0] acc;
  integer   i;
  always @* begin
    acc = 8'b0;
    for (i = 0; i < 8; i = i + 1)
      if (sel_in[i] && sel_in[7 - i])
        acc = acc | din[i*8 +: 8];
  end
  assign o = acc;

  wire [3:0] q = a ^ 4'b0101;
  assign p = {q[0], q[1], q[2], q[3]};

  // n[f]: any of sel bits f and 2 + f.
  assign n = {sel_in[1] | sel_in[3], sel_in[0] | sel_in[2]};
endmodule
