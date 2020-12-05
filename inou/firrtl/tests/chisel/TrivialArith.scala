// See README.md for license details.

package design

import chisel3._

class TrivialArith extends Module {
  val io = IO(new Bundle {
    val value1        = Input(UInt(8.W))
    val value2        = Input(UInt(8.W))
    val value3        = Input(UInt(8.W))
    val outputAdd     = Output(UInt(10.W))
    val outputMul     = Output(UInt(16.W))
  })

  io.outputAdd := io.value1 +& io.value2 +& io.value3
  io.outputMul := io.value1 * io.value2
}
