// See README.md for license details.

package design

import chisel3._

/**
  * The point of Test1 is to test the following functionalities:
  * - Expressions:
  *   - Mux
  *   - SubIndex
  *   - SubAccess
  */
class Test1 extends Module {
  val io = IO(new Bundle {
    val mux_value1        = Input(UInt(16.W))
    val mux_value2        = Input(UInt(16.W))
    val mux_value3        = Input(UInt(16.W))
    val mux_sel1          = Input(Bool())
    val mux_sel2          = Input(Bool())

    val vec_set           = Input(Bool())
    val vec_subAcc        = Input(UInt(2.W))
    val vec               = Input(Vec(4, UInt(16.W)))

    val mux_out           = Output(UInt(16.W))
    val vec_subInd_out    = Output(UInt(16.W))
    val vec_subAcc_out    = Output(UInt(16.W))
  })

  io.mux_out := Mux(io.mux_sel1, io.mux_value1, Mux(io.mux_sel2, io.mux_value2, io.mux_value3))

  when (io.vec_set) {
    io.vec_subInd_out := io.vec(1)
    io.vec_subAcc_out := io.vec(io.vec_subAcc)
  } .otherwise {
    io.vec_subInd_out := 0.U
    io.vec_subAcc_out := 0.U
  }

}
