module flatten_bundle(
   input signed [2:0] in1
  ,input [4:0] in2
  ,input signed [6:0] in3
  ,output reg signed [42:0] out
);

  // assign out = {10'sh100, in3, {1'h0, in2}, 17'shff00, in1};
  assign out = {10'sh100, in3, {1'h0, in2}, 17'shff00, in1};

endmodule
