// See README.md for license details.

package design

import chisel3._

/**
  * Compute GCD using subtraction method.
  * Subtracts the smaller from the larger until register y is zero.
  * value in register x is then the GCD
  */
class SubModule extends Module {
  val io = IO(new Bundle {
    val inp = Input(UInt(4.W))
    val out = Output(UInt(4.W))
  })

  val submodule_inst = Module(new SubModuleSubMod());
  submodule_inst.io.a := io.inp
  io.out := submodule_inst.io.b
}

class SubModuleSubMod extends Module {
  val io = IO(new Bundle {
    val a = Input(UInt(4.W))
    val b = Output(UInt(4.W))
  })

  io.b := io.a + 1.U
}
