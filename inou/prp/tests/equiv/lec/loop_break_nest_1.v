/*
*/
module nest(input [1:0] stop, output reg [3:0] hits);
  integer o, n;
  always @* begin
    hits = 4'd0;
    for (o = 0; o < 2; o = o + 1) begin
      for (n = 0; n < 2; n = n + 1) begin
        hits[o*2 + n] = 1'b1;
        if (stop[o]) break;
      end
    end
  end
endmodule
