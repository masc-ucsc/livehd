module setmask(input [15:0] a, input [7:0] b, input [3:0] c, input s, output reg [15:0] y, output reg [15:0] z);
  always @* begin
    y = a;
    y[11:4] = b;
    if (s) y[3:0] = c;
    z = 16'h0;
    z[7:0] = b ^ a[7:0];
    z[15:12] = c;
  end
endmodule
