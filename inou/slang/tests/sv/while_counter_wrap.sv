// :test: error
// :error: while loops with non-constant conditions
// The narrowed update wraps before the bound; this is not a bounded scan.
module while_counter_wrap(input logic run, output logic [3:0] count);
  integer i;
  always_comb begin
    i = 0;
    while (i < 4 && run) i = 1'(i + 1);
    count = i[3:0];
  end
endmodule
