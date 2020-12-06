// See README.md for license details.

package design

import chisel3._

/*class Trivial extends Module {
  val io = IO(new Bundle {
    val a = Input (UInt(1.W));
    val b = Input (UInt(1.W));
    val x = Output(UInt(1.W));
  })

  io.x := io.a & io.b
}*/

class Trivial extends Module {
  val io = IO(new Bundle {
    val outp = Output(UInt(4.W))
  })

  io.outp := 5.U
}
