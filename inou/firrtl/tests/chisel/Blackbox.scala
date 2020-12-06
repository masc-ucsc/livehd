// See README.md for license details.

package design

import chisel3._
import chisel3.util._
import chisel3.experimental._


class Blackbox extends Module {
  val io = IO(new Bundle {
    val val0        = Input(UInt(33.W))
    val val1        = Input(UInt(32.W))
    val val2        = Input(UInt(32.W))
    val valOut      = Output(UInt(33.W))
  })

  val submodule_inst = Module(new BlackboxInline());
  submodule_inst.io.in1 := io.val1
  submodule_inst.io.in2 := io.val2
  io.valOut := submodule_inst.io.out ^ io.val0
}

class BlackboxInline extends BlackBox with HasBlackBoxInline {
  val io = IO(new Bundle() {
    val in1 = Input(UInt(32.W))
    val in2 = Input(UInt(32.W))
    val out = Output(UInt(33.W))
  })
  setInline("BlackboxInline.v",
    s"""
      |module BlackboxInline(
      |    input  [31:0] in1,
      |    input  [31:0] in2,
      |    output reg [32:0] out
      |);
      |always @* begin
      |  out = in1 + in2;
      |end
      |endmodule
    """.stripMargin)
}
