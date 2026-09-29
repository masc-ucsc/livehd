module top(input clk, input request, output now, output delayed);
  reg \flags.active , \flags.delayed ;
  always @(posedge clk) begin
    \flags.active  <= 0;
    \flags.delayed  <= \flags.active ;
    if (request) \flags.active  <= 1;
  end
  assign now = \flags.active ;
  assign delayed = \flags.delayed ;
endmodule
