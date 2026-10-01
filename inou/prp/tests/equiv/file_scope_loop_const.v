// Golden for file_scope_loop_const.prp: INIT = 0x55 (even bits),
// FILE_INIT = 0x0F, FILE_XOR = 0x30.
module file_scope_loop_const (
  input        clock,
  input        rst,
  input        en,
  input  [7:0] d,
  output [7:0] q,
  output [7:0] p,
  output [7:0] v
);
  reg [7:0] r;
  reg [7:0] s;
  always @(posedge clock) begin
    if (rst) begin
      r <= 8'h55;
      s <= 8'h0F;
    end else if (en) begin
      r <= d;
      s <= d;
    end
  end
  assign q = r;
  assign p = s;
  assign v = d ^ 8'h30;
endmodule
