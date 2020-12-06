circuit Test5 :
  module Test5 :
    input clock : Clock
    input reset : UInt<1>
    input io_inp1 : SInt<32>
    input io_inp2 : SInt<32>
    output io_out : SInt<32>
    output io_out20 : UInt<4>
    output io_out21 : SInt<4>
    output io_out22 : SInt<4>
    output io_out23 : SInt<8>
    output io_out3 : UInt<4>
    output io_out4 : UInt<16>

    node _io_out_T = mul(io_inp1, io_inp2) @[Test5.scala 28:21]
    io_out <= asSInt(bits(shr(_io_out_T, 12), 31, 0)) @[Test5.scala 28:10]
    io_out20 <= UInt<3>("h5") @[Test5.scala 30:12]
    io_out21 <= asSInt(UInt<4>("h5")) @[Test5.scala 31:12]
    io_out22 <= asSInt(UInt<4>("hb")) @[Test5.scala 32:12]
    io_out23 <= asSInt(UInt<6>("h20")) @[Test5.scala 33:12]
    io_out3 <= UInt<4>("ha") @[Test5.scala 35:11]
    io_out4 <= UInt<16>("habcd") @[Test5.scala 36:11]