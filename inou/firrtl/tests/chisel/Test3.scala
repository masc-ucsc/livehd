// See README.md for license details.

package design

import chisel3._

/**
  * The point of Test3 is to test the following functionalities:
  * - Operations:
  *   - Pad
  */
class Test3 extends Module {
  val io = IO(new Bundle {
    val inp        = Input (Bits(12.W))
    val out_pad    = Output(Bits(16.W))
  })

  io.out_pad := io.inp.pad(16)
}
