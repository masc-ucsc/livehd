// Golden for reg_partial_write_chain: two independently-enabled non-blocking
// single-bit writes to one register (the per-entry register-file idiom). Both
// writes must land when both enables fire in the same cycle; each bit holds
// independently otherwise. The LEC fails if the Pyrope side's second partial
// write rebuilds the word from stale q (dropping the first write).
module \reg_partial_write_chain.top (
  input        clock,
  input        reset,
  input        en7,
  input        en6,
  input  [1:0] d,
  output [1:0] q
);
reg [7:0] data;
always @(posedge clock) begin
  if (en7) data[7] <= d[0];
  if (en6) data[6] <= d[1];
end
assign q = data[7:6];
endmodule
