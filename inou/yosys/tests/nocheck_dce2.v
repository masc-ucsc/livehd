module nocheck_dce2(input clk, input a, input b, output d);
reg [1:0] array1;
reg [1:0] array2;

reg c;
always @(*) begin
  c = a ^b;
end

always @(posedge clk) begin
  array1[c] <= array1[c]^a;
  array2[c] <= array2[c]^a;
  d <= array1[~c];
end
endmodule

