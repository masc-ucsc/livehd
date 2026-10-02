// :test: roundtrip
// :lec_timeout: 30
module writer_latch_attributes(input clock, input reset, input en, input [3:0] d,
                        output reg [3:0] lo, hi, output reg sampled);
  always @(posedge clock) sampled <= en;
  always_latch if (!clock) begin
    if (en) lo <= d;
  end
  always_latch begin
    if (reset) hi <= 4'h9;
    else if (clock && en) hi <= lo;
  end
endmodule
