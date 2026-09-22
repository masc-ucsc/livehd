// :test: error
// :error: while loops with non-constant conditions
// A runtime while with no finite induction bound must not be truncated.
module unbounded_while(input logic run, output logic [3:0] count);
  always_comb begin
    count = 0;
    while (run) count = count + 1;
  end
endmodule
