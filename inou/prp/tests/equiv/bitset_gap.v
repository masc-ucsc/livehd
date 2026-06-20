// Golden for bitset_gap. Each accumulator is built with explicit blocking
// bit-assignments in source order (last write wins), the zero gaps left
// implicit by the `= 0` init.
module \bitset_gap.foo (
   input  [31:0] i,
   output [15:0] out_gap,
   output [15:0] out_over,
   output [12:0] out_rev
);
  reg [15:0] g;
  always @* begin
    g       = 16'b0;
    g[4:0]  = i[4:0];
    g[12]   = i[31];   // bits 5..11 stay 0 (the gap)
  end
  assign out_gap = g;

  reg [15:0] o;
  always @* begin
    o       = 16'b0;
    o[7:0]  = i[7:0];
    o[11:4] = i[15:8]; // overwrites bits 4..7
  end
  assign out_over = o;

  reg [12:0] r;
  always @* begin
    r       = 13'b0;
    r[12]   = i[31];
    r[10:5] = i[30:25];
    r[4:1]  = i[11:8];
    r[0]    = i[0];
  end
  assign out_rev = r;
endmodule
