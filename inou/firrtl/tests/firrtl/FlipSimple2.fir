circuit FlipSimple2 : 
  module FlipSimple2 :
    output io : {flip in : {flip a: {flip ab: UInt<1>, ac:UInt<1>}, d: UInt<3>, e: UInt<1>}, out:{ flip a: {flip ab: UInt<1>, ac:UInt<1>}, d: UInt<2> }}

    wire _T   : {flip a: {flip ab: UInt<1>, ac:UInt<1>}, d: UInt<2>} 
    _T.a.ac = 0 //temp


    wire _T_1 : {flip a: {flip ab: UInt<1>, ac:UInt<1>}, d: UInt<2>} 
    _T <- io.in
    _T.a.ab <- $io.in.a.ab


    %io.in.a.ac <- _T.a.ac.__last_value

    //_T_1 <- _T
    _T.a.ac <- _T_1.a.ac.__last_value

    io.out <- _T_1
    _T_1.a.ac <- $io.in.a.ac 


