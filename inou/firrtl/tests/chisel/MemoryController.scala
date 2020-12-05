package design

import chisel3._

class DDR3Command extends Bundle {
  val casN = UInt(1.W)
  val rasN = UInt(1.W)
  val ba   = UInt(3.W)

  // user function to specify defaults (sync reset)
  def defaults() = {
    casN := 1.U
    rasN := 1.U
    ba   := 0.U
  }
}

class MemoryController extends Module {
  val io = IO(new Bundle {
    val ddr3 = Output(new DDR3Command())
  })

  // create a wire of the bundle and set its default values (sync reset)
  val resetDDR3Cmd = Wire(new DDR3Command())
  resetDDR3Cmd.defaults()

  val nextDDR3Cmd = RegInit(new DDR3Command(), resetDDR3Cmd)

  // for example purposes, only update select fields
  when (true.B) {
    nextDDR3Cmd.ba   := 3.U
    nextDDR3Cmd.casN := 0.U
    //nextDDR3Cmd.rasN := 1.U
  }

  io.ddr3 := nextDDR3Cmd
}
