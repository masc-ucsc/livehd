circuit PlusAnd :
  module PlusAnd :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip vecI : SInt<23>[5], bund : { vecO : SInt<23>[5]}}

    io.bund.vecO[0] <= io.vecI[0] @[PlusAnd.scala 15:16]
    io.bund.vecO[1] <= io.vecI[1] @[PlusAnd.scala 15:16]
    io.bund.vecO[2] <= io.vecI[2] @[PlusAnd.scala 15:16]
    io.bund.vecO[3] <= io.vecI[3] @[PlusAnd.scala 15:16]
    io.bund.vecO[4] <= io.vecI[4] @[PlusAnd.scala 15:16]