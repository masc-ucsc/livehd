// See README.md for license details.

package design

import chisel3._

/**
  * The point of Test6 is to test the following functionalities:
  */
class Test6 extends Module {
  val io = IO(new Bundle {
    val in   = Input (Vec(20, UInt(16.W)))
    val addr = Input (UInt(5.W))
    val out  = Output(UInt(16.W))
  })

  io.out := io.in(io.addr)
}
