circuit RegisterFileSimple :
  module RegisterFileSimple : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip readreg1 : UInt<1>, flip writereg : UInt<1>, flip writedata : UInt<1>, flip wen : UInt<1>, readdata1 : UInt<1>}
    
    reg regs : UInt<1>[2], clock 
    when io.wen : 
      regs[io.writereg] <= io.writedata 
      skip 
    io.readdata1 <= regs[io.readreg1] 
 
    node _T = eq(io.readreg1, io.writereg) 
    node _T_1 = and(_T, io.wen) 
    when _T_1 : 
      io.readdata1 <= io.writedata 
      skip 
