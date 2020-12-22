circuit Counter :
  module Counter :
    input clock : Clock
    input reset : UInt<1>
    input io_inc : UInt<1>
    input io_amt : UInt<4>
    output io_tot : UInt<8>

    reg _T : UInt<8>, clock with :
      reset => (UInt<1>("h0"), _T) @[Counter.scala 18:20]
    node _T_1 = add(_T, io_amt) @[Counter.scala 19:35]
    node _T_2 = tail(_T_1, 1) @[Counter.scala 19:35]
    node _T_3 = gt(_T_2, UInt<8>("hff")) @[Counter.scala 15:11]
    node _T_4 = mux(_T_3, UInt<1>("h0"), _T_2) @[Counter.scala 15:8]
    node _GEN_0 = mux(io_inc, _T_4, _T) @[Counter.scala 19:15 Counter.scala 19:19 Counter.scala 18:20]
    io_tot <= _T @[Counter.scala 32:10]
    _T <= mux(reset, UInt<8>("h0"), _GEN_0) @[Counter.scala 18:20 Counter.scala 18:20]