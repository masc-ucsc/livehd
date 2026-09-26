// An ASYNCHRONOUS reset written through concatenated, mixed-direction, signed,
// extended and truncated lvalues. roundtrip_sim: the reset/data VALUES are
// checked by native directed vectors (async_reset_concat_shapes_tb.prp), and
// every register must keep its asynchronous sensitivity list -- one demoted
// block fails :verilog_not_re: (native sim is cycle-based and cannot see a
// reset asserted between clock edges).
// :test: roundtrip_sim
// :verilog_re: always @\(posedge clk or posedge rst
// :verilog_not_re: always @\(posedge clk[[:space:]]*\)
module async_reset_concat_shapes (
  input logic clk,
  input logic rst,
  input logic [79:0] data,
  output logic [79:0] wide,
  output logic signed [3:0] signed_q,
  output logic [2:0] tail,
  output logic [0:7] ascending,
  output logic [7:0] sliced,
  output logic [4:0] extended,
  output logic [2:0] truncated
);
  always @(posedge clk or posedge rst)
    if (rst) begin
      {wide, {signed_q, tail}} <= {80'h8123_4567_89ab_cdef_0123, 4'hd, 3'b101};
      {ascending[0:3], sliced[3:0], ascending[4:7], sliced[7:4]} <= 16'h1234;
      {extended, truncated} <= -1;
    end else begin
      {wide, {signed_q, tail}} <= {data, data[3:0], data[2:0]};
      {ascending[0:3], sliced[3:0], ascending[4:7], sliced[7:4]} <= data[15:0];
      {extended, truncated} <= data[7:0];
    end
endmodule
