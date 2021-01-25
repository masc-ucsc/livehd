circuit Xor6Thread2 :
  module Xor3 :
    input clock : Clock
    input reset : UInt<1>
    input io_inp : UInt<8>
    input io_inp_vec_0 : UInt<8>
    input io_inp_vec_1 : UInt<8>
    output io_out : UInt<8>

    node tmp_vec_0 = io_inp @[Xor6Thread2.scala 9:21 Xor6Thread2.scala 10:14]
    node _tmp_vec_1_T = xor(tmp_vec_0, io_inp_vec_0) @[Xor6Thread2.scala 13:32]
    node tmp_vec_1 = _tmp_vec_1_T @[Xor6Thread2.scala 9:21 Xor6Thread2.scala 13:16]
    node _tmp_vec_2_T = xor(tmp_vec_1, io_inp_vec_1) @[Xor6Thread2.scala 13:32]
    node tmp_vec_2 = _tmp_vec_2_T @[Xor6Thread2.scala 9:21 Xor6Thread2.scala 13:16]
    io_out <= tmp_vec_2 @[Xor6Thread2.scala 16:10]

  module Xor6Thread2 :
    input clock : Clock
    input reset : UInt<1>
    input io_inp : UInt<8>
    input io_inp_vec_0 : UInt<8>
    input io_inp_vec_1 : UInt<8>
    output io_out : UInt<8>

    inst m0 of Xor3 @[Xor6Thread2.scala 25:18]
    inst m1 of Xor3 @[Xor6Thread2.scala 31:18]
    io_out <= m1.io_out @[Xor6Thread2.scala 36:10]
    m0.clock <= clock
    m0.reset <= reset
    m0.io_inp <= io_inp @[Xor6Thread2.scala 26:13]
    m0.io_inp_vec_0 <= io_inp_vec_0 @[Xor6Thread2.scala 28:22]
    m0.io_inp_vec_1 <= io_inp_vec_1 @[Xor6Thread2.scala 28:22]
    m1.clock <= clock
    m1.reset <= reset
    m1.io_inp <= m0.io_out @[Xor6Thread2.scala 32:13]
    m1.io_inp_vec_0 <= io_inp_vec_0 @[Xor6Thread2.scala 34:22]
    m1.io_inp_vec_1 <= io_inp_vec_1 @[Xor6Thread2.scala 34:22]