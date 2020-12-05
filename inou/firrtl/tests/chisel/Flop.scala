// See README.md for license details.

package design

import chisel3._

class Flop extends Module {
  val io = IO(new Bundle {
    val inp     = Input(UInt(16.W))
    val loading = Input(Bool())
    val out     = Output(UInt(16.W))
  })

  val x  = Reg(UInt(16.W))

  when(io.loading) {
    x := io.inp
  }

  io.out := x
}
