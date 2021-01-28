circuit Xor6Thread2 :
  module Xor3 :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip ii : UInt<8>, flip iivec : UInt<8>[2], oo : UInt<8>}

    wire tmp_vec : UInt<8>[3] @[Xor6Thread2.scala 9:21]
    tmp_vec[0] <= io.ii @[Xor6Thread2.scala 10:14]
    node _tmp_vec_1_T = xor(tmp_vec[0], io.iivec[0]) @[Xor6Thread2.scala 13:32]
    tmp_vec[1] <= _tmp_vec_1_T @[Xor6Thread2.scala 13:16]
    node _tmp_vec_2_T = xor(tmp_vec[1], io.iivec[1]) @[Xor6Thread2.scala 13:32]
    tmp_vec[2] <= _tmp_vec_2_T @[Xor6Thread2.scala 13:16]
    io.oo <= tmp_vec[2] @[Xor6Thread2.scala 16:9]

  module Xor6Thread2 :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip ii : UInt<8>, flip iivec : UInt<8>[2], oo : UInt<8>}

    inst m0 of Xor3 @[Xor6Thread2.scala 25:18]
    m0.clock <= clock
    m0.reset <= reset
    m0.io.ii <= io.ii @[Xor6Thread2.scala 26:12]
    m0.io.iivec[0] <= io.iivec[0] @[Xor6Thread2.scala 28:20]
    m0.io.iivec[1] <= io.iivec[1] @[Xor6Thread2.scala 28:20]
    inst m1 of Xor3 @[Xor6Thread2.scala 31:18]
    m1.clock <= clock
    m1.reset <= reset
    m1.io.ii <= m0.io.oo @[Xor6Thread2.scala 32:12]
    m1.io.iivec[0] <= io.iivec[0] @[Xor6Thread2.scala 34:20]
    m1.io.iivec[1] <= io.iivec[1] @[Xor6Thread2.scala 34:20]
    wire tmp_wire : UInt<8> @[Xor6Thread2.scala 36:22]
    tmp_wire <= m1.io.oo @[Xor6Thread2.scala 37:12]
    node _io_oo_T = and(tmp_wire, UInt<8>("hff")) @[Xor6Thread2.scala 39:21]
    io.oo <= _io_oo_T @[Xor6Thread2.scala 39:9]