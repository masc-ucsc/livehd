circuit Xor6Thread2 :
  module Xor3 :
    input clock : Clock
    input reset : UInt<1>
    input io_ii : UInt<8>
    input io_iivec_0 : UInt<8>
    input io_iivec_1 : UInt<8>
    output io_oo : UInt<8>

    node tmp_vec_0 = io_ii @[Xor6Thread2.scala 9:21 Xor6Thread2.scala 10:14]
    node _tmp_vec_1_T = xor(tmp_vec_0, io_iivec_0) @[Xor6Thread2.scala 13:32]
    node tmp_vec_1 = _tmp_vec_1_T @[Xor6Thread2.scala 9:21 Xor6Thread2.scala 13:16]
    node _tmp_vec_2_T = xor(tmp_vec_1, io_iivec_1) @[Xor6Thread2.scala 13:32]
    node tmp_vec_2 = _tmp_vec_2_T @[Xor6Thread2.scala 9:21 Xor6Thread2.scala 13:16]
    io_oo <= tmp_vec_2 @[Xor6Thread2.scala 16:9]

  module Xor6Thread2 :
    input clock : Clock
    input reset : UInt<1>
    input io_ii : UInt<8>
    input io_iivec_0 : UInt<8>
    input io_iivec_1 : UInt<8>
    output io_oo : UInt<8>

    inst m0 of Xor3 @[Xor6Thread2.scala 25:18]
    inst m1 of Xor3 @[Xor6Thread2.scala 31:18]
    node tmp_wire = m1.io_oo @[Xor6Thread2.scala 36:22 Xor6Thread2.scala 37:12]
    node _io_oo_T = and(tmp_wire, UInt<8>("hff")) @[Xor6Thread2.scala 39:21]
    io_oo <= _io_oo_T @[Xor6Thread2.scala 39:9]
    m0.clock <= clock
    m0.reset <= reset
    m0.io_ii <= io_ii @[Xor6Thread2.scala 26:12]
    m0.io_iivec_0 <= io_iivec_0 @[Xor6Thread2.scala 28:20]
    m0.io_iivec_1 <= io_iivec_1 @[Xor6Thread2.scala 28:20]
    m1.clock <= clock
    m1.reset <= reset
    m1.io_ii <= m0.io_oo @[Xor6Thread2.scala 32:12]
    m1.io_iivec_0 <= io_iivec_0 @[Xor6Thread2.scala 34:20]
    m1.io_iivec_1 <= io_iivec_1 @[Xor6Thread2.scala 34:20]