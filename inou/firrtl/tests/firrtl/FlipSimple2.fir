circuit FlipSimple2 : 
  module FlipSimple2 :
    input   myinput : {flip a: {flip a_0: UInt<1>, b_0:UInt<1>}, b: UInt<3>, c: UInt<1>}
    output myoutput : {flip a: {flip a_0: UInt<1>, b_0:UInt<1>}, b: UInt<2>}
    wire _T   : {flip a: {flip a_0: UInt<1>, b_0:UInt<1>}, b: UInt<2>} 
    wire _T_1 : {flip a: {flip a_0: UInt<1>, b_0:UInt<1>}, b: UInt<2>} 
    _T <- myinput
    _T_1 <- _T
    myoutput <- _T_1
