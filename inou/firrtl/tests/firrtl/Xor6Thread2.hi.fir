circuit Xor6Thread2 :
  module Xor3 :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip inp : UInt<8>, flip inp_vec : UInt<8>[2], out : UInt<8>}

    wire tmp_vec : UInt<8>[3] @[Xor6Thread2.scala 9:21]
    tmp_vec[0] <= io.inp @[Xor6Thread2.scala 10:14]
    node _tmp_vec_1_T = xor(tmp_vec[0], io.inp_vec[0]) @[Xor6Thread2.scala 13:32]
    tmp_vec[1] <= _tmp_vec_1_T @[Xor6Thread2.scala 13:16]
    node _tmp_vec_2_T = xor(tmp_vec[1], io.inp_vec[1]) @[Xor6Thread2.scala 13:32]
    tmp_vec[2] <= _tmp_vec_2_T @[Xor6Thread2.scala 13:16]
    io.out <= tmp_vec[2] @[Xor6Thread2.scala 16:10]

  module Xor6Thread2 :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip inp : UInt<8>, flip inp_vec : UInt<8>[2], out : UInt<8>}

    inst m0 of Xor3 @[Xor6Thread2.scala 25:18]
    m0.clock <= clock
    m0.reset <= reset
    m0.io.inp <= io.inp @[Xor6Thread2.scala 26:13]
    m0.io.inp_vec[0] <= io.inp_vec[0] @[Xor6Thread2.scala 28:22]
    m0.io.inp_vec[1] <= io.inp_vec[1] @[Xor6Thread2.scala 28:22]
    inst m1 of Xor3 @[Xor6Thread2.scala 31:18]
    m1.clock <= clock
    m1.reset <= reset
    m1.io.inp <= m0.io.out @[Xor6Thread2.scala 32:13]
    m1.io.inp_vec[0] <= io.inp_vec[0] @[Xor6Thread2.scala 34:22]
    m1.io.inp_vec[1] <= io.inp_vec[1] @[Xor6Thread2.scala 34:22]
    io.out <= m1.io.out @[Xor6Thread2.scala 36:10]