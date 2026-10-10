// Packed selects that reach outside their vector (IEEE 1800 11.5.1): only the
// in-range bits are written, the rest read X; a signed combinational target
// re-signs after a write to its sign bit; a runtime slice past the top writes
// only the bits that fit.
module partial_oob_select(input clock, input reset, input [3:0] a, input b, input [1:0] i,
                          output [3:0] o0, output [2:0] o1, output signed [3:0] o2, output [7:0] o3);
  reg [3:0] t;
  always @(*) begin t = a; t[1 -: 3] = 3'b111; end
  assign o0 = t;
  assign o1 = a[1 -: 3] & 3'b110;   // bit -1 reads X; mask it off
  reg signed [1:0] s;
  always @(*) begin s <= a[1:0]; s[1] <= b; end
  assign o2 = s;
  reg [3:0] u;
  always @(*) begin u = 4'd2; u[i +: 2] = 2'b11; end
  assign o3 = u;
endmodule
