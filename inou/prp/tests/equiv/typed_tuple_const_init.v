// Golden for typed_tuple_const_init.prp.
module top(input [5:0] a, input w,
           output ky, output [5:0] kz,
           output jy, output [5:0] jz,
           output cy, output [5:0] cz);
  assign ky = w;           assign kz = a ^ 6'd3;
  assign jy = ~w;          assign jz = a;
  assign cy = (a == 6'd7); assign cz = a ^ 6'd5;
endmodule
