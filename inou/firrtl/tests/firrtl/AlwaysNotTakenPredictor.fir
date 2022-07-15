circuit AlwaysNotTakenPredictor :
  module AlwaysNotTakenPredictor : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip pc : UInt<32>, flip update : UInt<1>, flip taken : UInt<1>, prediction : UInt<1>}
    
    wire _T : UInt<2>[32] 
    _T[0] <= UInt<2>("h02") 
    _T[1] <= UInt<2>("h02") 
    _T[2] <= UInt<2>("h02") 
    _T[3] <= UInt<2>("h02") 
    _T[4] <= UInt<2>("h02") 
    _T[5] <= UInt<2>("h02") 
    _T[6] <= UInt<2>("h02") 
    _T[7] <= UInt<2>("h02") 
    _T[8] <= UInt<2>("h02") 
    _T[9] <= UInt<2>("h02") 
    _T[10] <= UInt<2>("h02") 
    _T[11] <= UInt<2>("h02") 
    _T[12] <= UInt<2>("h02") 
    _T[13] <= UInt<2>("h02") 
    _T[14] <= UInt<2>("h02") 
    _T[15] <= UInt<2>("h02") 
    _T[16] <= UInt<2>("h02") 
    _T[17] <= UInt<2>("h02") 
    _T[18] <= UInt<2>("h02") 
    _T[19] <= UInt<2>("h02") 
    _T[20] <= UInt<2>("h02") 
    _T[21] <= UInt<2>("h02") 
    _T[22] <= UInt<2>("h02") 
    _T[23] <= UInt<2>("h02") 
    _T[24] <= UInt<2>("h02") 
    _T[25] <= UInt<2>("h02") 
    _T[26] <= UInt<2>("h02") 
    _T[27] <= UInt<2>("h02") 
    _T[28] <= UInt<2>("h02") 
    _T[29] <= UInt<2>("h02") 
    _T[30] <= UInt<2>("h02") 
    _T[31] <= UInt<2>("h02") 
    reg predictionTable : UInt<2>[32], clock with : (reset => (reset, _T)) 
    io.prediction <= UInt<1>("h00") 
    
