circuit SimpleALU :
  module SimpleALU :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip a : UInt<4>, flip b : UInt<4>, flip opcode : UInt<2>, out : UInt<4>}

    io.out <= UInt<1>("h0") @[SimpleALU.scala 44:10]
    node _T = eq(io.opcode, UInt<1>("h0")) @[SimpleALU.scala 45:19]
    when _T : @[SimpleALU.scala 45:28]
      node _T_1 = add(io.a, io.b) @[SimpleALU.scala 46:20]
      node _T_2 = tail(_T_1, 1) @[SimpleALU.scala 46:20]
      io.out <= _T_2 @[SimpleALU.scala 46:12]
    else :
      node _T_3 = eq(io.opcode, UInt<1>("h1")) @[SimpleALU.scala 47:26]
      when _T_3 : @[SimpleALU.scala 47:35]
        node _T_4 = sub(io.a, io.b) @[SimpleALU.scala 48:20]
        node _T_5 = tail(_T_4, 1) @[SimpleALU.scala 48:20]
        io.out <= _T_5 @[SimpleALU.scala 48:12]
      else :
        node _T_6 = eq(io.opcode, UInt<2>("h2")) @[SimpleALU.scala 49:26]
        when _T_6 : @[SimpleALU.scala 49:35]
          io.out <= io.a @[SimpleALU.scala 50:12]
        else :
          io.out <= io.b @[SimpleALU.scala 52:12]