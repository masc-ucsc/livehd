// A comparison bound to a 1-bit function formal (and returned through one).
module function_bool_arg(input clock, input reset, input [3:0] a, input [3:0] b,
                         output [30:0] o, output [2:0] p);
  function automatic [30:0] f0(input a0);
    f0 = a0;
  endfunction
  function automatic f1(input [3:0] x, input y);
    f1 = (x > 4'd7) ^ y;
  endfunction
  assign o = f0(a < b) + f0(a == b);
  assign p = {f1(a, b == 4'd2), f1(b, a < 4'd3), f0(a != 0)};
endmodule
