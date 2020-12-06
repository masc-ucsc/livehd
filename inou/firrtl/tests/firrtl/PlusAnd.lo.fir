circuit PlusAnd :
  module PlusAnd :
    input clock : Clock
    input reset : UInt<1>
    input io_vecI_0 : SInt<23>
    input io_vecI_1 : SInt<23>
    input io_vecI_2 : SInt<23>
    input io_vecI_3 : SInt<23>
    input io_vecI_4 : SInt<23>
    output io_bund_vecO_0 : SInt<23>
    output io_bund_vecO_1 : SInt<23>
    output io_bund_vecO_2 : SInt<23>
    output io_bund_vecO_3 : SInt<23>
    output io_bund_vecO_4 : SInt<23>

    io_bund_vecO_0 <= io_vecI_0 @[PlusAnd.scala 15:16]
    io_bund_vecO_1 <= io_vecI_1 @[PlusAnd.scala 15:16]
    io_bund_vecO_2 <= io_vecI_2 @[PlusAnd.scala 15:16]
    io_bund_vecO_3 <= io_vecI_3 @[PlusAnd.scala 15:16]
    io_bund_vecO_4 <= io_vecI_4 @[PlusAnd.scala 15:16]