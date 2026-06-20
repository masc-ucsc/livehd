// Golden for 2i-issues D (corner): tuple output leaves written in if-arms.
//   c ? (first=1, second=2) : (first=3, second=4)
module \tup_out_field_arms.top (
   input        c
  ,output reg [7:0] \p.first
  ,output reg [7:0] \p.second
);
  always @* begin
    if (c) begin
      \p.first  = 8'd1;
      \p.second  = 8'd2;
    end else begin
      \p.first  = 8'd3;
      \p.second  = 8'd4;
    end
  end
endmodule
