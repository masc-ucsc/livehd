module dce2(input clk, input a, input b, output d);
reg array1;
reg array2;

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

