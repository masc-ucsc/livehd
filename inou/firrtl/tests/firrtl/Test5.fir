circuit Test5 :
  module Test5 :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip inp1 : Fixed<32><<16>>, flip inp2 : Fixed<32><<16>>, out : Fixed<32><<20>>, out20 : UInt<4>, out21 : SInt<4>, out22 : SInt<4>, out23 : SInt<8>, out3 : UInt<4>, out4 : UInt<16>}

    node _T = mul(io.inp1, io.inp2) @[Test5.scala 28:21]
    io.out <= _T @[Test5.scala 28:10]
    io.out20 <= UInt<3>("h5") @[Test5.scala 30:12]
    io.out21 <= asSInt(UInt<4>("h5")) @[Test5.scala 31:12]
    io.out22 <= asSInt(UInt<4>("hb")) @[Test5.scala 32:12]
    io.out23 <= asSInt(UInt<6>("h20")) @[Test5.scala 33:12]
    io.out3 <= UInt<4>("ha") @[Test5.scala 35:11]
    io.out4 <= UInt<25>("h123abcd") @[Test5.scala 36:11]