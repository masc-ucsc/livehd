// See README.md for license details.

package design

import chisel3._

/* Tests having a submodule and using
 * the bulk connect syntax (<>). */
class BundleConnect extends Module {
  val io = IO(new Bundle {
    val in1 = Input(UInt(4.W))
    val in2 = Input(UInt(4.W))
    val out = Output(UInt(4.W))
  })

  val submodule_inst = Module(new BundleConnectSubMod());
  io <> submodule_inst.io
}

class BundleConnectSubMod extends Module {
  val io = IO(new Bundle {
    val in1 = Input(UInt(4.W))
    val in2 = Input(UInt(4.W))
    val out = Output(UInt(4.W))
  })

  io.out := io.in1 + io.in2
}
