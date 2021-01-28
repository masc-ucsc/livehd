circuit XorSelfThread1 :
  module XorSelf :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip ii : UInt<8>, flip iivec : UInt<8>[2], oo : UInt<8>}

    wire tmp_vec : UInt<8>[3] @[XorSelfThread1.scala 9:21]
    tmp_vec[0] <= io.ii @[XorSelfThread1.scala 10:14]
    node _tmp_vec_1_T = xor(tmp_vec[0], io.iivec[0]) @[XorSelfThread1.scala 13:32]
    tmp_vec[1] <= _tmp_vec_1_T @[XorSelfThread1.scala 13:16]
    node _tmp_vec_2_T = xor(tmp_vec[1], io.iivec[1]) @[XorSelfThread1.scala 13:32]
    tmp_vec[2] <= _tmp_vec_2_T @[XorSelfThread1.scala 13:16]
    io.oo <= tmp_vec[1] @[XorSelfThread1.scala 16:9]

  module XorSelfThread1 :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip ii : UInt<8>, flip iivec : UInt<8>[2], oo : UInt<8>}

    inst m0 of XorSelf @[XorSelfThread1.scala 25:18]
    m0.clock <= clock
    m0.reset <= reset
    m0.io.ii <= io.ii @[XorSelfThread1.scala 26:12]
    m0.io.iivec[0] <= io.iivec[0] @[XorSelfThread1.scala 27:15]
    m0.io.iivec[1] <= io.iivec[1] @[XorSelfThread1.scala 27:15]
    wire tmp_wire : UInt<8> @[XorSelfThread1.scala 28:22]
    tmp_wire <= m0.io.oo @[XorSelfThread1.scala 30:12]
    node _io_oo_T = and(tmp_wire, UInt<8>("hff")) @[XorSelfThread1.scala 32:21]
    io.oo <= _io_oo_T @[XorSelfThread1.scala 32:9]