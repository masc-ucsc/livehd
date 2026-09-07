// Async-reset latches must retain their reset value after reset is released.
// Current Yosys emits $adlatch; the importer folds reset into D and EN.
// Exercise both reset/gate polarities and a nonzero multibit reset value.
module latch_async(input c, input rst, input d, output logic q, output logic [2:0] qn);

always_latch begin
  if (rst)
    q <= 1'b0;
  else if (c)
    q <= d;
end
always_latch begin
  if (!rst)
    qn <= 3'b101;
  else if (!c)
    qn <= {d, ~d, d};
end
endmodule
