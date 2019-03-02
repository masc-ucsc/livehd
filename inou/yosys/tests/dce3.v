module dce3(input clk, input b, input a, output d);
  reg [1:0] array1;

  always @(posedge clk) begin
    d <= array1[a]^b;
    array1[a] <= a^b;
  end

endmodule

