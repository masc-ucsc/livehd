circuit RegisterFile :
  module RegisterFile : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip readreg1 : UInt<5>, flip readreg2 : UInt<5>, flip writereg : UInt<5>, flip writedata : UInt<32>, flip wen : UInt<1>, readdata1 : UInt<32>, readdata2 : UInt<32>}
    
    reg regs : UInt<32>[32], clock 
    when io.wen : 
      regs[io.writereg] <= io.writedata 
      skip 
    io.readdata1 <= regs[io.readreg1] 
    io.readdata2 <= regs[io.readreg2] 
    node _T = eq(io.readreg1, io.writereg) 
    node _T_1 = and(_T, io.wen) 
    when _T_1 : 
      io.readdata1 <= io.writedata 
      skip 
    else : 
      node _T_2 = eq(io.readreg2, io.writereg) 
      node _T_3 = and(_T_2, io.wen) 
      when _T_3 : 
        io.readdata2 <= io.writedata 
        skip 
    
