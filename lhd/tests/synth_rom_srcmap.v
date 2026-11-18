// A read-only memory with an initializer: memory lowering generates its RTL into a temporary directory, and the
// lowered module's source map points at that generated file.
module synth_rom_srcmap(input clk, input [7:0] a, output reg [15:0] q);
  reg [15:0] rom [0:255];
  integer i;
  initial for (i = 0; i < 256; i = i + 1) rom[i] = (i * 16'd40503) ^ (i << 7) ^ 16'h5a3c;
  always @(posedge clk) q <= rom[a];
endmodule
