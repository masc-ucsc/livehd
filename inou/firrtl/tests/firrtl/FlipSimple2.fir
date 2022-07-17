circuit FlipSimple2 : 
  module FlipSimple2 :
    output io : {flip in : {flip a: {flip ab: UInt<1>, ac:UInt<1>}, d: UInt<3>, e: UInt<1>}, out:{ flip a: {flip ab: UInt<1>, ac:UInt<1>}, d: UInt<2> }}

    wire _T   : {flip a: {flip ab: UInt<1>, ac:UInt<1>}, d: UInt<2>} 
;   wire _T_1 : {flip a: {flip ab: UInt<1>, ac:UInt<1>}, d: UInt<2>} 
    _T <- io.in
;   _T_1 <- _T
;   io.out <- _T_1
    io.out <- _T
