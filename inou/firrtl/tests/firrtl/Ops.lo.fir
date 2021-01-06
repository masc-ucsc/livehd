circuit Ops :
  module Ops :
    input sel : UInt<4>
    input is : SInt<8>
    input iu : UInt<8>
    output os : SInt<14>
    output ou : UInt<13>
    output obool : UInt<1>

    node _GEN_0 = mux(eq(sel, UInt<3>("h5")), add(gt(is, is), gt(iu, iu)), add(lt(is, is), lt(iu, iu)))
    node _GEN_1 = mux(eq(sel, UInt<3>("h4")), add(leq(is, is), leq(iu, iu)), _GEN_0)
    node _GEN_2 = mux(eq(sel, UInt<3>("h5")), add(geq(is, is), geq(iu, iu)), _GEN_1)
    node _GEN_3 = mux(eq(sel, UInt<3>("h4")), add(neq(is, is), neq(iu, iu)), _GEN_2)
    node _GEN_4 = mux(eq(sel, UInt<3>("h5")), add(eq(is, is), eq(iu, iu)), _GEN_3)
    node _GEN_5 = mux(eq(sel, UInt<3>("h4")), add(xor(is, is), xor(iu, iu)), _GEN_4)
    node _GEN_6 = mux(eq(sel, UInt<3>("h5")), add(or(is, is), or(iu, iu)), _GEN_5)
    node _GEN_7 = mux(eq(sel, UInt<3>("h4")), add(and(is, is), and(iu, iu)), _GEN_6)
    node _GEN_8 = mux(eq(sel, UInt<2>("h3")), rem(is, is), SInt<1>("h0"))
    node _GEN_9 = mux(eq(sel, UInt<2>("h3")), rem(iu, iu), _GEN_7)
    node _GEN_10 = mux(eq(sel, UInt<2>("h2")), mux(eq(is, SInt<1>("h0")), SInt<2>("h1"), div(is, is)), _GEN_8)
    node _GEN_11 = mux(eq(sel, UInt<2>("h2")), mux(eq(iu, UInt<1>("h0")), UInt<1>("h1"), div(iu, iu)), _GEN_9)
    node _GEN_12 = mux(eq(sel, UInt<1>("h1")), sub(is, is), _GEN_10)
    node _GEN_13 = mux(eq(sel, UInt<1>("h1")), sub(iu, iu), _GEN_11)
    node _GEN_14 = mux(eq(sel, UInt<1>("h0")), add(is, is), _GEN_12)
    node _GEN_15 = mux(eq(sel, UInt<1>("h0")), add(iu, iu), _GEN_13)
    os <= _GEN_14
    ou <= _GEN_15
    obool <= UInt<1>("h0")