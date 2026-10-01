// Golden for spec_memory_ww_last_wins: nonblocking writes in program order, the
// later one wins on a collision; reads see the committed contents.
module spec_memory_ww_last_wins(input clk, input w1, input [1:0] a1, input [3:0] d1,
                                input w2, input [1:0] a2, input [3:0] d2,
                                input [1:0] ra, output [3:0] qp, output [3:0] qo);
  reg [3:0] p [0:3];
  reg [3:0] o [0:3];
  always @(posedge clk) begin
    if (w1) begin p[a1] <= d1; o[a1] <= d1; end
    if (w2) begin p[a2] <= d2; o[a2] <= d2; end
  end
  assign qp = p[ra];
  assign qo = o[ra];
endmodule
