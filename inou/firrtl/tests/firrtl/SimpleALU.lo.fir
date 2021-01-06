circuit SimpleALU :
  module SimpleALU :
    input clock : Clock
    input reset : UInt<1>
    input io_a : UInt<4>
    input io_b : UInt<4>
    input io_opcode : UInt<2>
    output io_out : UInt<4>

    node _T = eq(io_opcode, UInt<1>("h0")) @[SimpleALU.scala 45:19]
    node _T_1 = add(io_a, io_b) @[SimpleALU.scala 46:20]
    node _T_2 = tail(_T_1, 1) @[SimpleALU.scala 46:20]
    node _T_3 = eq(io_opcode, UInt<1>("h1")) @[SimpleALU.scala 47:26]
    node _T_4 = sub(io_a, io_b) @[SimpleALU.scala 48:20]
    node _T_5 = tail(_T_4, 1) @[SimpleALU.scala 48:20]
    node _T_6 = eq(io_opcode, UInt<2>("h2")) @[SimpleALU.scala 49:26]
    node _GEN_0 = mux(_T_6, io_a, io_b) @[SimpleALU.scala 49:35 SimpleALU.scala 50:12 SimpleALU.scala 52:12]
    node _GEN_1 = mux(_T_3, _T_5, _GEN_0) @[SimpleALU.scala 47:35 SimpleALU.scala 48:12]
    node _GEN_2 = mux(_T, _T_2, _GEN_1) @[SimpleALU.scala 45:28 SimpleALU.scala 46:12]
    io_out <= _GEN_2