package matrix_basis;
  parameter logic [3:0] A[4] = '{4'h9, 4'h6, 4'h3, 4'hc};
  parameter logic [3:0] B[3:0] = '{4'h1, 4'h4, 4'h8, 4'h2};
  function automatic logic [3:0] mvm(input logic [3:0] data, input logic [3:0] matrix[4]);
    mvm = 0;
    for (int i=0; i<4; i++) if (data[i]) mvm ^= matrix[i];
  endfunction
endpackage
module const_array_arg(input logic [3:0] data, output logic [7:0] result);
  import matrix_basis::*;
  assign result = {mvm(data, A), mvm(data, B)};
endmodule
