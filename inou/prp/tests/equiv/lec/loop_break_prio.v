/*
:lec_top: prio
*/
module prio(input [3:0] req, output reg [1:0] sel, output reg gnt);
  always @* begin
    gnt = 1'b1;
    if      (req[0]) sel = 2'd0;
    else if (req[1]) sel = 2'd1;
    else if (req[2]) sel = 2'd2;
    else             sel = 2'd3;
  end
endmodule
