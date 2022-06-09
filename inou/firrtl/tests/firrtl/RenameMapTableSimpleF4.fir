circuit RenameMapTableSimpleF4 :
  module RenameMapTableSimpleF4 : 
    input clock : Clock
    input reset : UInt<1>
    output io : {map_resps : {prs1 : UInt<6>}[1], flip remap_reqs : {valid : UInt<1>}[1]}
    
    io is invalid
    reg regs : UInt<4>[1][4], clock @[rename-maptable.scala 71:25]
    node _T = bits(io.remap_reqs[0].valid, 0, 0) @[Bitwise.scala 72:15]

    io.map_resps[0].prs1 <= _T

