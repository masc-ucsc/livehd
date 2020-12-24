module HiLoMultiplier(
  input        clock,
  input        reset,
  input  [3:0] io_A,
  input  [3:0] io_B,
  output [3:0] io_Hi,
  output [3:0] io_Lo
);
  wire [7:0] mult = io_A * io_B; // @[HiLoMultiplier.scala 14:19]
  assign io_Hi = mult[7:4]; // @[HiLoMultiplier.scala 16:16]
  assign io_Lo = mult[3:0]; // @[HiLoMultiplier.scala 15:16]
endmodule
