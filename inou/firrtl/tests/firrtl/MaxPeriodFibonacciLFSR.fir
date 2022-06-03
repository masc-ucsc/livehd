circuit MaxPeriodFibonacciLFSR :   
  module MaxPeriodFibonacciLFSR : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip seed : {valid : UInt<1>, bits : UInt<1>[3]}, flip increment : UInt<1>, out : UInt<1>[3]}
    
    wire _T : UInt<1>[3] @[PRNG.scala 38:28]
    _T[0] <= UInt<1>("h00") @[PRNG.scala 38:28]
    _T[1] <= UInt<1>("h00") @[PRNG.scala 38:28]
    _T[2] <= UInt<1>("h01") @[PRNG.scala 38:28]
    reg state : UInt<1>[3], clock with : (reset => (reset, _T)) @[PRNG.scala 47:50]
    when io.increment : @[PRNG.scala 61:23]
      node _K_1 = xor(state[2], state[0]) @[LFSR.scala 15:41]
      node _K_2 = xor(_K_1, state[1]) @[LFSR.scala 15:41]
      node _K_3 = xor(_K_2, state[0]) @[LFSR.scala 15:41]
      state[0] <= _K_3 @[PRNG.scala 62:11]
      state[1] <= state[0] @[PRNG.scala 62:11]
      state[2] <= state[1] @[PRNG.scala 62:11]
    when io.seed.valid : @[PRNG.scala 65:25]
      state[0] <= io.seed.bits[0] @[PRNG.scala 66:11]
      state[1] <= io.seed.bits[1] @[PRNG.scala 66:11]
      state[2] <= io.seed.bits[2] @[PRNG.scala 66:11]
    io.out[0] <= state[0] @[PRNG.scala 69:10]
    io.out[1] <= state[1] @[PRNG.scala 69:10]
    io.out[2] <= state[2] @[PRNG.scala 69:10]


