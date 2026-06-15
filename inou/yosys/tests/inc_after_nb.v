// `x++` is a BLOCKING write even when it follows nonblocking `<=` assignments
// in the same sequential process. The reader must not inherit the prior `<=`
// style for the increment's write-back (it would false-flag a style mix).
module inc_after_nb
  ( input            clk
  , input      [1:0] st
  , input            a
  , input            b
  , output reg [7:0] ecount
  , output reg [7:0] q
  );
  always_ff @(posedge clk) begin
    int unsigned errs;
    errs = 0;
    q <= q + 8'd1;          // nonblocking first
    case (st)
      2'd0: begin
        if (a) errs++;       // blocking increment after the <=
        if (b) errs++;
        ecount <= ecount + errs[7:0];
      end
      default: ecount <= ecount;
    endcase
  end
endmodule
