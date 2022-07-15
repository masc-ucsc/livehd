circuit NullLocalBranchPredictorBank :
  module NullLocalBranchPredictorBank : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip f0_valid : UInt<1>, flip f0_pc : UInt<40>, f1_lhist : UInt<1>, f3_lhist : UInt<1>, flip f3_taken_br : UInt<1>, flip f3_fire : UInt<1>, flip update : {valid : UInt<1>, mispredict : UInt<1>, repair : UInt<1>, pc : UInt<40>, lhist : UInt<1>}}
    
    clock is invalid
    reset is invalid
    io is invalid
    io.f1_lhist <= UInt<1>("h00") @[local.scala 40:15]
    io.f3_lhist <= UInt<1>("h00") @[local.scala 41:15]
    
