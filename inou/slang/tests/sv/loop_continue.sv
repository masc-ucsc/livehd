// :test: error
// :error: [Cc]ontinue
module loop_continue(input [3:0] req, output reg [3:0] o);
  integer i;
  always @* begin
    o = 4'd0;
    for (i = 0; i < 4; i = i + 1) begin
      if (!req[i]) continue;
      o[i] = 1'b1;
    end
  end
endmodule
