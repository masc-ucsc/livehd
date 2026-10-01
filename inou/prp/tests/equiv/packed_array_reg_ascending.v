// Golden for packed_array_reg_ascending: an ASCENDING packed register array.
// Element 0 of `logic [0:4][7:0]` is the MOST significant byte of the whole
// value (IEEE 1800 7.4.1), so `r[j] <= y` lands on bits (4-j)*8 +: 8 of `q`;
// j = 5..7 writes nothing.
module packed_array_reg_ascending (
  input         clock,
  input         reset,
  input         ld,
  input  [2:0]  j,
  input  [7:0]  y,
  input  [39:0] d,
  output [39:0] q
);
  logic [0:4][7:0] r;
  always_ff @(posedge clock) begin
    if (reset) r <= '0;
    else if (ld) r <= d;
    else r[j] <= y;
  end
  assign q = r;
endmodule
