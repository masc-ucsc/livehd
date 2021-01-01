circuit Cell :
  module Cell :
    input clock : Clock
    input reset : UInt<1>
    input io_neighbors_0 : UInt<1>
    input io_neighbors_1 : UInt<1>
    input io_neighbors_2 : UInt<1>
    input io_neighbors_3 : UInt<1>
    input io_neighbors_4 : UInt<1>
    input io_neighbors_5 : UInt<1>
    input io_neighbors_6 : UInt<1>
    input io_neighbors_7 : UInt<1>
    output io_out : UInt<1>
    input io_running : UInt<1>
    input io_writeEnable : UInt<1>
    input io_writeValue : UInt<1>

(1) 0u3bits     //before firbits
(2) __fir_const //before firbits


*    reg isAlive : UInt<1>, clock with :
*      reset => (UInt<1>("h0"), isAlive) @[Life.scala 16:24]
*   node _T = eq(io_running, UInt<1>("h0")) @[Life.scala 18:8]
*    node _GEN_0 = mux(io_writeEnable, io_writeValue, isAlive) @[Life.scala 19:26 Life.scala 20:15 Life.scala 23:17]
    node _T_1 = add(io_neighbors_7, UInt<3>("h0")) @[Life.scala 26:81]
    node _T_2 = tail(_T_1, 1) @[Life.scala 26:81]
    node _T_3 = add(io_neighbors_6, _T_2) @[Life.scala 26:81]
    node _T_4 = tail(_T_3, 1) @[Life.scala 26:81]
    node _T_5 = add(io_neighbors_5, _T_4) @[Life.scala 26:81]
    node _T_6 = tail(_T_5, 1) @[Life.scala 26:81]
    node _T_7 = add(io_neighbors_4, _T_6) @[Life.scala 26:81]
    node _T_8 = tail(_T_7, 1) @[Life.scala 26:81]
    node _T_9 = add(io_neighbors_3, _T_8) @[Life.scala 26:81]
    node _T_10 = tail(_T_9, 1) @[Life.scala 26:81]
    node _T_11 = add(io_neighbors_2, _T_10) @[Life.scala 26:81]
    node _T_12 = tail(_T_11, 1) @[Life.scala 26:81]
    node _T_13 = add(io_neighbors_1, _T_12) @[Life.scala 26:81]
    node _T_14 = tail(_T_13, 1) @[Life.scala 26:81]
    node _T_15 = add(io_neighbors_0, _T_14) @[Life.scala 26:81]
    node _T_16 = tail(_T_15, 1) @[Life.scala 26:81]
    node _T_17 = lt(_T_16, UInt<2>("h2")) @[Life.scala 29:18]
    node _T_18 = lt(_T_16, UInt<3>("h4")) @[Life.scala 31:24]
    node _GEN_1 = mux(_T_18, UInt<1>("h1"), UInt<1>("h0")) @[Life.scala 31:31 Life.scala 32:17 Life.scala 34:17]
    node _GEN_2 = mux(_T_17, UInt<1>("h0"), _GEN_1) @[Life.scala 29:25 Life.scala 30:17]
*   node _T_19 = eq(isAlive, UInt<1>("h0")) @[Life.scala 37:12]
    node _T_20 = eq(_T_16, UInt<2>("h3")) @[Life.scala 37:30]
*   node _T_21 = and(_T_19, _T_20) @[Life.scala 37:21]
*    node _GEN_3 = mux(_T_21, UInt<1>("h1"), UInt<1>("h0")) @[Life.scala 37:39 Life.scala 38:17 Life.scala 41:19]
*    node _GEN_4 = mux(isAlive, _GEN_2, _GEN_3) @[Life.scala 28:19]
*    node _GEN_5 = mux(_T, _GEN_0, _GEN_4) @[Life.scala 18:21]
*    io_out <= isAlive @[Life.scala 46:10]
*    isAlive <= mux(reset, UInt<1>("h0"), _GEN_5) @[Life.scala 16:24 Life.scala 16:24]