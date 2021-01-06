circuit SingleEvenFilter :
  module PredicateFilter :
    input clock : Clock
    input reset : UInt<1>
    input io_in_valid : UInt<1>
    input io_in_bits : UInt<16>
    output io_out_valid : UInt<1>
    output io_out_bits : UInt<16>

    node _T = leq(io_in_bits, UInt<4>("h9")) @[SingleEvenFilter.scala 30:51]
    node _T_1 = and(io_in_valid, _T) @[SingleEvenFilter.scala 24:31]
    io_out_valid <= _T_1 @[SingleEvenFilter.scala 24:16]
    io_out_bits <= io_in_bits @[SingleEvenFilter.scala 25:16]

  module PredicateFilter_1 :
    input clock : Clock
    input reset : UInt<1>
    input io_in_valid : UInt<1>
    input io_in_bits : UInt<16>
    output io_out_valid : UInt<1>
    output io_out_bits : UInt<16>

    node _T = bits(io_in_bits, 0, 0) @[SingleEvenFilter.scala 35:50]
    node _T_1 = bits(_T, 0, 0) @[SingleEvenFilter.scala 35:54]
    node _T_2 = and(io_in_valid, _T_1) @[SingleEvenFilter.scala 24:31]
    io_out_valid <= _T_2 @[SingleEvenFilter.scala 24:16]
    io_out_bits <= io_in_bits @[SingleEvenFilter.scala 25:16]

  module SingleEvenFilter :
    input clock : Clock
    input reset : UInt<1>
    input io_in_valid : UInt<1>
    input io_in_bits : UInt<16>
    output io_out_valid : UInt<1>
    output io_out_bits : UInt<16>

    inst single of PredicateFilter @[SingleEvenFilter.scala 30:11]
    inst even of PredicateFilter_1 @[SingleEvenFilter.scala 35:11]
    io_out_valid <= even.io_out_valid @[SingleEvenFilter.scala 43:17]
    io_out_bits <= even.io_out_bits @[SingleEvenFilter.scala 43:17]
    single.clock <= clock
    single.reset <= reset
    single.io_in_valid <= io_in_valid @[SingleEvenFilter.scala 41:17]
    single.io_in_bits <= io_in_bits @[SingleEvenFilter.scala 41:17]
    even.clock <= clock
    even.reset <= reset
    even.io_in_valid <= single.io_out_valid @[SingleEvenFilter.scala 42:17]
    even.io_in_bits <= single.io_out_bits @[SingleEvenFilter.scala 42:17]