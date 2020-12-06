
package design

import chisel3._
class Smem extends Module {
  val width: Int = 32
  val io = IO(new Bundle {
    //val enable = Input(Bool())
    //val write = Input(Bool())
    val addr = Input(UInt(10.W))
    //val dataIn = Input(UInt(width.W))
    val dataOut = Output(UInt(width.W))
  })

  val mem = SyncReadMem(1024, UInt(width.W))
  // Create one write port and one read port
  //mem.write(io.addr, io.dataIn)
  io.dataOut := mem.read(io.addr, io.addr > 200.U)
}
