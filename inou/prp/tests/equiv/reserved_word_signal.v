// Golden for reserved_word_signal (2i-issues F). The output named `packed` is an
// SV-2012 keyword, so it MUST be escaped (\packed ) for the reference itself to
// parse. The generated implementation is RED until cgen escapes it the same way.
module top(input [7:0] a, output [7:0] \packed );
  assign \packed = a + 8'd1;
endmodule
