// FIXME tracker -- a MEMORY write is not gated by the async reset (LEC-refuted).
// In `always @(posedge clk or posedge rst) if (rst) <resets> else <body>`, a clock
// edge taken while `rst` is asserted runs the RESET branch, so the else-body -- and
// the `m[a] <= din` in it -- does NOT execute. The reset structure is absorbed into
// the flops' `reset_pin=/async=true` attributes, but the memory has no reset
// attribute and simply LOSES the implicit `~rst` on its write enable: look at the
// Pyrope below, `if we != 0 { m[a] = din }` with no `rst` anywhere. The array is
// therefore written while the design is held in reset. Two corpus shapes (a scalar
// reset next to a memory write, and a counter next to a memory write).
module mem_write_during_reset (
  input            clk,
  input            rst,
  input            we,
  input      [1:0] a,
  input      [3:0] din,
  output reg [3:0] ack,
  output     [3:0] dout
);
  reg [3:0] m [0:3];

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      ack <= 4'b0;
    end else begin
      if (we) m[a] <= din;
      ack <= din;
    end
  end

  assign dout = m[a];
endmodule
