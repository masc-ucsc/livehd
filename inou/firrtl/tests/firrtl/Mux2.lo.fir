circuit Mux2 :
  module Mux2 :
    input clock : Clock
    input reset : UInt<1>
    input io_sel : UInt<1>
    input io_in0 : UInt<1>
    input io_in1 : UInt<1>
    output io_out : UInt<1>

    node _io_out_T = and(io_sel, io_in1) @[Mux2.scala 18:21]
    node _io_out_T_1 = not(io_sel) @[Mux2.scala 18:34]
    node _io_out_T_2 = and(_io_out_T_1, io_in0) @[Mux2.scala 18:42]
    node _io_out_T_3 = or(_io_out_T, _io_out_T_2) @[Mux2.scala 18:31]
    io_out <= _io_out_T_3 @[Mux2.scala 18:10]