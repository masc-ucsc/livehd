// Golden for width_extend_match.prp, written by hand from the same
// specification: a width difference is not a mismatch — the wider net is the
// narrower one's EXTENSION (sign for signed, zero for unsigned), and only an
// extension that does not hold is a real difference.
//
// All four directions, plus the two cases that catch a comparison which picks
// the WRONG extension:
//   w4  a negative s3 widened to s8   -> zero-extending a signed net shows up
//   w5  a u30 with bit 29 set widened -> sign-extending an unsigned net shows up
module top(
   input signed [2:0] a
  ,input signed [7:0] b
  ,input [29:0] c
  ,input [49:0] d
  ,output reg signed [7:0] w0
  ,output reg signed [2:0] w1
  ,output reg [49:0] w2
  ,output reg [29:0] w3
  ,output reg signed [7:0] w4
  ,output reg [49:0] w5
);
always_comb begin
  w0 = $signed(a);        // s3  -> s8   sign extension
  w1 = $signed(b[2:0]);   // s8  -> s3   narrowing (representable)
  w2 = {20'b0, c};        // u30 -> u50  zero extension
  w3 = d[29:0];           // u50 -> u30  narrowing (fits)
  w4 = -8'sd3;            // negative, widened: must sign-extend to 8'hFD
  w5 = 50'h20000000;      // bit 29 set, widened: must NOT smear into 30..49
end
endmodule
