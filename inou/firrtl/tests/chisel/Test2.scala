// See README.md for license details.

package design

import chisel3._

/**
  * The point of Test2 is to test the following functionalities:
  * - Primitive Operations:
  *   - Head
  *   - Tail
  *
  *   - Use of multiple arithmetic operators in one statement (+ and *).
  *
  *   - Extract Bits (Simple)
  *   - Extract Bits (Wire Intermediary, implicit bw)
  *   - Extract Bits (Wire Intermediary, explicit bw)
  *   - Pad
  *
  *   - Concat
  *
  *   - And
  *   - And Reduce
  */
class Test2 extends Module {
  val io = IO(new Bundle {
    val in_val            = Input(Bits(5.W))

    val out_head          = Output(UInt(4.W))
    val out_extractS      = Output(UInt(3.W))
    val out_extractI      = Output(UInt(3.W))
    val out_extractE      = Output(UInt(3.W))

    //val out_tail          = Output(Uint(3.W))
  })

  //Head
  io.out_head := io.in_val.head(4)

  //Extract Bits
  io.out_extractS := io.in_val(3,1)

  val eI = Wire(UInt())
  eI := io.in_val(3,1)
  io.out_extractI := eI

  val eE = Wire(UInt(3.W))
  eE := io.in_val(3,1)
  io.out_extractE := eE


  /*val bw_no   = Wire(UInt())
  val bw_yes  = Wire(UInt(4.W))
  bw_no := io.in_head.head(4)
  bw_yes := io.in_head.head(4)
  io.out_headWireNoBW := bw_no
  io.out_headWireBW   := bw_yes*/
}
