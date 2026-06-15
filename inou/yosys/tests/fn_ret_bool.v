// A function whose body computes a boolean comparison, then a caller compares
// the result `!= 0`. The reader must coerce the boolean result to integer (a
// Verilog function returns a bit-vector), else upass typecheck flags the
// caller's `f(x) != 0` as a bool-vs-int comparison mismatch.
module fn_ret_bool
  ( input  [7:0]     a
  , input  [7:0]     b
  , output reg [7:0] o
  );
  function automatic is_hi(input [7:0] v);
    is_hi = v[7] == 1'b1;
  endfunction
  always @* begin
    o = 8'h0;
    if (is_hi(a) != 1'b0) o = b;
  end
endmodule
