// Golden for generic_loop_port_width: the OR of every lane whose select bit is
// set. Every bit of `wide`, of `a ^ b` and of `a` can reach an output.
module top(
    input  wire [3:0]  sel,
    input  wire [63:0] wide,
    input  wire [31:0] a,
    input  wire [31:0] b,
    output wire [15:0] w,
    output wire [15:0] x,
    output wire [7:0]  y
);
  wire [31:0] ab = a ^ b;
  assign w = ({16{sel[0]}} & wide[15:0])  | ({16{sel[1]}} & wide[31:16])
           | ({16{sel[2]}} & wide[47:32]) | ({16{sel[3]}} & wide[63:48]);
  assign x = ({16{sel[0]}} & ab[15:0])    | ({16{sel[1]}} & ab[31:16]);
  assign y = ({8{sel[0]}} & a[7:0])   | ({8{sel[1]}} & a[15:8])
           | ({8{sel[2]}} & a[23:16]) | ({8{sel[3]}} & a[31:24]);
endmodule
