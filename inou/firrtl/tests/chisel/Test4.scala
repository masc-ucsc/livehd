// See README.md for license details.

package design

import chisel3._

/**
  * The point of Test4 is to test the following functionalities:
  * - Operations:
  *   - as_x
  */
class Test4 extends Module {
  val io = IO(new Bundle {
    val inp        = Input (UInt(16.W))
    val out        = Output(SInt(16.W))
  })

  io.out := io.inp.asSInt
}
