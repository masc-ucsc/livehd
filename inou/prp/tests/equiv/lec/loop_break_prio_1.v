/*
*/
module prio(input [3:0] req, output reg [1:0] sel, output reg gnt);
  integer i;
  always @* begin
    sel = 2'd0; gnt = 1'b0;
    for (i = 0; i < 4; i = i + 1) begin
      sel = i[1:0];
      gnt = 1'b1;
      if (req[i]) break;
    end
  end
endmodule
