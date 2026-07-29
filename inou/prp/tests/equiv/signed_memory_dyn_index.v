// Regression: EVERY signed memory failed upass.bitwidth's element-fit check.
//
// A dynamic index turns the array into a memory, and the reader models memory
// storage as RAW BITS -- an element read is `q[i]#sext[0..=W-1]`, so the value
// flowing into an element write carries the UNSIGNED reinterpretation of that
// storage (range [0, 2^W-1]). check_array_elem_fit judged it against the
// SIGNED element envelope only, so `reg signed [16:0] q [3:0]` died on
// "`q` (value 0) does not fit its declared range [-65536, 65535]" -- a range
// that plainly contains 0. An unsigned array escaped only because that
// judgement tests r.min, never r.max.
module signed_memory_dyn_index(input clk, input en, input [1:0] sel,
                               input signed [16:0] d, output signed [16:0] o);
  reg signed [16:0] q [3:0];
  always @(posedge clk) if (en) q[sel] <= d;
  assign o = q[sel];
endmodule
