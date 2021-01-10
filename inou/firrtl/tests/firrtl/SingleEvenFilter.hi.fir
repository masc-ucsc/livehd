circuit SingleEvenFilter :
  module PredicateFilter :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip in : { valid : UInt<1>, bits : UInt<16>}, out : { valid : UInt<1>, bits : UInt<16>}}

    node _T = leq(io.in.bits, UInt<4>("h9")) @[SingleEvenFilter.scala 30:51]
    node _T_1 = and(io.in.valid, _T) @[SingleEvenFilter.scala 24:31]
    io.out.valid <= _T_1 @[SingleEvenFilter.scala 24:16]
    io.out.bits <= io.in.bits @[SingleEvenFilter.scala 25:16]

  module PredicateFilter_1 :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip in : { valid : UInt<1>, bits : UInt<16>}, out : { valid : UInt<1>, bits : UInt<16>}}

    node _T = bits(io.in.bits, 0, 0) @[SingleEvenFilter.scala 35:50]
    node _T_1 = bits(_T, 0, 0) @[SingleEvenFilter.scala 35:54]
    node _T_2 = and(io.in.valid, _T_1) @[SingleEvenFilter.scala 24:31]
    io.out.valid <= _T_2 @[SingleEvenFilter.scala 24:16]
    io.out.bits <= io.in.bits @[SingleEvenFilter.scala 25:16]

  module SingleEvenFilter :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip in : { valid : UInt<1>, bits : UInt<16>}, out : { valid : UInt<1>, bits : UInt<16>}}

    inst single of PredicateFilter @[SingleEvenFilter.scala 30:11]
    single.clock <= clock
    single.reset <= reset
    inst even of PredicateFilter_1 @[SingleEvenFilter.scala 35:11]
    even.clock <= clock
    even.reset <= reset
    single.io.in.bits <= io.in.bits @[SingleEvenFilter.scala 41:17]
    single.io.in.valid <= io.in.valid @[SingleEvenFilter.scala 41:17]
    even.io.in.bits <= single.io.out.bits @[SingleEvenFilter.scala 42:17]
    even.io.in.valid <= single.io.out.valid @[SingleEvenFilter.scala 42:17]
    io.out.bits <= even.io.out.bits @[SingleEvenFilter.scala 43:17]
    io.out.valid <= even.io.out.valid @[SingleEvenFilter.scala 43:17]