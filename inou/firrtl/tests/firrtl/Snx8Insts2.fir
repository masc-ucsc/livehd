;buildInfoPackage: chisel3, version: 3.4.2, scalaVersion: 2.12.12, sbtVersion: 1.3.10
circuit Snx8Insts2 : 
  module Snx0_4 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snx8Insts2.scala 10:20]
    node t0 = tail(_t0_T, 1) @[Snx8Insts2.scala 10:20]
    node inv0 = not(t0) @[Snx8Insts2.scala 11:15]
    node x0 = xor(t0, inv0) @[Snx8Insts2.scala 12:18]
    node invx0 = not(x0) @[Snx8Insts2.scala 13:15]
    node _io_z_T = xor(invx0, io.a) @[Snx8Insts2.scala 14:17]
    io.z <= _io_z_T @[Snx8Insts2.scala 14:8]
    
  module Snx1_4 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<2>}
    
    node _t0_T = add(io.a, io.b) @[Snx8Insts2.scala 22:20]
    node t0 = tail(_t0_T, 1) @[Snx8Insts2.scala 22:20]
    node inv0 = not(t0) @[Snx8Insts2.scala 23:15]
    node x0 = xor(t0, inv0) @[Snx8Insts2.scala 24:18]
    node invx0 = not(x0) @[Snx8Insts2.scala 25:15]
    node _io_z_T = xor(invx0, io.a) @[Snx8Insts2.scala 26:17]
    io.z <= _io_z_T @[Snx8Insts2.scala 26:8]
    
  module Snx8Insts2 : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    inst m0 of Snx0_4 @[Snx8Insts2.scala 35:18]
    m0.clock <= clock
    m0.reset <= reset
    m0.io.a <= io.a @[Snx8Insts2.scala 36:11]
    m0.io.b <= io.b @[Snx8Insts2.scala 37:11]
    inst m1 of Snx1_4 @[Snx8Insts2.scala 39:18]
    m1.clock <= clock
    m1.reset <= reset
    m1.io.a <= io.a @[Snx8Insts2.scala 40:11]
    m1.io.b <= io.b @[Snx8Insts2.scala 41:11]
    node _sum_T = add(m0.io.z, m1.io.z) @[Snx8Insts2.scala 42:21]
    node sum = tail(_sum_T, 1) @[Snx8Insts2.scala 42:21]
    node _io_z_T = xor(sum, io.a) @[Snx8Insts2.scala 43:15]
    io.z <= _io_z_T @[Snx8Insts2.scala 43:8]
    
