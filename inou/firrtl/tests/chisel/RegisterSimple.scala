// See README.md for license details.

package pb_tests

import chisel3._

class RegisterSimple extends Module {
  val io = IO(new Bundle {
    val inVal         = Input(UInt(16.W))
    val outVal        = Output(UInt(16.W))
  })

  val x  = Reg(UInt())

  when (x === 0.U) {
    x := io.inVal;
  } .otherwise {
    x := x - 1.U
  }

  io.outVal := x
}
