// See README.md for license details.

package design

import chisel3._

/**
  * The point of SimpleBitOps is to test the following functionalities:
  * - Operations:
  *   - And/Xor/Or
  *   - And_/Xor_/Or_Reduce
  *   - Not
  *   - Shift Left/Right
  */
class SimpleBitOps extends Module {
  val io = IO(new Bundle {
    val inp1           = Input (Bits(4.W))
    val inp2           = Input (Bits(4.W))

    val out_and        = Output(Bits(4.W))
    val out_andr       = Output(Bits(1.W))

    val out_xor        = Output(Bits(4.W))
    val out_xorr       = Output(Bits(1.W))

    val out_or         = Output(Bits(4.W))
    val out_orr        = Output(Bits(1.W))

    val out_not        = Output(Bits(4.W))
  })

  io.out_and := io.inp1 & io.inp2
  io.out_xor := io.inp1 ^ io.inp2
  io.out_or  := io.inp1 | io.inp2

  io.out_andr := io.inp1.andR
  io.out_xorr := io.inp1.xorR
  io.out_orr  := io.inp1.orR

  io.out_not := ~io.inp1
}
