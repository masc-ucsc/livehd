// See README.md for license details.

package design

import chisel3._

/**
  * Compute GCD using subtraction method.
  * Subtracts the smaller from the larger until register y is zero.
  * value in register x is then the GCD
  */
class Register extends Module {
  val io = IO(new Bundle {
    val inVal         = Input(UInt(16.W))
    val loadingValues = Input(Bool())
    val outVal        = Output(UInt(16.W))
  })

  val x  = Reg(UInt())

  when (io.loadingValues) {
    x := io.inVal;
  } .otherwise {
    when (x > 0.U) {
      x := x - 1.U
    }
  }

  io.outVal := x
}
