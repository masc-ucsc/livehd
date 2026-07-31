// A posedge write port and negedge registered read port on a 2x2 memory.
module phase_mem_write_read (
  input  wire       clk,
  input  wire       we,
  input  wire       wa,
  input  wire [1:0] wd,
  input  wire       ra,
  output reg  [1:0] q
);
  reg [1:0] mem [0:1];

  always @(posedge clk)
    if (we) mem[wa] <= wd;

  always @(negedge clk)
    q <= mem[ra];
endmodule
