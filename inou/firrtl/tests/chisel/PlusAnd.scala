// See README.md for license details.

package design

import chisel3._

class PlusAnd extends Module {
  val io = IO(new Bundle {
    val vecI          = Input(Vec(5, SInt(23.W)))
    val bund          = Output( new Bundle {
      val vecO        = Output(Vec(5, SInt(23.W)))
    })
  })

  io.bund.vecO := io.vecI;
}
