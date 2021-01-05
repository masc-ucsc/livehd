circuit Cell_alone :
  module Cell_alone :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip neighbors : UInt<1>[8], out : UInt<1>, flip running : UInt<1>, flip writeEnable : UInt<1>, flip writeValue : UInt<1>}

    reg isAlive : UInt<1>, clock with :
      reset => (reset, UInt<1>("h0")) @[Life.scala 16:24]
    node _T = eq(io.running, UInt<1>("h0")) @[Life.scala 18:8]
    when _T : @[Life.scala 18:21]
      when io.writeEnable : @[Life.scala 19:26]
        isAlive <= io.writeValue @[Life.scala 20:15]
      else :
        isAlive <= isAlive @[Life.scala 23:17]
    else :
      node _T_1 = add(io.neighbors[7], UInt<3>("h0")) @[Life.scala 26:81]
      node _T_2 = tail(_T_1, 1) @[Life.scala 26:81]
      node _T_3 = add(io.neighbors[6], _T_2) @[Life.scala 26:81]
      node _T_4 = tail(_T_3, 1) @[Life.scala 26:81]
      node _T_5 = add(io.neighbors[5], _T_4) @[Life.scala 26:81]
      node _T_6 = tail(_T_5, 1) @[Life.scala 26:81]
      node _T_7 = add(io.neighbors[4], _T_6) @[Life.scala 26:81]
      node _T_8 = tail(_T_7, 1) @[Life.scala 26:81]
      node _T_9 = add(io.neighbors[3], _T_8) @[Life.scala 26:81]
      node _T_10 = tail(_T_9, 1) @[Life.scala 26:81]
      node _T_11 = add(io.neighbors[2], _T_10) @[Life.scala 26:81]
      node _T_12 = tail(_T_11, 1) @[Life.scala 26:81]
      node _T_13 = add(io.neighbors[1], _T_12) @[Life.scala 26:81]
      node _T_14 = tail(_T_13, 1) @[Life.scala 26:81]
      node _T_15 = add(io.neighbors[0], _T_14) @[Life.scala 26:81]
      node _T_16 = tail(_T_15, 1) @[Life.scala 26:81]
      when isAlive : @[Life.scala 28:19]
        node _T_17 = lt(_T_16, UInt<2>("h2")) @[Life.scala 29:18]
        when _T_17 : @[Life.scala 29:25]
          isAlive <= UInt<1>("h0") @[Life.scala 30:17]
        else :
          node _T_18 = lt(_T_16, UInt<3>("h4")) @[Life.scala 31:24]
          when _T_18 : @[Life.scala 31:31]
            isAlive <= UInt<1>("h1") @[Life.scala 32:17]
          else :
            isAlive <= UInt<1>("h0") @[Life.scala 34:17]
      else :
        node _T_19 = eq(isAlive, UInt<1>("h0")) @[Life.scala 37:12]
        node _T_20 = eq(_T_16, UInt<2>("h3")) @[Life.scala 37:30]
        node _T_21 = and(_T_19, _T_20) @[Life.scala 37:21]
        when _T_21 : @[Life.scala 37:39]
          isAlive <= UInt<1>("h1") @[Life.scala 38:17]
        else :
          isAlive <= UInt<1>("h0") @[Life.scala 41:19]
    io.out <= isAlive @[Life.scala 46:10]