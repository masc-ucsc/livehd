module subarray_port(input logic [15:0] data, input logic row, output logic [3:0] result);
  wire [3:0] rows[1:0][2:3];
  assign rows[0][2] = data[3:0];
  assign rows[0][3] = data[7:4];
  assign rows[1][2] = data[11:8];
  assign rows[1][3] = data[15:12];
  subarray_port_leaf leaf(.items(rows[row]), .result(result));
endmodule
module subarray_port_leaf(input logic [3:0] items[-2:-1], output logic [3:0] result);
  assign result = items[-2] ^ {items[-1][2:0], items[-1][3]};
endmodule
