/*
:lec_top: nest
*/
module nest(input [1:0] stop, output reg [3:0] hits);
  always @* begin
    hits    = 4'd0;
    hits[0] = 1'b1;
    hits[1] = ~stop[0];
    hits[2] = 1'b1;
    hits[3] = ~stop[1];
  end
endmodule
