module array(input [2:0] idx, output reg [7:0] val);

  reg [7:0] array1 [0:2];
  reg [7:0] array2 [2:0];

initial begin
  array1[0] = 8'haa;
  array1[1] = 8'hbb;
  array1[2] = 8'hcc;
  array2[0] = 8'haa;
  array2[1] = 8'hbb;
  array2[2] = 8'hcc;
end

always @(*) begin
  val = array1[idx] - array2[idx];
end

endmodule

