// See README.md for license details.

package design

import chisel3._
import chisel3.core.FixedPoint

/**
  * The point of Test5 is to test the following functionalities:
  * - Types/Expressions/Operations involving:
  *   - FixedPoint
  */
class Test5 extends Module {
  val io = IO(new Bundle {
    val inp1 = Input(FixedPoint(32.W, 16.BP))
    val inp2 = Input(FixedPoint(32.W, 16.BP))
    val out  = Output(FixedPoint(32.W, 20.BP))

    val out20 = Output(UInt(4.W))
    val out21 = Output(SInt(4.W))
    val out22 = Output(SInt(4.W))
    val out23 = Output(SInt(8.W))

    val out3 = Output(UInt(4.W))
    val out4 = Output(UInt(16.W))
  })

  io.out := io.inp1 * io.inp2

  io.out20 := 5.U
  io.out21 := 5.S
  io.out22 := -5.S
  io.out23 := -32.S

  io.out3 := "b1010".U
  io.out4 := "h_0123_abcd".U
}
