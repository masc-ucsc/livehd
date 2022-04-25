circuit FlipSimple : 
  module FlipSimple :
    input clock : Clock
    input reset : UInt<1>
    output auto : {flip foo : {bar : UInt<4>, flip baz : UInt<5>}}

    clock is invalid
    reset is invalid
    auto is invalid

    wire _T : {bar : UInt<4>, flip baz : UInt<5>}
    _T is invalid

    _T <- auto.foo
    node _T_1 = add(_T.bar, UInt<4>("h0d"))
    _T.baz <= _T_1

