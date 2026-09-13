module top(input [7:0] a, output reg [7:0] y);
  localparam STEP = 0;
  integer i;
  always @* begin
    y = a;
    if (STEP != 0) begin
      for (i = 0; i < 8; i = i + STEP)
        y[i] = ~a[i];
    end
  end
endmodule
