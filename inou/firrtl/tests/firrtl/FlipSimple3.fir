circuit FlipSimple3 : 
  module FlipSimple3 :
    ;output io : {flip in : {flip ab: UInt<1>}, out:{ flip ab: UInt<1>}
    output io : {flip in : {flip ab:UInt<1>}, out:{ flip ab:UInt<1>}}

    wire _T   : {flip ab: UInt<1>} 
    _T <- io.in
    io.out <- _T
