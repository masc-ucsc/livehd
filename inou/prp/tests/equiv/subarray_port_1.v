// Three dimensions, nonzero bounds, and a descending surviving dimension.
module subarray_port(input logic [15:0] data, input logic row, output logic [3:0] result);
  wire [3:0] rows[3:2][2:3][1:0];
  assign rows[2] = '{default: '{default: 4'h0}};
  assign rows[3][2][1] = data[3:0];
  assign rows[3][2][0] = data[7:4];
  assign rows[3][3][1] = data[11:8];
  assign rows[3][3][0] = data[15:12];
  subarray_port_leaf leaf(.items(rows[3][2+row]), .result(result));
endmodule
module subarray_port_leaf(input logic [3:0] items[-2:-1], output logic [3:0] result);
  assign result = items[-2] ^ {items[-1][2:0], items[-1][3]};
endmodule
