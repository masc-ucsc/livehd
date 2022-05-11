circuit FlipSimple2 : 
  module FlipSimple2 :
    input   myinput : {flip a: {flip ab: UInt<1>, ac:UInt<1>}, d: UInt<3>, e: UInt<1>}
    output myoutput : {flip a: {flip ab: UInt<1>, ac:UInt<1>}, d: UInt<2>}
    wire _T   : {flip a: {flip ab: UInt<1>, ac:UInt<1>}, d: UInt<2>} 
    wire _T_1 : {flip a: {flip ab: UInt<1>, ac:UInt<1>}, d: UInt<2>} 
    myinput.a <- _T.a
    _T.d <- myinput.d
    _T_1 <- _T
    myoutput <- _T_1
