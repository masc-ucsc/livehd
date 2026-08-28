// Golden for array_reg_explicit_reset_pin.prp: a 1-entry memory with a
// SYNCHRONOUS reset on its own `rst` port, which the write is gated behind.
module array_reg_explicit_reset_pin(
    input        clk,
    input        rst,
    input        we,
    input  [7:0] d,
    output [7:0] q
);
  reg [7:0] mem [0:0];
  assign q = mem[0];
  always @(posedge clk) begin
    if (rst) begin
      mem[0] <= 8'h0;
    end else if (we) begin
      mem[0] <= d;
    end
  end
endmodule
