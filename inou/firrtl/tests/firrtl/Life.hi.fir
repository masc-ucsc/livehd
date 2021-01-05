circuit Life :
  module Cell :
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

  module Life :
    input clock : Clock
    input reset : UInt<1>
    output io : { state : UInt<1>[1][2], flip running : UInt<1>, flip writeValue : UInt<1>, flip writeRowAddress : UInt<2>, flip writeColAddress : UInt<1>}

    inst Cell of Cell @[Life.scala 59:52]
    Cell.clock <= clock
    Cell.reset <= reset
    inst Cell_1 of Cell @[Life.scala 59:52]
    Cell_1.clock <= clock
    Cell_1.reset <= reset
    io.state[0][0] <= Cell.io.out @[Life.scala 65:24]
    Cell.io.running <= io.running @[Life.scala 66:32]
    Cell.io.writeValue <= io.writeValue @[Life.scala 67:35]
    node _T = eq(io.writeRowAddress, UInt<1>("h0")) @[Life.scala 68:58]
    node _T_1 = eq(io.writeColAddress, UInt<1>("h0")) @[Life.scala 68:89]
    node _T_2 = and(_T, _T_1) @[Life.scala 68:68]
    Cell.io.writeEnable <= _T_2 @[Life.scala 68:36]
    io.state[1][0] <= Cell_1.io.out @[Life.scala 65:24]
    Cell_1.io.running <= io.running @[Life.scala 66:32]
    Cell_1.io.writeValue <= io.writeValue @[Life.scala 67:35]
    node _T_3 = eq(io.writeRowAddress, UInt<1>("h1")) @[Life.scala 68:58]
    node _T_4 = eq(io.writeColAddress, UInt<1>("h0")) @[Life.scala 68:89]
    node _T_5 = and(_T_3, _T_4) @[Life.scala 68:68]
    Cell_1.io.writeEnable <= _T_5 @[Life.scala 68:36]
    Cell.io.neighbors[0] <= Cell_1.io.out @[Life.scala 89:46]
    Cell.io.neighbors[1] <= Cell_1.io.out @[Life.scala 89:46]
    Cell.io.neighbors[2] <= Cell_1.io.out @[Life.scala 89:46]
    Cell.io.neighbors[3] <= Cell.io.out @[Life.scala 89:46]
    Cell.io.neighbors[4] <= Cell.io.out @[Life.scala 89:46]
    Cell.io.neighbors[5] <= Cell_1.io.out @[Life.scala 89:46]
    Cell.io.neighbors[6] <= Cell_1.io.out @[Life.scala 89:46]
    Cell.io.neighbors[7] <= Cell_1.io.out @[Life.scala 89:46]
    Cell_1.io.neighbors[0] <= Cell.io.out @[Life.scala 89:46]
    Cell_1.io.neighbors[1] <= Cell.io.out @[Life.scala 89:46]
    Cell_1.io.neighbors[2] <= Cell.io.out @[Life.scala 89:46]
    Cell_1.io.neighbors[3] <= Cell_1.io.out @[Life.scala 89:46]
    Cell_1.io.neighbors[4] <= Cell_1.io.out @[Life.scala 89:46]
    Cell_1.io.neighbors[5] <= Cell.io.out @[Life.scala 89:46]
    Cell_1.io.neighbors[6] <= Cell.io.out @[Life.scala 89:46]
    Cell_1.io.neighbors[7] <= Cell.io.out @[Life.scala 89:46]