
circuit MaxPeriodFibonacciLFSR :   
  module MaxPeriodFibonacciLFSR : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip seed : {valid : UInt<1>, bits : UInt<1>[2]}, flip increment : UInt<1>, out : UInt<1>[2]}
    
    wire _T : UInt<1>[2] @[PRNG.scala 38:28]
    _T[0] <= UInt<1>("h00") @[PRNG.scala 38:28]
    _T[1] <= UInt<1>("h01") @[PRNG.scala 38:28]
    reg state : UInt<1>[2], clock with : (reset => (reset, _T)) @[PRNG.scala 47:50]
;    when io.increment : @[PRNG.scala 61:23]
;      node _T_1 = xor(state[0], state[1]) @[LFSR.scala 15:41]
;      node _T_2 = xor(_T_1, state[1]) @[LFSR.scala 15:41]
;      node _T_3 = xor(_T_2, state[0]) @[LFSR.scala 15:41]
;      state[0] <= _T_3 @[PRNG.scala 62:11]
;      state[1] <= state[0] @[PRNG.scala 62:11]
;      skip @[PRNG.scala 61:23]
    when io.seed.valid : @[PRNG.scala 65:25]
      state[0] <= io.seed.bits[0] @[PRNG.scala 66:11]
      state[1] <= io.seed.bits[1] @[PRNG.scala 66:11]
      skip @[PRNG.scala 65:25]
    io.out[0] <= state[0] @[PRNG.scala 69:10]
    io.out[1] <= state[1] @[PRNG.scala 69:10]
