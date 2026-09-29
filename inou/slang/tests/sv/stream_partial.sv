// :test: error
// :error: multiple of the slice size
module stream_partial(input [11:0] d, output [11:0] q);
  assign q = {<<8{d}};   // 12 is not a multiple of 8
endmodule
