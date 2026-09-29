// Behavioral golden for loop_runtime_break.prp.
//
// The loop counts iterations until the first `stop` bit that is set, so `total`
// is the INDEX of the lowest set bit — 8 when no bit is set.  Spelled as the
// priority chain the comptime-bounded loop unrolls to:
//
//   stop == 8'b0000_0000 -> 8   (no break: all eight iterations ran)
//   stop == 8'b1000_0000 -> 7   (break at i=7, after seven increments)
//   stop == 8'bx100_0000 -> 6
//   ...
//   stop == 8'bxxxx_xxx1 -> 0   (break at i=0, before any increment)
module \loop_runtime_break.top (
  input  [7:0]  stop,
  output [11:0] total
);
  reg [11:0] acc;
  always @* begin
    if      (stop[0]) acc = 12'd0;
    else if (stop[1]) acc = 12'd1;
    else if (stop[2]) acc = 12'd2;
    else if (stop[3]) acc = 12'd3;
    else if (stop[4]) acc = 12'd4;
    else if (stop[5]) acc = 12'd5;
    else if (stop[6]) acc = 12'd6;
    else if (stop[7]) acc = 12'd7;
    else              acc = 12'd8;
  end
  assign total = acc;
endmodule
