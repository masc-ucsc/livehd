;buildInfoPackage: chisel3, version: 3.4.2, scalaVersion: 2.12.13, sbtVersion: 1.3.10
circuit Snxn0k : 
  module SnxnLv3Inst0 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 468:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 468:20]
    node inv0 = not(t0) @[Snxn0k.scala 469:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 470:18]
    node invx0 = not(x0) @[Snxn0k.scala 471:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 472:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 472:18]
    node inv1 = not(t1) @[Snxn0k.scala 473:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 474:18]
    node invx1 = not(x1) @[Snxn0k.scala 475:15]
    io.z <= invx1 @[Snxn0k.scala 477:8]
    
  module SnxnLv2Inst0 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 247:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 247:20]
    node inv0 = not(t0) @[Snxn0k.scala 248:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 249:18]
    node invx0 = not(x0) @[Snxn0k.scala 250:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 251:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 251:18]
    node inv1 = not(t1) @[Snxn0k.scala 252:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 253:18]
    node invx1 = not(x1) @[Snxn0k.scala 254:15]
    inst inst_SnxnLv3Inst0 of SnxnLv3Inst0 @[Snxn0k.scala 256:33]
    inst_SnxnLv3Inst0.clock <= clock
    inst_SnxnLv3Inst0.reset <= reset
    inst_SnxnLv3Inst0.io.a <= io.a @[Snxn0k.scala 257:26]
    inst_SnxnLv3Inst0.io.b <= io.b @[Snxn0k.scala 258:26]
    node _io_z_T = xor(inst_SnxnLv3Inst0.io.z, io.a) @[Snxn0k.scala 261:15]
    io.z <= _io_z_T @[Snxn0k.scala 261:8]
    
  module SnxnLv2Inst1 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 270:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 270:20]
    node inv0 = not(t0) @[Snxn0k.scala 271:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 272:18]
    node invx0 = not(x0) @[Snxn0k.scala 273:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 274:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 274:18]
    node inv1 = not(t1) @[Snxn0k.scala 275:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 276:18]
    node invx1 = not(x1) @[Snxn0k.scala 277:15]
    io.z <= invx1 @[Snxn0k.scala 279:8]
    
  module SnxnLv2Inst2 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 288:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 288:20]
    node inv0 = not(t0) @[Snxn0k.scala 289:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 290:18]
    node invx0 = not(x0) @[Snxn0k.scala 291:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 292:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 292:18]
    node inv1 = not(t1) @[Snxn0k.scala 293:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 294:18]
    node invx1 = not(x1) @[Snxn0k.scala 295:15]
    io.z <= invx1 @[Snxn0k.scala 297:8]
    
  module SnxnLv2Inst3 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 306:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 306:20]
    node inv0 = not(t0) @[Snxn0k.scala 307:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 308:18]
    node invx0 = not(x0) @[Snxn0k.scala 309:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 310:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 310:18]
    node inv1 = not(t1) @[Snxn0k.scala 311:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 312:18]
    node invx1 = not(x1) @[Snxn0k.scala 313:15]
    io.z <= invx1 @[Snxn0k.scala 315:8]
    
  module SnxnLv1Inst0 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 78:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 78:20]
    node inv0 = not(t0) @[Snxn0k.scala 79:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 80:18]
    node invx0 = not(x0) @[Snxn0k.scala 81:15]
    inst inst_SnxnLv2Inst0 of SnxnLv2Inst0 @[Snxn0k.scala 83:33]
    inst_SnxnLv2Inst0.clock <= clock
    inst_SnxnLv2Inst0.reset <= reset
    inst_SnxnLv2Inst0.io.a <= io.a @[Snxn0k.scala 84:26]
    inst_SnxnLv2Inst0.io.b <= io.b @[Snxn0k.scala 85:26]
    inst inst_SnxnLv2Inst1 of SnxnLv2Inst1 @[Snxn0k.scala 87:33]
    inst_SnxnLv2Inst1.clock <= clock
    inst_SnxnLv2Inst1.reset <= reset
    inst_SnxnLv2Inst1.io.a <= io.a @[Snxn0k.scala 88:26]
    inst_SnxnLv2Inst1.io.b <= io.b @[Snxn0k.scala 89:26]
    inst inst_SnxnLv2Inst2 of SnxnLv2Inst2 @[Snxn0k.scala 91:33]
    inst_SnxnLv2Inst2.clock <= clock
    inst_SnxnLv2Inst2.reset <= reset
    inst_SnxnLv2Inst2.io.a <= io.a @[Snxn0k.scala 92:26]
    inst_SnxnLv2Inst2.io.b <= io.b @[Snxn0k.scala 93:26]
    inst inst_SnxnLv2Inst3 of SnxnLv2Inst3 @[Snxn0k.scala 95:33]
    inst_SnxnLv2Inst3.clock <= clock
    inst_SnxnLv2Inst3.reset <= reset
    inst_SnxnLv2Inst3.io.a <= io.a @[Snxn0k.scala 96:26]
    inst_SnxnLv2Inst3.io.b <= io.b @[Snxn0k.scala 97:26]
    node _sum_T = add(inst_SnxnLv2Inst0.io.z, inst_SnxnLv2Inst1.io.z) @[Snxn0k.scala 99:36]
    node _sum_T_1 = tail(_sum_T, 1) @[Snxn0k.scala 99:36]
    node _sum_T_2 = add(_sum_T_1, inst_SnxnLv2Inst2.io.z) @[Snxn0k.scala 99:61]
    node _sum_T_3 = tail(_sum_T_2, 1) @[Snxn0k.scala 99:61]
    node _sum_T_4 = add(_sum_T_3, inst_SnxnLv2Inst3.io.z) @[Snxn0k.scala 99:86]
    node sum = tail(_sum_T_4, 1) @[Snxn0k.scala 99:86]
    node _io_z_T = xor(sum, io.a) @[Snxn0k.scala 100:15]
    io.z <= _io_z_T @[Snxn0k.scala 100:8]
    
  module SnxnLv2Inst4 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 396:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 396:20]
    node inv0 = not(t0) @[Snxn0k.scala 397:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 398:18]
    node invx0 = not(x0) @[Snxn0k.scala 399:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 400:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 400:18]
    node inv1 = not(t1) @[Snxn0k.scala 401:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 402:18]
    node invx1 = not(x1) @[Snxn0k.scala 403:15]
    io.z <= invx1 @[Snxn0k.scala 405:8]
    
  module SnxnLv2Inst5 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 414:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 414:20]
    node inv0 = not(t0) @[Snxn0k.scala 415:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 416:18]
    node invx0 = not(x0) @[Snxn0k.scala 417:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 418:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 418:18]
    node inv1 = not(t1) @[Snxn0k.scala 419:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 420:18]
    node invx1 = not(x1) @[Snxn0k.scala 421:15]
    io.z <= invx1 @[Snxn0k.scala 423:8]
    
  module SnxnLv2Inst6 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 432:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 432:20]
    node inv0 = not(t0) @[Snxn0k.scala 433:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 434:18]
    node invx0 = not(x0) @[Snxn0k.scala 435:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 436:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 436:18]
    node inv1 = not(t1) @[Snxn0k.scala 437:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 438:18]
    node invx1 = not(x1) @[Snxn0k.scala 439:15]
    io.z <= invx1 @[Snxn0k.scala 441:8]
    
  module SnxnLv2Inst7 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 450:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 450:20]
    node inv0 = not(t0) @[Snxn0k.scala 451:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 452:18]
    node invx0 = not(x0) @[Snxn0k.scala 453:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 454:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 454:18]
    node inv1 = not(t1) @[Snxn0k.scala 455:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 456:18]
    node invx1 = not(x1) @[Snxn0k.scala 457:15]
    io.z <= invx1 @[Snxn0k.scala 459:8]
    
  module SnxnLv1Inst1 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 140:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 140:20]
    node inv0 = not(t0) @[Snxn0k.scala 141:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 142:18]
    node invx0 = not(x0) @[Snxn0k.scala 143:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 144:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 144:18]
    node inv1 = not(t1) @[Snxn0k.scala 145:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 146:18]
    node invx1 = not(x1) @[Snxn0k.scala 147:15]
    inst inst_SnxnLv2Inst4 of SnxnLv2Inst4 @[Snxn0k.scala 149:33]
    inst_SnxnLv2Inst4.clock <= clock
    inst_SnxnLv2Inst4.reset <= reset
    inst_SnxnLv2Inst4.io.a <= io.a @[Snxn0k.scala 150:26]
    inst_SnxnLv2Inst4.io.b <= io.b @[Snxn0k.scala 151:26]
    inst inst_SnxnLv2Inst5 of SnxnLv2Inst5 @[Snxn0k.scala 153:33]
    inst_SnxnLv2Inst5.clock <= clock
    inst_SnxnLv2Inst5.reset <= reset
    inst_SnxnLv2Inst5.io.a <= io.a @[Snxn0k.scala 154:26]
    inst_SnxnLv2Inst5.io.b <= io.b @[Snxn0k.scala 155:26]
    inst inst_SnxnLv2Inst6 of SnxnLv2Inst6 @[Snxn0k.scala 157:33]
    inst_SnxnLv2Inst6.clock <= clock
    inst_SnxnLv2Inst6.reset <= reset
    inst_SnxnLv2Inst6.io.a <= io.a @[Snxn0k.scala 158:26]
    inst_SnxnLv2Inst6.io.b <= io.b @[Snxn0k.scala 159:26]
    inst inst_SnxnLv2Inst7 of SnxnLv2Inst7 @[Snxn0k.scala 161:33]
    inst_SnxnLv2Inst7.clock <= clock
    inst_SnxnLv2Inst7.reset <= reset
    inst_SnxnLv2Inst7.io.a <= io.a @[Snxn0k.scala 162:26]
    inst_SnxnLv2Inst7.io.b <= io.b @[Snxn0k.scala 163:26]
    node _sum_T = add(inst_SnxnLv2Inst4.io.z, inst_SnxnLv2Inst5.io.z) @[Snxn0k.scala 165:36]
    node _sum_T_1 = tail(_sum_T, 1) @[Snxn0k.scala 165:36]
    node _sum_T_2 = add(_sum_T_1, inst_SnxnLv2Inst6.io.z) @[Snxn0k.scala 165:61]
    node _sum_T_3 = tail(_sum_T_2, 1) @[Snxn0k.scala 165:61]
    node _sum_T_4 = add(_sum_T_3, inst_SnxnLv2Inst7.io.z) @[Snxn0k.scala 165:86]
    node sum = tail(_sum_T_4, 1) @[Snxn0k.scala 165:86]
    node _io_z_T = xor(sum, io.a) @[Snxn0k.scala 166:15]
    io.z <= _io_z_T @[Snxn0k.scala 166:8]
    
  module SnxnLv2Inst8 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 360:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 360:20]
    node inv0 = not(t0) @[Snxn0k.scala 361:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 362:18]
    node invx0 = not(x0) @[Snxn0k.scala 363:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 364:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 364:18]
    node inv1 = not(t1) @[Snxn0k.scala 365:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 366:18]
    node invx1 = not(x1) @[Snxn0k.scala 367:15]
    io.z <= invx1 @[Snxn0k.scala 369:8]
    
  module SnxnLv2Inst9 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 378:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 378:20]
    node inv0 = not(t0) @[Snxn0k.scala 379:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 380:18]
    node invx0 = not(x0) @[Snxn0k.scala 381:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 382:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 382:18]
    node inv1 = not(t1) @[Snxn0k.scala 383:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 384:18]
    node invx1 = not(x1) @[Snxn0k.scala 385:15]
    io.z <= invx1 @[Snxn0k.scala 387:8]
    
  module SnxnLv2Inst10 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 324:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 324:20]
    node inv0 = not(t0) @[Snxn0k.scala 325:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 326:18]
    node invx0 = not(x0) @[Snxn0k.scala 327:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 328:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 328:18]
    node inv1 = not(t1) @[Snxn0k.scala 329:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 330:18]
    node invx1 = not(x1) @[Snxn0k.scala 331:15]
    io.z <= invx1 @[Snxn0k.scala 333:8]
    
  module SnxnLv2Inst11 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 342:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 342:20]
    node inv0 = not(t0) @[Snxn0k.scala 343:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 344:18]
    node invx0 = not(x0) @[Snxn0k.scala 345:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 346:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 346:18]
    node inv1 = not(t1) @[Snxn0k.scala 347:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 348:18]
    node invx1 = not(x1) @[Snxn0k.scala 349:15]
    io.z <= invx1 @[Snxn0k.scala 351:8]
    
  module SnxnLv1Inst2 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 109:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 109:20]
    node inv0 = not(t0) @[Snxn0k.scala 110:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 111:18]
    node invx0 = not(x0) @[Snxn0k.scala 112:15]
    inst inst_SnxnLv2Inst8 of SnxnLv2Inst8 @[Snxn0k.scala 114:33]
    inst_SnxnLv2Inst8.clock <= clock
    inst_SnxnLv2Inst8.reset <= reset
    inst_SnxnLv2Inst8.io.a <= io.a @[Snxn0k.scala 115:26]
    inst_SnxnLv2Inst8.io.b <= io.b @[Snxn0k.scala 116:26]
    inst inst_SnxnLv2Inst9 of SnxnLv2Inst9 @[Snxn0k.scala 118:33]
    inst_SnxnLv2Inst9.clock <= clock
    inst_SnxnLv2Inst9.reset <= reset
    inst_SnxnLv2Inst9.io.a <= io.a @[Snxn0k.scala 119:26]
    inst_SnxnLv2Inst9.io.b <= io.b @[Snxn0k.scala 120:26]
    inst inst_SnxnLv2Inst10 of SnxnLv2Inst10 @[Snxn0k.scala 122:34]
    inst_SnxnLv2Inst10.clock <= clock
    inst_SnxnLv2Inst10.reset <= reset
    inst_SnxnLv2Inst10.io.a <= io.a @[Snxn0k.scala 123:27]
    inst_SnxnLv2Inst10.io.b <= io.b @[Snxn0k.scala 124:27]
    inst inst_SnxnLv2Inst11 of SnxnLv2Inst11 @[Snxn0k.scala 126:34]
    inst_SnxnLv2Inst11.clock <= clock
    inst_SnxnLv2Inst11.reset <= reset
    inst_SnxnLv2Inst11.io.a <= io.a @[Snxn0k.scala 127:27]
    inst_SnxnLv2Inst11.io.b <= io.b @[Snxn0k.scala 128:27]
    node _sum_T = add(inst_SnxnLv2Inst8.io.z, inst_SnxnLv2Inst9.io.z) @[Snxn0k.scala 130:36]
    node _sum_T_1 = tail(_sum_T, 1) @[Snxn0k.scala 130:36]
    node _sum_T_2 = add(_sum_T_1, inst_SnxnLv2Inst10.io.z) @[Snxn0k.scala 130:61]
    node _sum_T_3 = tail(_sum_T_2, 1) @[Snxn0k.scala 130:61]
    node _sum_T_4 = add(_sum_T_3, inst_SnxnLv2Inst11.io.z) @[Snxn0k.scala 130:87]
    node sum = tail(_sum_T_4, 1) @[Snxn0k.scala 130:87]
    node _io_z_T = xor(sum, io.a) @[Snxn0k.scala 131:15]
    io.z <= _io_z_T @[Snxn0k.scala 131:8]
    
  module SnxnLv2Inst12 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 175:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 175:20]
    node inv0 = not(t0) @[Snxn0k.scala 176:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 177:18]
    node invx0 = not(x0) @[Snxn0k.scala 178:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 179:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 179:18]
    node inv1 = not(t1) @[Snxn0k.scala 180:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 181:18]
    node invx1 = not(x1) @[Snxn0k.scala 182:15]
    io.z <= invx1 @[Snxn0k.scala 184:8]
    
  module SnxnLv2Inst13 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 193:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 193:20]
    node inv0 = not(t0) @[Snxn0k.scala 194:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 195:18]
    node invx0 = not(x0) @[Snxn0k.scala 196:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 197:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 197:18]
    node inv1 = not(t1) @[Snxn0k.scala 198:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 199:18]
    node invx1 = not(x1) @[Snxn0k.scala 200:15]
    io.z <= invx1 @[Snxn0k.scala 202:8]
    
  module SnxnLv2Inst14 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 211:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 211:20]
    node inv0 = not(t0) @[Snxn0k.scala 212:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 213:18]
    node invx0 = not(x0) @[Snxn0k.scala 214:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 215:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 215:18]
    node inv1 = not(t1) @[Snxn0k.scala 216:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 217:18]
    node invx1 = not(x1) @[Snxn0k.scala 218:15]
    io.z <= invx1 @[Snxn0k.scala 220:8]
    
  module SnxnLv2Inst15 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 229:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 229:20]
    node inv0 = not(t0) @[Snxn0k.scala 230:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 231:18]
    node invx0 = not(x0) @[Snxn0k.scala 232:15]
    node _t1_T = add(x0, invx0) @[Snxn0k.scala 233:18]
    node t1 = tail(_t1_T, 1) @[Snxn0k.scala 233:18]
    node inv1 = not(t1) @[Snxn0k.scala 234:15]
    node x1 = xor(t1, inv1) @[Snxn0k.scala 235:18]
    node invx1 = not(x1) @[Snxn0k.scala 236:15]
    io.z <= invx1 @[Snxn0k.scala 238:8]
    
  module SnxnLv1Inst3 : 
    input clock : Clock
    input reset : Reset
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 47:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 47:20]
    node inv0 = not(t0) @[Snxn0k.scala 48:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 49:18]
    node invx0 = not(x0) @[Snxn0k.scala 50:15]
    inst inst_SnxnLv2Inst12 of SnxnLv2Inst12 @[Snxn0k.scala 52:34]
    inst_SnxnLv2Inst12.clock <= clock
    inst_SnxnLv2Inst12.reset <= reset
    inst_SnxnLv2Inst12.io.a <= io.a @[Snxn0k.scala 53:27]
    inst_SnxnLv2Inst12.io.b <= io.b @[Snxn0k.scala 54:27]
    inst inst_SnxnLv2Inst13 of SnxnLv2Inst13 @[Snxn0k.scala 56:34]
    inst_SnxnLv2Inst13.clock <= clock
    inst_SnxnLv2Inst13.reset <= reset
    inst_SnxnLv2Inst13.io.a <= io.a @[Snxn0k.scala 57:27]
    inst_SnxnLv2Inst13.io.b <= io.b @[Snxn0k.scala 58:27]
    inst inst_SnxnLv2Inst14 of SnxnLv2Inst14 @[Snxn0k.scala 60:34]
    inst_SnxnLv2Inst14.clock <= clock
    inst_SnxnLv2Inst14.reset <= reset
    inst_SnxnLv2Inst14.io.a <= io.a @[Snxn0k.scala 61:27]
    inst_SnxnLv2Inst14.io.b <= io.b @[Snxn0k.scala 62:27]
    inst inst_SnxnLv2Inst15 of SnxnLv2Inst15 @[Snxn0k.scala 64:34]
    inst_SnxnLv2Inst15.clock <= clock
    inst_SnxnLv2Inst15.reset <= reset
    inst_SnxnLv2Inst15.io.a <= io.a @[Snxn0k.scala 65:27]
    inst_SnxnLv2Inst15.io.b <= io.b @[Snxn0k.scala 66:27]
    node _sum_T = add(inst_SnxnLv2Inst12.io.z, inst_SnxnLv2Inst13.io.z) @[Snxn0k.scala 68:37]
    node _sum_T_1 = tail(_sum_T, 1) @[Snxn0k.scala 68:37]
    node _sum_T_2 = add(_sum_T_1, inst_SnxnLv2Inst14.io.z) @[Snxn0k.scala 68:63]
    node _sum_T_3 = tail(_sum_T_2, 1) @[Snxn0k.scala 68:63]
    node _sum_T_4 = add(_sum_T_3, inst_SnxnLv2Inst15.io.z) @[Snxn0k.scala 68:89]
    node sum = tail(_sum_T_4, 1) @[Snxn0k.scala 68:89]
    node _io_z_T = xor(sum, io.a) @[Snxn0k.scala 69:15]
    io.z <= _io_z_T @[Snxn0k.scala 69:8]
    
  module Snxn0k : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip a : UInt<1>, flip b : UInt<1>, z : UInt<1>}
    
    node _t0_T = add(io.a, io.b) @[Snxn0k.scala 16:20]
    node t0 = tail(_t0_T, 1) @[Snxn0k.scala 16:20]
    node inv0 = not(t0) @[Snxn0k.scala 17:15]
    node x0 = xor(t0, inv0) @[Snxn0k.scala 18:18]
    node invx0 = not(x0) @[Snxn0k.scala 19:15]
    inst inst_SnxnLv1Inst0 of SnxnLv1Inst0 @[Snxn0k.scala 21:33]
    inst_SnxnLv1Inst0.clock <= clock
    inst_SnxnLv1Inst0.reset <= reset
    inst_SnxnLv1Inst0.io.a <= io.a @[Snxn0k.scala 22:26]
    inst_SnxnLv1Inst0.io.b <= io.b @[Snxn0k.scala 23:26]
    inst inst_SnxnLv1Inst1 of SnxnLv1Inst1 @[Snxn0k.scala 25:33]
    inst_SnxnLv1Inst1.clock <= clock
    inst_SnxnLv1Inst1.reset <= reset
    inst_SnxnLv1Inst1.io.a <= io.a @[Snxn0k.scala 26:26]
    inst_SnxnLv1Inst1.io.b <= io.b @[Snxn0k.scala 27:26]
    inst inst_SnxnLv1Inst2 of SnxnLv1Inst2 @[Snxn0k.scala 29:33]
    inst_SnxnLv1Inst2.clock <= clock
    inst_SnxnLv1Inst2.reset <= reset
    inst_SnxnLv1Inst2.io.a <= io.a @[Snxn0k.scala 30:26]
    inst_SnxnLv1Inst2.io.b <= io.b @[Snxn0k.scala 31:26]
    inst inst_SnxnLv1Inst3 of SnxnLv1Inst3 @[Snxn0k.scala 33:33]
    inst_SnxnLv1Inst3.clock <= clock
    inst_SnxnLv1Inst3.reset <= reset
    inst_SnxnLv1Inst3.io.a <= io.a @[Snxn0k.scala 34:26]
    inst_SnxnLv1Inst3.io.b <= io.b @[Snxn0k.scala 35:26]
    node _sum_T = add(inst_SnxnLv1Inst0.io.z, inst_SnxnLv1Inst1.io.z) @[Snxn0k.scala 37:36]
    node _sum_T_1 = tail(_sum_T, 1) @[Snxn0k.scala 37:36]
    node _sum_T_2 = add(_sum_T_1, inst_SnxnLv1Inst2.io.z) @[Snxn0k.scala 37:61]
    node _sum_T_3 = tail(_sum_T_2, 1) @[Snxn0k.scala 37:61]
    node _sum_T_4 = add(_sum_T_3, inst_SnxnLv1Inst3.io.z) @[Snxn0k.scala 37:86]
    node sum = tail(_sum_T_4, 1) @[Snxn0k.scala 37:86]
    node _io_z_T = xor(sum, io.a) @[Snxn0k.scala 38:15]
    io.z <= _io_z_T @[Snxn0k.scala 38:8]
    
