
ä¬
¼¼
FPU
clock" 
reset
è	
ioß	*Ü	
inst
 
fromint_data
@
fcsr_rm

1
fcsr_flags#*!
valid

bits


store_data
@

toint_data
@
dmem_resp_val

dmem_resp_type

dmem_resp_tag

dmem_resp_data
@
valid

fcsr_rdy

nack_mem


illegal_rm

killx

killm

dec*
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags


sboard_set


sboard_clr

sboard_clra

cp_req*
ready

valid

âbitsÙ*Ö
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags

rm

typ

in1
A
in2
A
in3
A
\cp_respQ*O
ready

valid

)bits!*
data
A
exc



io
 


io
 P9
ex_reg_valid
	

clock"	

reset*	

0FPU.scala 509:25<z%


ex_reg_valid:


iovalidFPU.scala 509:25Y2B
	req_valid5R3

ex_reg_valid:
:


iocp_reqvalidFPU.scala 510:32T>
ex_reg_inst
 	

clock"	

0*

ex_reg_instReg.scala 34:16d:N
:


iovalid9z#


ex_reg_inst:


ioinstReg.scala 35:23Reg.scala 35:19m2Q
ex_cp_validBR@:
:


iocp_reqready:
:


iocp_reqvalidDecoupled.scala 30:37E2.
_T_215$R":


iokillx	

0FPU.scala 513:48C2,
_T_216"R 

ex_reg_valid


_T_215FPU.scala 513:45B2+
_T_217!R


_T_216

ex_cp_validFPU.scala 513:58Q:
mem_reg_valid
	

clock"	

reset*	

0FPU.scala 513:266z


mem_reg_valid


_T_217FPU.scala 513:26V@
mem_reg_inst
 	

clock"	

0*

mem_reg_instReg.scala 34:16c:M


ex_reg_valid9z#


mem_reg_inst

ex_reg_instReg.scala 35:23Reg.scala 35:19P9
mem_cp_valid
	

clock"	

reset*	

0FPU.scala 515:25:z#


mem_cp_valid

ex_cp_validFPU.scala 515:25N27
_T_221-R+:


iokillm:


ionack_memFPU.scala 516:25D2-
_T_223#R!

mem_cp_valid	

0FPU.scala 516:44<2%
killmR


_T_221


_T_223FPU.scala 516:41=2&
_T_225R	

killm	

0FPU.scala 517:49C2,
_T_226"R 


_T_225

mem_cp_validFPU.scala 517:56D2-
_T_227#R!

mem_reg_valid


_T_226FPU.scala 517:45P9
wb_reg_valid
	

clock"	

reset*	

0FPU.scala 517:255z


wb_reg_valid


_T_227FPU.scala 517:25O8
wb_cp_valid
	

clock"	

reset*	

0FPU.scala 518:24:z#


wb_cp_valid

mem_cp_validFPU.scala 518:24/*

fp_decoder
FPUDecoderFPU.scala 520:26 
:



fp_decoderio
 -z&
:



fp_decoderclock	

clock
 -z&
:



fp_decoderreset	

reset
 Kz4
 :
:



fp_decoderioinst:


ioinstFPU.scala 521:22³

cp_ctrl*
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags
FPU.scala 523:21%

	
cp_ctrlFPU.scala 523:21C+

	
cp_ctrl:
:


iocp_reqbitsFPU.scala 524:11Dz-
:
:


iocp_respvalid	

0FPU.scala 525:20Mz6
':%
:
:


iocp_respbitsdata	

0FPU.scala 526:24Õ¾
_T_282*
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags
	

clock"	

0*


_T_282Reg.scala 34:16p:Z
:


iovalidE.



_T_282 :
:



fp_decoderiosigsReg.scala 35:23Reg.scala 35:19N27
ex_ctrl,2*


ex_cp_valid
	
cp_ctrl


_T_282FPU.scala 529:20ÙÂ
mem_ctrl*
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags
	

clock"	

0*


mem_ctrlReg.scala 34:16Y:C


	req_valid2



mem_ctrl
	
ex_ctrlReg.scala 35:23Reg.scala 35:19×À
wb_ctrl*
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags
	

clock"	

0*
	
wb_ctrlReg.scala 34:16]:G


mem_reg_valid2

	
wb_ctrl


mem_ctrlReg.scala 35:23Reg.scala 35:19M6
load_wb
	

clock"	

0*
	
load_wbFPU.scala 534:20?z(

	
load_wb:


iodmem_resp_valFPU.scala 534:20K24
_T_381*R(:


iodmem_resp_type
0
0FPU.scala 535:52>2'
_T_383R


_T_381	

0FPU.scala 535:34ZD
load_wb_single
	

clock"	

0*

load_wb_singleReg.scala 34:16i:S
:


iodmem_resp_val6z 


load_wb_single


_T_383Reg.scala 35:23Reg.scala 35:19V@
load_wb_data
@	

clock"	

0*

load_wb_dataReg.scala 34:16w:a
:


iodmem_resp_valDz.


load_wb_data:


iodmem_resp_dataReg.scala 35:23Reg.scala 35:19T>
load_wb_tag
	

clock"	

0*

load_wb_tagReg.scala 34:16u:_
:


iodmem_resp_valBz,


load_wb_tag:


iodmem_resp_tagReg.scala 35:23Reg.scala 35:19J2,
_T_387"R 

load_wb_data
31
31recFNFromFN.scala 47:22J2,
_T_388"R 

load_wb_data
30
23recFNFromFN.scala 48:23I2+
_T_389!R

load_wb_data
22
0recFNFromFN.scala 49:25E2'
_T_391R


_T_388	

0recFNFromFN.scala 51:34E2'
_T_393R


_T_389	

0recFNFromFN.scala 52:38D2&
_T_394R


_T_391


_T_393recFNFromFN.scala 53:34=2
_T_395R


_T_389
9recFNFromFN.scala 56:26D2&
_T_396R


_T_395
31
16CircuitMath.scala 35:17C2%
_T_397R


_T_395
15
0CircuitMath.scala 36:17E2'
_T_399R


_T_396	

0CircuitMath.scala 37:22C2%
_T_400R


_T_396
15
8CircuitMath.scala 35:17B2$
_T_401R


_T_396
7
0CircuitMath.scala 36:17E2'
_T_403R


_T_400	

0CircuitMath.scala 37:22B2$
_T_404R


_T_400
7
4CircuitMath.scala 35:17B2$
_T_405R


_T_400
3
0CircuitMath.scala 36:17E2'
_T_407R


_T_404	

0CircuitMath.scala 37:22B2$
_T_408R


_T_404
3
3CircuitMath.scala 32:12B2$
_T_410R


_T_404
2
2CircuitMath.scala 32:12A2$
_T_412R


_T_404
1
1CircuitMath.scala 30:8O21
_T_413'2%



_T_410	

2


_T_412CircuitMath.scala 32:10O21
_T_414'2%



_T_408	

3


_T_413CircuitMath.scala 32:10B2$
_T_415R


_T_405
3
3CircuitMath.scala 32:12B2$
_T_417R


_T_405
2
2CircuitMath.scala 32:12A2$
_T_419R


_T_405
1
1CircuitMath.scala 30:8O21
_T_420'2%



_T_417	

2


_T_419CircuitMath.scala 32:10O21
_T_421'2%



_T_415	

3


_T_420CircuitMath.scala 32:10N20
_T_422&2$



_T_407


_T_414


_T_421CircuitMath.scala 38:21<2&
_T_423R


_T_407


_T_422Cat.scala 30:58B2$
_T_424R


_T_401
7
4CircuitMath.scala 35:17B2$
_T_425R


_T_401
3
0CircuitMath.scala 36:17E2'
_T_427R


_T_424	

0CircuitMath.scala 37:22B2$
_T_428R


_T_424
3
3CircuitMath.scala 32:12B2$
_T_430R


_T_424
2
2CircuitMath.scala 32:12A2$
_T_432R


_T_424
1
1CircuitMath.scala 30:8O21
_T_433'2%



_T_430	

2


_T_432CircuitMath.scala 32:10O21
_T_434'2%



_T_428	

3


_T_433CircuitMath.scala 32:10B2$
_T_435R


_T_425
3
3CircuitMath.scala 32:12B2$
_T_437R


_T_425
2
2CircuitMath.scala 32:12A2$
_T_439R


_T_425
1
1CircuitMath.scala 30:8O21
_T_440'2%



_T_437	

2


_T_439CircuitMath.scala 32:10O21
_T_441'2%



_T_435	

3


_T_440CircuitMath.scala 32:10N20
_T_442&2$



_T_427


_T_434


_T_441CircuitMath.scala 38:21<2&
_T_443R


_T_427


_T_442Cat.scala 30:58N20
_T_444&2$



_T_403


_T_423


_T_443CircuitMath.scala 38:21<2&
_T_445R


_T_403


_T_444Cat.scala 30:58C2%
_T_446R


_T_397
15
8CircuitMath.scala 35:17B2$
_T_447R


_T_397
7
0CircuitMath.scala 36:17E2'
_T_449R


_T_446	

0CircuitMath.scala 37:22B2$
_T_450R


_T_446
7
4CircuitMath.scala 35:17B2$
_T_451R


_T_446
3
0CircuitMath.scala 36:17E2'
_T_453R


_T_450	

0CircuitMath.scala 37:22B2$
_T_454R


_T_450
3
3CircuitMath.scala 32:12B2$
_T_456R


_T_450
2
2CircuitMath.scala 32:12A2$
_T_458R


_T_450
1
1CircuitMath.scala 30:8O21
_T_459'2%



_T_456	

2


_T_458CircuitMath.scala 32:10O21
_T_460'2%



_T_454	

3


_T_459CircuitMath.scala 32:10B2$
_T_461R


_T_451
3
3CircuitMath.scala 32:12B2$
_T_463R


_T_451
2
2CircuitMath.scala 32:12A2$
_T_465R


_T_451
1
1CircuitMath.scala 30:8O21
_T_466'2%



_T_463	

2


_T_465CircuitMath.scala 32:10O21
_T_467'2%



_T_461	

3


_T_466CircuitMath.scala 32:10N20
_T_468&2$



_T_453


_T_460


_T_467CircuitMath.scala 38:21<2&
_T_469R


_T_453


_T_468Cat.scala 30:58B2$
_T_470R


_T_447
7
4CircuitMath.scala 35:17B2$
_T_471R


_T_447
3
0CircuitMath.scala 36:17E2'
_T_473R


_T_470	

0CircuitMath.scala 37:22B2$
_T_474R


_T_470
3
3CircuitMath.scala 32:12B2$
_T_476R


_T_470
2
2CircuitMath.scala 32:12A2$
_T_478R


_T_470
1
1CircuitMath.scala 30:8O21
_T_479'2%



_T_476	

2


_T_478CircuitMath.scala 32:10O21
_T_480'2%



_T_474	

3


_T_479CircuitMath.scala 32:10B2$
_T_481R


_T_471
3
3CircuitMath.scala 32:12B2$
_T_483R


_T_471
2
2CircuitMath.scala 32:12A2$
_T_485R


_T_471
1
1CircuitMath.scala 30:8O21
_T_486'2%



_T_483	

2


_T_485CircuitMath.scala 32:10O21
_T_487'2%



_T_481	

3


_T_486CircuitMath.scala 32:10N20
_T_488&2$



_T_473


_T_480


_T_487CircuitMath.scala 38:21<2&
_T_489R


_T_473


_T_488Cat.scala 30:58N20
_T_490&2$



_T_449


_T_469


_T_489CircuitMath.scala 38:21<2&
_T_491R


_T_449


_T_490Cat.scala 30:58N20
_T_492&2$



_T_399


_T_445


_T_491CircuitMath.scala 38:21<2&
_T_493R


_T_399


_T_492Cat.scala 30:5882
_T_494R


_T_493recFNFromFN.scala 56:13D2&
_T_495R



_T_389


_T_494recFNFromFN.scala 58:25C2%
_T_496R


_T_495
21
0recFNFromFN.scala 58:37=2'
_T_498R


_T_496	

0Cat.scala 30:58O25
_T_503+2)
	

1

511		

0	Bitwise.scala 71:12D2&
_T_504R


_T_494


_T_503recFNFromFN.scala 62:27N20
_T_505&2$



_T_391


_T_504


_T_388recFNFromFN.scala 61:16P22
_T_509(2&



_T_391	

2	

1recFNFromFN.scala 64:47G2)
_T_510R

128


_T_509recFNFromFN.scala 64:42D2&
_T_511R


_T_505


_T_510recFNFromFN.scala 64:15=2
_T_512R


_T_511
1recFNFromFN.scala 64:15B2$
_T_513R


_T_512
8
7recFNFromFN.scala 67:25E2'
_T_515R


_T_513	

3recFNFromFN.scala 67:50E2'
_T_517R


_T_393	

0recFNFromFN.scala 68:17D2&
_T_518R


_T_515


_T_517recFNFromFN.scala 67:63>2$
_T_519R


_T_394
0
0Bitwise.scala 71:15L22
_T_522(2&



_T_519	

7	

0Bitwise.scala 71:12=2
_T_523R


_T_522
6recFNFromFN.scala 71:4582
_T_524R


_T_523recFNFromFN.scala 71:28D2&
_T_525R


_T_512


_T_524recFNFromFN.scala 71:26=2
_T_526R


_T_518
6recFNFromFN.scala 72:22D2&
_T_527R


_T_525


_T_526recFNFromFN.scala 71:64N20
_T_528&2$



_T_391


_T_498


_T_389recFNFromFN.scala 73:27<2&
_T_529R


_T_387


_T_527Cat.scala 30:58;2%
rec_sR


_T_529


_T_528Cat.scala 30:58J2,
_T_530"R 

load_wb_data
63
63recFNFromFN.scala 47:22J2,
_T_531"R 

load_wb_data
62
52recFNFromFN.scala 48:23I2+
_T_532!R

load_wb_data
51
0recFNFromFN.scala 49:25E2'
_T_534R


_T_531	

0recFNFromFN.scala 51:34E2'
_T_536R


_T_532	

0recFNFromFN.scala 52:38D2&
_T_537R


_T_534


_T_536recFNFromFN.scala 53:34>2 
_T_538R


_T_532
12recFNFromFN.scala 56:26D2&
_T_539R


_T_538
63
32CircuitMath.scala 35:17C2%
_T_540R


_T_538
31
0CircuitMath.scala 36:17E2'
_T_542R


_T_539	

0CircuitMath.scala 37:22D2&
_T_543R


_T_539
31
16CircuitMath.scala 35:17C2%
_T_544R


_T_539
15
0CircuitMath.scala 36:17E2'
_T_546R


_T_543	

0CircuitMath.scala 37:22C2%
_T_547R


_T_543
15
8CircuitMath.scala 35:17B2$
_T_548R


_T_543
7
0CircuitMath.scala 36:17E2'
_T_550R


_T_547	

0CircuitMath.scala 37:22B2$
_T_551R


_T_547
7
4CircuitMath.scala 35:17B2$
_T_552R


_T_547
3
0CircuitMath.scala 36:17E2'
_T_554R


_T_551	

0CircuitMath.scala 37:22B2$
_T_555R


_T_551
3
3CircuitMath.scala 32:12B2$
_T_557R


_T_551
2
2CircuitMath.scala 32:12A2$
_T_559R


_T_551
1
1CircuitMath.scala 30:8O21
_T_560'2%



_T_557	

2


_T_559CircuitMath.scala 32:10O21
_T_561'2%



_T_555	

3


_T_560CircuitMath.scala 32:10B2$
_T_562R


_T_552
3
3CircuitMath.scala 32:12B2$
_T_564R


_T_552
2
2CircuitMath.scala 32:12A2$
_T_566R


_T_552
1
1CircuitMath.scala 30:8O21
_T_567'2%



_T_564	

2


_T_566CircuitMath.scala 32:10O21
_T_568'2%



_T_562	

3


_T_567CircuitMath.scala 32:10N20
_T_569&2$



_T_554


_T_561


_T_568CircuitMath.scala 38:21<2&
_T_570R


_T_554


_T_569Cat.scala 30:58B2$
_T_571R


_T_548
7
4CircuitMath.scala 35:17B2$
_T_572R


_T_548
3
0CircuitMath.scala 36:17E2'
_T_574R


_T_571	

0CircuitMath.scala 37:22B2$
_T_575R


_T_571
3
3CircuitMath.scala 32:12B2$
_T_577R


_T_571
2
2CircuitMath.scala 32:12A2$
_T_579R


_T_571
1
1CircuitMath.scala 30:8O21
_T_580'2%



_T_577	

2


_T_579CircuitMath.scala 32:10O21
_T_581'2%



_T_575	

3


_T_580CircuitMath.scala 32:10B2$
_T_582R


_T_572
3
3CircuitMath.scala 32:12B2$
_T_584R


_T_572
2
2CircuitMath.scala 32:12A2$
_T_586R


_T_572
1
1CircuitMath.scala 30:8O21
_T_587'2%



_T_584	

2


_T_586CircuitMath.scala 32:10O21
_T_588'2%



_T_582	

3


_T_587CircuitMath.scala 32:10N20
_T_589&2$



_T_574


_T_581


_T_588CircuitMath.scala 38:21<2&
_T_590R


_T_574


_T_589Cat.scala 30:58N20
_T_591&2$



_T_550


_T_570


_T_590CircuitMath.scala 38:21<2&
_T_592R


_T_550


_T_591Cat.scala 30:58C2%
_T_593R


_T_544
15
8CircuitMath.scala 35:17B2$
_T_594R


_T_544
7
0CircuitMath.scala 36:17E2'
_T_596R


_T_593	

0CircuitMath.scala 37:22B2$
_T_597R


_T_593
7
4CircuitMath.scala 35:17B2$
_T_598R


_T_593
3
0CircuitMath.scala 36:17E2'
_T_600R


_T_597	

0CircuitMath.scala 37:22B2$
_T_601R


_T_597
3
3CircuitMath.scala 32:12B2$
_T_603R


_T_597
2
2CircuitMath.scala 32:12A2$
_T_605R


_T_597
1
1CircuitMath.scala 30:8O21
_T_606'2%



_T_603	

2


_T_605CircuitMath.scala 32:10O21
_T_607'2%



_T_601	

3


_T_606CircuitMath.scala 32:10B2$
_T_608R


_T_598
3
3CircuitMath.scala 32:12B2$
_T_610R


_T_598
2
2CircuitMath.scala 32:12A2$
_T_612R


_T_598
1
1CircuitMath.scala 30:8O21
_T_613'2%



_T_610	

2


_T_612CircuitMath.scala 32:10O21
_T_614'2%



_T_608	

3


_T_613CircuitMath.scala 32:10N20
_T_615&2$



_T_600


_T_607


_T_614CircuitMath.scala 38:21<2&
_T_616R


_T_600


_T_615Cat.scala 30:58B2$
_T_617R


_T_594
7
4CircuitMath.scala 35:17B2$
_T_618R


_T_594
3
0CircuitMath.scala 36:17E2'
_T_620R


_T_617	

0CircuitMath.scala 37:22B2$
_T_621R


_T_617
3
3CircuitMath.scala 32:12B2$
_T_623R


_T_617
2
2CircuitMath.scala 32:12A2$
_T_625R


_T_617
1
1CircuitMath.scala 30:8O21
_T_626'2%



_T_623	

2


_T_625CircuitMath.scala 32:10O21
_T_627'2%



_T_621	

3


_T_626CircuitMath.scala 32:10B2$
_T_628R


_T_618
3
3CircuitMath.scala 32:12B2$
_T_630R


_T_618
2
2CircuitMath.scala 32:12A2$
_T_632R


_T_618
1
1CircuitMath.scala 30:8O21
_T_633'2%



_T_630	

2


_T_632CircuitMath.scala 32:10O21
_T_634'2%



_T_628	

3


_T_633CircuitMath.scala 32:10N20
_T_635&2$



_T_620


_T_627


_T_634CircuitMath.scala 38:21<2&
_T_636R


_T_620


_T_635Cat.scala 30:58N20
_T_637&2$



_T_596


_T_616


_T_636CircuitMath.scala 38:21<2&
_T_638R


_T_596


_T_637Cat.scala 30:58N20
_T_639&2$



_T_546


_T_592


_T_638CircuitMath.scala 38:21<2&
_T_640R


_T_546


_T_639Cat.scala 30:58D2&
_T_641R


_T_540
31
16CircuitMath.scala 35:17C2%
_T_642R


_T_540
15
0CircuitMath.scala 36:17E2'
_T_644R


_T_641	

0CircuitMath.scala 37:22C2%
_T_645R


_T_641
15
8CircuitMath.scala 35:17B2$
_T_646R


_T_641
7
0CircuitMath.scala 36:17E2'
_T_648R


_T_645	

0CircuitMath.scala 37:22B2$
_T_649R


_T_645
7
4CircuitMath.scala 35:17B2$
_T_650R


_T_645
3
0CircuitMath.scala 36:17E2'
_T_652R


_T_649	

0CircuitMath.scala 37:22B2$
_T_653R


_T_649
3
3CircuitMath.scala 32:12B2$
_T_655R


_T_649
2
2CircuitMath.scala 32:12A2$
_T_657R


_T_649
1
1CircuitMath.scala 30:8O21
_T_658'2%



_T_655	

2


_T_657CircuitMath.scala 32:10O21
_T_659'2%



_T_653	

3


_T_658CircuitMath.scala 32:10B2$
_T_660R


_T_650
3
3CircuitMath.scala 32:12B2$
_T_662R


_T_650
2
2CircuitMath.scala 32:12A2$
_T_664R


_T_650
1
1CircuitMath.scala 30:8O21
_T_665'2%



_T_662	

2


_T_664CircuitMath.scala 32:10O21
_T_666'2%



_T_660	

3


_T_665CircuitMath.scala 32:10N20
_T_667&2$



_T_652


_T_659


_T_666CircuitMath.scala 38:21<2&
_T_668R


_T_652


_T_667Cat.scala 30:58B2$
_T_669R


_T_646
7
4CircuitMath.scala 35:17B2$
_T_670R


_T_646
3
0CircuitMath.scala 36:17E2'
_T_672R


_T_669	

0CircuitMath.scala 37:22B2$
_T_673R


_T_669
3
3CircuitMath.scala 32:12B2$
_T_675R


_T_669
2
2CircuitMath.scala 32:12A2$
_T_677R


_T_669
1
1CircuitMath.scala 30:8O21
_T_678'2%



_T_675	

2


_T_677CircuitMath.scala 32:10O21
_T_679'2%



_T_673	

3


_T_678CircuitMath.scala 32:10B2$
_T_680R


_T_670
3
3CircuitMath.scala 32:12B2$
_T_682R


_T_670
2
2CircuitMath.scala 32:12A2$
_T_684R


_T_670
1
1CircuitMath.scala 30:8O21
_T_685'2%



_T_682	

2


_T_684CircuitMath.scala 32:10O21
_T_686'2%



_T_680	

3


_T_685CircuitMath.scala 32:10N20
_T_687&2$



_T_672


_T_679


_T_686CircuitMath.scala 38:21<2&
_T_688R


_T_672


_T_687Cat.scala 30:58N20
_T_689&2$



_T_648


_T_668


_T_688CircuitMath.scala 38:21<2&
_T_690R


_T_648


_T_689Cat.scala 30:58C2%
_T_691R


_T_642
15
8CircuitMath.scala 35:17B2$
_T_692R


_T_642
7
0CircuitMath.scala 36:17E2'
_T_694R


_T_691	

0CircuitMath.scala 37:22B2$
_T_695R


_T_691
7
4CircuitMath.scala 35:17B2$
_T_696R


_T_691
3
0CircuitMath.scala 36:17E2'
_T_698R


_T_695	

0CircuitMath.scala 37:22B2$
_T_699R


_T_695
3
3CircuitMath.scala 32:12B2$
_T_701R


_T_695
2
2CircuitMath.scala 32:12A2$
_T_703R


_T_695
1
1CircuitMath.scala 30:8O21
_T_704'2%



_T_701	

2


_T_703CircuitMath.scala 32:10O21
_T_705'2%



_T_699	

3


_T_704CircuitMath.scala 32:10B2$
_T_706R


_T_696
3
3CircuitMath.scala 32:12B2$
_T_708R


_T_696
2
2CircuitMath.scala 32:12A2$
_T_710R


_T_696
1
1CircuitMath.scala 30:8O21
_T_711'2%



_T_708	

2


_T_710CircuitMath.scala 32:10O21
_T_712'2%



_T_706	

3


_T_711CircuitMath.scala 32:10N20
_T_713&2$



_T_698


_T_705


_T_712CircuitMath.scala 38:21<2&
_T_714R


_T_698


_T_713Cat.scala 30:58B2$
_T_715R


_T_692
7
4CircuitMath.scala 35:17B2$
_T_716R


_T_692
3
0CircuitMath.scala 36:17E2'
_T_718R


_T_715	

0CircuitMath.scala 37:22B2$
_T_719R


_T_715
3
3CircuitMath.scala 32:12B2$
_T_721R


_T_715
2
2CircuitMath.scala 32:12A2$
_T_723R


_T_715
1
1CircuitMath.scala 30:8O21
_T_724'2%



_T_721	

2


_T_723CircuitMath.scala 32:10O21
_T_725'2%



_T_719	

3


_T_724CircuitMath.scala 32:10B2$
_T_726R


_T_716
3
3CircuitMath.scala 32:12B2$
_T_728R


_T_716
2
2CircuitMath.scala 32:12A2$
_T_730R


_T_716
1
1CircuitMath.scala 30:8O21
_T_731'2%



_T_728	

2


_T_730CircuitMath.scala 32:10O21
_T_732'2%



_T_726	

3


_T_731CircuitMath.scala 32:10N20
_T_733&2$



_T_718


_T_725


_T_732CircuitMath.scala 38:21<2&
_T_734R


_T_718


_T_733Cat.scala 30:58N20
_T_735&2$



_T_694


_T_714


_T_734CircuitMath.scala 38:21<2&
_T_736R


_T_694


_T_735Cat.scala 30:58N20
_T_737&2$



_T_644


_T_690


_T_736CircuitMath.scala 38:21<2&
_T_738R


_T_644


_T_737Cat.scala 30:58N20
_T_739&2$



_T_542


_T_640


_T_738CircuitMath.scala 38:21<2&
_T_740R


_T_542


_T_739Cat.scala 30:5882
_T_741R


_T_740recFNFromFN.scala 56:13D2&
_T_742R



_T_532


_T_741recFNFromFN.scala 58:25C2%
_T_743R


_T_742
50
0recFNFromFN.scala 58:37=2'
_T_745R


_T_743	

0Cat.scala 30:58P26
_T_750,2*
	

1

4095	

0Bitwise.scala 71:12D2&
_T_751R


_T_741


_T_750recFNFromFN.scala 62:27N20
_T_752&2$



_T_534


_T_751


_T_531recFNFromFN.scala 61:16P22
_T_756(2&



_T_534	

2	

1recFNFromFN.scala 64:47H2*
_T_757 R

1024


_T_756recFNFromFN.scala 64:42D2&
_T_758R


_T_752


_T_757recFNFromFN.scala 64:15=2
_T_759R


_T_758
1recFNFromFN.scala 64:15D2&
_T_760R


_T_759
11
10recFNFromFN.scala 67:25E2'
_T_762R


_T_760	

3recFNFromFN.scala 67:50E2'
_T_764R


_T_536	

0recFNFromFN.scala 68:17D2&
_T_765R


_T_762


_T_764recFNFromFN.scala 67:63>2$
_T_766R


_T_537
0
0Bitwise.scala 71:15L22
_T_769(2&



_T_766	

7	

0Bitwise.scala 71:12=2
_T_770R


_T_769
9recFNFromFN.scala 71:4582
_T_771R


_T_770recFNFromFN.scala 71:28D2&
_T_772R


_T_759


_T_771recFNFromFN.scala 71:26=2
_T_773R


_T_765
9recFNFromFN.scala 72:22D2&
_T_774R


_T_772


_T_773recFNFromFN.scala 71:64N20
_T_775&2$



_T_534


_T_745


_T_532recFNFromFN.scala 73:27<2&
_T_776R


_T_530


_T_774Cat.scala 30:58<2&
_T_777R


_T_776


_T_775Cat.scala 30:58P29
_T_779/R-	

rec_s

16142026964402700288AFPU.scala 543:33]2F
load_wb_data_recoded.2,


load_wb_single


_T_779


_T_777FPU.scala 543:10XA
regfile
A 2_T_8492_T_8532_T_857:_T_782:_T_1131J
 FPU.scala 547:20;#
!:
:

	
regfile_T_849addrFPU.scala 547:20:"
 :
:

	
regfile_T_849clkFPU.scala 547:20;#
!:
:

	
regfile_T_853addrFPU.scala 547:20:"
 :
:

	
regfile_T_853clkFPU.scala 547:20;#
!:
:

	
regfile_T_857addrFPU.scala 547:20:"
 :
:

	
regfile_T_857clkFPU.scala 547:20Ez.
:
:

	
regfile_T_849en	

0FPU.scala 547:20Ez.
:
:

	
regfile_T_853en	

0FPU.scala 547:20Ez.
:
:

	
regfile_T_857en	

0FPU.scala 547:20;#
!:
:

	
regfile_T_782addrFPU.scala 547:20:"
 :
:

	
regfile_T_782clkFPU.scala 547:20<$
": 
:

	
regfile_T_1131addrFPU.scala 547:20;#
!:
:

	
regfile_T_1131clkFPU.scala 547:20Ez.
:
:

	
regfile_T_782en	

0FPU.scala 547:20Fz/
 :
:

	
regfile_T_1131en	

0FPU.scala 547:20;#
!:
:

	
regfile_T_782dataFPU.scala 547:20;#
!:
:

	
regfile_T_782maskFPU.scala 547:20<$
": 
:

	
regfile_T_1131dataFPU.scala 547:20<$
": 
:

	
regfile_T_1131maskFPU.scala 547:20§:

	
load_wb;z4
!:
:

	
regfile_T_782addr

load_wb_tag
 4z-
 :
:

	
regfile_T_782clk	

clock
 5z.
:
:

	
regfile_T_782en	

1
 7z0
!:
:

	
regfile_T_782mask	

0
 Tz=
!:
:

	
regfile_T_782data

load_wb_data_recodedFPU.scala 549:26Gz0
!:
:

	
regfile_T_782mask	

1FPU.scala 549:26FPU.scala 548:18K4
ex_ra1
	

clock"	

0*


ex_ra1FPU.scala 554:53K4
ex_ra2
	

clock"	

0*


ex_ra2FPU.scala 554:53K4
ex_ra3
	

clock"	

0*


ex_ra3FPU.scala 554:53Ò:º
:


iovalid:ê
*:(
 :
:



fp_decoderiosigsren1`2I
_T_787?R=,:*
 :
:



fp_decoderiosigsswap12	

0FPU.scala 557:13:



_T_787C2,
_T_788"R :


ioinst
19
15FPU.scala 557:49/z



ex_ra1


_T_788FPU.scala 557:39FPU.scala 557:30¼:¤
,:*
 :
:



fp_decoderiosigsswap12C2,
_T_789"R :


ioinst
19
15FPU.scala 558:48/z



ex_ra2


_T_789FPU.scala 558:38FPU.scala 558:29FPU.scala 556:25â:Ê
*:(
 :
:



fp_decoderiosigsren2¼:¤
,:*
 :
:



fp_decoderiosigsswap12C2,
_T_790"R :


ioinst
24
20FPU.scala 561:48/z



ex_ra1


_T_790FPU.scala 561:38FPU.scala 561:29¼:¤
,:*
 :
:



fp_decoderiosigsswap23C2,
_T_791"R :


ioinst
24
20FPU.scala 562:48/z



ex_ra3


_T_791FPU.scala 562:38FPU.scala 562:29`2I
_T_793?R=,:*
 :
:



fp_decoderiosigsswap12	

0FPU.scala 563:13`2I
_T_795?R=,:*
 :
:



fp_decoderiosigsswap23	

0FPU.scala 563:32=2&
_T_796R


_T_793


_T_795FPU.scala 563:29:



_T_796C2,
_T_797"R :


ioinst
24
20FPU.scala 563:68/z



ex_ra2


_T_797FPU.scala 563:58FPU.scala 563:49FPU.scala 560:25º:¢
*:(
 :
:



fp_decoderiosigsren3C2,
_T_798"R :


ioinst
31
27FPU.scala 565:44/z



ex_ra3


_T_798FPU.scala 565:34FPU.scala 565:25FPU.scala 555:19B2+
_T_799!R

ex_reg_inst
14
12FPU.scala 567:30>2'
_T_801R


_T_799	

7FPU.scala 567:38B2+
_T_802!R

ex_reg_inst
14
12FPU.scala 567:74O28
ex_rm/2-



_T_801:


iofcsr_rm


_T_802FPU.scala 567:18ù
á
reqÙ*Ö
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags

rm

typ

in1
A
in2
A
in3
AFPU.scala 569:17!	


reqFPU.scala 569:17-


req
	
ex_ctrlFPU.scala 570:73z
:


reqrm	

ex_rmFPU.scala 571:10.2'
_T_847R


ex_ra1	

0
 +2$
_T_848R


_T_847
4
0
 6z/
!:
:

	
regfile_T_849addr


_T_848
 4z-
 :
:

	
regfile_T_849clk	

clock
 5z.
:
:

	
regfile_T_849en	

1
 Lz5
:


reqin1!:
:

	
regfile_T_849dataFPU.scala 572:11.2'
_T_851R


ex_ra2	

0
 +2$
_T_852R


_T_851
4
0
 6z/
!:
:

	
regfile_T_853addr


_T_852
 4z-
 :
:

	
regfile_T_853clk	

clock
 5z.
:
:

	
regfile_T_853en	

1
 Lz5
:


reqin2!:
:

	
regfile_T_853dataFPU.scala 573:11.2'
_T_855R


ex_ra3	

0
 +2$
_T_856R


_T_855
4
0
 6z/
!:
:

	
regfile_T_857addr


_T_856
 4z-
 :
:

	
regfile_T_857clk	

clock
 5z.
:
:

	
regfile_T_857en	

1
 Lz5
:


reqin3!:
:

	
regfile_T_857dataFPU.scala 574:11B2+
_T_858!R

ex_reg_inst
21
20FPU.scala 575:255z
:


reqtyp


_T_858FPU.scala 575:11Ò:º


ex_cp_valid>'


req:
:


iocp_reqbitsFPU.scala 577:9æ:Î
(:&
:
:


iocp_reqbitsswap23Pz9
:


reqin2%:#
:
:


iocp_reqbitsin3FPU.scala 579:15Pz9
:


reqin3%:#
:
:


iocp_reqbitsin2FPU.scala 580:15FPU.scala 578:34FPU.scala 576:22)*
sfma
FPUFMAPipeFPU.scala 584:20
:


sfmaio
 'z 
:


sfmaclock	

clock
 'z 
:


sfmareset	

reset
 J23
_T_859)R'

	req_valid:

	
ex_ctrlfmaFPU.scala 585:33J23
_T_860)R'


_T_859:

	
ex_ctrlsingleFPU.scala 585:48Hz1
#:!
:
:


sfmaioinvalid


_T_860FPU.scala 585:20E-
": 
:
:


sfmaioinbits

reqFPU.scala 586:19&*
fpiuFPToIntFPU.scala 588:20
:


fpiuio
 'z 
:


fpiuclock	

clock
 'z 
:


fpiureset	

reset
 S2<
_T_8612R0:

	
ex_ctrltoint:

	
ex_ctrldivFPU.scala 589:51H21
_T_862'R%


_T_861:

	
ex_ctrlsqrtFPU.scala 589:66I22
_T_865(R&:

	
ex_ctrlcmd


13FPU.scala 589:97>2'
_T_866R	

5


_T_865FPU.scala 589:97=2&
_T_867R


_T_862


_T_866FPU.scala 589:82@2)
_T_868R

	req_valid


_T_867FPU.scala 589:33Hz1
#:!
:
:


fpiuioinvalid


_T_868FPU.scala 589:20E-
": 
:
:


fpiuioinbits

reqFPU.scala 590:19_zH
:


io
store_data.:,
#:!
:
:


fpiuiooutbitsstoreFPU.scala 591:17_zH
:


io
toint_data.:,
#:!
:
:


fpiuiooutbitstointFPU.scala 592:17]2F
_T_869<R:$:"
:
:


fpiuiooutvalid

mem_cp_validFPU.scala 593:26J23
_T_870)R'


_T_869:



mem_ctrltointFPU.scala 593:42Ü:Ä



_T_870pzY
':%
:
:


iocp_respbitsdata.:,
#:!
:
:


fpiuiooutbitstointFPU.scala 594:26Dz-
:
:


iocp_respvalid	

1FPU.scala 595:22FPU.scala 593:60&*
ifpuIntToFPFPU.scala 598:20
:


ifpuio
 'z 
:


ifpuclock	

clock
 'z 
:


ifpureset	

reset
 N27
_T_872-R+

	req_valid:

	
ex_ctrlfromintFPU.scala 599:33Hz1
#:!
:
:


ifpuioinvalid


_T_872FPU.scala 599:20E-
": 
:
:


ifpuioinbits

reqFPU.scala 600:19u2^
_T_873T2R


ex_cp_valid%:#
:
:


iocp_reqbitsin1:


iofromint_dataFPU.scala 601:29Pz9
+:)
": 
:
:


ifpuioinbitsin1


_T_873FPU.scala 601:23%*
fpmuFPToFPFPU.scala 603:20
:


fpmuio
 'z 
:


fpmuclock	

clock
 'z 
:


fpmureset	

reset
 O28
_T_874.R,

	req_valid:

	
ex_ctrlfastpipeFPU.scala 604:33Hz1
#:!
:
:


fpmuioinvalid


_T_874FPU.scala 604:20E-
": 
:
:


fpmuioinbits

reqFPU.scala 605:19^zG
:
:


fpmuiolt+:)
#:!
:
:


fpiuiooutbitsltFPU.scala 606:14U>
divSqrt_wen
	

clock"	

0*

divSqrt_wenFPU.scala 608:245z


divSqrt_wen	

0FPU.scala 608:24 

divSqrt_inReady

 


divSqrt_inReady
 )z"


divSqrt_inReady	

0
 YB
divSqrt_waddr
	

clock"	

0*

divSqrt_waddrFPU.scala 610:26[D
divSqrt_single
	

clock"	

0*

divSqrt_singleFPU.scala 611:27.

divSqrt_wdata
AFPU.scala 612:27+


divSqrt_wdataFPU.scala 612:27.

divSqrt_flags
FPU.scala 613:27+


divSqrt_flagsFPU.scala 613:27U>
divSqrt_in_flight
	

clock"	

reset*	

0FPU.scala 614:30[D
divSqrt_killed
	

clock"	

0*

divSqrt_killedFPU.scala 615:271*

FPUFMAPipeFPUFMAPipe_1FPU.scala 624:28 
:



FPUFMAPipeio
 -z&
:



FPUFMAPipeclock	

clock
 -z&
:



FPUFMAPipereset	

reset
 J23
_T_883)R'

	req_valid:

	
ex_ctrlfmaFPU.scala 625:41K24
_T_885*R(:

	
ex_ctrlsingle	

0FPU.scala 625:59=2&
_T_886R


_T_883


_T_885FPU.scala 625:56Nz7
):'
:
:



FPUFMAPipeioinvalid


_T_886FPU.scala 625:28K3
(:&
:
:



FPUFMAPipeioinbits

reqFPU.scala 626:27Y2B
_T_889826
:



mem_ctrlfastpipe	

1	

0FPU.scala 631:23X2A
_T_892725
:



mem_ctrlfromint	

1	

0FPU.scala 631:23V2?
_T_8935R3:



mem_ctrlfma:



mem_ctrlsingleFPU.scala 622:56I22
_T_896(2&



_T_893	

2	

0FPU.scala 631:23L25
_T_898+R):



mem_ctrlsingle	

0FPU.scala 627:65H21
_T_899'R%:



mem_ctrlfma


_T_898FPU.scala 627:62I22
_T_902(2&



_T_899	

4	

0FPU.scala 631:23=2&
_T_903R


_T_889


_T_892FPU.scala 631:78=2&
_T_904R


_T_903


_T_896FPU.scala 631:78E2.
memLatencyMaskR


_T_904


_T_902FPU.scala 631:78G0
wen
	

clock"	

reset*	

0FPU.scala 645:16v
wbInfoH2F
B*@
rd

single

cp

pipeid
	

clock"	

0*


wbInfoFPU.scala 646:19X2A
_T_9667R5:



mem_ctrlfma:



mem_ctrlfastpipeFPU.scala 647:48L25
_T_967+R)


_T_966:



mem_ctrlfromintFPU.scala 647:69E2.
mem_wen#R!

mem_reg_valid


_T_967FPU.scala 647:31X2A
_T_970725
:

	
ex_ctrlfastpipe	

2	

0FPU.scala 631:23W2@
_T_973624
:

	
ex_ctrlfromint	

2	

0FPU.scala 631:23T2=
_T_9743R1:

	
ex_ctrlfma:

	
ex_ctrlsingleFPU.scala 622:56I22
_T_977(2&



_T_974	

4	

0FPU.scala 631:23K24
_T_979*R(:

	
ex_ctrlsingle	

0FPU.scala 627:65G20
_T_980&R$:

	
ex_ctrlfma


_T_979FPU.scala 627:62I22
_T_983(2&



_T_980	

8	

0FPU.scala 631:23=2&
_T_984R


_T_970


_T_973FPU.scala 631:78=2&
_T_985R


_T_984


_T_977FPU.scala 631:78=2&
_T_986R


_T_985


_T_983FPU.scala 631:78E2.
_T_987$R"

memLatencyMask


_T_986FPU.scala 648:62>2'
_T_989R


_T_987	

0FPU.scala 648:89>2'
_T_990R
	
mem_wen


_T_989FPU.scala 648:43X2A
_T_993725
:

	
ex_ctrlfastpipe	

4	

0FPU.scala 631:23W2@
_T_996624
:

	
ex_ctrlfromint	

4	

0FPU.scala 631:23T2=
_T_9973R1:

	
ex_ctrlfma:

	
ex_ctrlsingleFPU.scala 622:56J23
_T_1000(2&



_T_997	

8	

0FPU.scala 631:23L25
_T_1002*R(:

	
ex_ctrlsingle	

0FPU.scala 627:65I22
_T_1003'R%:

	
ex_ctrlfma
	
_T_1002FPU.scala 627:62L25
_T_1006*2(

	
_T_1003


16	

0FPU.scala 631:23>2'
_T_1007R


_T_993


_T_996FPU.scala 631:78@2)
_T_1008R
	
_T_1007
	
_T_1000FPU.scala 631:78@2)
_T_1009R
	
_T_1008
	
_T_1006FPU.scala 631:78=2%
_T_1010R

wen
	
_T_1009FPU.scala 648:101A2)
_T_1012R
	
_T_1010	

0FPU.scala 648:128?2(
_T_1013R


_T_990
	
_T_1012FPU.scala 648:93\F
write_port_busy
	

clock"	

0*

write_port_busyReg.scala 34:16_:I


	req_valid8z"


write_port_busy
	
_T_1013Reg.scala 35:23Reg.scala 35:1992"
_T_1015R

wen
1
1FPU.scala 651:14h:Q

	
_T_1015B*
B



wbInfo
0B



wbInfo
1FPU.scala 651:33FPU.scala 651:2192"
_T_1016R

wen
2
2FPU.scala 651:14h:Q

	
_T_1016B*
B



wbInfo
1B



wbInfo
2FPU.scala 651:33FPU.scala 651:2142
_T_1017R	

wen
1FPU.scala 653:14,z


wen
	
_T_1017FPU.scala 653:7#:ü"

	
mem_wen>2'
_T_1019R	

killm	

0FPU.scala 655:11Ó:»

	
_T_101942
_T_1020R	

wen
1FPU.scala 656:18G20
_T_1021%R#
	
_T_1020

memLatencyMaskFPU.scala 656:23-z


wen
	
_T_1021FPU.scala 656:11FPU.scala 655:19H21
_T_1023&R$

write_port_busy	

0FPU.scala 659:13D2-
_T_1024"R 

memLatencyMask
0
0FPU.scala 659:47@2)
_T_1025R
	
_T_1023
	
_T_1024FPU.scala 659:30	:	

	
_T_1025Fz/
:
B



wbInfo
0cp

mem_cp_validFPU.scala 660:22Rz;
:
B



wbInfo
0single:



mem_ctrlsingleFPU.scala 661:26Z2C
_T_1028826
:



mem_ctrlfastpipe	

0	

0FPU.scala 633:63Y2B
_T_1031725
:



mem_ctrlfromint	

1	

0FPU.scala 633:63W2@
_T_10325R3:



mem_ctrlfma:



mem_ctrlsingleFPU.scala 622:56K24
_T_1035)2'

	
_T_1032	

2	

0FPU.scala 633:63M26
_T_1037+R):



mem_ctrlsingle	

0FPU.scala 627:65J23
_T_1038(R&:



mem_ctrlfma
	
_T_1037FPU.scala 627:62K24
_T_1041)2'

	
_T_1038	

3	

0FPU.scala 633:63A2)
_T_1042R
	
_T_1028
	
_T_1031FPU.scala 633:108A2)
_T_1043R
	
_T_1042
	
_T_1035FPU.scala 633:108A2)
_T_1044R
	
_T_1043
	
_T_1041FPU.scala 633:108Ez.
:
B



wbInfo
0pipeid
	
_T_1044FPU.scala 662:26C2,
_T_1045!R

mem_reg_inst
11
7FPU.scala 663:37Az*
:
B



wbInfo
0rd
	
_T_1045FPU.scala 663:22FPU.scala 659:52H21
_T_1047&R$

write_port_busy	

0FPU.scala 659:13D2-
_T_1048"R 

memLatencyMask
1
1FPU.scala 659:47@2)
_T_1049R
	
_T_1047
	
_T_1048FPU.scala 659:30	:	

	
_T_1049Fz/
:
B



wbInfo
1cp

mem_cp_validFPU.scala 660:22Rz;
:
B



wbInfo
1single:



mem_ctrlsingleFPU.scala 661:26Z2C
_T_1052826
:



mem_ctrlfastpipe	

0	

0FPU.scala 633:63Y2B
_T_1055725
:



mem_ctrlfromint	

1	

0FPU.scala 633:63W2@
_T_10565R3:



mem_ctrlfma:



mem_ctrlsingleFPU.scala 622:56K24
_T_1059)2'

	
_T_1056	

2	

0FPU.scala 633:63M26
_T_1061+R):



mem_ctrlsingle	

0FPU.scala 627:65J23
_T_1062(R&:



mem_ctrlfma
	
_T_1061FPU.scala 627:62K24
_T_1065)2'

	
_T_1062	

3	

0FPU.scala 633:63A2)
_T_1066R
	
_T_1052
	
_T_1055FPU.scala 633:108A2)
_T_1067R
	
_T_1066
	
_T_1059FPU.scala 633:108A2)
_T_1068R
	
_T_1067
	
_T_1065FPU.scala 633:108Ez.
:
B



wbInfo
1pipeid
	
_T_1068FPU.scala 662:26C2,
_T_1069!R

mem_reg_inst
11
7FPU.scala 663:37Az*
:
B



wbInfo
1rd
	
_T_1069FPU.scala 663:22FPU.scala 659:52H21
_T_1071&R$

write_port_busy	

0FPU.scala 659:13D2-
_T_1072"R 

memLatencyMask
2
2FPU.scala 659:47@2)
_T_1073R
	
_T_1071
	
_T_1072FPU.scala 659:30	:	

	
_T_1073Fz/
:
B



wbInfo
2cp

mem_cp_validFPU.scala 660:22Rz;
:
B



wbInfo
2single:



mem_ctrlsingleFPU.scala 661:26Z2C
_T_1076826
:



mem_ctrlfastpipe	

0	

0FPU.scala 633:63Y2B
_T_1079725
:



mem_ctrlfromint	

1	

0FPU.scala 633:63W2@
_T_10805R3:



mem_ctrlfma:



mem_ctrlsingleFPU.scala 622:56K24
_T_1083)2'

	
_T_1080	

2	

0FPU.scala 633:63M26
_T_1085+R):



mem_ctrlsingle	

0FPU.scala 627:65J23
_T_1086(R&:



mem_ctrlfma
	
_T_1085FPU.scala 627:62K24
_T_1089)2'

	
_T_1086	

3	

0FPU.scala 633:63A2)
_T_1090R
	
_T_1076
	
_T_1079FPU.scala 633:108A2)
_T_1091R
	
_T_1090
	
_T_1083FPU.scala 633:108A2)
_T_1092R
	
_T_1091
	
_T_1089FPU.scala 633:108Ez.
:
B



wbInfo
2pipeid
	
_T_1092FPU.scala 662:26C2,
_T_1093!R

mem_reg_inst
11
7FPU.scala 663:37Az*
:
B



wbInfo
2rd
	
_T_1093FPU.scala 663:22FPU.scala 659:52FPU.scala 654:18c2L
waddrC2A


divSqrt_wen

divSqrt_waddr:
B



wbInfo
0rdFPU.scala 668:18W2=
_T_10952R0:
B



wbInfo
0pipeid	

1Package.scala 18:26W2=
_T_10972R0:
B



wbInfo
0pipeid	

2Package.scala 19:17C2)
_T_1099R
	
_T_1095	

0Package.scala 18:26C2)
_T_1101R
	
_T_1095	

1Package.scala 19:172~
_T_1102s2q

	
_T_11013:1
):'
:
:



FPUFMAPipeiooutbitsdata-:+
#:!
:
:


sfmaiooutbitsdataPackage.scala 19:12C2)
_T_1104R
	
_T_1095	

0Package.scala 18:26C2)
_T_1106R
	
_T_1095	

1Package.scala 19:172x
_T_1107m2k

	
_T_1106-:+
#:!
:
:


ifpuiooutbitsdata-:+
#:!
:
:


fpmuiooutbitsdataPackage.scala 19:12N24
_T_1108)2'

	
_T_1097
	
_T_1102
	
_T_1107Package.scala 19:12T2=
wdata0321


divSqrt_wen

divSqrt_wdata
	
_T_1108FPU.scala 669:19j2S
wsingleH2F


divSqrt_wen

divSqrt_single:
B



wbInfo
0singleFPU.scala 670:20=2&
_T_1109R


wdata0
32
0FPU.scala 673:36S2<
_T_11111R/
	
_T_1109

16142026964402700288AFPU.scala 673:44H21
wdata(2&

	
wsingle
	
_T_1111


wdata0FPU.scala 673:19W2=
_T_11132R0:
B



wbInfo
0pipeid	

1Package.scala 18:26W2=
_T_11152R0:
B



wbInfo
0pipeid	

2Package.scala 19:17C2)
_T_1117R
	
_T_1113	

0Package.scala 18:26C2)
_T_1119R
	
_T_1113	

1Package.scala 19:172|
_T_1120q2o

	
_T_11192:0
):'
:
:



FPUFMAPipeiooutbitsexc,:*
#:!
:
:


sfmaiooutbitsexcPackage.scala 19:12C2)
_T_1122R
	
_T_1113	

0Package.scala 18:26C2)
_T_1124R
	
_T_1113	

1Package.scala 19:172v
_T_1125k2i

	
_T_1124,:*
#:!
:
:


ifpuiooutbitsexc,:*
#:!
:
:


fpmuiooutbitsexcPackage.scala 19:12K21
wexc)2'

	
_T_1115
	
_T_1120
	
_T_1125Package.scala 19:12P29
_T_1127.R,:
B



wbInfo
0cp	

0FPU.scala 676:1092"
_T_1128R

wen
0
0FPU.scala 676:30@2)
_T_1129R
	
_T_1127
	
_T_1128FPU.scala 676:24D2-
_T_1130"R 
	
_T_1129

divSqrt_wenFPU.scala 676:35:

	
_T_11306z/
": 
:

	
regfile_T_1131addr	

waddr
 5z.
!:
:

	
regfile_T_1131clk	

clock
 6z/
 :
:

	
regfile_T_1131en	

1
 8z1
": 
:

	
regfile_T_1131mask	

0
 Fz/
": 
:

	
regfile_T_1131data	

wdataFPU.scala 677:20Hz1
": 
:

	
regfile_T_1131mask	

1FPU.scala 677:20FPU.scala 676:5192"
_T_1132R

wen
0
0FPU.scala 689:28P29
_T_1133.R,:
B



wbInfo
0cp
	
_T_1132FPU.scala 689:22¸: 

	
_T_1133Kz4
':%
:
:


iocp_respbitsdata	

wdataFPU.scala 690:26Dz-
:
:


iocp_respvalid	

1FPU.scala 691:22FPU.scala 689:33E2.
_T_1136#R!

ex_reg_valid	

0FPU.scala 693:22Cz,
:
:


iocp_reqready
	
_T_1136FPU.scala 693:19W2@
wb_toint_valid.R,

wb_reg_valid:

	
wb_ctrltointFPU.scala 695:37V@
wb_toint_exc
	

clock"	

0*

wb_toint_excReg.scala 34:16:q
:



mem_ctrltointVz@


wb_toint_exc,:*
#:!
:
:


fpiuiooutbitsexcReg.scala 35:23Reg.scala 35:19K24
_T_1138)R'

wb_toint_valid

divSqrt_wenFPU.scala 697:4192"
_T_1139R

wen
0
0FPU.scala 697:62@2)
_T_1140R
	
_T_1138
	
_T_1139FPU.scala 697:56Gz0
!:
:


io
fcsr_flagsvalid
	
_T_1140FPU.scala 697:23V2@
_T_1142523


wb_toint_valid

wb_toint_exc	

0FPU.scala 699:8T2>
_T_1144321


divSqrt_wen

divSqrt_flags	

0FPU.scala 700:8@2)
_T_1145R
	
_T_1142
	
_T_1144FPU.scala 699:4892"
_T_1146R

wen
0
0FPU.scala 701:12G21
_T_1148&2$

	
_T_1146

wexc	

0FPU.scala 701:8@2)
_T_1149R
	
_T_1145
	
_T_1148FPU.scala 700:46Fz/
 :
:


io
fcsr_flagsbits
	
_T_1149FPU.scala 698:22U2>
_T_11503R1:



mem_ctrldiv:



mem_ctrlsqrtFPU.scala 703:51F2/
_T_1151$R"

mem_reg_valid
	
_T_1150FPU.scala 703:34H21
_T_1153&R$

divSqrt_inReady	

0FPU.scala 703:73<2%
_T_1155R

wen	

0FPU.scala 703:97@2)
_T_1156R
	
_T_1153
	
_T_1155FPU.scala 703:90C2,

units_busyR
	
_T_1151
	
_T_1156FPU.scala 703:69Q2:
_T_1157/R-

ex_reg_valid:

	
ex_ctrlwflagsFPU.scala 704:33S2<
_T_11581R/

mem_reg_valid:



mem_ctrlwflagsFPU.scala 704:68@2)
_T_1159R
	
_T_1157
	
_T_1158FPU.scala 704:51Q29
_T_1160.R,

wb_reg_valid:

	
wb_ctrltointFPU.scala 704:103@2)
_T_1161R
	
_T_1159
	
_T_1160FPU.scala 704:87=2%
_T_1163R

wen	

0FPU.scala 704:127A2)
_T_1164R
	
_T_1161
	
_T_1163FPU.scala 704:120K23
_T_1165(R&
	
_T_1164

divSqrt_in_flightFPU.scala 704:131@2)
_T_1167R
	
_T_1165	

0FPU.scala 704:18:z#
:


iofcsr_rdy
	
_T_1167FPU.scala 704:15K24
_T_1168)R'


units_busy

write_port_busyFPU.scala 705:29J23
_T_1169(R&
	
_T_1168

divSqrt_in_flightFPU.scala 705:48:z#
:


ionack_mem
	
_T_1169FPU.scala 705:15K3
:


iodec :
:



fp_decoderiosigsFPU.scala 706:10D2-
_T_1171"R 

wb_cp_valid	

0FPU.scala 708:36E2.
_T_1172#R!

wb_reg_valid
	
_T_1171FPU.scala 708:33M26
_T_1174+R):



mem_ctrlsingle	

0FPU.scala 627:65J23
_T_1175(R&:



mem_ctrlfma
	
_T_1174FPU.scala 627:62A2)
_T_1177R	

0
	
_T_1175FPU.scala 707:123J23
_T_1178(R&
	
_T_1177:



mem_ctrldivFPU.scala 708:96L24
_T_1179)R'
	
_T_1178:



mem_ctrlsqrtFPU.scala 708:112M6
_T_1180
	

clock"	

0*
	
_T_1180FPU.scala 708:551z

	
_T_1180
	
_T_1179FPU.scala 708:55@2)
_T_1181R
	
_T_1172
	
_T_1180FPU.scala 708:49<z%
:


io
sboard_set
	
_T_1181FPU.scala 708:17D2-
_T_1183"R 

wb_cp_valid	

0FPU.scala 709:2092"
_T_1184R

wen
0
0FPU.scala 709:56T2=
_T_11862R0:
B



wbInfo
0pipeid	

3FPU.scala 709:99A2)
_T_1188R	

0
	
_T_1186FPU.scala 707:123@2)
_T_1189R
	
_T_1184
	
_T_1188FPU.scala 709:60D2-
_T_1190"R 

divSqrt_wen
	
_T_1189FPU.scala 709:49@2)
_T_1191R
	
_T_1183
	
_T_1190FPU.scala 709:33<z%
:


io
sboard_clr
	
_T_1191FPU.scala 709:17;z$
:


iosboard_clra	

waddrFPU.scala 710:18D2-
_T_1192"R :


ioinst
14
14FPU.scala 712:27D2-
_T_1193"R :


ioinst
13
12FPU.scala 712:43@2)
_T_1195R
	
_T_1193	

3FPU.scala 712:51H21
_T_1197&R$:


iofcsr_rm	

4FPU.scala 712:69@2)
_T_1198R
	
_T_1195
	
_T_1197FPU.scala 712:55@2)
_T_1199R
	
_T_1192
	
_T_1198FPU.scala 712:32<z%
:


io
illegal_rm
	
_T_1199FPU.scala 712:177z 


divSqrt_wdata	

0FPU.scala 714:177z 


divSqrt_flags	

0FPU.scala 715:17M6
_T_1203
	

clock"	

0*
	
_T_1203FPU.scala 718:25M6
_T_1205
	

clock"	

0*
	
_T_1205FPU.scala 719:35M6
_T_1207
A	

clock"	

0*
	
_T_1207FPU.scala 720:355*
DivSqrtRecF64DivSqrtRecF64FPU.scala 722:25#
:


DivSqrtRecF64io
 0z)
:


DivSqrtRecF64clock	

clock
 0z)
:


DivSqrtRecF64reset	

reset
 §2
_T_12082
%:#
:


DivSqrtRecF64iosqrtOp+:)
:


DivSqrtRecF64ioinReady_sqrt*:(
:


DivSqrtRecF64ioinReady_divFPU.scala 723:279z"


divSqrt_inReady
	
_T_1208FPU.scala 723:212j
_T_1209_R]+:)
:


DivSqrtRecF64iooutValid_div,:*
:


DivSqrtRecF64iooutValid_sqrtFPU.scala 724:52U2>
_T_12103R1:



mem_ctrldiv:



mem_ctrlsqrtFPU.scala 725:58F2/
_T_1211$R"

mem_reg_valid
	
_T_1210FPU.scala 725:41J23
_T_1213(R&

divSqrt_in_flight	

0FPU.scala 725:79@2)
_T_1214R
	
_T_1211
	
_T_1213FPU.scala 725:76Lz5
&:$
:


DivSqrtRecF64ioinValid
	
_T_1214FPU.scala 725:24Vz?
%:#
:


DivSqrtRecF64iosqrtOp:



mem_ctrlsqrtFPU.scala 726:23czL
 :
:


DivSqrtRecF64ioa(:&
:
:


fpiuio	as_doublein1FPU.scala 727:18czL
 :
:


DivSqrtRecF64iob(:&
:
:


fpiuio	as_doublein2FPU.scala 728:18mzV
+:)
:


DivSqrtRecF64ioroundingMode':%
:
:


fpiuio	as_doublermFPU.scala 729:29c2L
_T_1215AR?&:$
:


DivSqrtRecF64ioinValid

divSqrt_inReadyFPU.scala 731:30²:

	
_T_1215;z$


divSqrt_in_flight	

1FPU.scala 732:256z


divSqrt_killed	

killmFPU.scala 733:22Ez.


divSqrt_single:



mem_ctrlsingleFPU.scala 734:22C2,
_T_1217!R

mem_reg_inst
11
7FPU.scala 735:367z 


divSqrt_waddr
	
_T_1217FPU.scala 735:21Qz:

	
_T_1203+:)
:


DivSqrtRecF64ioroundingModeFPU.scala 736:18FPU.scala 731:50:é

	
_T_1209G20
_T_1219%R#

divSqrt_killed	

0FPU.scala 740:225z


divSqrt_wen
	
_T_1219FPU.scala 740:19Hz1

	
_T_1207": 
:


DivSqrtRecF64iooutFPU.scala 741:28;z$


divSqrt_in_flight	

0FPU.scala 742:25Sz<

	
_T_1205-:+
:


DivSqrtRecF64ioexceptionFlagsFPU.scala 743:28FPU.scala 739:295*
RecFNToRecFNRecFNToRecFN_2FPU.scala 746:34"
:


RecFNToRecFNio
 /z(
:


RecFNToRecFNclock	

clock
 /z(
:


RecFNToRecFNreset	

reset
 Fz/
 :
:


RecFNToRecFNioin
	
_T_1207FPU.scala 747:28Pz9
*:(
:


RecFNToRecFNioroundingMode
	
_T_1203FPU.scala 748:38h2Q
_T_1221F2D


divSqrt_single!:
:


RecFNToRecFNioout
	
_T_1207FPU.scala 749:257z 


divSqrt_wdata
	
_T_1221FPU.scala 749:19s2\
_T_1223Q2O


divSqrt_single,:*
:


RecFNToRecFNioexceptionFlags	

0FPU.scala 750:48@2)
_T_1224R
	
_T_1205
	
_T_1223FPU.scala 750:437z 


divSqrt_flags
	
_T_1224FPU.scala 750:19
ÌEÉE

FPUDecoder
clock" 
reset
¹
io°*­
inst
 
sigs*
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags



io
 


io
 E2,
_T_22#R!:


ioinst	

4 Decode.scala 13:65?2%
_T_24R	

_T_22	

4 Decode.scala 13:121M24
_T_26+R):


ioinst

	134217744 Decode.scala 13:65G2-
_T_28$R"	

_T_26

	134217744 Decode.scala 13:121>2%
_T_30R	

0	

_T_24Decode.scala 14:30<2#
_T_31R	

_T_30	

_T_28Decode.scala 14:30E2,
_T_33#R!:


ioinst	

8 Decode.scala 13:65?2%
_T_35R	

_T_33	

8 Decode.scala 13:121M24
_T_37+R):


ioinst

	268435472 Decode.scala 13:65G2-
_T_39$R"	

_T_37

	268435472 Decode.scala 13:121>2%
_T_41R	

0	

_T_35Decode.scala 14:30<2#
_T_42R	

_T_41	

_T_39Decode.scala 14:30F2-
_T_44$R":


ioinst


64 Decode.scala 13:65?2%
_T_46R	

_T_44	

0 Decode.scala 13:121M24
_T_48+R):


ioinst

	536870912 Decode.scala 13:65G2-
_T_50$R"	

_T_48

	536870912 Decode.scala 13:121>2%
_T_52R	

0	

_T_46Decode.scala 14:30<2#
_T_53R	

_T_52	

_T_50Decode.scala 14:30N25
_T_55,R*:


ioinst


1073741824 Decode.scala 13:65H2.
_T_57%R#	

_T_55


1073741824 Decode.scala 13:121>2%
_T_59R	

0	

_T_46Decode.scala 14:30<2#
_T_60R	

_T_59	

_T_57Decode.scala 14:30F2-
_T_62$R":


ioinst


16 Decode.scala 13:65?2%
_T_64R	

_T_62	

0 Decode.scala 13:121>2%
_T_66R	

0	

_T_64Decode.scala 14:3092#
_T_67R	

_T_42	

_T_31Cat.scala 30:5892#
_T_68R	

_T_66	

_T_60Cat.scala 30:5892#
_T_69R	

_T_68	

_T_53Cat.scala 30:58=2'
	decoder_0R	

_T_69	

_T_67Cat.scala 30:58B2)
	decoder_1R	

0	

_T_46Decode.scala 14:30N25
_T_72,R*:


ioinst


2147483680 Decode.scala 13:65?2%
_T_74R	

_T_72	

0 Decode.scala 13:121F2-
_T_76$R":


ioinst


48 Decode.scala 13:65?2%
_T_78R	

_T_76	

0 Decode.scala 13:121M24
_T_80+R):


ioinst

	268435488 Decode.scala 13:65G2-
_T_82$R"	

_T_80

	268435456 Decode.scala 13:121>2%
_T_84R	

0	

_T_74Decode.scala 14:30<2#
_T_85R	

_T_84	

_T_78Decode.scala 14:30@2'
	decoder_2R	

_T_85	

_T_82Decode.scala 14:30N25
_T_87,R*:


ioinst


2147483652 Decode.scala 13:65?2%
_T_89R	

_T_87	

0 Decode.scala 13:121M24
_T_91+R):


ioinst

	268435460 Decode.scala 13:65?2%
_T_93R	

_T_91	

0 Decode.scala 13:121F2-
_T_95$R":


ioinst


80 Decode.scala 13:65@2&
_T_97R	

_T_95


64 Decode.scala 13:121>2%
_T_99R	

0	

_T_89Decode.scala 14:30=2$
_T_100R	

_T_99	

_T_93Decode.scala 14:30A2(
	decoder_3R


_T_100	

_T_97Decode.scala 14:30O26
_T_102,R*:


ioinst


1073741828 Decode.scala 13:65A2'
_T_104R


_T_102	

0 Decode.scala 13:121G2.
_T_106$R":


ioinst


32 Decode.scala 13:65B2(
_T_108R


_T_106


32 Decode.scala 13:121@2'
_T_110R	

0


_T_104Decode.scala 14:30?2&
_T_111R


_T_110


_T_108Decode.scala 14:30A2(
	decoder_4R


_T_111	

_T_97Decode.scala 14:30B2)
	decoder_5R	

0	

_T_97Decode.scala 14:30O26
_T_114,R*:


ioinst


1342177296 Decode.scala 13:65J20
_T_116&R$


_T_114


1342177296 Decode.scala 13:121?2&
_T_118R	

0	

_T_46Decode.scala 14:30B2)
	decoder_6R


_T_118


_T_116Decode.scala 14:30N25
_T_120+R):


ioinst

	805306384 Decode.scala 13:65B2(
_T_122R


_T_120


16 Decode.scala 13:121C2*
	decoder_7R	

0


_T_122Decode.scala 14:30I20
_T_125&R$:


ioinst

4160 Decode.scala 13:65A2'
_T_127R


_T_125	

0 Decode.scala 13:121M24
_T_129*R(:


ioinst


33554496 Decode.scala 13:65B2(
_T_131R


_T_129


64 Decode.scala 13:121@2'
_T_133R	

0


_T_127Decode.scala 14:30B2)
	decoder_8R


_T_133


_T_131Decode.scala 14:30O26
_T_135,R*:


ioinst


2415919120 Decode.scala 13:65J20
_T_137&R$


_T_135


2415919120 Decode.scala 13:121C2*
	decoder_9R	

0


_T_137Decode.scala 14:30O26
_T_140,R*:


ioinst


2415919120 Decode.scala 13:65J20
_T_142&R$


_T_140


2147483664 Decode.scala 13:121@2'
_T_144R	

0


_T_108Decode.scala 14:30C2*

decoder_10R


_T_144


_T_142Decode.scala 14:30O26
_T_146,R*:


ioinst


2684354576 Decode.scala 13:65I2/
_T_148%R#


_T_146

	536870928 Decode.scala 13:121O26
_T_150,R*:


ioinst


3489660944 Decode.scala 13:65J20
_T_152&R$


_T_150


1073741840 Decode.scala 13:121@2'
_T_154R	

0


_T_148Decode.scala 14:30C2*

decoder_11R


_T_154


_T_152Decode.scala 14:30O26
_T_156,R*:


ioinst


1879048196 Decode.scala 13:65A2'
_T_158R


_T_156	

0 Decode.scala 13:121O26
_T_160,R*:


ioinst


1744830468 Decode.scala 13:65A2'
_T_162R


_T_160	

0 Decode.scala 13:121@2'
_T_164R	

0


_T_158Decode.scala 14:30?2&
_T_165R


_T_164


_T_162Decode.scala 14:30B2)

decoder_12R


_T_165	

_T_97Decode.scala 14:30O26
_T_167,R*:


ioinst


1476395024 Decode.scala 13:65I2/
_T_169%R#


_T_167

	402653200 Decode.scala 13:121D2+

decoder_13R	

0


_T_169Decode.scala 14:30O26
_T_172,R*:


ioinst


3489660944 Decode.scala 13:65J20
_T_174&R$


_T_172


1342177296 Decode.scala 13:121D2+

decoder_14R	

0


_T_174Decode.scala 14:30N25
_T_177+R):


ioinst

	536870916 Decode.scala 13:65A2'
_T_179R


_T_177	

0 Decode.scala 13:121N25
_T_181+R):


ioinst

	134225920 Decode.scala 13:65I2/
_T_183%R#


_T_181

	134217728 Decode.scala 13:121O26
_T_185,R*:


ioinst


3221225476 Decode.scala 13:65J20
_T_187&R$


_T_185


2147483648 Decode.scala 13:121@2'
_T_189R	

0


_T_179Decode.scala 14:30>2%
_T_190R


_T_189	

_T_97Decode.scala 14:30?2&
_T_191R


_T_190


_T_183Decode.scala 14:30C2*

decoder_15R


_T_191


_T_187Decode.scala 14:30Az*
:
:


iosigscmd

	decoder_0FPU.scala 149:40Bz+
:
:


iosigsldst

	decoder_1FPU.scala 149:40Az*
:
:


iosigswen

	decoder_2FPU.scala 149:40Bz+
:
:


iosigsren1

	decoder_3FPU.scala 149:40Bz+
:
:


iosigsren2

	decoder_4FPU.scala 149:40Bz+
:
:


iosigsren3

	decoder_5FPU.scala 149:40Dz-
:
:


iosigsswap12

	decoder_6FPU.scala 149:40Dz-
:
:


iosigsswap23

	decoder_7FPU.scala 149:40Dz-
:
:


iosigssingle

	decoder_8FPU.scala 149:40Ez.
:
:


iosigsfromint

	decoder_9FPU.scala 149:40Dz-
:
:


iosigstoint


decoder_10FPU.scala 149:40Gz0
:
:


iosigsfastpipe


decoder_11FPU.scala 149:40Bz+
:
:


iosigsfma


decoder_12FPU.scala 149:40Bz+
:
:


iosigsdiv


decoder_13FPU.scala 149:40Cz,
:
:


iosigssqrt


decoder_14FPU.scala 149:40Ez.
:
:


iosigswflags


decoder_15FPU.scala 149:40
  

FPUFMAPipe
clock" 
reset
Ø
ioÏ*Ì
inù*ö
valid

âbitsÙ*Ö
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags

rm

typ

in1
A
in2
A
in3
A
Eout>*<
valid

)bits!*
data
A
exc



io
 


io
 52
oneR	

1
31FPU.scala 479:21T2=
_T_1313R1!:
:
:


ioinbitsin1
32
32FPU.scala 480:29T2=
_T_1323R1!:
:
:


ioinbitsin2
32
32FPU.scala 480:53=2&
_T_133R


_T_131


_T_132FPU.scala 480:3752
zeroR


_T_133
32FPU.scala 480:62I2
valid
	

clock"	

0*	

validFPU.scala 482:18=z&
	

valid:
:


ioinvalidFPU.scala 482:18
inÙ*Ö
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags

rm

typ

in1
A
in2
A
in3
A	

clock"	

0*

inFPU.scala 483:15Ä:¬
:
:


ioinvalid9"


in:
:


ioinbitsFPU.scala 485:8R2;
_T_1771R/!:
:
:


ioinbitscmd
1
1FPU.scala 488:33o2X
_T_178NRL": 
:
:


ioinbitsren3$:"
:
:


ioinbitsswap23FPU.scala 488:48=2&
_T_179R


_T_177


_T_178FPU.scala 488:37R2;
_T_1801R/!:
:
:


ioinbitscmd
0
0FPU.scala 488:78<2&
_T_181R


_T_179


_T_180Cat.scala 30:584z
:


incmd


_T_181FPU.scala 488:12p:Y
$:"
:
:


ioinbitsswap231z
:


inin2

oneFPU.scala 489:32FPU.scala 489:23o2X
_T_182NRL": 
:
:


ioinbitsren3$:"
:
:


ioinbitsswap23FPU.scala 490:21E2'
_T_184R


_T_182	

0Conditional.scala 19:11^:@



_T_1842z
:


inin3

zeroFPU.scala 490:45Conditional.scala 19:15FPU.scala 484:22)*
fmaMulAddRecFNFPU.scala 493:19
:


fmaio
 &z
:


fmaclock	

clock
 &z
:


fmareset	

reset
 Az*
:
:


fmaioop:


incmdFPU.scala 494:13Jz3
!:
:


fmaioroundingMode:


inrmFPU.scala 495:23@z)
:
:


fmaioa:


inin1FPU.scala 496:12@z)
:
:


fmaiob:


inin2FPU.scala 497:12@z)
:
:


fmaioc:


inin3FPU.scala 498:12?
(
res!*
data
A
exc
FPU.scala 500:17!	


resFPU.scala 500:17Dz-
:


resdata:
:


fmaiooutFPU.scala 501:12Nz7
:


resexc#:!
:


fmaioexceptionFlagsFPU.scala 502:11K3
_T_192
	

clock"	

reset*	

0Valid.scala 47:18/z



_T_192	

validValid.scala 47:18eO
_T_196!*
data
A
exc
	

clock"	

0*


_T_196Reg.scala 34:16O:9
	

valid,



_T_196

resReg.scala 35:23Reg.scala 35:19K3
_T_201
	

clock"	

reset*	

0Valid.scala 47:180z



_T_201


_T_192Valid.scala 47:18eO
_T_205!*
data
A
exc
	

clock"	

0*


_T_205Reg.scala 34:16S:=



_T_192/



_T_205


_T_196Reg.scala 35:23Reg.scala 35:19`
H
_T_217>*<
valid

)bits!*
data
A
exc
Valid.scala 42:21%



_T_217Valid.scala 42:21;z#
:



_T_217valid


_T_201Valid.scala 43:17;"
:



_T_217bits


_T_205Valid.scala 44:165
:


ioout


_T_217FPU.scala 503:10
Ãª¿ª
FPToInt
clock" 
reset
â
ioÙ*Ö
inù*ö
valid

âbitsÙ*Ö
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags

rm

typ

in1
A
in2
A
in3
A
ç	as_doubleÙ*Ö
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags

rm

typ

in1
A
in2
A
in3
A
eout^*\
valid

IbitsA*?
lt

store
@
toint
@
exc



io
 


io
 
inÙ*Ö
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags

rm

typ

in1
A
in2
A
in3
A	

clock"	

0*

inFPU.scala 286:15I2
valid
	

clock"	

0*	

validFPU.scala 287:18=z&
	

valid:
:


ioinvalidFPU.scala 287:18ô:Ü
:
:


ioinvalid9"


in:
:


ioinbitsFPU.scala 292:8V2?
_T_2245R3": 
:
:


ioinbitsldst	

0FPU.scala 293:47W2@
_T_2256R4$:"
:
:


ioinbitssingle


_T_224FPU.scala 293:44V2?
_T_2285R3!:
:
:


ioinbitscmd


12FPU.scala 293:82?2(
_T_229R


12


_T_228FPU.scala 293:82>2'
_T_231R


_T_229	

0FPU.scala 293:82=2&
_T_232R


_T_225


_T_231FPU.scala 293:64º:¢



_T_232T2=
_T_2333R1!:
:
:


ioinbitsin1
32
32FPU.scala 237:18S2<
_T_2342R0!:
:
:


ioinbitsin1
22
0FPU.scala 238:21T2=
_T_2353R1!:
:
:


ioinbitsin1
31
23FPU.scala 239:1972 
_T_236R


_T_234
53FPU.scala 240:2872 
_T_237R	


_T_236
24FPU.scala 240:43;2$
_T_238R


_T_235
8
6FPU.scala 242:26A2*
_T_240 R


_T_235

2048FPU.scala 243:3162
_T_241R


_T_240
1FPU.scala 243:31@2)
_T_243R


_T_241

256	FPU.scala 243:5312
_T_244R


_T_243FPU.scala 243:5362
_T_245R


_T_244
1FPU.scala 243:53>2'
_T_247R


_T_238	

0FPU.scala 244:19>2'
_T_249R


_T_238	

6FPU.scala 244:36=2&
_T_250R


_T_247


_T_249FPU.scala 244:25;2$
_T_251R


_T_245
8
0FPU.scala 244:65<2&
_T_252R


_T_238


_T_251Cat.scala 30:58<2%
_T_253R


_T_245
11
0FPU.scala 245:52G20
_T_254&2$



_T_250


_T_252


_T_253FPU.scala 244:10<2&
_T_255R


_T_233


_T_254Cat.scala 30:58<2&
_T_256R


_T_255


_T_237Cat.scala 30:584z
:


inin1


_T_256FPU.scala 294:14T2=
_T_2573R1!:
:
:


ioinbitsin2
32
32FPU.scala 237:18S2<
_T_2582R0!:
:
:


ioinbitsin2
22
0FPU.scala 238:21T2=
_T_2593R1!:
:
:


ioinbitsin2
31
23FPU.scala 239:1972 
_T_260R


_T_258
53FPU.scala 240:2872 
_T_261R	


_T_260
24FPU.scala 240:43;2$
_T_262R


_T_259
8
6FPU.scala 242:26A2*
_T_264 R


_T_259

2048FPU.scala 243:3162
_T_265R


_T_264
1FPU.scala 243:31@2)
_T_267R


_T_265

256	FPU.scala 243:5312
_T_268R


_T_267FPU.scala 243:5362
_T_269R


_T_268
1FPU.scala 243:53>2'
_T_271R


_T_262	

0FPU.scala 244:19>2'
_T_273R


_T_262	

6FPU.scala 244:36=2&
_T_274R


_T_271


_T_273FPU.scala 244:25;2$
_T_275R


_T_269
8
0FPU.scala 244:65<2&
_T_276R


_T_262


_T_275Cat.scala 30:58<2%
_T_277R


_T_269
11
0FPU.scala 245:52G20
_T_278&2$



_T_274


_T_276


_T_277FPU.scala 244:10<2&
_T_279R


_T_257


_T_278Cat.scala 30:58<2&
_T_280R


_T_279


_T_261Cat.scala 30:584z
:


inin2


_T_280FPU.scala 295:14FPU.scala 293:98FPU.scala 291:22I2+
_T_281!R:


inin1
32
32fNFromRecFN.scala 45:22I2+
_T_282!R:


inin1
31
23fNFromRecFN.scala 46:23H2*
_T_283 R:


inin1
22
0fNFromRecFN.scala 47:25B2$
_T_284R


_T_282
6
0fNFromRecFN.scala 49:39E2'
_T_286R


_T_284	

2fNFromRecFN.scala 49:57B2$
_T_287R


_T_282
8
6fNFromRecFN.scala 51:19E2'
_T_289R


_T_287	

1fNFromRecFN.scala 51:44B2$
_T_290R


_T_282
8
7fNFromRecFN.scala 52:24E2'
_T_292R


_T_290	

1fNFromRecFN.scala 52:49D2&
_T_293R


_T_292


_T_286fNFromRecFN.scala 52:62D2&
_T_294R


_T_289


_T_293fNFromRecFN.scala 51:57B2$
_T_295R


_T_282
8
7fNFromRecFN.scala 55:20E2'
_T_297R


_T_295	

1fNFromRecFN.scala 55:45E2'
_T_299R


_T_286	

0fNFromRecFN.scala 56:18D2&
_T_300R


_T_297


_T_299fNFromRecFN.scala 55:58B2$
_T_301R


_T_282
8
7fNFromRecFN.scala 57:23E2'
_T_303R


_T_301	

2fNFromRecFN.scala 57:48D2&
_T_304R


_T_300


_T_303fNFromRecFN.scala 56:39B2$
_T_305R


_T_282
8
7fNFromRecFN.scala 58:30E2'
_T_307R


_T_305	

3fNFromRecFN.scala 58:55B2$
_T_308R


_T_282
6
6fNFromRecFN.scala 59:39D2&
_T_309R


_T_307


_T_308fNFromRecFN.scala 59:31B2$
_T_311R


_T_282
4
0fNFromRecFN.scala 61:46E2'
_T_312R	

2


_T_311fNFromRecFN.scala 61:3982
_T_313R


_T_312fNFromRecFN.scala 61:39=2
_T_314R


_T_313
1fNFromRecFN.scala 61:39=2'
_T_316R	

1


_T_283Cat.scala 30:58D2&
_T_317R


_T_316


_T_314fNFromRecFN.scala 63:35C2%
_T_318R


_T_317
22
0fNFromRecFN.scala 63:53B2$
_T_319R


_T_282
7
0fNFromRecFN.scala 65:18G2)
_T_321R


_T_319

129fNFromRecFN.scala 65:3682
_T_322R


_T_321fNFromRecFN.scala 65:36=2
_T_323R


_T_322
1fNFromRecFN.scala 65:36>2$
_T_324R


_T_307
0
0Bitwise.scala 71:15N24
_T_327*2(



_T_324

255	

0Bitwise.scala 71:12N20
_T_328&2$



_T_304


_T_323


_T_327fNFromRecFN.scala 68:16D2&
_T_329R


_T_304


_T_309fNFromRecFN.scala 70:26O21
_T_331'2%



_T_294


_T_318	

0fNFromRecFN.scala 72:20N20
_T_332&2$



_T_329


_T_283


_T_331fNFromRecFN.scala 70:16<2&
_T_333R


_T_281


_T_328Cat.scala 30:58<2&
_T_334R


_T_333


_T_332Cat.scala 30:58@2&
_T_335R


_T_334
31
31Package.scala 40:38>2$
_T_336R


_T_335
0
0Bitwise.scala 71:15U2;
_T_33912/



_T_336


4294967295 	

0 Bitwise.scala 71:12=2'
unrec_sR


_T_339


_T_334Cat.scala 30:58I2+
_T_340!R:


inin1
64
64fNFromRecFN.scala 45:22I2+
_T_341!R:


inin1
63
52fNFromRecFN.scala 46:23H2*
_T_342 R:


inin1
51
0fNFromRecFN.scala 47:25B2$
_T_343R


_T_341
9
0fNFromRecFN.scala 49:39E2'
_T_345R


_T_343	

2fNFromRecFN.scala 49:57C2%
_T_346R


_T_341
11
9fNFromRecFN.scala 51:19E2'
_T_348R


_T_346	

1fNFromRecFN.scala 51:44D2&
_T_349R


_T_341
11
10fNFromRecFN.scala 52:24E2'
_T_351R


_T_349	

1fNFromRecFN.scala 52:49D2&
_T_352R


_T_351


_T_345fNFromRecFN.scala 52:62D2&
_T_353R


_T_348


_T_352fNFromRecFN.scala 51:57D2&
_T_354R


_T_341
11
10fNFromRecFN.scala 55:20E2'
_T_356R


_T_354	

1fNFromRecFN.scala 55:45E2'
_T_358R


_T_345	

0fNFromRecFN.scala 56:18D2&
_T_359R


_T_356


_T_358fNFromRecFN.scala 55:58D2&
_T_360R


_T_341
11
10fNFromRecFN.scala 57:23E2'
_T_362R


_T_360	

2fNFromRecFN.scala 57:48D2&
_T_363R


_T_359


_T_362fNFromRecFN.scala 56:39D2&
_T_364R


_T_341
11
10fNFromRecFN.scala 58:30E2'
_T_366R


_T_364	

3fNFromRecFN.scala 58:55B2$
_T_367R


_T_341
9
9fNFromRecFN.scala 59:39D2&
_T_368R


_T_366


_T_367fNFromRecFN.scala 59:31B2$
_T_370R


_T_341
5
0fNFromRecFN.scala 61:46E2'
_T_371R	

2


_T_370fNFromRecFN.scala 61:3982
_T_372R


_T_371fNFromRecFN.scala 61:39=2
_T_373R


_T_372
1fNFromRecFN.scala 61:39=2'
_T_375R	

1


_T_342Cat.scala 30:58D2&
_T_376R


_T_375


_T_373fNFromRecFN.scala 63:35C2%
_T_377R


_T_376
51
0fNFromRecFN.scala 63:53C2%
_T_378R


_T_341
10
0fNFromRecFN.scala 65:18H2*
_T_380 R


_T_378

1025fNFromRecFN.scala 65:3682
_T_381R


_T_380fNFromRecFN.scala 65:36=2
_T_382R


_T_381
1fNFromRecFN.scala 65:36>2$
_T_383R


_T_366
0
0Bitwise.scala 71:15O25
_T_386+2)



_T_383

2047	

0Bitwise.scala 71:12N20
_T_387&2$



_T_363


_T_382


_T_386fNFromRecFN.scala 68:16D2&
_T_388R


_T_363


_T_368fNFromRecFN.scala 70:26O21
_T_390'2%



_T_353


_T_377	

0fNFromRecFN.scala 72:20N20
_T_391&2$



_T_388


_T_342


_T_390fNFromRecFN.scala 70:16<2&
_T_392R


_T_340


_T_387Cat.scala 30:58<2&
_T_393R


_T_392


_T_391Cat.scala 30:58S2<
	unrec_int/2-
:


insingle
	
unrec_s


_T_393FPU.scala 304:10B2+
_T_394!R:


inin1
32
32FPU.scala 201:18B2+
_T_395!R:


inin1
31
23FPU.scala 202:17A2*
_T_396 R:


inin1
22
0FPU.scala 203:17;2$
_T_397R


_T_395
8
6FPU.scala 205:26;2$
_T_398R


_T_397
2
1FPU.scala 206:27>2'
_T_400R


_T_398	

3FPU.scala 207:30;2$
_T_401R


_T_395
6
0FPU.scala 209:32>2'
_T_403R


_T_401	

2FPU.scala 209:48>2'
_T_405R


_T_397	

1FPU.scala 210:28>2'
_T_407R


_T_398	

1FPU.scala 210:50=2&
_T_408R


_T_407


_T_403FPU.scala 210:62=2&
_T_409R


_T_405


_T_408FPU.scala 210:40>2'
_T_411R


_T_398	

1FPU.scala 211:27>2'
_T_413R


_T_403	

0FPU.scala 211:42=2&
_T_414R


_T_411


_T_413FPU.scala 211:39>2'
_T_416R


_T_398	

2FPU.scala 211:71=2&
_T_417R


_T_414


_T_416FPU.scala 211:61>2'
_T_419R


_T_397	

0FPU.scala 212:23;2$
_T_420R


_T_395
6
6FPU.scala 213:34>2'
_T_422R


_T_420	

0FPU.scala 213:30=2&
_T_423R


_T_400


_T_422FPU.scala 213:2712
_T_424R


_T_397FPU.scala 214:22>2'
_T_426R


_T_424	

0FPU.scala 214:22=2&
_T_427R


_T_396
22
22FPU.scala 215:31>2'
_T_429R


_T_427	

0FPU.scala 215:27=2&
_T_430R


_T_426


_T_429FPU.scala 215:24=2&
_T_431R


_T_396
22
22FPU.scala 216:30=2&
_T_432R


_T_426


_T_431FPU.scala 216:24>2'
_T_434R


_T_394	

0FPU.scala 218:34=2&
_T_435R


_T_423


_T_434FPU.scala 218:31>2'
_T_437R


_T_394	

0FPU.scala 218:53=2&
_T_438R


_T_417


_T_437FPU.scala 218:50>2'
_T_440R


_T_394	

0FPU.scala 219:24=2&
_T_441R


_T_409


_T_440FPU.scala 219:21>2'
_T_443R


_T_394	

0FPU.scala 219:41=2&
_T_444R


_T_419


_T_443FPU.scala 219:38=2&
_T_445R


_T_419


_T_394FPU.scala 219:55=2&
_T_446R


_T_409


_T_394FPU.scala 220:21=2&
_T_447R


_T_417


_T_394FPU.scala 220:39=2&
_T_448R


_T_423


_T_394FPU.scala 220:54<2&
_T_449R


_T_447


_T_448Cat.scala 30:58<2&
_T_450R


_T_444


_T_445Cat.scala 30:58<2&
_T_451R


_T_450


_T_446Cat.scala 30:58<2&
_T_452R


_T_451


_T_449Cat.scala 30:58<2&
_T_453R


_T_438


_T_441Cat.scala 30:58<2&
_T_454R


_T_432


_T_430Cat.scala 30:58<2&
_T_455R


_T_454


_T_435Cat.scala 30:58<2&
_T_456R


_T_455


_T_453Cat.scala 30:58@2*

classify_sR


_T_456


_T_452Cat.scala 30:58B2+
_T_457!R:


inin1
64
64FPU.scala 201:18B2+
_T_458!R:


inin1
63
52FPU.scala 202:17A2*
_T_459 R:


inin1
51
0FPU.scala 203:17<2%
_T_460R


_T_458
11
9FPU.scala 205:26;2$
_T_461R


_T_460
2
1FPU.scala 206:27>2'
_T_463R


_T_461	

3FPU.scala 207:30;2$
_T_464R


_T_458
9
0FPU.scala 209:32>2'
_T_466R


_T_464	

2FPU.scala 209:48>2'
_T_468R


_T_460	

1FPU.scala 210:28>2'
_T_470R


_T_461	

1FPU.scala 210:50=2&
_T_471R


_T_470


_T_466FPU.scala 210:62=2&
_T_472R


_T_468


_T_471FPU.scala 210:40>2'
_T_474R


_T_461	

1FPU.scala 211:27>2'
_T_476R


_T_466	

0FPU.scala 211:42=2&
_T_477R


_T_474


_T_476FPU.scala 211:39>2'
_T_479R


_T_461	

2FPU.scala 211:71=2&
_T_480R


_T_477


_T_479FPU.scala 211:61>2'
_T_482R


_T_460	

0FPU.scala 212:23;2$
_T_483R


_T_458
9
9FPU.scala 213:34>2'
_T_485R


_T_483	

0FPU.scala 213:30=2&
_T_486R


_T_463


_T_485FPU.scala 213:2712
_T_487R


_T_460FPU.scala 214:22>2'
_T_489R


_T_487	

0FPU.scala 214:22=2&
_T_490R


_T_459
51
51FPU.scala 215:31>2'
_T_492R


_T_490	

0FPU.scala 215:27=2&
_T_493R


_T_489


_T_492FPU.scala 215:24=2&
_T_494R


_T_459
51
51FPU.scala 216:30=2&
_T_495R


_T_489


_T_494FPU.scala 216:24>2'
_T_497R


_T_457	

0FPU.scala 218:34=2&
_T_498R


_T_486


_T_497FPU.scala 218:31>2'
_T_500R


_T_457	

0FPU.scala 218:53=2&
_T_501R


_T_480


_T_500FPU.scala 218:50>2'
_T_503R


_T_457	

0FPU.scala 219:24=2&
_T_504R


_T_472


_T_503FPU.scala 219:21>2'
_T_506R


_T_457	

0FPU.scala 219:41=2&
_T_507R


_T_482


_T_506FPU.scala 219:38=2&
_T_508R


_T_482


_T_457FPU.scala 219:55=2&
_T_509R


_T_472


_T_457FPU.scala 220:21=2&
_T_510R


_T_480


_T_457FPU.scala 220:39=2&
_T_511R


_T_486


_T_457FPU.scala 220:54<2&
_T_512R


_T_510


_T_511Cat.scala 30:58<2&
_T_513R


_T_507


_T_508Cat.scala 30:58<2&
_T_514R


_T_513


_T_509Cat.scala 30:58<2&
_T_515R


_T_514


_T_512Cat.scala 30:58<2&
_T_516R


_T_501


_T_504Cat.scala 30:58<2&
_T_517R


_T_495


_T_493Cat.scala 30:58<2&
_T_518R


_T_517


_T_498Cat.scala 30:58<2&
_T_519R


_T_518


_T_516Cat.scala 30:58<2&
_T_520R


_T_519


_T_515Cat.scala 30:58Y2B
classify_out220
:


insingle


classify_s


_T_520FPU.scala 316:10+*
dcmpCompareRecFNFPU.scala 319:20
:


dcmpio
 'z 
:


dcmpclock	

clock
 'z 
:


dcmpreset	

reset
 Az*
:
:


dcmpioa:


inin1FPU.scala 320:13Az*
:
:


dcmpiob:


inin2FPU.scala 321:13?2(
_T_521R:


inrm
1
1FPU.scala 322:30>2'
_T_523R


_T_521	

0FPU.scala 322:24Dz-
:
:


dcmpio	signaling


_T_523FPU.scala 322:21?2(
_T_524R:


inrm
0
0FPU.scala 324:33P29
_T_525/2-



_T_524

classify_out

	unrec_intFPU.scala 324:27Iz2
$:"
:
:


iooutbitstoint


_T_525FPU.scala 324:21Lz5
$:"
:
:


iooutbitsstore

	unrec_intFPU.scala 325:21Hz1
": 
:
:


iooutbitsexc	

0FPU.scala 326:19D2-
_T_529#R!:


incmd


12FPU.scala 328:16>2'
_T_530R	

4


_T_529FPU.scala 328:16â:Ê



_T_53052
_T_531R:


inrmFPU.scala 329:27X2B
_T_5328R6:
:


dcmpiolt:
:


dcmpioeqCat.scala 30:58=2&
_T_533R


_T_531


_T_532FPU.scala 329:34>2'
_T_535R


_T_533	

0FPU.scala 329:65Iz2
$:"
:
:


iooutbitstoint


_T_535FPU.scala 329:23azJ
": 
:
:


iooutbitsexc$:"
:


dcmpioexceptionFlagsFPU.scala 330:21FPU.scala 328:30D2-
_T_538#R!:


incmd


12FPU.scala 332:16>2'
_T_539R	

8


_T_538FPU.scala 332:16:û



_T_539-*
	RecFNToIN	RecFNToINFPU.scala 336:24
:


	RecFNToINio
 ,z%
:


	RecFNToINclock	

clock
 ,z%
:


	RecFNToINreset	

reset
 Gz0
:
:


	RecFNToINioin:


inin1FPU.scala 337:18Pz9
':%
:


	RecFNToINioroundingMode:


inrmFPU.scala 338:28@2)
_T_540R:


intyp
0
0FPU.scala 339:3512
_T_541R


_T_540FPU.scala 339:28Iz2
$:"
:


	RecFNToINio	signedOut


_T_541FPU.scala 339:25C2)
_T_542R:


intyp
1
1Package.scala 44:13>2'
_T_544R


_T_542	

0FPU.scala 340:44ó:Û



_T_544T2:
_T_5450R.:
:


	RecFNToINioout
31
31Package.scala 40:38>2$
_T_546R


_T_545
0
0Bitwise.scala 71:15U2;
_T_54912/



_T_546


4294967295 	

0 Bitwise.scala 71:12P2:
_T_5500R.


_T_549:
:


	RecFNToINiooutCat.scala 30:58Iz2
$:"
:
:


iooutbitstoint


_T_550FPU.scala 341:27]2F
_T_551<R:,:*
:


	RecFNToINiointExceptionFlags
2
1FPU.scala 342:57>2'
_T_553R


_T_551	

0FPU.scala 342:64^2F
_T_555<R:,:*
:


	RecFNToINiointExceptionFlags
0
0FPU.scala 342:106=2'
_T_556R


_T_553	

0Cat.scala 30:58<2&
_T_557R


_T_556


_T_555Cat.scala 30:58Gz0
": 
:
:


iooutbitsexc


_T_557FPU.scala 342:25FPU.scala 340:511*
RecFNToIN_1RecFNToIN_1FPU.scala 336:24!
:


RecFNToIN_1io
 .z'
:


RecFNToIN_1clock	

clock
 .z'
:


RecFNToIN_1reset	

reset
 Iz2
:
:


RecFNToIN_1ioin:


inin1FPU.scala 337:18Rz;
):'
:


RecFNToIN_1ioroundingMode:


inrmFPU.scala 338:28@2)
_T_558R:


intyp
0
0FPU.scala 339:3512
_T_559R


_T_558FPU.scala 339:28Kz4
&:$
:


RecFNToIN_1io	signedOut


_T_559FPU.scala 339:25C2)
_T_560R:


intyp
1
1Package.scala 44:13>2'
_T_562R


_T_560	

1FPU.scala 340:44Î:¶



_T_562_zH
$:"
:
:


iooutbitstoint :
:


RecFNToIN_1iooutFPU.scala 341:27_2H
_T_563>R<.:,
:


RecFNToIN_1iointExceptionFlags
2
1FPU.scala 342:57>2'
_T_565R


_T_563	

0FPU.scala 342:64`2H
_T_567>R<.:,
:


RecFNToIN_1iointExceptionFlags
0
0FPU.scala 342:106=2'
_T_568R


_T_565	

0Cat.scala 30:58<2&
_T_569R


_T_568


_T_567Cat.scala 30:58Gz0
": 
:
:


iooutbitsexc


_T_569FPU.scala 342:25FPU.scala 340:51FPU.scala 332:33>z'
:
:


iooutvalid	

validFPU.scala 347:16Tz=
!:
:
:


iooutbitslt:
:


dcmpioltFPU.scala 348:187
:


io	as_double

inFPU.scala 349:16
®ÎªÎ
IntToFP
clock" 
reset
Ø
ioÏ*Ì
inù*ö
valid

âbitsÙ*Ö
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags

rm

typ

in1
A
in2
A
in3
A
Eout>*<
valid

)bits!*
data
A
exc



io
 


io
 K3
_T_132
	

clock"	

reset*	

0Valid.scala 47:18?z'



_T_132:
:


ioinvalidValid.scala 47:18
_T_155Ù*Ö
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags

rm

typ

in1
A
in2
A
in3
A	

clock"	

0*


_T_155Reg.scala 34:16p:Z
:
:


ioinvalid=&



_T_155:
:


ioinbitsReg.scala 35:23Reg.scala 35:19

inù*ö
valid

âbitsÙ*Ö
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags

rm

typ

in1
A
in2
A
in3
AValid.scala 42:21!


inValid.scala 42:217z
:


invalid


_T_132Valid.scala 43:177
:


inbits


_T_155Valid.scala 44:16?
(
mux!*
data
A
exc
FPU.scala 360:17!	


muxFPU.scala 360:176z
:


muxexc	

0FPU.scala 361:11S25
_T_276+R):
:


inbitsin1
31
31recFNFromFN.scala 47:22S25
_T_277+R):
:


inbitsin1
30
23recFNFromFN.scala 48:23R24
_T_278*R(:
:


inbitsin1
22
0recFNFromFN.scala 49:25E2'
_T_280R


_T_277	

0recFNFromFN.scala 51:34E2'
_T_282R


_T_278	

0recFNFromFN.scala 52:38D2&
_T_283R


_T_280


_T_282recFNFromFN.scala 53:34=2
_T_284R


_T_278
9recFNFromFN.scala 56:26D2&
_T_285R


_T_284
31
16CircuitMath.scala 35:17C2%
_T_286R


_T_284
15
0CircuitMath.scala 36:17E2'
_T_288R


_T_285	

0CircuitMath.scala 37:22C2%
_T_289R


_T_285
15
8CircuitMath.scala 35:17B2$
_T_290R


_T_285
7
0CircuitMath.scala 36:17E2'
_T_292R


_T_289	

0CircuitMath.scala 37:22B2$
_T_293R


_T_289
7
4CircuitMath.scala 35:17B2$
_T_294R


_T_289
3
0CircuitMath.scala 36:17E2'
_T_296R


_T_293	

0CircuitMath.scala 37:22B2$
_T_297R


_T_293
3
3CircuitMath.scala 32:12B2$
_T_299R


_T_293
2
2CircuitMath.scala 32:12A2$
_T_301R


_T_293
1
1CircuitMath.scala 30:8O21
_T_302'2%



_T_299	

2


_T_301CircuitMath.scala 32:10O21
_T_303'2%



_T_297	

3


_T_302CircuitMath.scala 32:10B2$
_T_304R


_T_294
3
3CircuitMath.scala 32:12B2$
_T_306R


_T_294
2
2CircuitMath.scala 32:12A2$
_T_308R


_T_294
1
1CircuitMath.scala 30:8O21
_T_309'2%



_T_306	

2


_T_308CircuitMath.scala 32:10O21
_T_310'2%



_T_304	

3


_T_309CircuitMath.scala 32:10N20
_T_311&2$



_T_296


_T_303


_T_310CircuitMath.scala 38:21<2&
_T_312R


_T_296


_T_311Cat.scala 30:58B2$
_T_313R


_T_290
7
4CircuitMath.scala 35:17B2$
_T_314R


_T_290
3
0CircuitMath.scala 36:17E2'
_T_316R


_T_313	

0CircuitMath.scala 37:22B2$
_T_317R


_T_313
3
3CircuitMath.scala 32:12B2$
_T_319R


_T_313
2
2CircuitMath.scala 32:12A2$
_T_321R


_T_313
1
1CircuitMath.scala 30:8O21
_T_322'2%



_T_319	

2


_T_321CircuitMath.scala 32:10O21
_T_323'2%



_T_317	

3


_T_322CircuitMath.scala 32:10B2$
_T_324R


_T_314
3
3CircuitMath.scala 32:12B2$
_T_326R


_T_314
2
2CircuitMath.scala 32:12A2$
_T_328R


_T_314
1
1CircuitMath.scala 30:8O21
_T_329'2%



_T_326	

2


_T_328CircuitMath.scala 32:10O21
_T_330'2%



_T_324	

3


_T_329CircuitMath.scala 32:10N20
_T_331&2$



_T_316


_T_323


_T_330CircuitMath.scala 38:21<2&
_T_332R


_T_316


_T_331Cat.scala 30:58N20
_T_333&2$



_T_292


_T_312


_T_332CircuitMath.scala 38:21<2&
_T_334R


_T_292


_T_333Cat.scala 30:58C2%
_T_335R


_T_286
15
8CircuitMath.scala 35:17B2$
_T_336R


_T_286
7
0CircuitMath.scala 36:17E2'
_T_338R


_T_335	

0CircuitMath.scala 37:22B2$
_T_339R


_T_335
7
4CircuitMath.scala 35:17B2$
_T_340R


_T_335
3
0CircuitMath.scala 36:17E2'
_T_342R


_T_339	

0CircuitMath.scala 37:22B2$
_T_343R


_T_339
3
3CircuitMath.scala 32:12B2$
_T_345R


_T_339
2
2CircuitMath.scala 32:12A2$
_T_347R


_T_339
1
1CircuitMath.scala 30:8O21
_T_348'2%



_T_345	

2


_T_347CircuitMath.scala 32:10O21
_T_349'2%



_T_343	

3


_T_348CircuitMath.scala 32:10B2$
_T_350R


_T_340
3
3CircuitMath.scala 32:12B2$
_T_352R


_T_340
2
2CircuitMath.scala 32:12A2$
_T_354R


_T_340
1
1CircuitMath.scala 30:8O21
_T_355'2%



_T_352	

2


_T_354CircuitMath.scala 32:10O21
_T_356'2%



_T_350	

3


_T_355CircuitMath.scala 32:10N20
_T_357&2$



_T_342


_T_349


_T_356CircuitMath.scala 38:21<2&
_T_358R


_T_342


_T_357Cat.scala 30:58B2$
_T_359R


_T_336
7
4CircuitMath.scala 35:17B2$
_T_360R


_T_336
3
0CircuitMath.scala 36:17E2'
_T_362R


_T_359	

0CircuitMath.scala 37:22B2$
_T_363R


_T_359
3
3CircuitMath.scala 32:12B2$
_T_365R


_T_359
2
2CircuitMath.scala 32:12A2$
_T_367R


_T_359
1
1CircuitMath.scala 30:8O21
_T_368'2%



_T_365	

2


_T_367CircuitMath.scala 32:10O21
_T_369'2%



_T_363	

3


_T_368CircuitMath.scala 32:10B2$
_T_370R


_T_360
3
3CircuitMath.scala 32:12B2$
_T_372R


_T_360
2
2CircuitMath.scala 32:12A2$
_T_374R


_T_360
1
1CircuitMath.scala 30:8O21
_T_375'2%



_T_372	

2


_T_374CircuitMath.scala 32:10O21
_T_376'2%



_T_370	

3


_T_375CircuitMath.scala 32:10N20
_T_377&2$



_T_362


_T_369


_T_376CircuitMath.scala 38:21<2&
_T_378R


_T_362


_T_377Cat.scala 30:58N20
_T_379&2$



_T_338


_T_358


_T_378CircuitMath.scala 38:21<2&
_T_380R


_T_338


_T_379Cat.scala 30:58N20
_T_381&2$



_T_288


_T_334


_T_380CircuitMath.scala 38:21<2&
_T_382R


_T_288


_T_381Cat.scala 30:5882
_T_383R


_T_382recFNFromFN.scala 56:13D2&
_T_384R



_T_278


_T_383recFNFromFN.scala 58:25C2%
_T_385R


_T_384
21
0recFNFromFN.scala 58:37=2'
_T_387R


_T_385	

0Cat.scala 30:58O25
_T_392+2)
	

1

511		

0	Bitwise.scala 71:12D2&
_T_393R


_T_383


_T_392recFNFromFN.scala 62:27N20
_T_394&2$



_T_280


_T_393


_T_277recFNFromFN.scala 61:16P22
_T_398(2&



_T_280	

2	

1recFNFromFN.scala 64:47G2)
_T_399R

128


_T_398recFNFromFN.scala 64:42D2&
_T_400R


_T_394


_T_399recFNFromFN.scala 64:15=2
_T_401R


_T_400
1recFNFromFN.scala 64:15B2$
_T_402R


_T_401
8
7recFNFromFN.scala 67:25E2'
_T_404R


_T_402	

3recFNFromFN.scala 67:50E2'
_T_406R


_T_282	

0recFNFromFN.scala 68:17D2&
_T_407R


_T_404


_T_406recFNFromFN.scala 67:63>2$
_T_408R


_T_283
0
0Bitwise.scala 71:15L22
_T_411(2&



_T_408	

7	

0Bitwise.scala 71:12=2
_T_412R


_T_411
6recFNFromFN.scala 71:4582
_T_413R


_T_412recFNFromFN.scala 71:28D2&
_T_414R


_T_401


_T_413recFNFromFN.scala 71:26=2
_T_415R


_T_407
6recFNFromFN.scala 72:22D2&
_T_416R


_T_414


_T_415recFNFromFN.scala 71:64N20
_T_417&2$



_T_280


_T_387


_T_278recFNFromFN.scala 73:27<2&
_T_418R


_T_276


_T_416Cat.scala 30:58<2&
_T_419R


_T_418


_T_417Cat.scala 30:586z
:


muxdata


_T_419FPU.scala 362:12P29
_T_421/R-:
:


inbitssingle	

0FPU.scala 363:24i:ëh



_T_421S25
_T_422+R):
:


inbitsin1
63
63recFNFromFN.scala 47:22S25
_T_423+R):
:


inbitsin1
62
52recFNFromFN.scala 48:23R24
_T_424*R(:
:


inbitsin1
51
0recFNFromFN.scala 49:25E2'
_T_426R


_T_423	

0recFNFromFN.scala 51:34E2'
_T_428R


_T_424	

0recFNFromFN.scala 52:38D2&
_T_429R


_T_426


_T_428recFNFromFN.scala 53:34>2 
_T_430R


_T_424
12recFNFromFN.scala 56:26D2&
_T_431R


_T_430
63
32CircuitMath.scala 35:17C2%
_T_432R


_T_430
31
0CircuitMath.scala 36:17E2'
_T_434R


_T_431	

0CircuitMath.scala 37:22D2&
_T_435R


_T_431
31
16CircuitMath.scala 35:17C2%
_T_436R


_T_431
15
0CircuitMath.scala 36:17E2'
_T_438R


_T_435	

0CircuitMath.scala 37:22C2%
_T_439R


_T_435
15
8CircuitMath.scala 35:17B2$
_T_440R


_T_435
7
0CircuitMath.scala 36:17E2'
_T_442R


_T_439	

0CircuitMath.scala 37:22B2$
_T_443R


_T_439
7
4CircuitMath.scala 35:17B2$
_T_444R


_T_439
3
0CircuitMath.scala 36:17E2'
_T_446R


_T_443	

0CircuitMath.scala 37:22B2$
_T_447R


_T_443
3
3CircuitMath.scala 32:12B2$
_T_449R


_T_443
2
2CircuitMath.scala 32:12A2$
_T_451R


_T_443
1
1CircuitMath.scala 30:8O21
_T_452'2%



_T_449	

2


_T_451CircuitMath.scala 32:10O21
_T_453'2%



_T_447	

3


_T_452CircuitMath.scala 32:10B2$
_T_454R


_T_444
3
3CircuitMath.scala 32:12B2$
_T_456R


_T_444
2
2CircuitMath.scala 32:12A2$
_T_458R


_T_444
1
1CircuitMath.scala 30:8O21
_T_459'2%



_T_456	

2


_T_458CircuitMath.scala 32:10O21
_T_460'2%



_T_454	

3


_T_459CircuitMath.scala 32:10N20
_T_461&2$



_T_446


_T_453


_T_460CircuitMath.scala 38:21<2&
_T_462R


_T_446


_T_461Cat.scala 30:58B2$
_T_463R


_T_440
7
4CircuitMath.scala 35:17B2$
_T_464R


_T_440
3
0CircuitMath.scala 36:17E2'
_T_466R


_T_463	

0CircuitMath.scala 37:22B2$
_T_467R


_T_463
3
3CircuitMath.scala 32:12B2$
_T_469R


_T_463
2
2CircuitMath.scala 32:12A2$
_T_471R


_T_463
1
1CircuitMath.scala 30:8O21
_T_472'2%



_T_469	

2


_T_471CircuitMath.scala 32:10O21
_T_473'2%



_T_467	

3


_T_472CircuitMath.scala 32:10B2$
_T_474R


_T_464
3
3CircuitMath.scala 32:12B2$
_T_476R


_T_464
2
2CircuitMath.scala 32:12A2$
_T_478R


_T_464
1
1CircuitMath.scala 30:8O21
_T_479'2%



_T_476	

2


_T_478CircuitMath.scala 32:10O21
_T_480'2%



_T_474	

3


_T_479CircuitMath.scala 32:10N20
_T_481&2$



_T_466


_T_473


_T_480CircuitMath.scala 38:21<2&
_T_482R


_T_466


_T_481Cat.scala 30:58N20
_T_483&2$



_T_442


_T_462


_T_482CircuitMath.scala 38:21<2&
_T_484R


_T_442


_T_483Cat.scala 30:58C2%
_T_485R


_T_436
15
8CircuitMath.scala 35:17B2$
_T_486R


_T_436
7
0CircuitMath.scala 36:17E2'
_T_488R


_T_485	

0CircuitMath.scala 37:22B2$
_T_489R


_T_485
7
4CircuitMath.scala 35:17B2$
_T_490R


_T_485
3
0CircuitMath.scala 36:17E2'
_T_492R


_T_489	

0CircuitMath.scala 37:22B2$
_T_493R


_T_489
3
3CircuitMath.scala 32:12B2$
_T_495R


_T_489
2
2CircuitMath.scala 32:12A2$
_T_497R


_T_489
1
1CircuitMath.scala 30:8O21
_T_498'2%



_T_495	

2


_T_497CircuitMath.scala 32:10O21
_T_499'2%



_T_493	

3


_T_498CircuitMath.scala 32:10B2$
_T_500R


_T_490
3
3CircuitMath.scala 32:12B2$
_T_502R


_T_490
2
2CircuitMath.scala 32:12A2$
_T_504R


_T_490
1
1CircuitMath.scala 30:8O21
_T_505'2%



_T_502	

2


_T_504CircuitMath.scala 32:10O21
_T_506'2%



_T_500	

3


_T_505CircuitMath.scala 32:10N20
_T_507&2$



_T_492


_T_499


_T_506CircuitMath.scala 38:21<2&
_T_508R


_T_492


_T_507Cat.scala 30:58B2$
_T_509R


_T_486
7
4CircuitMath.scala 35:17B2$
_T_510R


_T_486
3
0CircuitMath.scala 36:17E2'
_T_512R


_T_509	

0CircuitMath.scala 37:22B2$
_T_513R


_T_509
3
3CircuitMath.scala 32:12B2$
_T_515R


_T_509
2
2CircuitMath.scala 32:12A2$
_T_517R


_T_509
1
1CircuitMath.scala 30:8O21
_T_518'2%



_T_515	

2


_T_517CircuitMath.scala 32:10O21
_T_519'2%



_T_513	

3


_T_518CircuitMath.scala 32:10B2$
_T_520R


_T_510
3
3CircuitMath.scala 32:12B2$
_T_522R


_T_510
2
2CircuitMath.scala 32:12A2$
_T_524R


_T_510
1
1CircuitMath.scala 30:8O21
_T_525'2%



_T_522	

2


_T_524CircuitMath.scala 32:10O21
_T_526'2%



_T_520	

3


_T_525CircuitMath.scala 32:10N20
_T_527&2$



_T_512


_T_519


_T_526CircuitMath.scala 38:21<2&
_T_528R


_T_512


_T_527Cat.scala 30:58N20
_T_529&2$



_T_488


_T_508


_T_528CircuitMath.scala 38:21<2&
_T_530R


_T_488


_T_529Cat.scala 30:58N20
_T_531&2$



_T_438


_T_484


_T_530CircuitMath.scala 38:21<2&
_T_532R


_T_438


_T_531Cat.scala 30:58D2&
_T_533R


_T_432
31
16CircuitMath.scala 35:17C2%
_T_534R


_T_432
15
0CircuitMath.scala 36:17E2'
_T_536R


_T_533	

0CircuitMath.scala 37:22C2%
_T_537R


_T_533
15
8CircuitMath.scala 35:17B2$
_T_538R


_T_533
7
0CircuitMath.scala 36:17E2'
_T_540R


_T_537	

0CircuitMath.scala 37:22B2$
_T_541R


_T_537
7
4CircuitMath.scala 35:17B2$
_T_542R


_T_537
3
0CircuitMath.scala 36:17E2'
_T_544R


_T_541	

0CircuitMath.scala 37:22B2$
_T_545R


_T_541
3
3CircuitMath.scala 32:12B2$
_T_547R


_T_541
2
2CircuitMath.scala 32:12A2$
_T_549R


_T_541
1
1CircuitMath.scala 30:8O21
_T_550'2%



_T_547	

2


_T_549CircuitMath.scala 32:10O21
_T_551'2%



_T_545	

3


_T_550CircuitMath.scala 32:10B2$
_T_552R


_T_542
3
3CircuitMath.scala 32:12B2$
_T_554R


_T_542
2
2CircuitMath.scala 32:12A2$
_T_556R


_T_542
1
1CircuitMath.scala 30:8O21
_T_557'2%



_T_554	

2


_T_556CircuitMath.scala 32:10O21
_T_558'2%



_T_552	

3


_T_557CircuitMath.scala 32:10N20
_T_559&2$



_T_544


_T_551


_T_558CircuitMath.scala 38:21<2&
_T_560R


_T_544


_T_559Cat.scala 30:58B2$
_T_561R


_T_538
7
4CircuitMath.scala 35:17B2$
_T_562R


_T_538
3
0CircuitMath.scala 36:17E2'
_T_564R


_T_561	

0CircuitMath.scala 37:22B2$
_T_565R


_T_561
3
3CircuitMath.scala 32:12B2$
_T_567R


_T_561
2
2CircuitMath.scala 32:12A2$
_T_569R


_T_561
1
1CircuitMath.scala 30:8O21
_T_570'2%



_T_567	

2


_T_569CircuitMath.scala 32:10O21
_T_571'2%



_T_565	

3


_T_570CircuitMath.scala 32:10B2$
_T_572R


_T_562
3
3CircuitMath.scala 32:12B2$
_T_574R


_T_562
2
2CircuitMath.scala 32:12A2$
_T_576R


_T_562
1
1CircuitMath.scala 30:8O21
_T_577'2%



_T_574	

2


_T_576CircuitMath.scala 32:10O21
_T_578'2%



_T_572	

3


_T_577CircuitMath.scala 32:10N20
_T_579&2$



_T_564


_T_571


_T_578CircuitMath.scala 38:21<2&
_T_580R


_T_564


_T_579Cat.scala 30:58N20
_T_581&2$



_T_540


_T_560


_T_580CircuitMath.scala 38:21<2&
_T_582R


_T_540


_T_581Cat.scala 30:58C2%
_T_583R


_T_534
15
8CircuitMath.scala 35:17B2$
_T_584R


_T_534
7
0CircuitMath.scala 36:17E2'
_T_586R


_T_583	

0CircuitMath.scala 37:22B2$
_T_587R


_T_583
7
4CircuitMath.scala 35:17B2$
_T_588R


_T_583
3
0CircuitMath.scala 36:17E2'
_T_590R


_T_587	

0CircuitMath.scala 37:22B2$
_T_591R


_T_587
3
3CircuitMath.scala 32:12B2$
_T_593R


_T_587
2
2CircuitMath.scala 32:12A2$
_T_595R


_T_587
1
1CircuitMath.scala 30:8O21
_T_596'2%



_T_593	

2


_T_595CircuitMath.scala 32:10O21
_T_597'2%



_T_591	

3


_T_596CircuitMath.scala 32:10B2$
_T_598R


_T_588
3
3CircuitMath.scala 32:12B2$
_T_600R


_T_588
2
2CircuitMath.scala 32:12A2$
_T_602R


_T_588
1
1CircuitMath.scala 30:8O21
_T_603'2%



_T_600	

2


_T_602CircuitMath.scala 32:10O21
_T_604'2%



_T_598	

3


_T_603CircuitMath.scala 32:10N20
_T_605&2$



_T_590


_T_597


_T_604CircuitMath.scala 38:21<2&
_T_606R


_T_590


_T_605Cat.scala 30:58B2$
_T_607R


_T_584
7
4CircuitMath.scala 35:17B2$
_T_608R


_T_584
3
0CircuitMath.scala 36:17E2'
_T_610R


_T_607	

0CircuitMath.scala 37:22B2$
_T_611R


_T_607
3
3CircuitMath.scala 32:12B2$
_T_613R


_T_607
2
2CircuitMath.scala 32:12A2$
_T_615R


_T_607
1
1CircuitMath.scala 30:8O21
_T_616'2%



_T_613	

2


_T_615CircuitMath.scala 32:10O21
_T_617'2%



_T_611	

3


_T_616CircuitMath.scala 32:10B2$
_T_618R


_T_608
3
3CircuitMath.scala 32:12B2$
_T_620R


_T_608
2
2CircuitMath.scala 32:12A2$
_T_622R


_T_608
1
1CircuitMath.scala 30:8O21
_T_623'2%



_T_620	

2


_T_622CircuitMath.scala 32:10O21
_T_624'2%



_T_618	

3


_T_623CircuitMath.scala 32:10N20
_T_625&2$



_T_610


_T_617


_T_624CircuitMath.scala 38:21<2&
_T_626R


_T_610


_T_625Cat.scala 30:58N20
_T_627&2$



_T_586


_T_606


_T_626CircuitMath.scala 38:21<2&
_T_628R


_T_586


_T_627Cat.scala 30:58N20
_T_629&2$



_T_536


_T_582


_T_628CircuitMath.scala 38:21<2&
_T_630R


_T_536


_T_629Cat.scala 30:58N20
_T_631&2$



_T_434


_T_532


_T_630CircuitMath.scala 38:21<2&
_T_632R


_T_434


_T_631Cat.scala 30:5882
_T_633R


_T_632recFNFromFN.scala 56:13D2&
_T_634R



_T_424


_T_633recFNFromFN.scala 58:25C2%
_T_635R


_T_634
50
0recFNFromFN.scala 58:37=2'
_T_637R


_T_635	

0Cat.scala 30:58P26
_T_642,2*
	

1

4095	

0Bitwise.scala 71:12D2&
_T_643R


_T_633


_T_642recFNFromFN.scala 62:27N20
_T_644&2$



_T_426


_T_643


_T_423recFNFromFN.scala 61:16P22
_T_648(2&



_T_426	

2	

1recFNFromFN.scala 64:47H2*
_T_649 R

1024


_T_648recFNFromFN.scala 64:42D2&
_T_650R


_T_644


_T_649recFNFromFN.scala 64:15=2
_T_651R


_T_650
1recFNFromFN.scala 64:15D2&
_T_652R


_T_651
11
10recFNFromFN.scala 67:25E2'
_T_654R


_T_652	

3recFNFromFN.scala 67:50E2'
_T_656R


_T_428	

0recFNFromFN.scala 68:17D2&
_T_657R


_T_654


_T_656recFNFromFN.scala 67:63>2$
_T_658R


_T_429
0
0Bitwise.scala 71:15L22
_T_661(2&



_T_658	

7	

0Bitwise.scala 71:12=2
_T_662R


_T_661
9recFNFromFN.scala 71:4582
_T_663R


_T_662recFNFromFN.scala 71:28D2&
_T_664R


_T_651


_T_663recFNFromFN.scala 71:26=2
_T_665R


_T_657
9recFNFromFN.scala 72:22D2&
_T_666R


_T_664


_T_665recFNFromFN.scala 71:64N20
_T_667&2$



_T_426


_T_637


_T_424recFNFromFN.scala 73:27<2&
_T_668R


_T_422


_T_666Cat.scala 30:58<2&
_T_669R


_T_668


_T_667Cat.scala 30:586z
:


muxdata


_T_669FPU.scala 364:14FPU.scala 363:41@2)
_T_670R:
:


inbitsin1FPU.scala 370:39

_T_671
A
 



_T_671
 z



_T_671


_T_670
 K24
_T_672*R(:
:


inbitsin1
31
0FPU.scala 372:33M23
_T_673)R':
:


inbitstyp
1
1Package.scala 44:13>2'
_T_675R


_T_673	

0FPU.scala 373:49Ð:¸



_T_675J23
_T_676)R':
:


inbitstyp
0
0FPU.scala 374:3112
_T_677R


_T_672FPU.scala 374:4512
_T_678R


_T_672FPU.scala 374:60G20
_T_679&2$



_T_676


_T_677


_T_678FPU.scala 374:19/z



_T_671


_T_679FPU.scala 374:13FPU.scala 373:5622
intValueR


_T_671FPU.scala 377:9M26
_T_682,R*:
:


inbitscmd	

4FPU.scala 380:21>2'
_T_683R	

0


_T_682FPU.scala 380:21°:



_T_683-*
	INToRecFN	INToRecFNFPU.scala 381:21
:


	INToRecFNio
 ,z%
:


	INToRecFNclock	

clock
 ,z%
:


	INToRecFNreset	

reset
 J23
_T_684)R':
:


inbitstyp
0
0FPU.scala 382:3612
_T_685R


_T_684FPU.scala 382:24Hz1
#:!
:


	INToRecFNiosignedIn


_T_685FPU.scala 382:21Dz-
:
:


	INToRecFNioin


intValueFPU.scala 383:15ZzC
':%
:


	INToRecFNioroundingMode:
:


inbitsrmFPU.scala 384:251*
INToRecFN_1INToRecFN_1FPU.scala 391:25!
:


INToRecFN_1io
 .z'
:


INToRecFN_1clock	

clock
 .z'
:


INToRecFN_1reset	

reset
 J23
_T_686)R':
:


inbitstyp
0
0FPU.scala 392:4012
_T_687R


_T_686FPU.scala 392:28Jz3
%:#
:


INToRecFN_1iosignedIn


_T_687FPU.scala 392:25Fz/
:
:


INToRecFN_1ioin


intValueFPU.scala 393:19\zE
):'
:


INToRecFN_1ioroundingMode:
:


inbitsrmFPU.scala 394:29M26
_T_688,R*	 :
:


INToRecFN_1ioout
33FPU.scala 396:36P2:
_T_6890R.


_T_688:
:


	INToRecFNiooutCat.scala 30:586z
:


muxdata


_T_689FPU.scala 396:18Tz=
:


muxexc):'
:


	INToRecFNioexceptionFlagsFPU.scala 397:17P29
_T_691/R-:
:


inbitssingle	

0FPU.scala 398:15Ê:²



_T_691Lz5
:


muxdata :
:


INToRecFN_1iooutFPU.scala 399:20Vz?
:


muxexc+:)
:


INToRecFN_1ioexceptionFlagsFPU.scala 400:19FPU.scala 398:32FPU.scala 380:38K3
_T_694
	

clock"	

reset*	

0Valid.scala 47:187z



_T_694:


invalidValid.scala 47:18eO
_T_698!*
data
A
exc
	

clock"	

0*


_T_698Reg.scala 34:16W:A
:


invalid,



_T_698

muxReg.scala 35:23Reg.scala 35:19`
H
_T_710>*<
valid

)bits!*
data
A
exc
Valid.scala 42:21%



_T_710Valid.scala 42:21;z#
:



_T_710valid


_T_694Valid.scala 43:17;"
:



_T_710bits


_T_698Valid.scala 44:165
:


ioout


_T_710FPU.scala 405:12
×LÔL
FPToFP
clock" 
reset
è
ioß*Ü
inù*ö
valid

âbitsÙ*Ö
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags

rm

typ

in1
A
in2
A
in3
A
Eout>*<
valid

)bits!*
data
A
exc

lt



io
 


io
 K3
_T_134
	

clock"	

reset*	

0Valid.scala 47:18?z'



_T_134:
:


ioinvalidValid.scala 47:18
_T_157Ù*Ö
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags

rm

typ

in1
A
in2
A
in3
A	

clock"	

0*


_T_157Reg.scala 34:16p:Z
:
:


ioinvalid=&



_T_157:
:


ioinbitsReg.scala 35:23Reg.scala 35:19

inù*ö
valid

âbitsÙ*Ö
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags

rm

typ

in1
A
in2
A
in3
AValid.scala 42:21!


inValid.scala 42:217z
:


invalid


_T_134Valid.scala 43:177
:


inbits


_T_157Valid.scala 44:16I22
_T_272(R&:
:


inbitsrm
1
1FPU.scala 417:33[2D
_T_273:R8:
:


inbitsin1:
:


inbitsin2FPU.scala 417:50I22
_T_274(R&:
:


inbitsrm
0
0FPU.scala 417:79@2)
_T_275R:
:


inbitsin2FPU.scala 417:84V2?
_T_276523



_T_274


_T_275:
:


inbitsin2FPU.scala 417:68H21
signNum&2$



_T_272


_T_273


_T_276FPU.scala 417:22>2'
_T_277R
	
signNum
32
32FPU.scala 418:30K24
_T_278*R(:
:


inbitsin1
31
0FPU.scala 418:47=2'
fsgnj_sR


_T_277


_T_278Cat.scala 30:58F2/
_T_279%R#	:
:


inbitsin1
33FPU.scala 421:54=2'
_T_280R


_T_279
	
fsgnj_sCat.scala 30:58>2'
_T_281R
	
signNum
64
64FPU.scala 422:49K24
_T_282*R(:
:


inbitsin1
63
0FPU.scala 422:66<2&
_T_283R


_T_281


_T_282Cat.scala 30:58X2A
fsgnj826
:
:


inbitssingle


_T_280


_T_283FPU.scala 421:21?
(
mux!*
data
A
exc
FPU.scala 424:19!	


muxFPU.scala 424:196z
:


muxexc	

0FPU.scala 425:135z
:


muxdata	

fsgnjFPU.scala 426:14N27
_T_292-R+:
:


inbitscmd


13FPU.scala 428:23>2'
_T_293R	

5


_T_292FPU.scala 428:23×!:¿!



_T_293K25
_T_294+R):
:


inbitsin1
31
29FPU.scala 226:712
_T_295R


_T_294FPU.scala 226:58>2'
_T_297R


_T_295	

0FPU.scala 226:58K25
_T_298+R):
:


inbitsin2
31
29FPU.scala 226:712
_T_299R


_T_298FPU.scala 226:58>2'
_T_301R


_T_299	

0FPU.scala 226:58K25
_T_302+R):
:


inbitsin1
31
29FPU.scala 226:712
_T_303R


_T_302FPU.scala 226:58>2'
_T_305R


_T_303	

0FPU.scala 226:58L25
_T_306+R):
:


inbitsin1
22
22FPU.scala 231:46>2'
_T_308R


_T_306	

0FPU.scala 231:43=2&
_T_309R


_T_305


_T_308FPU.scala 231:40K25
_T_310+R):
:


inbitsin2
31
29FPU.scala 226:712
_T_311R


_T_310FPU.scala 226:58>2'
_T_313R


_T_311	

0FPU.scala 226:58L25
_T_314+R):
:


inbitsin2
22
22FPU.scala 231:46>2'
_T_316R


_T_314	

0FPU.scala 231:43=2&
_T_317R


_T_313


_T_316FPU.scala 231:40=2&
_T_318R


_T_309


_T_317FPU.scala 434:31=2&
_T_319R


_T_297


_T_301FPU.scala 435:43=2&
_T_320R


_T_318


_T_319FPU.scala 435:32\2D
_T_323:R8


3762290688!

16143152864309542912AFPU.scala 436:10072
_T_324R


_T_323
1FPU.scala 436:100I22
_T_325(R&:
:


inbitsrm
0
0FPU.scala 437:30A2*
_T_326 R


_T_325:


ioltFPU.scala 437:34>2'
_T_328R


_T_297	

0FPU.scala 437:47=2&
_T_329R


_T_326


_T_328FPU.scala 437:44=2&
_T_330R


_T_301


_T_329FPU.scala 437:17K25
_T_331+R):
:


inbitsin1
63
61FPU.scala 226:712
_T_332R


_T_331FPU.scala 226:58>2'
_T_334R


_T_332	

0FPU.scala 226:58K25
_T_335+R):
:


inbitsin2
63
61FPU.scala 226:712
_T_336R


_T_335FPU.scala 226:58>2'
_T_338R


_T_336	

0FPU.scala 226:58K25
_T_339+R):
:


inbitsin1
63
61FPU.scala 226:712
_T_340R


_T_339FPU.scala 226:58>2'
_T_342R


_T_340	

0FPU.scala 226:58L25
_T_343+R):
:


inbitsin1
51
51FPU.scala 231:46>2'
_T_345R


_T_343	

0FPU.scala 231:43=2&
_T_346R


_T_342


_T_345FPU.scala 231:40K25
_T_347+R):
:


inbitsin2
63
61FPU.scala 226:712
_T_348R


_T_347FPU.scala 226:58>2'
_T_350R


_T_348	

0FPU.scala 226:58L25
_T_351+R):
:


inbitsin2
51
51FPU.scala 231:46>2'
_T_353R


_T_351	

0FPU.scala 231:43=2&
_T_354R


_T_350


_T_353FPU.scala 231:40=2&
_T_355R


_T_346


_T_354FPU.scala 434:31=2&
_T_356R


_T_334


_T_338FPU.scala 435:43=2&
_T_357R


_T_355


_T_356FPU.scala 435:32I22
_T_359(R&:
:


inbitsrm
0
0FPU.scala 437:30A2*
_T_360 R


_T_359:


ioltFPU.scala 437:34>2'
_T_362R


_T_334	

0FPU.scala 437:47=2&
_T_363R


_T_360


_T_362FPU.scala 437:44=2&
_T_364R


_T_338


_T_363FPU.scala 437:17X2B
_T_365826
:
:


inbitssingle


_T_330


_T_364Misc.scala 42:9Y2B
_T_366826
:
:


inbitssingle


_T_318


_T_355Misc.scala 42:36Y2B
_T_367826
:
:


inbitssingle


_T_320


_T_357Misc.scala 42:63m2V
_T_368L2J
:
:


inbitssingle


_T_324

16143152864309542912AMisc.scala 42:9062
_T_369R


_T_366
4FPU.scala 443:285z
:


muxexc


_T_369FPU.scala 443:15e2N
_T_370D2B



_T_365:
:


inbitsin1:
:


inbitsin2FPU.scala 444:42G20
_T_371&2$



_T_367


_T_368


_T_370FPU.scala 444:226z
:


muxdata


_T_371FPU.scala 444:16FPU.scala 428:40M26
_T_374,R*:
:


inbitscmd	

4FPU.scala 450:25>2'
_T_375R	

0


_T_374FPU.scala 450:25ª:



_T_3755*
RecFNToRecFNRecFNToRecFN_2FPU.scala 451:25"
:


RecFNToRecFNio
 /z(
:


RecFNToRecFNclock	

clock
 /z(
:


RecFNToRecFNreset	

reset
 Tz=
 :
:


RecFNToRecFNioin:
:


inbitsin1FPU.scala 452:19]zF
*:(
:


RecFNToRecFNioroundingMode:
:


inbitsrmFPU.scala 453:297* 
RecFNToRecFN_1RecFNToRecFN_1FPU.scala 455:25$
:


RecFNToRecFN_1io
 1z*
:


RecFNToRecFN_1clock	

clock
 1z*
:


RecFNToRecFN_1reset	

reset
 Vz?
": 
:


RecFNToRecFN_1ioin:
:


inbitsin1FPU.scala 456:19_zH
,:*
:


RecFNToRecFN_1ioroundingMode:
:


inbitsrmFPU.scala 457:29î:Ö
:
:


inbitssingleP29
_T_376/R-	#:!
:


RecFNToRecFN_1ioout
33FPU.scala 460:38S2=
_T_3773R1


_T_376!:
:


RecFNToRecFNiooutCat.scala 30:586z
:


muxdata


_T_377FPU.scala 460:20Wz@
:


muxexc,:*
:


RecFNToRecFNioexceptionFlagsFPU.scala 461:19FPU.scala 459:31P29
_T_379/R-:
:


inbitssingle	

0FPU.scala 459:31Ð:¸



_T_379Oz8
:


muxdata#:!
:


RecFNToRecFN_1iooutFPU.scala 463:20YzB
:


muxexc.:,
:


RecFNToRecFN_1ioexceptionFlagsFPU.scala 464:19FPU.scala 462:21FPU.scala 450:42K3
_T_382
	

clock"	

reset*	

0Valid.scala 47:187z



_T_382:


invalidValid.scala 47:18eO
_T_386!*
data
A
exc
	

clock"	

0*


_T_386Reg.scala 34:16W:A
:


invalid,



_T_386

muxReg.scala 35:23Reg.scala 35:19`
H
_T_398>*<
valid

)bits!*
data
A
exc
Valid.scala 42:21%



_T_398Valid.scala 42:21;z#
:



_T_398valid


_T_382Valid.scala 43:17;"
:



_T_398bits


_T_386Valid.scala 44:165
:


ioout


_T_398FPU.scala 469:10
Ä"Á"
FPUFMAPipe_1
clock" 
reset
Ø
ioÏ*Ì
inù*ö
valid

âbitsÙ*Ö
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags

rm

typ

in1
A
in2
A
in3
A
Eout>*<
valid

)bits!*
data
A
exc



io
 


io
 52
oneR	

1
63FPU.scala 479:21T2=
_T_1313R1!:
:
:


ioinbitsin1
64
64FPU.scala 480:29T2=
_T_1323R1!:
:
:


ioinbitsin2
64
64FPU.scala 480:53=2&
_T_133R


_T_131


_T_132FPU.scala 480:3752
zeroR


_T_133
64FPU.scala 480:62I2
valid
	

clock"	

0*	

validFPU.scala 482:18=z&
	

valid:
:


ioinvalidFPU.scala 482:18
inÙ*Ö
cmd

ldst

wen

ren1

ren2

ren3

swap12

swap23

single

fromint

toint

fastpipe

fma

div

sqrt

wflags

rm

typ

in1
A
in2
A
in3
A	

clock"	

0*

inFPU.scala 483:15Ä:¬
:
:


ioinvalid9"


in:
:


ioinbitsFPU.scala 485:8R2;
_T_1771R/!:
:
:


ioinbitscmd
1
1FPU.scala 488:33o2X
_T_178NRL": 
:
:


ioinbitsren3$:"
:
:


ioinbitsswap23FPU.scala 488:48=2&
_T_179R


_T_177


_T_178FPU.scala 488:37R2;
_T_1801R/!:
:
:


ioinbitscmd
0
0FPU.scala 488:78<2&
_T_181R


_T_179


_T_180Cat.scala 30:584z
:


incmd


_T_181FPU.scala 488:12p:Y
$:"
:
:


ioinbitsswap231z
:


inin2

oneFPU.scala 489:32FPU.scala 489:23o2X
_T_182NRL": 
:
:


ioinbitsren3$:"
:
:


ioinbitsswap23FPU.scala 490:21E2'
_T_184R


_T_182	

0Conditional.scala 19:11^:@



_T_1842z
:


inin3

zeroFPU.scala 490:45Conditional.scala 19:15FPU.scala 484:22+*
fmaMulAddRecFN_1FPU.scala 493:19
:


fmaio
 &z
:


fmaclock	

clock
 &z
:


fmareset	

reset
 Az*
:
:


fmaioop:


incmdFPU.scala 494:13Jz3
!:
:


fmaioroundingMode:


inrmFPU.scala 495:23@z)
:
:


fmaioa:


inin1FPU.scala 496:12@z)
:
:


fmaiob:


inin2FPU.scala 497:12@z)
:
:


fmaioc:


inin3FPU.scala 498:12?
(
res!*
data
A
exc
FPU.scala 500:17!	


resFPU.scala 500:17Dz-
:


resdata:
:


fmaiooutFPU.scala 501:12Nz7
:


resexc#:!
:


fmaioexceptionFlagsFPU.scala 502:11K3
_T_192
	

clock"	

reset*	

0Valid.scala 47:18/z



_T_192	

validValid.scala 47:18eO
_T_196!*
data
A
exc
	

clock"	

0*


_T_196Reg.scala 34:16O:9
	

valid,



_T_196

resReg.scala 35:23Reg.scala 35:19K3
_T_201
	

clock"	

reset*	

0Valid.scala 47:180z



_T_201


_T_192Valid.scala 47:18eO
_T_205!*
data
A
exc
	

clock"	

0*


_T_205Reg.scala 34:16S:=



_T_192/



_T_205


_T_196Reg.scala 35:23Reg.scala 35:19K3
_T_210
	

clock"	

reset*	

0Valid.scala 47:180z



_T_210


_T_201Valid.scala 47:18eO
_T_214!*
data
A
exc
	

clock"	

0*


_T_214Reg.scala 34:16S:=



_T_201/



_T_214


_T_205Reg.scala 35:23Reg.scala 35:19`
H
_T_226>*<
valid

)bits!*
data
A
exc
Valid.scala 42:21%



_T_226Valid.scala 42:21;z#
:



_T_226valid


_T_210Valid.scala 43:17;"
:



_T_226bits


_T_214Valid.scala 44:165
:


ioout


_T_226FPU.scala 503:10
·´
DivSqrtRecF64
clock" 
reset
ö
ioí*ê
inReady_div

inReady_sqrt

inValid

sqrtOp

a
A
b
A
roundingMode

outValid_div

outValid_sqrt

out
A
exceptionFlags



io
 


io
 =*
dsDivSqrtRecF64_mulAddZ31DivSqrtRecF64.scala 59:20
:


dsio
 %z
:


dsclock	

clock
 %z
:


dsreset	

reset
 Zz:
:


ioinReady_div:
:


dsioinReady_divDivSqrtRecF64.scala 61:20\z<
:


ioinReady_sqrt :
:


dsioinReady_sqrtDivSqrtRecF64.scala 62:21Rz2
:
:


dsioinValid:


ioinValidDivSqrtRecF64.scala 63:19Pz0
:
:


dsiosqrtOp:


iosqrtOpDivSqrtRecF64.scala 64:18Fz&
:
:


dsioa:


ioaDivSqrtRecF64.scala 65:13Fz&
:
:


dsiob:


iobDivSqrtRecF64.scala 66:13\z<
 :
:


dsioroundingMode:


ioroundingModeDivSqrtRecF64.scala 67:24\z<
:


iooutValid_div :
:


dsiooutValid_divDivSqrtRecF64.scala 68:21^z>
:


iooutValid_sqrt!:
:


dsiooutValid_sqrtDivSqrtRecF64.scala 69:22Jz*
:


ioout:
:


dsiooutDivSqrtRecF64.scala 70:12`z@
:


ioexceptionFlags": 
:


dsioexceptionFlagsDivSqrtRecF64.scala 71:23,*
mulMul54DivSqrtRecF64.scala 73:21
:


mulio
 &z
:


mulclock	

clock
 &z
:


mulreset	

reset
 X28
_T_24/R-:
:


dsiousingMulAdd
0
0DivSqrtRecF64.scala 75:39Hz(
:
:


mulioval_s0	

_T_24DivSqrtRecF64.scala 75:19ezE
:
:


mulio
latch_a_s0": 
:


dsiolatchMulAddA_0DivSqrtRecF64.scala 76:23Zz:
:
:


mulioa_s0:
:


dsio	mulAddA_0DivSqrtRecF64.scala 77:17ezE
:
:


mulio
latch_b_s0": 
:


dsiolatchMulAddB_0DivSqrtRecF64.scala 78:23Zz:
:
:


muliob_s0:
:


dsio	mulAddB_0DivSqrtRecF64.scala 79:17Zz:
:
:


mulioc_s2:
:


dsio	mulAddC_2DivSqrtRecF64.scala 80:17dzD
": 
:


dsiomulAddResult_3:
:


mulio	result_s3DivSqrtRecF64.scala 81:26
Æ$Ã$
RecFNToRecFN_2
clock" 
reset
]
ioU*S
in
A
roundingMode

out
!
exceptionFlags



io
 


io
 J2)
_T_10 R:


ioin
63
52rawFNFromRecFN.scala 50:21D2#
_T_11R	

_T_10
11
9rawFNFromRecFN.scala 51:29F2%
_T_13R	

_T_11	

0rawFNFromRecFN.scala 51:54E2$
_T_14R	

_T_10
11
10rawFNFromRecFN.scala 52:29F2%
_T_16R	

_T_14	

3rawFNFromRecFN.scala 52:54
n
_T_24e*c
sign

isNaN

isInf

isZero

sExp

sig
8rawFNFromRecFN.scala 54:23-
	

_T_24rawFNFromRecFN.scala 54:23J2)
_T_31 R:


ioin
64
64rawFNFromRecFN.scala 55:23Az 
:
	

_T_24sign	

_T_31rawFNFromRecFN.scala 55:18C2"
_T_32R	

_T_10
9
9rawFNFromRecFN.scala 56:40D2#
_T_33R	

_T_16	

_T_32rawFNFromRecFN.scala 56:32Bz!
:
	

_T_24isNaN	

_T_33rawFNFromRecFN.scala 56:19C2"
_T_34R	

_T_10
9
9rawFNFromRecFN.scala 57:40F2%
_T_36R	

_T_34	

0rawFNFromRecFN.scala 57:35D2#
_T_37R	

_T_16	

_T_36rawFNFromRecFN.scala 57:32Bz!
:
	

_T_24isInf	

_T_37rawFNFromRecFN.scala 57:19Cz"
:
	

_T_24isZero	

_T_13rawFNFromRecFN.scala 58:2092
_T_38R	

_T_10rawFNFromRecFN.scala 59:25Az 
:
	

_T_24sExp	

_T_38rawFNFromRecFN.scala 59:18F2%
_T_41R	

_T_13	

0rawFNFromRecFN.scala 60:36I2(
_T_42R:


ioin
51
0rawFNFromRecFN.scala 60:48;2%
_T_44R	

_T_42	

0Cat.scala 30:58;2%
_T_45R	

0	

_T_41Cat.scala 30:5892#
_T_46R	

_T_45	

_T_44Cat.scala 30:58@z
:
	

_T_24sig	

_T_46rawFNFromRecFN.scala 60:17V28
_T_48/R-:
	

_T_24sExpR

2304resizeRawFN.scala 49:31
t
outRawFloate*c
sign

isNaN

isInf

isZero

sExp


sig
resizeRawFN.scala 51:230


outRawFloatresizeRawFN.scala 51:23Nz0
:


outRawFloatsign:
	

_T_24signresizeRawFN.scala 52:20Pz2
:


outRawFloatisNaN:
	

_T_24isNaNresizeRawFN.scala 53:20Pz2
:


outRawFloatisInf:
	

_T_24isInfresizeRawFN.scala 54:20Rz4
:


outRawFloatisZero:
	

_T_24isZeroresizeRawFN.scala 55:20I2+
_T_63"R 	

_T_48R	

0resizeRawFN.scala 60:31A2#
_T_64R	

_T_48
12
9resizeRawFN.scala 61:33C2%
_T_66R	

_T_64	

0resizeRawFN.scala 61:65N24
_T_71+2)
	

1

127	

0Bitwise.scala 71:12;2%
_T_73R	

_T_71	

0Cat.scala 30:58@2"
_T_74R	

_T_48
8
0resizeRawFN.scala 63:33J2,
_T_75#2!
	

_T_66	

_T_73	

_T_74resizeRawFN.scala 61:2592#
_T_76R	

_T_63	

_T_75Cat.scala 30:5862
_T_77R	

_T_76resizeRawFN.scala 65:20Dz&
:


outRawFloatsExp	

_T_77resizeRawFN.scala 56:18K2-
_T_78$R":
	

_T_24sig
55
30resizeRawFN.scala 71:28J2,
_T_79#R!:
	

_T_24sig
29
0resizeRawFN.scala 72:28C2%
_T_81R	

_T_79	

0resizeRawFN.scala 72:5692#
_T_82R	

_T_78	

_T_81Cat.scala 30:58Cz%
:


outRawFloatsig	

_T_82resizeRawFN.scala 67:17W23
_T_83*R(:


outRawFloatsig
24
24RoundRawFNToRecFN.scala 61:57I2%
_T_85R	

_T_83	

0RoundRawFNToRecFN.scala 61:49]29

invalidExc+R):


outRawFloatisNaN	

_T_85RoundRawFNToRecFN.scala 61:46H*(
RoundRawFNToRecFNRoundRawFNToRecFN_1RecFNToRecFN.scala 102:19'
:


RoundRawFNToRecFNio
 4z-
 :


RoundRawFNToRecFNclock	

clock
 4z-
 :


RoundRawFNToRecFNreset	

reset
 _z?
-:+
:


RoundRawFNToRecFNio
invalidExc


invalidExcRecFNToRecFN.scala 103:41]z=
.:,
:


RoundRawFNToRecFNioinfiniteExc	

0RecFNToRecFN.scala 104:42Y8
%:#
:


RoundRawFNToRecFNioin

outRawFloatRecFNToRecFN.scala 105:33kzK
/:-
:


RoundRawFNToRecFNioroundingMode:


ioroundingModeRecFNToRecFN.scala 106:43Yz9
:


ioout&:$
:


RoundRawFNToRecFNiooutRecFNToRecFN.scala 107:16ozO
:


ioexceptionFlags1:/
:


RoundRawFNToRecFNioexceptionFlagsRecFNToRecFN.scala 108:27

MulAddRecFN
clock" 
reset

io*
op

a
!
b
!
c
!
roundingMode

out
!
exceptionFlags



io
 


io
 G*(
mulAddRecFN_preMulMulAddRecFN_preMulMulAddRecFN.scala 598:15( 
:


mulAddRecFN_preMulio
 5z.
!:


mulAddRecFN_preMulclock	

clock
 5z.
!:


mulAddRecFN_preMulreset	

reset
 I**
mulAddRecFN_postMulMulAddRecFN_postMulMulAddRecFN.scala 600:15)!
:


mulAddRecFN_postMulio
 6z/
": 


mulAddRecFN_postMulclock	

clock
 6z/
": 


mulAddRecFN_postMulreset	

reset
 Wz8
&:$
:


mulAddRecFN_preMulioop:


ioopMulAddRecFN.scala 602:30Uz6
%:#
:


mulAddRecFN_preMulioa:


ioaMulAddRecFN.scala 603:30Uz6
%:#
:


mulAddRecFN_preMuliob:


iobMulAddRecFN.scala 604:30Uz6
%:#
:


mulAddRecFN_preMulioc:


iocMulAddRecFN.scala 605:30kzL
0:.
:


mulAddRecFN_preMulioroundingMode:


ioroundingModeMulAddRecFN.scala 606:40`
/:-
:


mulAddRecFN_postMulio
fromPreMul-:+
:


mulAddRecFN_preMulio	toPostMulMulAddRecFN.scala 608:392g
_T_16^R\+:)
:


mulAddRecFN_preMuliomulAddA+:)
:


mulAddRecFN_preMuliomulAddBMulAddRecFN.scala 610:39]2G
_T_18>R<	

0+:)
:


mulAddRecFN_preMuliomulAddCCat.scala 30:58B2#
_T_19R	

_T_16	

_T_18MulAddRecFN.scala 610:71<2
_T_20R	

_T_19
1MulAddRecFN.scala 610:71]z>
1:/
:


mulAddRecFN_postMuliomulAddResult	

_T_20MulAddRecFN.scala 609:41Zz;
:


ioout(:&
:


mulAddRecFN_postMuliooutMulAddRecFN.scala 613:12pzQ
:


ioexceptionFlags3:1
:


mulAddRecFN_postMulioexceptionFlagsMulAddRecFN.scala 614:23
á;Þ;
CompareRecFN
clock" 
reset

io{*y
a
A
b
A
	signaling

lt

eq

gt

exceptionFlags



io
 


io
 I2(
_T_16R:


ioa
63
52rawFNFromRecFN.scala 50:21D2#
_T_17R	

_T_16
11
9rawFNFromRecFN.scala 51:29F2%
_T_19R	

_T_17	

0rawFNFromRecFN.scala 51:54E2$
_T_20R	

_T_16
11
10rawFNFromRecFN.scala 52:29F2%
_T_22R	

_T_20	

3rawFNFromRecFN.scala 52:54
m
rawAe*c
sign

isNaN

isInf

isZero

sExp

sig
8rawFNFromRecFN.scala 54:23,



rawArawFNFromRecFN.scala 54:23I2(
_T_36R:


ioa
64
64rawFNFromRecFN.scala 55:23@z
:


rawAsign	

_T_36rawFNFromRecFN.scala 55:18C2"
_T_37R	

_T_16
9
9rawFNFromRecFN.scala 56:40D2#
_T_38R	

_T_22	

_T_37rawFNFromRecFN.scala 56:32Az 
:


rawAisNaN	

_T_38rawFNFromRecFN.scala 56:19C2"
_T_39R	

_T_16
9
9rawFNFromRecFN.scala 57:40F2%
_T_41R	

_T_39	

0rawFNFromRecFN.scala 57:35D2#
_T_42R	

_T_22	

_T_41rawFNFromRecFN.scala 57:32Az 
:


rawAisInf	

_T_42rawFNFromRecFN.scala 57:19Bz!
:


rawAisZero	

_T_19rawFNFromRecFN.scala 58:2092
_T_43R	

_T_16rawFNFromRecFN.scala 59:25@z
:


rawAsExp	

_T_43rawFNFromRecFN.scala 59:18F2%
_T_46R	

_T_19	

0rawFNFromRecFN.scala 60:36H2'
_T_47R:


ioa
51
0rawFNFromRecFN.scala 60:48;2%
_T_49R	

_T_47	

0Cat.scala 30:58;2%
_T_50R	

0	

_T_46Cat.scala 30:5892#
_T_51R	

_T_50	

_T_49Cat.scala 30:58?z
:


rawAsig	

_T_51rawFNFromRecFN.scala 60:17I2(
_T_52R:


iob
63
52rawFNFromRecFN.scala 50:21D2#
_T_53R	

_T_52
11
9rawFNFromRecFN.scala 51:29F2%
_T_55R	

_T_53	

0rawFNFromRecFN.scala 51:54E2$
_T_56R	

_T_52
11
10rawFNFromRecFN.scala 52:29F2%
_T_58R	

_T_56	

3rawFNFromRecFN.scala 52:54
m
rawBe*c
sign

isNaN

isInf

isZero

sExp

sig
8rawFNFromRecFN.scala 54:23,



rawBrawFNFromRecFN.scala 54:23I2(
_T_72R:


iob
64
64rawFNFromRecFN.scala 55:23@z
:


rawBsign	

_T_72rawFNFromRecFN.scala 55:18C2"
_T_73R	

_T_52
9
9rawFNFromRecFN.scala 56:40D2#
_T_74R	

_T_58	

_T_73rawFNFromRecFN.scala 56:32Az 
:


rawBisNaN	

_T_74rawFNFromRecFN.scala 56:19C2"
_T_75R	

_T_52
9
9rawFNFromRecFN.scala 57:40F2%
_T_77R	

_T_75	

0rawFNFromRecFN.scala 57:35D2#
_T_78R	

_T_58	

_T_77rawFNFromRecFN.scala 57:32Az 
:


rawBisInf	

_T_78rawFNFromRecFN.scala 57:19Bz!
:


rawBisZero	

_T_55rawFNFromRecFN.scala 58:2092
_T_79R	

_T_52rawFNFromRecFN.scala 59:25@z
:


rawBsExp	

_T_79rawFNFromRecFN.scala 59:18F2%
_T_82R	

_T_55	

0rawFNFromRecFN.scala 60:36H2'
_T_83R:


iob
51
0rawFNFromRecFN.scala 60:48;2%
_T_85R	

_T_83	

0Cat.scala 30:58;2%
_T_86R	

0	

_T_82Cat.scala 30:5892#
_T_87R	

_T_86	

_T_85Cat.scala 30:58?z
:


rawBsig	

_T_87rawFNFromRecFN.scala 60:17N2/
_T_89&R$:


rawAisNaN	

0CompareRecFN.scala 57:19N2/
_T_91&R$:


rawBisNaN	

0CompareRecFN.scala 57:35D2%
orderedR	

_T_89	

_T_91CompareRecFN.scala 57:32Y2:
bothInfs.R,:


rawAisInf:


rawBisInfCompareRecFN.scala 58:33\2=
	bothZeros0R.:


rawAisZero:


rawBisZeroCompareRecFN.scala 59:33U26
eqExps,R*:


rawAsExp:


rawBsExpCompareRecFN.scala 60:29T25
_T_92,R*:


rawAsExp:


rawBsExpCompareRecFN.scala 62:20R23
_T_93*R(:


rawAsig:


rawBsigCompareRecFN.scala 62:57C2$
_T_94R


eqExps	

_T_93CompareRecFN.scala 62:44J2+
common_ltMagsR	

_T_92	

_T_94CompareRecFN.scala 62:33R23
_T_95*R(:


rawAsig:


rawBsigCompareRecFN.scala 63:45K2,
common_eqMagsR


eqExps	

_T_95CompareRecFN.scala 63:32G2)
_T_97 R

	bothZeros	

0CompareRecFN.scala 66:9M2.
_T_99%R#:


rawBsign	

0CompareRecFN.scala 67:28L2-
_T_100#R!:


rawAsign	

_T_99CompareRecFN.scala 67:25H2)
_T_102R


bothInfs	

0CompareRecFN.scala 68:19M2.
_T_104$R"

common_ltMags	

0CompareRecFN.scala 69:38M2.
_T_105$R":


rawAsign


_T_104CompareRecFN.scala 69:35M2.
_T_107$R"

common_eqMags	

0CompareRecFN.scala 69:57E2&
_T_108R


_T_105


_T_107CompareRecFN.scala 69:54N2/
_T_110%R#:


rawBsign	

0CompareRecFN.scala 70:29L2-
_T_111#R!


_T_110

common_ltMagsCompareRecFN.scala 70:41E2&
_T_112R


_T_108


_T_111CompareRecFN.scala 69:74E2&
_T_113R


_T_102


_T_112CompareRecFN.scala 68:30E2&
_T_114R


_T_100


_T_113CompareRecFN.scala 67:41H2)

ordered_ltR	

_T_97


_T_114CompareRecFN.scala 66:21U26
_T_115,R*:


rawAsign:


rawBsignCompareRecFN.scala 72:34N2/
_T_116%R#


bothInfs

common_eqMagsCompareRecFN.scala 72:62E2&
_T_117R


_T_115


_T_116CompareRecFN.scala 72:49L2-

ordered_eqR

	bothZeros


_T_117CompareRecFN.scala 72:19Q2-
_T_118#R!:


rawAsig
53
53RoundRawFNToRecFN.scala 61:57K2'
_T_120R


_T_118	

0RoundRawFNToRecFN.scala 61:49S2/
_T_121%R#:


rawAisNaN


_T_120RoundRawFNToRecFN.scala 61:46Q2-
_T_122#R!:


rawBsig
53
53RoundRawFNToRecFN.scala 61:57K2'
_T_124R


_T_122	

0RoundRawFNToRecFN.scala 61:49S2/
_T_125%R#:


rawBisNaN


_T_124RoundRawFNToRecFN.scala 61:46E2&
_T_126R


_T_121


_T_125CompareRecFN.scala 75:29G2(
_T_128R
	
ordered	

0CompareRecFN.scala 76:30P21
_T_129'R%:


io	signaling


_T_128CompareRecFN.scala 76:27F2'
invalidR


_T_126


_T_129CompareRecFN.scala 75:52J2+
_T_130!R
	
ordered


ordered_ltCompareRecFN.scala 78:22;z
:


iolt


_T_130CompareRecFN.scala 78:11J2+
_T_131!R
	
ordered


ordered_eqCompareRecFN.scala 79:22;z
:


ioeq


_T_131CompareRecFN.scala 79:11J2+
_T_133!R


ordered_lt	

0CompareRecFN.scala 80:25F2'
_T_134R
	
ordered


_T_133CompareRecFN.scala 80:22J2+
_T_136!R


ordered_eq	

0CompareRecFN.scala 80:41E2&
_T_137R


_T_134


_T_136CompareRecFN.scala 80:38;z
:


iogt


_T_137CompareRecFN.scala 80:11>2(
_T_139R
	
invalid	

0Cat.scala 30:58Gz(
:


ioexceptionFlags


_T_139CompareRecFN.scala 82:23
>>
	RecFNToIN
clock" 
reset
w
ioo*m
in
A
roundingMode

	signedOut

out
 
intExceptionFlags



io
 


io
 D2(
sign R:


ioin
64
64RecFNToIN.scala 54:21C2'
exp R:


ioin
63
52RecFNToIN.scala 55:20D2(
fractR:


ioin
51
0RecFNToIN.scala 56:22=2!
_T_12R

exp
11
9RecFNToIN.scala 58:22B2&
isZeroR	

_T_12	

0RecFNToIN.scala 58:47>2"
_T_14R

exp
11
10RecFNToIN.scala 59:25C2'
invalidR	

_T_14	

3RecFNToIN.scala 59:50<2 
_T_16R

exp
9
9RecFNToIN.scala 60:33A2%
isNaNR
	
invalid	

_T_16RecFNToIN.scala 60:27L20
notSpecial_magGeOneR

exp
11
11RecFNToIN.scala 61:34G21
_T_17(R&

notSpecial_magGeOne	

fractCat.scala 30:58<2 
_T_18R

exp
4
0RecFNToIN.scala 74:20X2<
_T_20321


notSpecial_magGeOne	

_T_18	

0RecFNToIN.scala 73:16D2(

shiftedSigR
	

_T_17	

_T_20RecFNToIN.scala 72:40L20
unroundedInt R


shiftedSig
83
52RecFNToIN.scala 82:24E2)
_T_21 R


shiftedSig
52
51RecFNToIN.scala 85:23D2(
_T_22R


shiftedSig
50
0RecFNToIN.scala 86:23A2%
_T_24R	

_T_22	

0RecFNToIN.scala 86:41=2'
	roundBitsR	

_T_21	

_T_24Cat.scala 30:58B2&
_T_25R

	roundBits
1
0RecFNToIN.scala 88:58A2%
_T_27R	

_T_25	

0RecFNToIN.scala 88:65B2&
_T_29R


isZero	

0RecFNToIN.scala 88:70]2A
roundInexact12/


notSpecial_magGeOne	

_T_27	

_T_29RecFNToIN.scala 88:27B2&
_T_30R

	roundBits
2
1RecFNToIN.scala 91:2242
_T_31R	

_T_30RecFNToIN.scala 91:29A2%
_T_33R	

_T_31	

0RecFNToIN.scala 91:29B2&
_T_34R

	roundBits
1
0RecFNToIN.scala 91:4642
_T_35R	

_T_34RecFNToIN.scala 91:53A2%
_T_37R	

_T_35	

0RecFNToIN.scala 91:53?2#
_T_38R	

_T_33	

_T_37RecFNToIN.scala 91:34=2!
_T_39R

exp
10
0RecFNToIN.scala 92:2042
_T_40R	

_T_39RecFNToIN.scala 92:38A2%
_T_42R	

_T_40	

0RecFNToIN.scala 92:38B2&
_T_43R

	roundBits
1
0RecFNToIN.scala 92:53A2%
_T_45R	

_T_43	

0RecFNToIN.scala 92:60J2.
_T_47%2#
	

_T_42	

_T_45	

0RecFNToIN.scala 92:16f2J
roundIncr_nearestEven12/


notSpecial_magGeOne	

_T_38	

_T_47RecFNToIN.scala 90:12P24
_T_48+R):


ioroundingMode	

0RecFNToIN.scala 95:27O23
_T_49*R(	

_T_48

roundIncr_nearestEvenRecFNToIN.scala 95:51P24
_T_50+R):


ioroundingMode	

2RecFNToIN.scala 96:27E2)
_T_51 R

sign

roundInexactRecFNToIN.scala 96:60?2#
_T_52R	

_T_50	

_T_51RecFNToIN.scala 96:49?2#
_T_53R	

_T_49	

_T_52RecFNToIN.scala 95:78P24
_T_54+R):


ioroundingMode	

3RecFNToIN.scala 97:27@2$
_T_56R

sign	

0RecFNToIN.scala 97:53F2*
_T_57!R	

_T_56

roundInexactRecFNToIN.scala 97:60?2#
_T_58R	

_T_54	

_T_57RecFNToIN.scala 97:49C2'
	roundIncrR	

_T_53	

_T_58RecFNToIN.scala 96:78;2
_T_59R

unroundedIntRecFNToIN.scala 98:39Z2>
complUnroundedInt)2'


sign	

_T_59

unroundedIntRecFNToIN.scala 98:32C2&
_T_60R

	roundIncr

signRecFNToIN.scala 100:23N21
_T_62(R&

complUnroundedInt	

1RecFNToIN.scala 100:49:2
_T_63R	

_T_62
1RecFNToIN.scala 100:49Z2=

roundedInt/2-
	

_T_60	

_T_63

complUnroundedIntRecFNToIN.scala 100:12G2*
_T_64!R

unroundedInt
29
0RecFNToIN.scala 103:3852
_T_65R	

_T_64RecFNToIN.scala 103:56B2%
_T_67R	

_T_65	

0RecFNToIN.scala 103:56M20
roundCarryBut2R	

_T_67

	roundIncrRecFNToIN.scala 103:61?2"
posExpR

exp
10
0RecFNToIN.scala 104:21D2'
_T_69R


posExp


32RecFNToIN.scala 108:21D2'
_T_71R


posExp


31RecFNToIN.scala 109:26A2$
_T_73R

sign	

0RecFNToIN.scala 110:23G2*
_T_74!R

unroundedInt
30
0RecFNToIN.scala 110:45B2%
_T_76R	

_T_74	

0RecFNToIN.scala 110:63@2#
_T_77R	

_T_73	

_T_76RecFNToIN.scala 110:30D2'
_T_78R	

_T_77

	roundIncrRecFNToIN.scala 111:27@2#
_T_79R	

_T_71	

_T_78RecFNToIN.scala 109:50@2#
_T_80R	

_T_69	

_T_79RecFNToIN.scala 108:40A2$
_T_82R

sign	

0RecFNToIN.scala 112:18D2'
_T_84R


posExp


30RecFNToIN.scala 112:36@2#
_T_85R	

_T_82	

_T_84RecFNToIN.scala 112:25I2,
_T_86#R!	

_T_85

roundCarryBut2RecFNToIN.scala 112:60@2#
_T_87R	

_T_80	

_T_86RecFNToIN.scala 111:42c2F
overflow_signed321


notSpecial_magGeOne	

_T_87	

0RecFNToIN.scala 107:12D2'
_T_90R


posExp


32RecFNToIN.scala 117:29?2"
_T_91R

sign	

_T_90RecFNToIN.scala 117:18D2'
_T_93R


posExp


31RecFNToIN.scala 118:26H2+
_T_94"R 

unroundedInt
30
30RecFNToIN.scala 119:34@2#
_T_95R	

_T_93	

_T_94RecFNToIN.scala 118:50I2,
_T_96#R!	

_T_95

roundCarryBut2RecFNToIN.scala 119:49@2#
_T_97R	

_T_91	

_T_96RecFNToIN.scala 117:48C2&
_T_98R

sign

	roundIncrRecFNToIN.scala 120:18c2F
overflow_unsigned12/


notSpecial_magGeOne	

_T_97	

_T_98RecFNToIN.scala 116:12n2Q
overflowE2C
:


io	signedOut

overflow_signed

overflow_unsignedRecFNToIN.scala 122:23C2&
_T_100R	

isNaN	

0RecFNToIN.scala 124:27B2%
excSignR

sign


_T_100RecFNToIN.scala 124:24O22
_T_101(R&:


io	signedOut
	
excSignRecFNToIN.scala 126:26>2!
_T_103R	

1
31RecFNToIN.scala 126:45N21
_T_105'2%



_T_101


_T_103	

0RecFNToIN.scala 126:12E2(
_T_107R
	
excSign	

0RecFNToIN.scala 127:29N21
_T_108'R%:


io	signedOut


_T_107RecFNToIN.scala 127:26X2;
_T_11112/



_T_108


2147483647	

0RecFNToIN.scala 127:12C2&
_T_112R


_T_105


_T_111RecFNToIN.scala 126:72O22
_T_114(R&:


io	signedOut	

0RecFNToIN.scala 131:13E2(
_T_116R
	
excSign	

0RecFNToIN.scala 131:31C2&
_T_117R


_T_114


_T_116RecFNToIN.scala 131:28X2;
_T_12012/



_T_117


4294967295 	

0RecFNToIN.scala 131:12E2(
excValueR


_T_112


_T_120RecFNToIN.scala 130:11E2(
_T_122R
	
invalid	

0RecFNToIN.scala 135:35I2,
_T_123"R 

roundInexact


_T_122RecFNToIN.scala 135:32F2)
_T_125R


overflow	

0RecFNToIN.scala 135:48D2'
inexactR


_T_123


_T_125RecFNToIN.scala 135:45F2)
_T_126R
	
invalid


overflowRecFNToIN.scala 137:27S26
_T_127,2*



_T_126


excValue


roundedIntRecFNToIN.scala 137:18:z
:


ioout


_T_127RecFNToIN.scala 137:12?2)
_T_128R
	
invalid


overflowCat.scala 30:58=2'
_T_129R


_T_128
	
inexactCat.scala 30:58Hz+
:


iointExceptionFlags


_T_129RecFNToIN.scala 138:26
§>¤>
RecFNToIN_1
clock" 
reset
w
ioo*m
in
A
roundingMode

	signedOut

out
@
intExceptionFlags



io
 


io
 D2(
sign R:


ioin
64
64RecFNToIN.scala 54:21C2'
exp R:


ioin
63
52RecFNToIN.scala 55:20D2(
fractR:


ioin
51
0RecFNToIN.scala 56:22=2!
_T_12R

exp
11
9RecFNToIN.scala 58:22B2&
isZeroR	

_T_12	

0RecFNToIN.scala 58:47>2"
_T_14R

exp
11
10RecFNToIN.scala 59:25C2'
invalidR	

_T_14	

3RecFNToIN.scala 59:50<2 
_T_16R

exp
9
9RecFNToIN.scala 60:33A2%
isNaNR
	
invalid	

_T_16RecFNToIN.scala 60:27L20
notSpecial_magGeOneR

exp
11
11RecFNToIN.scala 61:34G21
_T_17(R&

notSpecial_magGeOne	

fractCat.scala 30:58<2 
_T_18R

exp
5
0RecFNToIN.scala 74:20X2<
_T_20321


notSpecial_magGeOne	

_T_18	

0RecFNToIN.scala 73:16D2(

shiftedSigR
	

_T_17	

_T_20RecFNToIN.scala 72:40M21
unroundedInt!R


shiftedSig
115
52RecFNToIN.scala 82:24E2)
_T_21 R


shiftedSig
52
51RecFNToIN.scala 85:23D2(
_T_22R


shiftedSig
50
0RecFNToIN.scala 86:23A2%
_T_24R	

_T_22	

0RecFNToIN.scala 86:41=2'
	roundBitsR	

_T_21	

_T_24Cat.scala 30:58B2&
_T_25R

	roundBits
1
0RecFNToIN.scala 88:58A2%
_T_27R	

_T_25	

0RecFNToIN.scala 88:65B2&
_T_29R


isZero	

0RecFNToIN.scala 88:70]2A
roundInexact12/


notSpecial_magGeOne	

_T_27	

_T_29RecFNToIN.scala 88:27B2&
_T_30R

	roundBits
2
1RecFNToIN.scala 91:2242
_T_31R	

_T_30RecFNToIN.scala 91:29A2%
_T_33R	

_T_31	

0RecFNToIN.scala 91:29B2&
_T_34R

	roundBits
1
0RecFNToIN.scala 91:4642
_T_35R	

_T_34RecFNToIN.scala 91:53A2%
_T_37R	

_T_35	

0RecFNToIN.scala 91:53?2#
_T_38R	

_T_33	

_T_37RecFNToIN.scala 91:34=2!
_T_39R

exp
10
0RecFNToIN.scala 92:2042
_T_40R	

_T_39RecFNToIN.scala 92:38A2%
_T_42R	

_T_40	

0RecFNToIN.scala 92:38B2&
_T_43R

	roundBits
1
0RecFNToIN.scala 92:53A2%
_T_45R	

_T_43	

0RecFNToIN.scala 92:60J2.
_T_47%2#
	

_T_42	

_T_45	

0RecFNToIN.scala 92:16f2J
roundIncr_nearestEven12/


notSpecial_magGeOne	

_T_38	

_T_47RecFNToIN.scala 90:12P24
_T_48+R):


ioroundingMode	

0RecFNToIN.scala 95:27O23
_T_49*R(	

_T_48

roundIncr_nearestEvenRecFNToIN.scala 95:51P24
_T_50+R):


ioroundingMode	

2RecFNToIN.scala 96:27E2)
_T_51 R

sign

roundInexactRecFNToIN.scala 96:60?2#
_T_52R	

_T_50	

_T_51RecFNToIN.scala 96:49?2#
_T_53R	

_T_49	

_T_52RecFNToIN.scala 95:78P24
_T_54+R):


ioroundingMode	

3RecFNToIN.scala 97:27@2$
_T_56R

sign	

0RecFNToIN.scala 97:53F2*
_T_57!R	

_T_56

roundInexactRecFNToIN.scala 97:60?2#
_T_58R	

_T_54	

_T_57RecFNToIN.scala 97:49C2'
	roundIncrR	

_T_53	

_T_58RecFNToIN.scala 96:78;2
_T_59R

unroundedIntRecFNToIN.scala 98:39Z2>
complUnroundedInt)2'


sign	

_T_59

unroundedIntRecFNToIN.scala 98:32C2&
_T_60R

	roundIncr

signRecFNToIN.scala 100:23N21
_T_62(R&

complUnroundedInt	

1RecFNToIN.scala 100:49:2
_T_63R	

_T_62
1RecFNToIN.scala 100:49Z2=

roundedInt/2-
	

_T_60	

_T_63

complUnroundedIntRecFNToIN.scala 100:12G2*
_T_64!R

unroundedInt
61
0RecFNToIN.scala 103:3852
_T_65R	

_T_64RecFNToIN.scala 103:56B2%
_T_67R	

_T_65	

0RecFNToIN.scala 103:56M20
roundCarryBut2R	

_T_67

	roundIncrRecFNToIN.scala 103:61?2"
posExpR

exp
10
0RecFNToIN.scala 104:21D2'
_T_69R


posExp


64RecFNToIN.scala 108:21D2'
_T_71R


posExp


63RecFNToIN.scala 109:26A2$
_T_73R

sign	

0RecFNToIN.scala 110:23G2*
_T_74!R

unroundedInt
62
0RecFNToIN.scala 110:45B2%
_T_76R	

_T_74	

0RecFNToIN.scala 110:63@2#
_T_77R	

_T_73	

_T_76RecFNToIN.scala 110:30D2'
_T_78R	

_T_77

	roundIncrRecFNToIN.scala 111:27@2#
_T_79R	

_T_71	

_T_78RecFNToIN.scala 109:50@2#
_T_80R	

_T_69	

_T_79RecFNToIN.scala 108:40A2$
_T_82R

sign	

0RecFNToIN.scala 112:18D2'
_T_84R


posExp


62RecFNToIN.scala 112:36@2#
_T_85R	

_T_82	

_T_84RecFNToIN.scala 112:25I2,
_T_86#R!	

_T_85

roundCarryBut2RecFNToIN.scala 112:60@2#
_T_87R	

_T_80	

_T_86RecFNToIN.scala 111:42c2F
overflow_signed321


notSpecial_magGeOne	

_T_87	

0RecFNToIN.scala 107:12D2'
_T_90R


posExp


64RecFNToIN.scala 117:29?2"
_T_91R

sign	

_T_90RecFNToIN.scala 117:18D2'
_T_93R


posExp


63RecFNToIN.scala 118:26H2+
_T_94"R 

unroundedInt
62
62RecFNToIN.scala 119:34@2#
_T_95R	

_T_93	

_T_94RecFNToIN.scala 118:50I2,
_T_96#R!	

_T_95

roundCarryBut2RecFNToIN.scala 119:49@2#
_T_97R	

_T_91	

_T_96RecFNToIN.scala 117:48C2&
_T_98R

sign

	roundIncrRecFNToIN.scala 120:18c2F
overflow_unsigned12/


notSpecial_magGeOne	

_T_97	

_T_98RecFNToIN.scala 116:12n2Q
overflowE2C
:


io	signedOut

overflow_signed

overflow_unsignedRecFNToIN.scala 122:23C2&
_T_100R	

isNaN	

0RecFNToIN.scala 124:27B2%
excSignR

sign


_T_100RecFNToIN.scala 124:24O22
_T_101(R&:


io	signedOut
	
excSignRecFNToIN.scala 126:26>2!
_T_103R	

1
63RecFNToIN.scala 126:45N21
_T_105'2%



_T_101


_T_103	

0RecFNToIN.scala 126:12E2(
_T_107R
	
excSign	

0RecFNToIN.scala 127:29N21
_T_108'R%:


io	signedOut


_T_107RecFNToIN.scala 127:26a2D
_T_111:28



_T_108

9223372036854775807?	

0RecFNToIN.scala 127:12C2&
_T_112R


_T_105


_T_111RecFNToIN.scala 126:72O22
_T_114(R&:


io	signedOut	

0RecFNToIN.scala 131:13E2(
_T_116R
	
excSign	

0RecFNToIN.scala 131:31C2&
_T_117R


_T_114


_T_116RecFNToIN.scala 131:28b2E
_T_120;29



_T_117

18446744073709551615@	

0RecFNToIN.scala 131:12E2(
excValueR


_T_112


_T_120RecFNToIN.scala 130:11E2(
_T_122R
	
invalid	

0RecFNToIN.scala 135:35I2,
_T_123"R 

roundInexact


_T_122RecFNToIN.scala 135:32F2)
_T_125R


overflow	

0RecFNToIN.scala 135:48D2'
inexactR


_T_123


_T_125RecFNToIN.scala 135:45F2)
_T_126R
	
invalid


overflowRecFNToIN.scala 137:27S26
_T_127,2*



_T_126


excValue


roundedIntRecFNToIN.scala 137:18:z
:


ioout


_T_127RecFNToIN.scala 137:12?2)
_T_128R
	
invalid


overflowCat.scala 30:58=2'
_T_129R


_T_128
	
inexactCat.scala 30:58Hz+
:


iointExceptionFlags


_T_129RecFNToIN.scala 138:26
üvùv
	INToRecFN
clock" 
reset
s
iok*i
signedIn

in
@
roundingMode

out
!
exceptionFlags



io
 


io
 E2)
_T_12 R:


ioin
63
63INToRecFN.scala 55:36I2-
sign%R#:


iosignedIn	

_T_12INToRecFN.scala 55:28F2*
_T_14!R	

0:


ioinINToRecFN.scala 56:2742
_T_15R	

_T_14INToRecFN.scala 56:2792
_T_16R	

_T_15
1INToRecFN.scala 56:27L20
absIn'2%


sign	

_T_16:


ioinINToRecFN.scala 56:2092
_T_17R	

absIn
0INToRecFN.scala 57:32B2$
_T_18R	

_T_17
63
32CircuitMath.scala 35:17A2#
_T_19R	

_T_17
31
0CircuitMath.scala 36:17C2%
_T_21R	

_T_18	

0CircuitMath.scala 37:22B2$
_T_22R	

_T_18
31
16CircuitMath.scala 35:17A2#
_T_23R	

_T_18
15
0CircuitMath.scala 36:17C2%
_T_25R	

_T_22	

0CircuitMath.scala 37:22A2#
_T_26R	

_T_22
15
8CircuitMath.scala 35:17@2"
_T_27R	

_T_22
7
0CircuitMath.scala 36:17C2%
_T_29R	

_T_26	

0CircuitMath.scala 37:22@2"
_T_30R	

_T_26
7
4CircuitMath.scala 35:17@2"
_T_31R	

_T_26
3
0CircuitMath.scala 36:17C2%
_T_33R	

_T_30	

0CircuitMath.scala 37:22@2"
_T_34R	

_T_30
3
3CircuitMath.scala 32:12@2"
_T_36R	

_T_30
2
2CircuitMath.scala 32:12?2"
_T_38R	

_T_30
1
1CircuitMath.scala 30:8L2.
_T_39%2#
	

_T_36	

2	

_T_38CircuitMath.scala 32:10L2.
_T_40%2#
	

_T_34	

3	

_T_39CircuitMath.scala 32:10@2"
_T_41R	

_T_31
3
3CircuitMath.scala 32:12@2"
_T_43R	

_T_31
2
2CircuitMath.scala 32:12?2"
_T_45R	

_T_31
1
1CircuitMath.scala 30:8L2.
_T_46%2#
	

_T_43	

2	

_T_45CircuitMath.scala 32:10L2.
_T_47%2#
	

_T_41	

3	

_T_46CircuitMath.scala 32:10J2,
_T_48#2!
	

_T_33	

_T_40	

_T_47CircuitMath.scala 38:2192#
_T_49R	

_T_33	

_T_48Cat.scala 30:58@2"
_T_50R	

_T_27
7
4CircuitMath.scala 35:17@2"
_T_51R	

_T_27
3
0CircuitMath.scala 36:17C2%
_T_53R	

_T_50	

0CircuitMath.scala 37:22@2"
_T_54R	

_T_50
3
3CircuitMath.scala 32:12@2"
_T_56R	

_T_50
2
2CircuitMath.scala 32:12?2"
_T_58R	

_T_50
1
1CircuitMath.scala 30:8L2.
_T_59%2#
	

_T_56	

2	

_T_58CircuitMath.scala 32:10L2.
_T_60%2#
	

_T_54	

3	

_T_59CircuitMath.scala 32:10@2"
_T_61R	

_T_51
3
3CircuitMath.scala 32:12@2"
_T_63R	

_T_51
2
2CircuitMath.scala 32:12?2"
_T_65R	

_T_51
1
1CircuitMath.scala 30:8L2.
_T_66%2#
	

_T_63	

2	

_T_65CircuitMath.scala 32:10L2.
_T_67%2#
	

_T_61	

3	

_T_66CircuitMath.scala 32:10J2,
_T_68#2!
	

_T_53	

_T_60	

_T_67CircuitMath.scala 38:2192#
_T_69R	

_T_53	

_T_68Cat.scala 30:58J2,
_T_70#2!
	

_T_29	

_T_49	

_T_69CircuitMath.scala 38:2192#
_T_71R	

_T_29	

_T_70Cat.scala 30:58A2#
_T_72R	

_T_23
15
8CircuitMath.scala 35:17@2"
_T_73R	

_T_23
7
0CircuitMath.scala 36:17C2%
_T_75R	

_T_72	

0CircuitMath.scala 37:22@2"
_T_76R	

_T_72
7
4CircuitMath.scala 35:17@2"
_T_77R	

_T_72
3
0CircuitMath.scala 36:17C2%
_T_79R	

_T_76	

0CircuitMath.scala 37:22@2"
_T_80R	

_T_76
3
3CircuitMath.scala 32:12@2"
_T_82R	

_T_76
2
2CircuitMath.scala 32:12?2"
_T_84R	

_T_76
1
1CircuitMath.scala 30:8L2.
_T_85%2#
	

_T_82	

2	

_T_84CircuitMath.scala 32:10L2.
_T_86%2#
	

_T_80	

3	

_T_85CircuitMath.scala 32:10@2"
_T_87R	

_T_77
3
3CircuitMath.scala 32:12@2"
_T_89R	

_T_77
2
2CircuitMath.scala 32:12?2"
_T_91R	

_T_77
1
1CircuitMath.scala 30:8L2.
_T_92%2#
	

_T_89	

2	

_T_91CircuitMath.scala 32:10L2.
_T_93%2#
	

_T_87	

3	

_T_92CircuitMath.scala 32:10J2,
_T_94#2!
	

_T_79	

_T_86	

_T_93CircuitMath.scala 38:2192#
_T_95R	

_T_79	

_T_94Cat.scala 30:58@2"
_T_96R	

_T_73
7
4CircuitMath.scala 35:17@2"
_T_97R	

_T_73
3
0CircuitMath.scala 36:17C2%
_T_99R	

_T_96	

0CircuitMath.scala 37:22A2#
_T_100R	

_T_96
3
3CircuitMath.scala 32:12A2#
_T_102R	

_T_96
2
2CircuitMath.scala 32:12@2#
_T_104R	

_T_96
1
1CircuitMath.scala 30:8O21
_T_105'2%



_T_102	

2


_T_104CircuitMath.scala 32:10O21
_T_106'2%



_T_100	

3


_T_105CircuitMath.scala 32:10A2#
_T_107R	

_T_97
3
3CircuitMath.scala 32:12A2#
_T_109R	

_T_97
2
2CircuitMath.scala 32:12@2#
_T_111R	

_T_97
1
1CircuitMath.scala 30:8O21
_T_112'2%



_T_109	

2


_T_111CircuitMath.scala 32:10O21
_T_113'2%



_T_107	

3


_T_112CircuitMath.scala 32:10M2/
_T_114%2#
	

_T_99


_T_106


_T_113CircuitMath.scala 38:21;2%
_T_115R	

_T_99


_T_114Cat.scala 30:58L2.
_T_116$2"
	

_T_75	

_T_95


_T_115CircuitMath.scala 38:21;2%
_T_117R	

_T_75


_T_116Cat.scala 30:58L2.
_T_118$2"
	

_T_25	

_T_71


_T_117CircuitMath.scala 38:21;2%
_T_119R	

_T_25


_T_118Cat.scala 30:58C2%
_T_120R	

_T_19
31
16CircuitMath.scala 35:17B2$
_T_121R	

_T_19
15
0CircuitMath.scala 36:17E2'
_T_123R


_T_120	

0CircuitMath.scala 37:22C2%
_T_124R


_T_120
15
8CircuitMath.scala 35:17B2$
_T_125R


_T_120
7
0CircuitMath.scala 36:17E2'
_T_127R


_T_124	

0CircuitMath.scala 37:22B2$
_T_128R


_T_124
7
4CircuitMath.scala 35:17B2$
_T_129R


_T_124
3
0CircuitMath.scala 36:17E2'
_T_131R


_T_128	

0CircuitMath.scala 37:22B2$
_T_132R


_T_128
3
3CircuitMath.scala 32:12B2$
_T_134R


_T_128
2
2CircuitMath.scala 32:12A2$
_T_136R


_T_128
1
1CircuitMath.scala 30:8O21
_T_137'2%



_T_134	

2


_T_136CircuitMath.scala 32:10O21
_T_138'2%



_T_132	

3


_T_137CircuitMath.scala 32:10B2$
_T_139R


_T_129
3
3CircuitMath.scala 32:12B2$
_T_141R


_T_129
2
2CircuitMath.scala 32:12A2$
_T_143R


_T_129
1
1CircuitMath.scala 30:8O21
_T_144'2%



_T_141	

2


_T_143CircuitMath.scala 32:10O21
_T_145'2%



_T_139	

3


_T_144CircuitMath.scala 32:10N20
_T_146&2$



_T_131


_T_138


_T_145CircuitMath.scala 38:21<2&
_T_147R


_T_131


_T_146Cat.scala 30:58B2$
_T_148R


_T_125
7
4CircuitMath.scala 35:17B2$
_T_149R


_T_125
3
0CircuitMath.scala 36:17E2'
_T_151R


_T_148	

0CircuitMath.scala 37:22B2$
_T_152R


_T_148
3
3CircuitMath.scala 32:12B2$
_T_154R


_T_148
2
2CircuitMath.scala 32:12A2$
_T_156R


_T_148
1
1CircuitMath.scala 30:8O21
_T_157'2%



_T_154	

2


_T_156CircuitMath.scala 32:10O21
_T_158'2%



_T_152	

3


_T_157CircuitMath.scala 32:10B2$
_T_159R


_T_149
3
3CircuitMath.scala 32:12B2$
_T_161R


_T_149
2
2CircuitMath.scala 32:12A2$
_T_163R


_T_149
1
1CircuitMath.scala 30:8O21
_T_164'2%



_T_161	

2


_T_163CircuitMath.scala 32:10O21
_T_165'2%



_T_159	

3


_T_164CircuitMath.scala 32:10N20
_T_166&2$



_T_151


_T_158


_T_165CircuitMath.scala 38:21<2&
_T_167R


_T_151


_T_166Cat.scala 30:58N20
_T_168&2$



_T_127


_T_147


_T_167CircuitMath.scala 38:21<2&
_T_169R


_T_127


_T_168Cat.scala 30:58C2%
_T_170R


_T_121
15
8CircuitMath.scala 35:17B2$
_T_171R


_T_121
7
0CircuitMath.scala 36:17E2'
_T_173R


_T_170	

0CircuitMath.scala 37:22B2$
_T_174R


_T_170
7
4CircuitMath.scala 35:17B2$
_T_175R


_T_170
3
0CircuitMath.scala 36:17E2'
_T_177R


_T_174	

0CircuitMath.scala 37:22B2$
_T_178R


_T_174
3
3CircuitMath.scala 32:12B2$
_T_180R


_T_174
2
2CircuitMath.scala 32:12A2$
_T_182R


_T_174
1
1CircuitMath.scala 30:8O21
_T_183'2%



_T_180	

2


_T_182CircuitMath.scala 32:10O21
_T_184'2%



_T_178	

3


_T_183CircuitMath.scala 32:10B2$
_T_185R


_T_175
3
3CircuitMath.scala 32:12B2$
_T_187R


_T_175
2
2CircuitMath.scala 32:12A2$
_T_189R


_T_175
1
1CircuitMath.scala 30:8O21
_T_190'2%



_T_187	

2


_T_189CircuitMath.scala 32:10O21
_T_191'2%



_T_185	

3


_T_190CircuitMath.scala 32:10N20
_T_192&2$



_T_177


_T_184


_T_191CircuitMath.scala 38:21<2&
_T_193R


_T_177


_T_192Cat.scala 30:58B2$
_T_194R


_T_171
7
4CircuitMath.scala 35:17B2$
_T_195R


_T_171
3
0CircuitMath.scala 36:17E2'
_T_197R


_T_194	

0CircuitMath.scala 37:22B2$
_T_198R


_T_194
3
3CircuitMath.scala 32:12B2$
_T_200R


_T_194
2
2CircuitMath.scala 32:12A2$
_T_202R


_T_194
1
1CircuitMath.scala 30:8O21
_T_203'2%



_T_200	

2


_T_202CircuitMath.scala 32:10O21
_T_204'2%



_T_198	

3


_T_203CircuitMath.scala 32:10B2$
_T_205R


_T_195
3
3CircuitMath.scala 32:12B2$
_T_207R


_T_195
2
2CircuitMath.scala 32:12A2$
_T_209R


_T_195
1
1CircuitMath.scala 30:8O21
_T_210'2%



_T_207	

2


_T_209CircuitMath.scala 32:10O21
_T_211'2%



_T_205	

3


_T_210CircuitMath.scala 32:10N20
_T_212&2$



_T_197


_T_204


_T_211CircuitMath.scala 38:21<2&
_T_213R


_T_197


_T_212Cat.scala 30:58N20
_T_214&2$



_T_173


_T_193


_T_213CircuitMath.scala 38:21<2&
_T_215R


_T_173


_T_214Cat.scala 30:58N20
_T_216&2$



_T_123


_T_169


_T_215CircuitMath.scala 38:21<2&
_T_217R


_T_123


_T_216Cat.scala 30:58M2/
_T_218%2#
	

_T_21


_T_119


_T_217CircuitMath.scala 38:21;2%
_T_219R	

_T_21


_T_218Cat.scala 30:5892
	normCountR


_T_219INToRecFN.scala 57:21D2(
_T_220R
	

absIn

	normCountINToRecFN.scala 58:27D2(
	normAbsInR


_T_220
63
0INToRecFN.scala 58:39E2)
_T_222R

	normAbsIn
40
39INToRecFN.scala 63:26D2(
_T_223R

	normAbsIn
38
0INToRecFN.scala 64:26C2'
_T_225R


_T_223	

0INToRecFN.scala 64:55?2)
	roundBitsR


_T_222


_T_225Cat.scala 30:58C2'
_T_226R

	roundBits
1
0INToRecFN.scala 72:33I2-
roundInexactR


_T_226	

0INToRecFN.scala 72:40Q25
_T_228+R):


ioroundingMode	

0INToRecFN.scala 74:30C2'
_T_229R

	roundBits
2
1INToRecFN.scala 75:2262
_T_230R


_T_229INToRecFN.scala 75:29C2'
_T_232R


_T_230	

0INToRecFN.scala 75:29C2'
_T_233R

	roundBits
1
0INToRecFN.scala 75:4662
_T_234R


_T_233INToRecFN.scala 75:53C2'
_T_236R


_T_234	

0INToRecFN.scala 75:53B2&
_T_237R


_T_232


_T_236INToRecFN.scala 75:34M21
_T_239'2%



_T_228


_T_237	

0INToRecFN.scala 74:12Q25
_T_240+R):


ioroundingMode	

2INToRecFN.scala 78:30F2*
_T_241 R

sign

roundInexactINToRecFN.scala 79:18M21
_T_243'2%



_T_240


_T_241	

0INToRecFN.scala 78:12B2&
_T_244R


_T_239


_T_243INToRecFN.scala 77:11Q25
_T_245+R):


ioroundingMode	

3INToRecFN.scala 82:30A2%
_T_247R

sign	

0INToRecFN.scala 83:13H2,
_T_248"R 


_T_247

roundInexactINToRecFN.scala 83:20M21
_T_250'2%



_T_245


_T_248	

0INToRecFN.scala 82:12A2%
roundR


_T_244


_T_250INToRecFN.scala 81:11E2)
_T_252R

	normAbsIn
63
40INToRecFN.scala 89:34D2.
unroundedNormR	

0


_T_252Cat.scala 30:58J2.
_T_255$R"

unroundedNorm	

1INToRecFN.scala 94:48;2
_T_256R


_T_255
1INToRecFN.scala 94:48W2;
roundedNorm,2*
	

round


_T_256

unroundedNormINToRecFN.scala 94:2692
_T_257R

	normCountINToRecFN.scala 97:24C2-
unroundedExpR	

0


_T_257Cat.scala 30:58C2-
_T_260#R!	

0

unroundedExpCat.scala 30:58H2+
_T_261!R

roundedNorm
24
24INToRecFN.scala 106:65C2&
_T_262R


_T_260


_T_261INToRecFN.scala 106:52@2#

roundedExpR


_T_262
1INToRecFN.scala 106:52F2)
_T_263R

	normAbsIn
63
63INToRecFN.scala 112:22E2(
_T_265R


roundedExp
7
0INToRecFN.scala 115:27Q24
_T_266*2(
	

0

128


_T_265INToRecFN.scala 113:16<2&
expOutR


_T_263


_T_266Cat.scala 30:58G2*
overflowR	

0	

0INToRecFN.scala 119:39L2/
inexact$R"

roundInexact


overflowINToRecFN.scala 120:32G2*
_T_267 R

roundedNorm
22
0INToRecFN.scala 122:44:2$
_T_268R

sign


expOutCat.scala 30:58<2&
_T_269R


_T_268


_T_267Cat.scala 30:58:z
:


ioout


_T_269INToRecFN.scala 122:12>2(
_T_272R	

0
	
inexactCat.scala 30:58?2)
_T_273R	

0


overflowCat.scala 30:58<2&
_T_274R


_T_273


_T_272Cat.scala 30:58Ez(
:


ioexceptionFlags


_T_274INToRecFN.scala 123:23
ÿvüv
INToRecFN_1
clock" 
reset
s
iok*i
signedIn

in
@
roundingMode

out
A
exceptionFlags



io
 


io
 E2)
_T_12 R:


ioin
63
63INToRecFN.scala 55:36I2-
sign%R#:


iosignedIn	

_T_12INToRecFN.scala 55:28F2*
_T_14!R	

0:


ioinINToRecFN.scala 56:2742
_T_15R	

_T_14INToRecFN.scala 56:2792
_T_16R	

_T_15
1INToRecFN.scala 56:27L20
absIn'2%


sign	

_T_16:


ioinINToRecFN.scala 56:2092
_T_17R	

absIn
0INToRecFN.scala 57:32B2$
_T_18R	

_T_17
63
32CircuitMath.scala 35:17A2#
_T_19R	

_T_17
31
0CircuitMath.scala 36:17C2%
_T_21R	

_T_18	

0CircuitMath.scala 37:22B2$
_T_22R	

_T_18
31
16CircuitMath.scala 35:17A2#
_T_23R	

_T_18
15
0CircuitMath.scala 36:17C2%
_T_25R	

_T_22	

0CircuitMath.scala 37:22A2#
_T_26R	

_T_22
15
8CircuitMath.scala 35:17@2"
_T_27R	

_T_22
7
0CircuitMath.scala 36:17C2%
_T_29R	

_T_26	

0CircuitMath.scala 37:22@2"
_T_30R	

_T_26
7
4CircuitMath.scala 35:17@2"
_T_31R	

_T_26
3
0CircuitMath.scala 36:17C2%
_T_33R	

_T_30	

0CircuitMath.scala 37:22@2"
_T_34R	

_T_30
3
3CircuitMath.scala 32:12@2"
_T_36R	

_T_30
2
2CircuitMath.scala 32:12?2"
_T_38R	

_T_30
1
1CircuitMath.scala 30:8L2.
_T_39%2#
	

_T_36	

2	

_T_38CircuitMath.scala 32:10L2.
_T_40%2#
	

_T_34	

3	

_T_39CircuitMath.scala 32:10@2"
_T_41R	

_T_31
3
3CircuitMath.scala 32:12@2"
_T_43R	

_T_31
2
2CircuitMath.scala 32:12?2"
_T_45R	

_T_31
1
1CircuitMath.scala 30:8L2.
_T_46%2#
	

_T_43	

2	

_T_45CircuitMath.scala 32:10L2.
_T_47%2#
	

_T_41	

3	

_T_46CircuitMath.scala 32:10J2,
_T_48#2!
	

_T_33	

_T_40	

_T_47CircuitMath.scala 38:2192#
_T_49R	

_T_33	

_T_48Cat.scala 30:58@2"
_T_50R	

_T_27
7
4CircuitMath.scala 35:17@2"
_T_51R	

_T_27
3
0CircuitMath.scala 36:17C2%
_T_53R	

_T_50	

0CircuitMath.scala 37:22@2"
_T_54R	

_T_50
3
3CircuitMath.scala 32:12@2"
_T_56R	

_T_50
2
2CircuitMath.scala 32:12?2"
_T_58R	

_T_50
1
1CircuitMath.scala 30:8L2.
_T_59%2#
	

_T_56	

2	

_T_58CircuitMath.scala 32:10L2.
_T_60%2#
	

_T_54	

3	

_T_59CircuitMath.scala 32:10@2"
_T_61R	

_T_51
3
3CircuitMath.scala 32:12@2"
_T_63R	

_T_51
2
2CircuitMath.scala 32:12?2"
_T_65R	

_T_51
1
1CircuitMath.scala 30:8L2.
_T_66%2#
	

_T_63	

2	

_T_65CircuitMath.scala 32:10L2.
_T_67%2#
	

_T_61	

3	

_T_66CircuitMath.scala 32:10J2,
_T_68#2!
	

_T_53	

_T_60	

_T_67CircuitMath.scala 38:2192#
_T_69R	

_T_53	

_T_68Cat.scala 30:58J2,
_T_70#2!
	

_T_29	

_T_49	

_T_69CircuitMath.scala 38:2192#
_T_71R	

_T_29	

_T_70Cat.scala 30:58A2#
_T_72R	

_T_23
15
8CircuitMath.scala 35:17@2"
_T_73R	

_T_23
7
0CircuitMath.scala 36:17C2%
_T_75R	

_T_72	

0CircuitMath.scala 37:22@2"
_T_76R	

_T_72
7
4CircuitMath.scala 35:17@2"
_T_77R	

_T_72
3
0CircuitMath.scala 36:17C2%
_T_79R	

_T_76	

0CircuitMath.scala 37:22@2"
_T_80R	

_T_76
3
3CircuitMath.scala 32:12@2"
_T_82R	

_T_76
2
2CircuitMath.scala 32:12?2"
_T_84R	

_T_76
1
1CircuitMath.scala 30:8L2.
_T_85%2#
	

_T_82	

2	

_T_84CircuitMath.scala 32:10L2.
_T_86%2#
	

_T_80	

3	

_T_85CircuitMath.scala 32:10@2"
_T_87R	

_T_77
3
3CircuitMath.scala 32:12@2"
_T_89R	

_T_77
2
2CircuitMath.scala 32:12?2"
_T_91R	

_T_77
1
1CircuitMath.scala 30:8L2.
_T_92%2#
	

_T_89	

2	

_T_91CircuitMath.scala 32:10L2.
_T_93%2#
	

_T_87	

3	

_T_92CircuitMath.scala 32:10J2,
_T_94#2!
	

_T_79	

_T_86	

_T_93CircuitMath.scala 38:2192#
_T_95R	

_T_79	

_T_94Cat.scala 30:58@2"
_T_96R	

_T_73
7
4CircuitMath.scala 35:17@2"
_T_97R	

_T_73
3
0CircuitMath.scala 36:17C2%
_T_99R	

_T_96	

0CircuitMath.scala 37:22A2#
_T_100R	

_T_96
3
3CircuitMath.scala 32:12A2#
_T_102R	

_T_96
2
2CircuitMath.scala 32:12@2#
_T_104R	

_T_96
1
1CircuitMath.scala 30:8O21
_T_105'2%



_T_102	

2


_T_104CircuitMath.scala 32:10O21
_T_106'2%



_T_100	

3


_T_105CircuitMath.scala 32:10A2#
_T_107R	

_T_97
3
3CircuitMath.scala 32:12A2#
_T_109R	

_T_97
2
2CircuitMath.scala 32:12@2#
_T_111R	

_T_97
1
1CircuitMath.scala 30:8O21
_T_112'2%



_T_109	

2


_T_111CircuitMath.scala 32:10O21
_T_113'2%



_T_107	

3


_T_112CircuitMath.scala 32:10M2/
_T_114%2#
	

_T_99


_T_106


_T_113CircuitMath.scala 38:21;2%
_T_115R	

_T_99


_T_114Cat.scala 30:58L2.
_T_116$2"
	

_T_75	

_T_95


_T_115CircuitMath.scala 38:21;2%
_T_117R	

_T_75


_T_116Cat.scala 30:58L2.
_T_118$2"
	

_T_25	

_T_71


_T_117CircuitMath.scala 38:21;2%
_T_119R	

_T_25


_T_118Cat.scala 30:58C2%
_T_120R	

_T_19
31
16CircuitMath.scala 35:17B2$
_T_121R	

_T_19
15
0CircuitMath.scala 36:17E2'
_T_123R


_T_120	

0CircuitMath.scala 37:22C2%
_T_124R


_T_120
15
8CircuitMath.scala 35:17B2$
_T_125R


_T_120
7
0CircuitMath.scala 36:17E2'
_T_127R


_T_124	

0CircuitMath.scala 37:22B2$
_T_128R


_T_124
7
4CircuitMath.scala 35:17B2$
_T_129R


_T_124
3
0CircuitMath.scala 36:17E2'
_T_131R


_T_128	

0CircuitMath.scala 37:22B2$
_T_132R


_T_128
3
3CircuitMath.scala 32:12B2$
_T_134R


_T_128
2
2CircuitMath.scala 32:12A2$
_T_136R


_T_128
1
1CircuitMath.scala 30:8O21
_T_137'2%



_T_134	

2


_T_136CircuitMath.scala 32:10O21
_T_138'2%



_T_132	

3


_T_137CircuitMath.scala 32:10B2$
_T_139R


_T_129
3
3CircuitMath.scala 32:12B2$
_T_141R


_T_129
2
2CircuitMath.scala 32:12A2$
_T_143R


_T_129
1
1CircuitMath.scala 30:8O21
_T_144'2%



_T_141	

2


_T_143CircuitMath.scala 32:10O21
_T_145'2%



_T_139	

3


_T_144CircuitMath.scala 32:10N20
_T_146&2$



_T_131


_T_138


_T_145CircuitMath.scala 38:21<2&
_T_147R


_T_131


_T_146Cat.scala 30:58B2$
_T_148R


_T_125
7
4CircuitMath.scala 35:17B2$
_T_149R


_T_125
3
0CircuitMath.scala 36:17E2'
_T_151R


_T_148	

0CircuitMath.scala 37:22B2$
_T_152R


_T_148
3
3CircuitMath.scala 32:12B2$
_T_154R


_T_148
2
2CircuitMath.scala 32:12A2$
_T_156R


_T_148
1
1CircuitMath.scala 30:8O21
_T_157'2%



_T_154	

2


_T_156CircuitMath.scala 32:10O21
_T_158'2%



_T_152	

3


_T_157CircuitMath.scala 32:10B2$
_T_159R


_T_149
3
3CircuitMath.scala 32:12B2$
_T_161R


_T_149
2
2CircuitMath.scala 32:12A2$
_T_163R


_T_149
1
1CircuitMath.scala 30:8O21
_T_164'2%



_T_161	

2


_T_163CircuitMath.scala 32:10O21
_T_165'2%



_T_159	

3


_T_164CircuitMath.scala 32:10N20
_T_166&2$



_T_151


_T_158


_T_165CircuitMath.scala 38:21<2&
_T_167R


_T_151


_T_166Cat.scala 30:58N20
_T_168&2$



_T_127


_T_147


_T_167CircuitMath.scala 38:21<2&
_T_169R


_T_127


_T_168Cat.scala 30:58C2%
_T_170R


_T_121
15
8CircuitMath.scala 35:17B2$
_T_171R


_T_121
7
0CircuitMath.scala 36:17E2'
_T_173R


_T_170	

0CircuitMath.scala 37:22B2$
_T_174R


_T_170
7
4CircuitMath.scala 35:17B2$
_T_175R


_T_170
3
0CircuitMath.scala 36:17E2'
_T_177R


_T_174	

0CircuitMath.scala 37:22B2$
_T_178R


_T_174
3
3CircuitMath.scala 32:12B2$
_T_180R


_T_174
2
2CircuitMath.scala 32:12A2$
_T_182R


_T_174
1
1CircuitMath.scala 30:8O21
_T_183'2%



_T_180	

2


_T_182CircuitMath.scala 32:10O21
_T_184'2%



_T_178	

3


_T_183CircuitMath.scala 32:10B2$
_T_185R


_T_175
3
3CircuitMath.scala 32:12B2$
_T_187R


_T_175
2
2CircuitMath.scala 32:12A2$
_T_189R


_T_175
1
1CircuitMath.scala 30:8O21
_T_190'2%



_T_187	

2


_T_189CircuitMath.scala 32:10O21
_T_191'2%



_T_185	

3


_T_190CircuitMath.scala 32:10N20
_T_192&2$



_T_177


_T_184


_T_191CircuitMath.scala 38:21<2&
_T_193R


_T_177


_T_192Cat.scala 30:58B2$
_T_194R


_T_171
7
4CircuitMath.scala 35:17B2$
_T_195R


_T_171
3
0CircuitMath.scala 36:17E2'
_T_197R


_T_194	

0CircuitMath.scala 37:22B2$
_T_198R


_T_194
3
3CircuitMath.scala 32:12B2$
_T_200R


_T_194
2
2CircuitMath.scala 32:12A2$
_T_202R


_T_194
1
1CircuitMath.scala 30:8O21
_T_203'2%



_T_200	

2


_T_202CircuitMath.scala 32:10O21
_T_204'2%



_T_198	

3


_T_203CircuitMath.scala 32:10B2$
_T_205R


_T_195
3
3CircuitMath.scala 32:12B2$
_T_207R


_T_195
2
2CircuitMath.scala 32:12A2$
_T_209R


_T_195
1
1CircuitMath.scala 30:8O21
_T_210'2%



_T_207	

2


_T_209CircuitMath.scala 32:10O21
_T_211'2%



_T_205	

3


_T_210CircuitMath.scala 32:10N20
_T_212&2$



_T_197


_T_204


_T_211CircuitMath.scala 38:21<2&
_T_213R


_T_197


_T_212Cat.scala 30:58N20
_T_214&2$



_T_173


_T_193


_T_213CircuitMath.scala 38:21<2&
_T_215R


_T_173


_T_214Cat.scala 30:58N20
_T_216&2$



_T_123


_T_169


_T_215CircuitMath.scala 38:21<2&
_T_217R


_T_123


_T_216Cat.scala 30:58M2/
_T_218%2#
	

_T_21


_T_119


_T_217CircuitMath.scala 38:21;2%
_T_219R	

_T_21


_T_218Cat.scala 30:5892
	normCountR


_T_219INToRecFN.scala 57:21D2(
_T_220R
	

absIn

	normCountINToRecFN.scala 58:27D2(
	normAbsInR


_T_220
63
0INToRecFN.scala 58:39E2)
_T_222R

	normAbsIn
11
10INToRecFN.scala 63:26C2'
_T_223R

	normAbsIn
9
0INToRecFN.scala 64:26C2'
_T_225R


_T_223	

0INToRecFN.scala 64:55?2)
	roundBitsR


_T_222


_T_225Cat.scala 30:58C2'
_T_226R

	roundBits
1
0INToRecFN.scala 72:33I2-
roundInexactR


_T_226	

0INToRecFN.scala 72:40Q25
_T_228+R):


ioroundingMode	

0INToRecFN.scala 74:30C2'
_T_229R

	roundBits
2
1INToRecFN.scala 75:2262
_T_230R


_T_229INToRecFN.scala 75:29C2'
_T_232R


_T_230	

0INToRecFN.scala 75:29C2'
_T_233R

	roundBits
1
0INToRecFN.scala 75:4662
_T_234R


_T_233INToRecFN.scala 75:53C2'
_T_236R


_T_234	

0INToRecFN.scala 75:53B2&
_T_237R


_T_232


_T_236INToRecFN.scala 75:34M21
_T_239'2%



_T_228


_T_237	

0INToRecFN.scala 74:12Q25
_T_240+R):


ioroundingMode	

2INToRecFN.scala 78:30F2*
_T_241 R

sign

roundInexactINToRecFN.scala 79:18M21
_T_243'2%



_T_240


_T_241	

0INToRecFN.scala 78:12B2&
_T_244R


_T_239


_T_243INToRecFN.scala 77:11Q25
_T_245+R):


ioroundingMode	

3INToRecFN.scala 82:30A2%
_T_247R

sign	

0INToRecFN.scala 83:13H2,
_T_248"R 


_T_247

roundInexactINToRecFN.scala 83:20M21
_T_250'2%



_T_245


_T_248	

0INToRecFN.scala 82:12A2%
roundR


_T_244


_T_250INToRecFN.scala 81:11E2)
_T_252R

	normAbsIn
63
11INToRecFN.scala 89:34D2.
unroundedNormR	

0


_T_252Cat.scala 30:58J2.
_T_255$R"

unroundedNorm	

1INToRecFN.scala 94:48;2
_T_256R


_T_255
1INToRecFN.scala 94:48W2;
roundedNorm,2*
	

round


_T_256

unroundedNormINToRecFN.scala 94:2692
_T_257R

	normCountINToRecFN.scala 97:24C2-
unroundedExpR	

0


_T_257Cat.scala 30:58C2-
_T_260#R!	

0

unroundedExpCat.scala 30:58H2+
_T_261!R

roundedNorm
53
53INToRecFN.scala 106:65C2&
_T_262R


_T_260


_T_261INToRecFN.scala 106:52@2#

roundedExpR


_T_262
1INToRecFN.scala 106:52F2)
_T_263R

	normAbsIn
63
63INToRecFN.scala 112:22F2)
_T_265R


roundedExp
10
0INToRecFN.scala 115:27R25
_T_266+2)
	

0

1024


_T_265INToRecFN.scala 113:16<2&
expOutR


_T_263


_T_266Cat.scala 30:58G2*
overflowR	

0	

0INToRecFN.scala 119:39L2/
inexact$R"

roundInexact


overflowINToRecFN.scala 120:32G2*
_T_267 R

roundedNorm
51
0INToRecFN.scala 122:44:2$
_T_268R

sign


expOutCat.scala 30:58<2&
_T_269R


_T_268


_T_267Cat.scala 30:58:z
:


ioout


_T_269INToRecFN.scala 122:12>2(
_T_272R	

0
	
inexactCat.scala 30:58?2)
_T_273R	

0


overflowCat.scala 30:58<2&
_T_274R


_T_273


_T_272Cat.scala 30:58Ez(
:


ioexceptionFlags


_T_274INToRecFN.scala 123:23
÷$ô$
RecFNToRecFN_1
clock" 
reset
]
ioU*S
in
!
roundingMode

out
A
exceptionFlags



io
 


io
 J2)
_T_10 R:


ioin
31
23rawFNFromRecFN.scala 50:21C2"
_T_11R	

_T_10
8
6rawFNFromRecFN.scala 51:29F2%
_T_13R	

_T_11	

0rawFNFromRecFN.scala 51:54C2"
_T_14R	

_T_10
8
7rawFNFromRecFN.scala 52:29F2%
_T_16R	

_T_14	

3rawFNFromRecFN.scala 52:54
n
_T_24e*c
sign

isNaN

isInf

isZero

sExp


sig
rawFNFromRecFN.scala 54:23-
	

_T_24rawFNFromRecFN.scala 54:23J2)
_T_31 R:


ioin
32
32rawFNFromRecFN.scala 55:23Az 
:
	

_T_24sign	

_T_31rawFNFromRecFN.scala 55:18C2"
_T_32R	

_T_10
6
6rawFNFromRecFN.scala 56:40D2#
_T_33R	

_T_16	

_T_32rawFNFromRecFN.scala 56:32Bz!
:
	

_T_24isNaN	

_T_33rawFNFromRecFN.scala 56:19C2"
_T_34R	

_T_10
6
6rawFNFromRecFN.scala 57:40F2%
_T_36R	

_T_34	

0rawFNFromRecFN.scala 57:35D2#
_T_37R	

_T_16	

_T_36rawFNFromRecFN.scala 57:32Bz!
:
	

_T_24isInf	

_T_37rawFNFromRecFN.scala 57:19Cz"
:
	

_T_24isZero	

_T_13rawFNFromRecFN.scala 58:2092
_T_38R	

_T_10rawFNFromRecFN.scala 59:25Az 
:
	

_T_24sExp	

_T_38rawFNFromRecFN.scala 59:18F2%
_T_41R	

_T_13	

0rawFNFromRecFN.scala 60:36I2(
_T_42R:


ioin
22
0rawFNFromRecFN.scala 60:48;2%
_T_44R	

_T_42	

0Cat.scala 30:58;2%
_T_45R	

0	

_T_41Cat.scala 30:5892#
_T_46R	

_T_45	

_T_44Cat.scala 30:58@z
:
	

_T_24sig	

_T_46rawFNFromRecFN.scala 60:17V28
_T_48/R-:
	

_T_24sExpR

1792resizeRawFN.scala 49:31
t
outRawFloate*c
sign

isNaN

isInf

isZero

sExp

sig
8resizeRawFN.scala 51:230


outRawFloatresizeRawFN.scala 51:23Nz0
:


outRawFloatsign:
	

_T_24signresizeRawFN.scala 52:20Pz2
:


outRawFloatisNaN:
	

_T_24isNaNresizeRawFN.scala 53:20Pz2
:


outRawFloatisInf:
	

_T_24isInfresizeRawFN.scala 54:20Rz4
:


outRawFloatisZero:
	

_T_24isZeroresizeRawFN.scala 55:20Dz&
:


outRawFloatsExp	

_T_48resizeRawFN.scala 56:18E2'
_T_62R:
	

_T_24sig
29resizeRawFN.scala 69:24Cz%
:


outRawFloatsig	

_T_62resizeRawFN.scala 67:17W23
_T_63*R(:


outRawFloatsig
53
53RoundRawFNToRecFN.scala 61:57I2%
_T_65R	

_T_63	

0RoundRawFNToRecFN.scala 61:49]29

invalidExc+R):


outRawFloatisNaN	

_T_65RoundRawFNToRecFN.scala 61:46U26
_T_67-R+:


outRawFloatisNaN	

0RecFNToRecFN.scala 69:40R23
_T_68*R(:


outRawFloatsign	

_T_67RecFNToRecFN.scala 69:37R23
_T_69*R(:


outRawFloatsExp
11
0RecFNToRecFN.scala 71:30d2E
_T_72<2:
:


outRawFloatisZero

3072	

0RecFNToRecFN.scala 72:2272
_T_73R	

_T_72RecFNToRecFN.scala 72:18B2#
_T_74R	

_T_69	

_T_73RecFNToRecFN.scala 71:47e2F
_T_75=R;:


outRawFloatisZero:


outRawFloatisInfRecFNToRecFN.scala 76:42Q22
_T_78)2'
	

_T_75

512	

0RecFNToRecFN.scala 76:2272
_T_79R	

_T_78RecFNToRecFN.scala 76:18B2#
_T_80R	

_T_74	

_T_79RecFNToRecFN.scala 75:21c2D
_T_83;29
:


outRawFloatisInf

3072	

0RecFNToRecFN.scala 80:20B2#
_T_84R	

_T_80	

_T_83RecFNToRecFN.scala 79:22c2D
_T_87;29
:


outRawFloatisNaN

3584	

0RecFNToRecFN.scala 84:20B2#
_T_88R	

_T_84	

_T_87RecFNToRecFN.scala 83:19?2 
_T_90R	

1
51RecFNToRecFN.scala 90:24Q22
_T_91)R':


outRawFloatsig
53
2RecFNToRecFN.scala 91:32\2=
_T_92422
:


outRawFloatisNaN	

_T_90	

_T_91RecFNToRecFN.scala 89:1692#
_T_93R	

_T_68	

_T_88Cat.scala 30:5892#
_T_94R	

_T_93	

_T_92Cat.scala 30:58;z
:


ioout	

_T_94RecFNToRecFN.scala 93:16@2*
_T_96!R


invalidExc	

0Cat.scala 30:58Fz'
:


ioexceptionFlags	

_T_96RecFNToRecFN.scala 94:27

MulAddRecFN_1
clock" 
reset

io*
op

a
A
b
A
c
A
roundingMode

out
A
exceptionFlags



io
 


io
 I**
mulAddRecFN_preMulMulAddRecFN_preMul_1MulAddRecFN.scala 598:15( 
:


mulAddRecFN_preMulio
 5z.
!:


mulAddRecFN_preMulclock	

clock
 5z.
!:


mulAddRecFN_preMulreset	

reset
 K*,
mulAddRecFN_postMulMulAddRecFN_postMul_1MulAddRecFN.scala 600:15)!
:


mulAddRecFN_postMulio
 6z/
": 


mulAddRecFN_postMulclock	

clock
 6z/
": 


mulAddRecFN_postMulreset	

reset
 Wz8
&:$
:


mulAddRecFN_preMulioop:


ioopMulAddRecFN.scala 602:30Uz6
%:#
:


mulAddRecFN_preMulioa:


ioaMulAddRecFN.scala 603:30Uz6
%:#
:


mulAddRecFN_preMuliob:


iobMulAddRecFN.scala 604:30Uz6
%:#
:


mulAddRecFN_preMulioc:


iocMulAddRecFN.scala 605:30kzL
0:.
:


mulAddRecFN_preMulioroundingMode:


ioroundingModeMulAddRecFN.scala 606:40`
/:-
:


mulAddRecFN_postMulio
fromPreMul-:+
:


mulAddRecFN_preMulio	toPostMulMulAddRecFN.scala 608:392g
_T_16^R\+:)
:


mulAddRecFN_preMuliomulAddA+:)
:


mulAddRecFN_preMuliomulAddBMulAddRecFN.scala 610:39]2G
_T_18>R<	

0+:)
:


mulAddRecFN_preMuliomulAddCCat.scala 30:58B2#
_T_19R	

_T_16	

_T_18MulAddRecFN.scala 610:71<2
_T_20R	

_T_19
1MulAddRecFN.scala 610:71]z>
1:/
:


mulAddRecFN_postMuliomulAddResult	

_T_20MulAddRecFN.scala 609:41Zz;
:


ioout(:&
:


mulAddRecFN_postMuliooutMulAddRecFN.scala 613:12pzQ
:


ioexceptionFlags3:1
:


mulAddRecFN_postMulioexceptionFlagsMulAddRecFN.scala 614:23
Ê Æ 
DivSqrtRecF64_mulAddZ31
clock" 
reset

io*
inReady_div

inReady_sqrt

inValid

sqrtOp

a
A
b
A
roundingMode

outValid_div

outValid_sqrt

out
A
exceptionFlags

usingMulAdd

latchMulAddA_0

	mulAddA_0
6
latchMulAddB_0

	mulAddB_0
6
	mulAddC_2
i
mulAddResult_3
i


io
 


io
 _5
valid_PA
	

clock"	

reset*	

0%#DivSqrtRecF64_mulAddZ31.scala 78:30d:
	sqrtOp_PA
	

clock"	

0*

	sqrtOp_PA%#DivSqrtRecF64_mulAddZ31.scala 79:30`6
sign_PA
	

clock"	

0*
	
sign_PA%#DivSqrtRecF64_mulAddZ31.scala 80:30pF
specialCodeB_PA
	

clock"	

0*

specialCodeB_PA%#DivSqrtRecF64_mulAddZ31.scala 82:30j@
fractB_51_PA
	

clock"	

0*

fractB_51_PA%#DivSqrtRecF64_mulAddZ31.scala 83:30pF
roundingMode_PA
	

clock"	

0*

roundingMode_PA%#DivSqrtRecF64_mulAddZ31.scala 84:30pF
specialCodeA_PA
	

clock"	

0*

specialCodeA_PA%#DivSqrtRecF64_mulAddZ31.scala 85:30j@
fractA_51_PA
	

clock"	

0*

fractA_51_PA%#DivSqrtRecF64_mulAddZ31.scala 86:30^4
exp_PA
	

clock"	

0*


exp_PA%#DivSqrtRecF64_mulAddZ31.scala 87:30pF
fractB_other_PA
3	

clock"	

0*

fractB_other_PA%#DivSqrtRecF64_mulAddZ31.scala 88:30pF
fractA_other_PA
3	

clock"	

0*

fractA_other_PA%#DivSqrtRecF64_mulAddZ31.scala 89:30_5
valid_PB
	

clock"	

reset*	

0%#DivSqrtRecF64_mulAddZ31.scala 91:30d:
	sqrtOp_PB
	

clock"	

0*

	sqrtOp_PB%#DivSqrtRecF64_mulAddZ31.scala 92:30`6
sign_PB
	

clock"	

0*
	
sign_PB%#DivSqrtRecF64_mulAddZ31.scala 93:30pF
specialCodeA_PB
	

clock"	

0*

specialCodeA_PB%#DivSqrtRecF64_mulAddZ31.scala 95:30j@
fractA_51_PB
	

clock"	

0*

fractA_51_PB%#DivSqrtRecF64_mulAddZ31.scala 96:30pF
specialCodeB_PB
	

clock"	

0*

specialCodeB_PB%#DivSqrtRecF64_mulAddZ31.scala 97:30j@
fractB_51_PB
	

clock"	

0*

fractB_51_PB%#DivSqrtRecF64_mulAddZ31.scala 98:30pF
roundingMode_PB
	

clock"	

0*

roundingMode_PB%#DivSqrtRecF64_mulAddZ31.scala 99:30_4
exp_PB
	

clock"	

0*


exp_PB&$DivSqrtRecF64_mulAddZ31.scala 100:30i>
fractA_0_PB
	

clock"	

0*

fractA_0_PB&$DivSqrtRecF64_mulAddZ31.scala 101:30qF
fractB_other_PB
3	

clock"	

0*

fractB_other_PB&$DivSqrtRecF64_mulAddZ31.scala 102:30`5
valid_PC
	

clock"	

reset*	

0&$DivSqrtRecF64_mulAddZ31.scala 104:30e:
	sqrtOp_PC
	

clock"	

0*

	sqrtOp_PC&$DivSqrtRecF64_mulAddZ31.scala 105:30a6
sign_PC
	

clock"	

0*
	
sign_PC&$DivSqrtRecF64_mulAddZ31.scala 106:30qF
specialCodeA_PC
	

clock"	

0*

specialCodeA_PC&$DivSqrtRecF64_mulAddZ31.scala 108:30k@
fractA_51_PC
	

clock"	

0*

fractA_51_PC&$DivSqrtRecF64_mulAddZ31.scala 109:30qF
specialCodeB_PC
	

clock"	

0*

specialCodeB_PC&$DivSqrtRecF64_mulAddZ31.scala 110:30k@
fractB_51_PC
	

clock"	

0*

fractB_51_PC&$DivSqrtRecF64_mulAddZ31.scala 111:30qF
roundingMode_PC
	

clock"	

0*

roundingMode_PC&$DivSqrtRecF64_mulAddZ31.scala 112:30_4
exp_PC
	

clock"	

0*


exp_PC&$DivSqrtRecF64_mulAddZ31.scala 113:30i>
fractA_0_PC
	

clock"	

0*

fractA_0_PC&$DivSqrtRecF64_mulAddZ31.scala 114:30qF
fractB_other_PC
3	

clock"	

0*

fractB_other_PC&$DivSqrtRecF64_mulAddZ31.scala 115:30b7

cycleNum_A
	

clock"	

reset*	

0&$DivSqrtRecF64_mulAddZ31.scala 117:30b7

cycleNum_B
	

clock"	

reset*	

0&$DivSqrtRecF64_mulAddZ31.scala 118:30b7

cycleNum_C
	

clock"	

reset*	

0&$DivSqrtRecF64_mulAddZ31.scala 119:30b7

cycleNum_E
	

clock"	

reset*	

0&$DivSqrtRecF64_mulAddZ31.scala 120:30e:
	fractR0_A
		

clock"	

0*

	fractR0_A&$DivSqrtRecF64_mulAddZ31.scala 122:30oD
hiSqrR0_A_sqrt

	

clock"	

0*

hiSqrR0_A_sqrt&$DivSqrtRecF64_mulAddZ31.scala 124:30qF
partNegSigma0_A
	

clock"	

0*

partNegSigma0_A&$DivSqrtRecF64_mulAddZ31.scala 125:30oD
nextMulAdd9A_A
		

clock"	

0*

nextMulAdd9A_A&$DivSqrtRecF64_mulAddZ31.scala 126:30oD
nextMulAdd9B_A
		

clock"	

0*

nextMulAdd9B_A&$DivSqrtRecF64_mulAddZ31.scala 127:30g<

ER1_B_sqrt
	

clock"	

0*


ER1_B_sqrt&$DivSqrtRecF64_mulAddZ31.scala 128:30mB
ESqrR1_B_sqrt
 	

clock"	

0*

ESqrR1_B_sqrt&$DivSqrtRecF64_mulAddZ31.scala 130:30a6
sigX1_B
:	

clock"	

0*
	
sigX1_B&$DivSqrtRecF64_mulAddZ31.scala 131:30i>
sqrSigma1_C
!	

clock"	

0*

sqrSigma1_C&$DivSqrtRecF64_mulAddZ31.scala 132:30a6
sigXN_C
:	

clock"	

0*
	
sigXN_C&$DivSqrtRecF64_mulAddZ31.scala 133:30c8
u_C_sqrt
	

clock"	

0*


u_C_sqrt&$DivSqrtRecF64_mulAddZ31.scala 134:30a6
E_E_div
	

clock"	

0*
	
E_E_div&$DivSqrtRecF64_mulAddZ31.scala 135:30_4
sigT_E
5	

clock"	

0*


sigT_E&$DivSqrtRecF64_mulAddZ31.scala 136:30c8
extraT_E
	

clock"	

0*


extraT_E&$DivSqrtRecF64_mulAddZ31.scala 137:30i>
isNegRemT_E
	

clock"	

0*

isNegRemT_E&$DivSqrtRecF64_mulAddZ31.scala 138:30k@
isZeroRemT_E
	

clock"	

0*

isZeroRemT_E&$DivSqrtRecF64_mulAddZ31.scala 139:30=

ready_PA
&$DivSqrtRecF64_mulAddZ31.scala 143:24:



ready_PA&$DivSqrtRecF64_mulAddZ31.scala 143:24=

ready_PB
&$DivSqrtRecF64_mulAddZ31.scala 144:24:



ready_PB&$DivSqrtRecF64_mulAddZ31.scala 144:24=

ready_PC
&$DivSqrtRecF64_mulAddZ31.scala 145:24:



ready_PC&$DivSqrtRecF64_mulAddZ31.scala 145:24?


leaving_PA
&$DivSqrtRecF64_mulAddZ31.scala 146:26<



leaving_PA&$DivSqrtRecF64_mulAddZ31.scala 146:26?


leaving_PB
&$DivSqrtRecF64_mulAddZ31.scala 147:26<



leaving_PB&$DivSqrtRecF64_mulAddZ31.scala 147:26?


leaving_PC
&$DivSqrtRecF64_mulAddZ31.scala 148:26<



leaving_PC&$DivSqrtRecF64_mulAddZ31.scala 148:26A

cyc_B10_sqrt
&$DivSqrtRecF64_mulAddZ31.scala 150:28>


cyc_B10_sqrt&$DivSqrtRecF64_mulAddZ31.scala 150:28@

cyc_B9_sqrt
&$DivSqrtRecF64_mulAddZ31.scala 151:28=


cyc_B9_sqrt&$DivSqrtRecF64_mulAddZ31.scala 151:28@

cyc_B8_sqrt
&$DivSqrtRecF64_mulAddZ31.scala 152:28=


cyc_B8_sqrt&$DivSqrtRecF64_mulAddZ31.scala 152:28@

cyc_B7_sqrt
&$DivSqrtRecF64_mulAddZ31.scala 153:28=


cyc_B7_sqrt&$DivSqrtRecF64_mulAddZ31.scala 153:28;

cyc_B6
&$DivSqrtRecF64_mulAddZ31.scala 155:228



cyc_B6&$DivSqrtRecF64_mulAddZ31.scala 155:22;

cyc_B5
&$DivSqrtRecF64_mulAddZ31.scala 156:228



cyc_B5&$DivSqrtRecF64_mulAddZ31.scala 156:22;

cyc_B4
&$DivSqrtRecF64_mulAddZ31.scala 157:228



cyc_B4&$DivSqrtRecF64_mulAddZ31.scala 157:22;

cyc_B3
&$DivSqrtRecF64_mulAddZ31.scala 158:228



cyc_B3&$DivSqrtRecF64_mulAddZ31.scala 158:22;

cyc_B2
&$DivSqrtRecF64_mulAddZ31.scala 159:228



cyc_B2&$DivSqrtRecF64_mulAddZ31.scala 159:22;

cyc_B1
&$DivSqrtRecF64_mulAddZ31.scala 160:228



cyc_B1&$DivSqrtRecF64_mulAddZ31.scala 160:22?


cyc_B6_div
&$DivSqrtRecF64_mulAddZ31.scala 162:26<



cyc_B6_div&$DivSqrtRecF64_mulAddZ31.scala 162:26?


cyc_B5_div
&$DivSqrtRecF64_mulAddZ31.scala 163:26<



cyc_B5_div&$DivSqrtRecF64_mulAddZ31.scala 163:26?


cyc_B4_div
&$DivSqrtRecF64_mulAddZ31.scala 164:26<



cyc_B4_div&$DivSqrtRecF64_mulAddZ31.scala 164:26?


cyc_B3_div
&$DivSqrtRecF64_mulAddZ31.scala 165:26<



cyc_B3_div&$DivSqrtRecF64_mulAddZ31.scala 165:26?


cyc_B2_div
&$DivSqrtRecF64_mulAddZ31.scala 166:26<



cyc_B2_div&$DivSqrtRecF64_mulAddZ31.scala 166:26?


cyc_B1_div
&$DivSqrtRecF64_mulAddZ31.scala 167:26<



cyc_B1_div&$DivSqrtRecF64_mulAddZ31.scala 167:26@

cyc_B6_sqrt
&$DivSqrtRecF64_mulAddZ31.scala 169:27=


cyc_B6_sqrt&$DivSqrtRecF64_mulAddZ31.scala 169:27@

cyc_B5_sqrt
&$DivSqrtRecF64_mulAddZ31.scala 170:27=


cyc_B5_sqrt&$DivSqrtRecF64_mulAddZ31.scala 170:27@

cyc_B4_sqrt
&$DivSqrtRecF64_mulAddZ31.scala 171:27=


cyc_B4_sqrt&$DivSqrtRecF64_mulAddZ31.scala 171:27@

cyc_B3_sqrt
&$DivSqrtRecF64_mulAddZ31.scala 172:27=


cyc_B3_sqrt&$DivSqrtRecF64_mulAddZ31.scala 172:27@

cyc_B2_sqrt
&$DivSqrtRecF64_mulAddZ31.scala 173:27=


cyc_B2_sqrt&$DivSqrtRecF64_mulAddZ31.scala 173:27@

cyc_B1_sqrt
&$DivSqrtRecF64_mulAddZ31.scala 174:27=


cyc_B1_sqrt&$DivSqrtRecF64_mulAddZ31.scala 174:27;

cyc_C5
&$DivSqrtRecF64_mulAddZ31.scala 176:228



cyc_C5&$DivSqrtRecF64_mulAddZ31.scala 176:22;

cyc_C4
&$DivSqrtRecF64_mulAddZ31.scala 177:228



cyc_C4&$DivSqrtRecF64_mulAddZ31.scala 177:22;

cyc_C3
&$DivSqrtRecF64_mulAddZ31.scala 178:228



cyc_C3&$DivSqrtRecF64_mulAddZ31.scala 178:22;

cyc_C2
&$DivSqrtRecF64_mulAddZ31.scala 179:228



cyc_C2&$DivSqrtRecF64_mulAddZ31.scala 179:22;

cyc_C1
&$DivSqrtRecF64_mulAddZ31.scala 180:228



cyc_C1&$DivSqrtRecF64_mulAddZ31.scala 180:22;

cyc_E4
&$DivSqrtRecF64_mulAddZ31.scala 182:228



cyc_E4&$DivSqrtRecF64_mulAddZ31.scala 182:22;

cyc_E3
&$DivSqrtRecF64_mulAddZ31.scala 183:228



cyc_E3&$DivSqrtRecF64_mulAddZ31.scala 183:22;

cyc_E2
&$DivSqrtRecF64_mulAddZ31.scala 184:228



cyc_E2&$DivSqrtRecF64_mulAddZ31.scala 184:22;

cyc_E1
&$DivSqrtRecF64_mulAddZ31.scala 185:228



cyc_E1&$DivSqrtRecF64_mulAddZ31.scala 185:22?


zSigma1_B4
.&$DivSqrtRecF64_mulAddZ31.scala 187:34<



zSigma1_B4&$DivSqrtRecF64_mulAddZ31.scala 187:34A

sigXNU_B3_CX
:&$DivSqrtRecF64_mulAddZ31.scala 188:34>


sigXNU_B3_CX&$DivSqrtRecF64_mulAddZ31.scala 188:34G

zComplSigT_C1_sqrt
6&$DivSqrtRecF64_mulAddZ31.scala 189:34D


zComplSigT_C1_sqrt&$DivSqrtRecF64_mulAddZ31.scala 189:34B

zComplSigT_C1
6&$DivSqrtRecF64_mulAddZ31.scala 190:34?


zComplSigT_C1&$DivSqrtRecF64_mulAddZ31.scala 190:34W2,
_T_133"R 

cyc_B7_sqrt	

0&$DivSqrtRecF64_mulAddZ31.scala 197:21S2(
_T_134R


ready_PA


_T_133&$DivSqrtRecF64_mulAddZ31.scala 197:18W2,
_T_136"R 

cyc_B6_sqrt	

0&$DivSqrtRecF64_mulAddZ31.scala 197:38Q2&
_T_137R


_T_134


_T_136&$DivSqrtRecF64_mulAddZ31.scala 197:35W2,
_T_139"R 

cyc_B5_sqrt	

0&$DivSqrtRecF64_mulAddZ31.scala 197:55Q2&
_T_140R


_T_137


_T_139&$DivSqrtRecF64_mulAddZ31.scala 197:52W2,
_T_142"R 

cyc_B4_sqrt	

0&$DivSqrtRecF64_mulAddZ31.scala 198:13Q2&
_T_143R


_T_140


_T_142&$DivSqrtRecF64_mulAddZ31.scala 197:69R2'
_T_145R


cyc_B3	

0&$DivSqrtRecF64_mulAddZ31.scala 198:30Q2&
_T_146R


_T_143


_T_145&$DivSqrtRecF64_mulAddZ31.scala 198:27R2'
_T_148R


cyc_B2	

0&$DivSqrtRecF64_mulAddZ31.scala 198:42Q2&
_T_149R


_T_146


_T_148&$DivSqrtRecF64_mulAddZ31.scala 198:39W2,
_T_151"R 

cyc_B1_sqrt	

0&$DivSqrtRecF64_mulAddZ31.scala 198:54Q2&
_T_152R


_T_149


_T_151&$DivSqrtRecF64_mulAddZ31.scala 198:51R2'
_T_154R


cyc_C5	

0&$DivSqrtRecF64_mulAddZ31.scala 199:13Q2&
_T_155R


_T_152


_T_154&$DivSqrtRecF64_mulAddZ31.scala 198:68R2'
_T_157R


cyc_C4	

0&$DivSqrtRecF64_mulAddZ31.scala 199:25Q2&
_T_158R


_T_155


_T_157&$DivSqrtRecF64_mulAddZ31.scala 199:22Pz%
:


ioinReady_div


_T_158&$DivSqrtRecF64_mulAddZ31.scala 195:20W2,
_T_160"R 

cyc_B6_sqrt	

0&$DivSqrtRecF64_mulAddZ31.scala 201:21S2(
_T_161R


ready_PA


_T_160&$DivSqrtRecF64_mulAddZ31.scala 201:18W2,
_T_163"R 

cyc_B5_sqrt	

0&$DivSqrtRecF64_mulAddZ31.scala 201:38Q2&
_T_164R


_T_161


_T_163&$DivSqrtRecF64_mulAddZ31.scala 201:35W2,
_T_166"R 

cyc_B4_sqrt	

0&$DivSqrtRecF64_mulAddZ31.scala 201:55Q2&
_T_167R


_T_164


_T_166&$DivSqrtRecF64_mulAddZ31.scala 201:52V2+
_T_169!R


cyc_B2_div	

0&$DivSqrtRecF64_mulAddZ31.scala 202:13Q2&
_T_170R


_T_167


_T_169&$DivSqrtRecF64_mulAddZ31.scala 201:69W2,
_T_172"R 

cyc_B1_sqrt	

0&$DivSqrtRecF64_mulAddZ31.scala 202:29Q2&
_T_173R


_T_170


_T_172&$DivSqrtRecF64_mulAddZ31.scala 202:26Qz&
:


ioinReady_sqrt


_T_173&$DivSqrtRecF64_mulAddZ31.scala 200:21g2<
_T_1742R0:


ioinReady_div:


ioinValid&$DivSqrtRecF64_mulAddZ31.scala 203:38Z2/
_T_176%R#:


iosqrtOp	

0&$DivSqrtRecF64_mulAddZ31.scala 203:55T2)
	cyc_S_divR


_T_174


_T_176&$DivSqrtRecF64_mulAddZ31.scala 203:52h2=
_T_1773R1:


ioinReady_sqrt:


ioinValid&$DivSqrtRecF64_mulAddZ31.scala 204:38]22

cyc_S_sqrt$R"


_T_177:


iosqrtOp&$DivSqrtRecF64_mulAddZ31.scala 204:52W2,
cyc_S#R!

	cyc_S_div


cyc_S_sqrt&$DivSqrtRecF64_mulAddZ31.scala 205:27U2*
signA_SR:


ioa
64
64&$DivSqrtRecF64_mulAddZ31.scala 207:24T2)
expA_SR:


ioa
63
52&$DivSqrtRecF64_mulAddZ31.scala 208:24U2*
fractA_SR:


ioa
51
0&$DivSqrtRecF64_mulAddZ31.scala 209:24X2-
specialCodeA_SR


expA_S
11
9&$DivSqrtRecF64_mulAddZ31.scala 210:32]22
	isZeroA_S%R#

specialCodeA_S	

0&$DivSqrtRecF64_mulAddZ31.scala 211:40W2,
_T_179"R 

specialCodeA_S
2
1&$DivSqrtRecF64_mulAddZ31.scala 212:39X2-
isSpecialA_SR


_T_179	

3&$DivSqrtRecF64_mulAddZ31.scala 212:46U2*
signB_SR:


iob
64
64&$DivSqrtRecF64_mulAddZ31.scala 214:24T2)
expB_SR:


iob
63
52&$DivSqrtRecF64_mulAddZ31.scala 215:24U2*
fractB_SR:


iob
51
0&$DivSqrtRecF64_mulAddZ31.scala 216:24X2-
specialCodeB_SR


expB_S
11
9&$DivSqrtRecF64_mulAddZ31.scala 217:32]22
	isZeroB_S%R#

specialCodeB_S	

0&$DivSqrtRecF64_mulAddZ31.scala 218:40W2,
_T_182"R 

specialCodeB_S
2
1&$DivSqrtRecF64_mulAddZ31.scala 219:39X2-
isSpecialB_SR


_T_182	

3&$DivSqrtRecF64_mulAddZ31.scala 219:46S2(
_T_184R
	
signA_S
	
signB_S&$DivSqrtRecF64_mulAddZ31.scala 221:50d29
sign_S/2-
:


iosqrtOp
	
signB_S


_T_184&$DivSqrtRecF64_mulAddZ31.scala 221:21W2-
_T_186#R!

isSpecialA_S	

0%#DivSqrtRecF64_mulAddZ31.scala 224:9X2-
_T_188#R!

isSpecialB_S	

0&$DivSqrtRecF64_mulAddZ31.scala 224:27Q2&
_T_189R


_T_186


_T_188&$DivSqrtRecF64_mulAddZ31.scala 224:24U2*
_T_191 R

	isZeroA_S	

0&$DivSqrtRecF64_mulAddZ31.scala 224:45Q2&
_T_192R


_T_189


_T_191&$DivSqrtRecF64_mulAddZ31.scala 224:42U2*
_T_194 R

	isZeroB_S	

0&$DivSqrtRecF64_mulAddZ31.scala 224:60[20
normalCase_S_divR


_T_192


_T_194&$DivSqrtRecF64_mulAddZ31.scala 224:57X2-
_T_196#R!

isSpecialB_S	

0&$DivSqrtRecF64_mulAddZ31.scala 225:29U2*
_T_198 R

	isZeroB_S	

0&$DivSqrtRecF64_mulAddZ31.scala 225:47Q2&
_T_199R


_T_196


_T_198&$DivSqrtRecF64_mulAddZ31.scala 225:44S2(
_T_201R
	
signB_S	

0&$DivSqrtRecF64_mulAddZ31.scala 225:62\21
normalCase_S_sqrtR


_T_199


_T_201&$DivSqrtRecF64_mulAddZ31.scala 225:59~2S
normalCase_SC2A
:


iosqrtOp

normalCase_S_sqrt

normalCase_S_div&$DivSqrtRecF64_mulAddZ31.scala 226:27b27

cyc_A4_div)R'

	cyc_S_div

normalCase_S_div&$DivSqrtRecF64_mulAddZ31.scala 228:50e2:
cyc_A7_sqrt+R)


cyc_S_sqrt

normalCase_S_sqrt&$DivSqrtRecF64_mulAddZ31.scala 229:50j2?
entering_PA_normalCase%R#


cyc_A4_div

cyc_A7_sqrt&$DivSqrtRecF64_mulAddZ31.scala 231:36T2)
_T_203R


ready_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 233:58S2(
_T_204R


valid_PA


_T_203&$DivSqrtRecF64_mulAddZ31.scala 233:55P2%
_T_205R	

cyc_S


_T_204&$DivSqrtRecF64_mulAddZ31.scala 233:42f2;
entering_PA,R*

entering_PA_normalCase


_T_205&$DivSqrtRecF64_mulAddZ31.scala 233:32X2-
_T_207#R!

normalCase_S	

0&$DivSqrtRecF64_mulAddZ31.scala 235:18P2%
_T_208R	

cyc_S


_T_207&$DivSqrtRecF64_mulAddZ31.scala 235:15T2)
_T_210R


valid_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 235:36Q2&
_T_211R


_T_208


_T_210&$DivSqrtRecF64_mulAddZ31.scala 235:33T2)
_T_213R


valid_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 236:29T2)
_T_215R


ready_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 236:43Q2&
_T_216R


_T_213


_T_215&$DivSqrtRecF64_mulAddZ31.scala 236:40U2*
_T_217 R


leaving_PB


_T_216&$DivSqrtRecF64_mulAddZ31.scala 236:25X2-
entering_PB_SR


_T_211


_T_217&$DivSqrtRecF64_mulAddZ31.scala 235:47X2-
_T_219#R!

normalCase_S	

0&$DivSqrtRecF64_mulAddZ31.scala 238:18P2%
_T_220R	

cyc_S


_T_219&$DivSqrtRecF64_mulAddZ31.scala 238:15T2)
_T_222R


valid_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 238:36Q2&
_T_223R


_T_220


_T_222&$DivSqrtRecF64_mulAddZ31.scala 238:33T2)
_T_225R


valid_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 238:50Q2&
_T_226R


_T_223


_T_225&$DivSqrtRecF64_mulAddZ31.scala 238:47Z2/
entering_PC_SR


_T_226


ready_PC&$DivSqrtRecF64_mulAddZ31.scala 238:61Z2/
_T_227%R#

entering_PA


leaving_PA&$DivSqrtRecF64_mulAddZ31.scala 240:23:X



_T_227Jz



valid_PA

entering_PA&$DivSqrtRecF64_mulAddZ31.scala 241:18&$DivSqrtRecF64_mulAddZ31.scala 240:38¥:ù


entering_PANz#


	sqrtOp_PA:


iosqrtOp&$DivSqrtRecF64_mulAddZ31.scala 244:25Dz

	
sign_PA


sign_S&$DivSqrtRecF64_mulAddZ31.scala 245:25Tz)


specialCodeB_PA

specialCodeB_S&$DivSqrtRecF64_mulAddZ31.scala 246:25S2(
_T_228R


fractB_S
51
51&$DivSqrtRecF64_mulAddZ31.scala 247:36Iz


fractB_51_PA


_T_228&$DivSqrtRecF64_mulAddZ31.scala 247:25Zz/


roundingMode_PA:


ioroundingMode&$DivSqrtRecF64_mulAddZ31.scala 248:25&$DivSqrtRecF64_mulAddZ31.scala 243:24Z2/
_T_230%R#:


iosqrtOp	

0&$DivSqrtRecF64_mulAddZ31.scala 250:26V2+
_T_231!R

entering_PA


_T_230&$DivSqrtRecF64_mulAddZ31.scala 250:23®:



_T_231Tz)


specialCodeA_PA

specialCodeA_S&$DivSqrtRecF64_mulAddZ31.scala 251:25S2(
_T_232R


fractA_S
51
51&$DivSqrtRecF64_mulAddZ31.scala 252:36Iz


fractA_51_PA


_T_232&$DivSqrtRecF64_mulAddZ31.scala 252:25&$DivSqrtRecF64_mulAddZ31.scala 250:39ë:¿


entering_PA_normalCaseQ2&
_T_233R


expB_S
11
11&$DivSqrtRecF64_mulAddZ31.scala 258:44>2$
_T_234R


_T_233
0
0Bitwise.scala 71:15L22
_T_237(2&



_T_234	

7	

0Bitwise.scala 71:12P2%
_T_238R


expB_S
10
0&$DivSqrtRecF64_mulAddZ31.scala 258:58E2
_T_239R


_T_238&$DivSqrtRecF64_mulAddZ31.scala 258:51<2&
_T_240R


_T_237


_T_239Cat.scala 30:58Q2&
_T_241R


expA_S


_T_240&$DivSqrtRecF64_mulAddZ31.scala 258:24J2
_T_242R


_T_241
1&$DivSqrtRecF64_mulAddZ31.scala 258:24c28
_T_243.2,
:


iosqrtOp


expB_S


_T_242&$DivSqrtRecF64_mulAddZ31.scala 256:16Cz



exp_PA


_T_243&$DivSqrtRecF64_mulAddZ31.scala 255:16R2'
_T_244R


fractB_S
50
0&$DivSqrtRecF64_mulAddZ31.scala 260:36Lz!


fractB_other_PA


_T_244&$DivSqrtRecF64_mulAddZ31.scala 260:25&$DivSqrtRecF64_mulAddZ31.scala 254:35Þ:²



cyc_A4_divR2'
_T_245R


fractA_S
50
0&$DivSqrtRecF64_mulAddZ31.scala 263:36Lz!


fractA_other_PA


_T_245&$DivSqrtRecF64_mulAddZ31.scala 263:25&$DivSqrtRecF64_mulAddZ31.scala 262:39_24

isZeroA_PA&R$

specialCodeA_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 266:42X2-
_T_247#R!

specialCodeA_PA
2
1&$DivSqrtRecF64_mulAddZ31.scala 267:41Y2.
isSpecialA_PAR


_T_247	

3&$DivSqrtRecF64_mulAddZ31.scala 267:48C2-
_T_250#R!	

1

fractA_51_PACat.scala 30:58F20
sigA_PA%R#


_T_250

fractA_other_PACat.scala 30:58_24

isZeroB_PA&R$

specialCodeB_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 270:42X2-
_T_252#R!

specialCodeB_PA
2
1&$DivSqrtRecF64_mulAddZ31.scala 271:41Y2.
isSpecialB_PAR


_T_252	

3&$DivSqrtRecF64_mulAddZ31.scala 271:48C2-
_T_255#R!	

1

fractB_51_PACat.scala 30:58F20
sigB_PA%R#


_T_255

fractB_other_PACat.scala 30:58Y2.
_T_257$R"

isSpecialB_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 276:13V2+
_T_259!R


isZeroB_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 276:32Q2&
_T_260R


_T_257


_T_259&$DivSqrtRecF64_mulAddZ31.scala 276:29S2(
_T_262R
	
sign_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 276:48Q2&
_T_263R


_T_260


_T_262&$DivSqrtRecF64_mulAddZ31.scala 276:45Y2.
_T_265$R"

isSpecialA_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 277:13Y2.
_T_267$R"

isSpecialB_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 277:32Q2&
_T_268R


_T_265


_T_267&$DivSqrtRecF64_mulAddZ31.scala 277:29V2+
_T_270!R


isZeroA_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 277:51Q2&
_T_271R


_T_268


_T_270&$DivSqrtRecF64_mulAddZ31.scala 277:48V2+
_T_273!R


isZeroB_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 277:67Q2&
_T_274R


_T_271


_T_273&$DivSqrtRecF64_mulAddZ31.scala 277:64e2:
normalCase_PA)2'


	sqrtOp_PA


_T_263


_T_274&$DivSqrtRecF64_mulAddZ31.scala 275:12o2D
valid_normalCase_leaving_PA%R#


cyc_B4_div

cyc_B7_sqrt&$DivSqrtRecF64_mulAddZ31.scala 280:502X
valid_leaving_PAD2B


normalCase_PA

valid_normalCase_leaving_PA


ready_PB&$DivSqrtRecF64_mulAddZ31.scala 282:12]22
_T_275(R&


valid_PA

valid_leaving_PA&$DivSqrtRecF64_mulAddZ31.scala 283:28Gz



leaving_PA


_T_275&$DivSqrtRecF64_mulAddZ31.scala 283:16T2)
_T_277R


valid_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 284:17[20
_T_278&R$


_T_277

valid_leaving_PA&$DivSqrtRecF64_mulAddZ31.scala 284:28Ez



ready_PA


_T_278&$DivSqrtRecF64_mulAddZ31.scala 284:14Z2/
_T_279%R#


valid_PA

normalCase_PA&$DivSqrtRecF64_mulAddZ31.scala 287:18v2K
entering_PB_normalCase1R/


_T_279

valid_normalCase_leaving_PA&$DivSqrtRecF64_mulAddZ31.scala 287:35a26
entering_PB'R%

entering_PB_S


leaving_PA&$DivSqrtRecF64_mulAddZ31.scala 288:37Z2/
_T_280%R#

entering_PB


leaving_PB&$DivSqrtRecF64_mulAddZ31.scala 290:23:X



_T_280Jz



valid_PB

entering_PB&$DivSqrtRecF64_mulAddZ31.scala 291:18&$DivSqrtRecF64_mulAddZ31.scala 290:38ß:³


entering_PBh2=
_T_281321



valid_PA

	sqrtOp_PA:


iosqrtOp&$DivSqrtRecF64_mulAddZ31.scala 294:31Fz


	sqrtOp_PB


_T_281&$DivSqrtRecF64_mulAddZ31.scala 294:25^23
_T_282)2'



valid_PA
	
sign_PA


sign_S&$DivSqrtRecF64_mulAddZ31.scala 295:31Dz

	
sign_PB


_T_282&$DivSqrtRecF64_mulAddZ31.scala 295:25n2C
_T_283927



valid_PA

specialCodeA_PA

specialCodeA_S&$DivSqrtRecF64_mulAddZ31.scala 296:31Lz!


specialCodeA_PB


_T_283&$DivSqrtRecF64_mulAddZ31.scala 296:25S2(
_T_284R


fractA_S
51
51&$DivSqrtRecF64_mulAddZ31.scala 297:67c28
_T_285.2,



valid_PA

fractA_51_PA


_T_284&$DivSqrtRecF64_mulAddZ31.scala 297:31Iz


fractA_51_PB


_T_285&$DivSqrtRecF64_mulAddZ31.scala 297:25n2C
_T_286927



valid_PA

specialCodeB_PA

specialCodeB_S&$DivSqrtRecF64_mulAddZ31.scala 298:31Lz!


specialCodeB_PB


_T_286&$DivSqrtRecF64_mulAddZ31.scala 298:25S2(
_T_287R


fractB_S
51
51&$DivSqrtRecF64_mulAddZ31.scala 299:67c28
_T_288.2,



valid_PA

fractB_51_PA


_T_287&$DivSqrtRecF64_mulAddZ31.scala 299:31Iz


fractB_51_PB


_T_288&$DivSqrtRecF64_mulAddZ31.scala 299:25t2I
_T_289?2=



valid_PA

roundingMode_PA:


ioroundingMode&$DivSqrtRecF64_mulAddZ31.scala 300:31Lz!


roundingMode_PB


_T_289&$DivSqrtRecF64_mulAddZ31.scala 300:25&$DivSqrtRecF64_mulAddZ31.scala 293:24:Ü


entering_PB_normalCaseCz



exp_PB


exp_PA&$DivSqrtRecF64_mulAddZ31.scala 303:25X2-
_T_290#R!

fractA_other_PA
0
0&$DivSqrtRecF64_mulAddZ31.scala 304:43Hz


fractA_0_PB


_T_290&$DivSqrtRecF64_mulAddZ31.scala 304:25Uz*


fractB_other_PB

fractB_other_PA&$DivSqrtRecF64_mulAddZ31.scala 305:25&$DivSqrtRecF64_mulAddZ31.scala 302:35_24

isZeroA_PB&R$

specialCodeA_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 308:42X2-
_T_292#R!

specialCodeA_PB
2
1&$DivSqrtRecF64_mulAddZ31.scala 309:41Y2.
isSpecialA_PBR


_T_292	

3&$DivSqrtRecF64_mulAddZ31.scala 309:48_24

isZeroB_PB&R$

specialCodeB_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 310:42X2-
_T_295#R!

specialCodeB_PB
2
1&$DivSqrtRecF64_mulAddZ31.scala 311:41Y2.
isSpecialB_PBR


_T_295	

3&$DivSqrtRecF64_mulAddZ31.scala 311:48Y2.
_T_298$R"

isSpecialB_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 314:13V2+
_T_300!R


isZeroB_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 314:32Q2&
_T_301R


_T_298


_T_300&$DivSqrtRecF64_mulAddZ31.scala 314:29S2(
_T_303R
	
sign_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 314:48Q2&
_T_304R


_T_301


_T_303&$DivSqrtRecF64_mulAddZ31.scala 314:45Y2.
_T_306$R"

isSpecialA_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 315:13Y2.
_T_308$R"

isSpecialB_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 315:32Q2&
_T_309R


_T_306


_T_308&$DivSqrtRecF64_mulAddZ31.scala 315:29V2+
_T_311!R


isZeroA_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 315:51Q2&
_T_312R


_T_309


_T_311&$DivSqrtRecF64_mulAddZ31.scala 315:48V2+
_T_314!R


isZeroB_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 315:67Q2&
_T_315R


_T_312


_T_314&$DivSqrtRecF64_mulAddZ31.scala 315:64e2:
normalCase_PB)2'


	sqrtOp_PB


_T_304


_T_315&$DivSqrtRecF64_mulAddZ31.scala 313:12n2C
valid_leaving_PB/2-


normalCase_PB


cyc_C3


ready_PC&$DivSqrtRecF64_mulAddZ31.scala 320:12]22
_T_316(R&


valid_PB

valid_leaving_PB&$DivSqrtRecF64_mulAddZ31.scala 321:28Gz



leaving_PB


_T_316&$DivSqrtRecF64_mulAddZ31.scala 321:16T2)
_T_318R


valid_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 322:17[20
_T_319&R$


_T_318

valid_leaving_PB&$DivSqrtRecF64_mulAddZ31.scala 322:28Ez



ready_PB


_T_319&$DivSqrtRecF64_mulAddZ31.scala 322:14Z2/
_T_320%R#


valid_PB

normalCase_PB&$DivSqrtRecF64_mulAddZ31.scala 325:18a26
entering_PC_normalCaseR


_T_320


cyc_C3&$DivSqrtRecF64_mulAddZ31.scala 325:35a26
entering_PC'R%

entering_PC_S


leaving_PB&$DivSqrtRecF64_mulAddZ31.scala 326:37Z2/
_T_321%R#

entering_PC


leaving_PC&$DivSqrtRecF64_mulAddZ31.scala 328:23:X



_T_321Jz



valid_PC

entering_PC&$DivSqrtRecF64_mulAddZ31.scala 329:18&$DivSqrtRecF64_mulAddZ31.scala 328:38ß:³


entering_PCh2=
_T_322321



valid_PB

	sqrtOp_PB:


iosqrtOp&$DivSqrtRecF64_mulAddZ31.scala 332:31Fz


	sqrtOp_PC


_T_322&$DivSqrtRecF64_mulAddZ31.scala 332:25^23
_T_323)2'



valid_PB
	
sign_PB


sign_S&$DivSqrtRecF64_mulAddZ31.scala 333:31Dz

	
sign_PC


_T_323&$DivSqrtRecF64_mulAddZ31.scala 333:25n2C
_T_324927



valid_PB

specialCodeA_PB

specialCodeA_S&$DivSqrtRecF64_mulAddZ31.scala 334:31Lz!


specialCodeA_PC


_T_324&$DivSqrtRecF64_mulAddZ31.scala 334:25S2(
_T_325R


fractA_S
51
51&$DivSqrtRecF64_mulAddZ31.scala 335:67c28
_T_326.2,



valid_PB

fractA_51_PB


_T_325&$DivSqrtRecF64_mulAddZ31.scala 335:31Iz


fractA_51_PC


_T_326&$DivSqrtRecF64_mulAddZ31.scala 335:25n2C
_T_327927



valid_PB

specialCodeB_PB

specialCodeB_S&$DivSqrtRecF64_mulAddZ31.scala 336:31Lz!


specialCodeB_PC


_T_327&$DivSqrtRecF64_mulAddZ31.scala 336:25S2(
_T_328R


fractB_S
51
51&$DivSqrtRecF64_mulAddZ31.scala 337:67c28
_T_329.2,



valid_PB

fractB_51_PB


_T_328&$DivSqrtRecF64_mulAddZ31.scala 337:31Iz


fractB_51_PC


_T_329&$DivSqrtRecF64_mulAddZ31.scala 337:25t2I
_T_330?2=



valid_PB

roundingMode_PB:


ioroundingMode&$DivSqrtRecF64_mulAddZ31.scala 338:31Lz!


roundingMode_PC


_T_330&$DivSqrtRecF64_mulAddZ31.scala 338:25&$DivSqrtRecF64_mulAddZ31.scala 331:24³:


entering_PC_normalCaseCz



exp_PC


exp_PB&$DivSqrtRecF64_mulAddZ31.scala 341:25Mz"


fractA_0_PC

fractA_0_PB&$DivSqrtRecF64_mulAddZ31.scala 342:25Uz*


fractB_other_PC

fractB_other_PB&$DivSqrtRecF64_mulAddZ31.scala 343:25&$DivSqrtRecF64_mulAddZ31.scala 340:35_24

isZeroA_PC&R$

specialCodeA_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 346:42X2-
_T_332#R!

specialCodeA_PC
2
1&$DivSqrtRecF64_mulAddZ31.scala 347:41Y2.
isSpecialA_PCR


_T_332	

3&$DivSqrtRecF64_mulAddZ31.scala 347:48X2-
_T_334#R!

specialCodeA_PC
0
0&$DivSqrtRecF64_mulAddZ31.scala 348:59R2'
_T_336R


_T_334	

0&$DivSqrtRecF64_mulAddZ31.scala 348:42[20
	isInfA_PC#R!

isSpecialA_PC


_T_336&$DivSqrtRecF64_mulAddZ31.scala 348:39X2-
_T_337#R!

specialCodeA_PC
0
0&$DivSqrtRecF64_mulAddZ31.scala 349:59[20
	isNaNA_PC#R!

isSpecialA_PC


_T_337&$DivSqrtRecF64_mulAddZ31.scala 349:39X2-
_T_339#R!

fractA_51_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 350:38Z2/
isSigNaNA_PCR

	isNaNA_PC


_T_339&$DivSqrtRecF64_mulAddZ31.scala 350:35_24

isZeroB_PC&R$

specialCodeB_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 352:42X2-
_T_341#R!

specialCodeB_PC
2
1&$DivSqrtRecF64_mulAddZ31.scala 353:41Y2.
isSpecialB_PCR


_T_341	

3&$DivSqrtRecF64_mulAddZ31.scala 353:48X2-
_T_343#R!

specialCodeB_PC
0
0&$DivSqrtRecF64_mulAddZ31.scala 354:59R2'
_T_345R


_T_343	

0&$DivSqrtRecF64_mulAddZ31.scala 354:42[20
	isInfB_PC#R!

isSpecialB_PC


_T_345&$DivSqrtRecF64_mulAddZ31.scala 354:39X2-
_T_346#R!

specialCodeB_PC
0
0&$DivSqrtRecF64_mulAddZ31.scala 355:59[20
	isNaNB_PC#R!

isSpecialB_PC


_T_346&$DivSqrtRecF64_mulAddZ31.scala 355:39X2-
_T_348#R!

fractB_51_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 356:38Z2/
isSigNaNB_PCR

	isNaNB_PC


_T_348&$DivSqrtRecF64_mulAddZ31.scala 356:35C2-
_T_350#R!	

1

fractB_51_PCCat.scala 30:58F20
sigB_PC%R#


_T_350

fractB_other_PCCat.scala 30:58Y2.
_T_352$R"

isSpecialB_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 360:24V2+
_T_354!R


isZeroB_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 360:43Q2&
_T_355R


_T_352


_T_354&$DivSqrtRecF64_mulAddZ31.scala 360:40S2(
_T_357R
	
sign_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 360:59Q2&
_T_358R


_T_355


_T_357&$DivSqrtRecF64_mulAddZ31.scala 360:56Y2.
_T_360$R"

isSpecialA_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 361:13Y2.
_T_362$R"

isSpecialB_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 361:32Q2&
_T_363R


_T_360


_T_362&$DivSqrtRecF64_mulAddZ31.scala 361:29V2+
_T_365!R


isZeroA_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 361:51Q2&
_T_366R


_T_363


_T_365&$DivSqrtRecF64_mulAddZ31.scala 361:48V2+
_T_368!R


isZeroB_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 361:67Q2&
_T_369R


_T_366


_T_368&$DivSqrtRecF64_mulAddZ31.scala 361:64e2:
normalCase_PC)2'


	sqrtOp_PC


_T_358


_T_369&$DivSqrtRecF64_mulAddZ31.scala 360:12R2'
_T_371R


exp_PC	

2&$DivSqrtRecF64_mulAddZ31.scala 363:27L2!
expP2_PCR


_T_371
1&$DivSqrtRecF64_mulAddZ31.scala 363:27O2$
_T_372R


exp_PC
0
0&$DivSqrtRecF64_mulAddZ31.scala 365:19R2'
_T_373R


expP2_PC
13
1&$DivSqrtRecF64_mulAddZ31.scala 366:25=2'
_T_375R


_T_373	

0Cat.scala 30:58P2%
_T_376R


exp_PC
13
1&$DivSqrtRecF64_mulAddZ31.scala 367:23=2'
_T_378R


_T_376	

1Cat.scala 30:58]22
expP1_PC&2$



_T_372


_T_375


_T_378&$DivSqrtRecF64_mulAddZ31.scala 365:12n2C
roundingMode_near_even_PC&R$

roundingMode_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 370:54k2@
roundingMode_minMag_PC&R$

roundingMode_PC	

1&$DivSqrtRecF64_mulAddZ31.scala 371:54h2=
roundingMode_min_PC&R$

roundingMode_PC	

2&$DivSqrtRecF64_mulAddZ31.scala 372:54h2=
roundingMode_max_PC&R$

roundingMode_PC	

3&$DivSqrtRecF64_mulAddZ31.scala 373:54}2R
roundMagUp_PCA2?

	
sign_PC

roundingMode_min_PC

roundingMode_max_PC&$DivSqrtRecF64_mulAddZ31.scala 376:12|2Q
overflowY_roundMagUp_PC6R4

roundingMode_near_even_PC

roundMagUp_PC&$DivSqrtRecF64_mulAddZ31.scala 377:61Y2.
_T_380$R"

roundMagUp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 378:27e2:
_T_3820R.

roundingMode_near_even_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 378:46Z2/
roundMagDown_PCR


_T_380


_T_382&$DivSqrtRecF64_mulAddZ31.scala 378:43Y2.
_T_384$R"

normalCase_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 380:28[20
valid_leaving_PCR


_T_384


cyc_E1&$DivSqrtRecF64_mulAddZ31.scala 380:44]22
_T_385(R&


valid_PC

valid_leaving_PC&$DivSqrtRecF64_mulAddZ31.scala 381:28Gz



leaving_PC


_T_385&$DivSqrtRecF64_mulAddZ31.scala 381:16T2)
_T_387R


valid_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 382:17[20
_T_388&R$


_T_387

valid_leaving_PC&$DivSqrtRecF64_mulAddZ31.scala 382:28Ez



ready_PC


_T_388&$DivSqrtRecF64_mulAddZ31.scala 382:14U2*
_T_390 R

	sqrtOp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 383:39U2*
_T_391 R


leaving_PC


_T_390&$DivSqrtRecF64_mulAddZ31.scala 383:36Qz&
:


iooutValid_div


_T_391&$DivSqrtRecF64_mulAddZ31.scala 383:22X2-
_T_392#R!


leaving_PC

	sqrtOp_PC&$DivSqrtRecF64_mulAddZ31.scala 384:36Rz'
:


iooutValid_sqrt


_T_392&$DivSqrtRecF64_mulAddZ31.scala 384:22V2+
_T_394!R


cycleNum_A	

0&$DivSqrtRecF64_mulAddZ31.scala 388:49a26
_T_395,R*

entering_PA_normalCase


_T_394&$DivSqrtRecF64_mulAddZ31.scala 388:34:ï



_T_395a26
_T_398,2*



cyc_A4_div	

3	

0&$DivSqrtRecF64_mulAddZ31.scala 390:16b27
_T_401-2+


cyc_A7_sqrt	

6	

0&$DivSqrtRecF64_mulAddZ31.scala 391:16Q2&
_T_402R


_T_398


_T_401&$DivSqrtRecF64_mulAddZ31.scala 390:74b27
_T_404-R+

entering_PA_normalCase	

0&$DivSqrtRecF64_mulAddZ31.scala 392:17V2+
_T_406!R


cycleNum_A	

1&$DivSqrtRecF64_mulAddZ31.scala 392:54E2
_T_407R


_T_406&$DivSqrtRecF64_mulAddZ31.scala 392:54J2
_T_408R


_T_407
1&$DivSqrtRecF64_mulAddZ31.scala 392:54\21
_T_410'2%



_T_404


_T_408	

0&$DivSqrtRecF64_mulAddZ31.scala 392:16Q2&
_T_411R


_T_402


_T_410&$DivSqrtRecF64_mulAddZ31.scala 391:74Gz



cycleNum_A


_T_411&$DivSqrtRecF64_mulAddZ31.scala 389:20&$DivSqrtRecF64_mulAddZ31.scala 388:63[20
cyc_A6_sqrt!R


cycleNum_A	

6&$DivSqrtRecF64_mulAddZ31.scala 396:35[20
cyc_A5_sqrt!R


cycleNum_A	

5&$DivSqrtRecF64_mulAddZ31.scala 397:35[20
cyc_A4_sqrt!R


cycleNum_A	

4&$DivSqrtRecF64_mulAddZ31.scala 398:35Z2/
cyc_A4%R#

cyc_A4_sqrt


cyc_A4_div&$DivSqrtRecF64_mulAddZ31.scala 402:30V2+
cyc_A3!R


cycleNum_A	

3&$DivSqrtRecF64_mulAddZ31.scala 403:30V2+
cyc_A2!R


cycleNum_A	

2&$DivSqrtRecF64_mulAddZ31.scala 404:30V2+
cyc_A1!R


cycleNum_A	

1&$DivSqrtRecF64_mulAddZ31.scala 405:30U2*
_T_419 R

	sqrtOp_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 407:32U2*

cyc_A3_divR


cyc_A3


_T_419&$DivSqrtRecF64_mulAddZ31.scala 407:29U2*
_T_421 R

	sqrtOp_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 408:32U2*

cyc_A2_divR


cyc_A2


_T_421&$DivSqrtRecF64_mulAddZ31.scala 408:29U2*
_T_423 R

	sqrtOp_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 409:32U2*

cyc_A1_divR


cyc_A1


_T_423&$DivSqrtRecF64_mulAddZ31.scala 409:29Y2.
cyc_A3_sqrtR


cyc_A3

	sqrtOp_PA&$DivSqrtRecF64_mulAddZ31.scala 411:30Y2.
cyc_A2_sqrtR


cyc_A2

	sqrtOp_PA&$DivSqrtRecF64_mulAddZ31.scala 412:30Y2.
cyc_A1_sqrtR


cyc_A1

	sqrtOp_PA&$DivSqrtRecF64_mulAddZ31.scala 413:30V2+
_T_425!R


cycleNum_B	

0&$DivSqrtRecF64_mulAddZ31.scala 415:33Q2&
_T_426R


cyc_A1


_T_425&$DivSqrtRecF64_mulAddZ31.scala 415:18¬:



_T_426a26
_T_429,2*


	sqrtOp_PA


10	

6&$DivSqrtRecF64_mulAddZ31.scala 418:20V2+
_T_431!R


cycleNum_B	

1&$DivSqrtRecF64_mulAddZ31.scala 419:28E2
_T_432R


_T_431&$DivSqrtRecF64_mulAddZ31.scala 419:28J2
_T_433R


_T_432
1&$DivSqrtRecF64_mulAddZ31.scala 419:28[20
_T_434&2$



cyc_A1


_T_429


_T_433&$DivSqrtRecF64_mulAddZ31.scala 417:16Gz



cycleNum_B


_T_434&$DivSqrtRecF64_mulAddZ31.scala 416:20&$DivSqrtRecF64_mulAddZ31.scala 415:47W2,
_T_436"R 


cycleNum_B


10&$DivSqrtRecF64_mulAddZ31.scala 423:33Iz


cyc_B10_sqrt


_T_436&$DivSqrtRecF64_mulAddZ31.scala 423:18V2+
_T_438!R


cycleNum_B	

9&$DivSqrtRecF64_mulAddZ31.scala 424:33Hz


cyc_B9_sqrt


_T_438&$DivSqrtRecF64_mulAddZ31.scala 424:18V2+
_T_440!R


cycleNum_B	

8&$DivSqrtRecF64_mulAddZ31.scala 425:33Hz


cyc_B8_sqrt


_T_440&$DivSqrtRecF64_mulAddZ31.scala 425:18V2+
_T_442!R


cycleNum_B	

7&$DivSqrtRecF64_mulAddZ31.scala 426:33Hz


cyc_B7_sqrt


_T_442&$DivSqrtRecF64_mulAddZ31.scala 426:18V2+
_T_444!R


cycleNum_B	

6&$DivSqrtRecF64_mulAddZ31.scala 428:27Cz



cyc_B6


_T_444&$DivSqrtRecF64_mulAddZ31.scala 428:12V2+
_T_446!R


cycleNum_B	

5&$DivSqrtRecF64_mulAddZ31.scala 429:27Cz



cyc_B5


_T_446&$DivSqrtRecF64_mulAddZ31.scala 429:12V2+
_T_448!R


cycleNum_B	

4&$DivSqrtRecF64_mulAddZ31.scala 430:27Cz



cyc_B4


_T_448&$DivSqrtRecF64_mulAddZ31.scala 430:12V2+
_T_450!R


cycleNum_B	

3&$DivSqrtRecF64_mulAddZ31.scala 431:27Cz



cyc_B3


_T_450&$DivSqrtRecF64_mulAddZ31.scala 431:12V2+
_T_452!R


cycleNum_B	

2&$DivSqrtRecF64_mulAddZ31.scala 432:27Cz



cyc_B2


_T_452&$DivSqrtRecF64_mulAddZ31.scala 432:12V2+
_T_454!R


cycleNum_B	

1&$DivSqrtRecF64_mulAddZ31.scala 433:27Cz



cyc_B1


_T_454&$DivSqrtRecF64_mulAddZ31.scala 433:12S2(
_T_455R


cyc_B6


valid_PA&$DivSqrtRecF64_mulAddZ31.scala 435:26U2*
_T_457 R

	sqrtOp_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 435:41Q2&
_T_458R


_T_455


_T_457&$DivSqrtRecF64_mulAddZ31.scala 435:38Gz



cyc_B6_div


_T_458&$DivSqrtRecF64_mulAddZ31.scala 435:16S2(
_T_459R


cyc_B5


valid_PA&$DivSqrtRecF64_mulAddZ31.scala 436:26U2*
_T_461 R

	sqrtOp_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 436:41Q2&
_T_462R


_T_459


_T_461&$DivSqrtRecF64_mulAddZ31.scala 436:38Gz



cyc_B5_div


_T_462&$DivSqrtRecF64_mulAddZ31.scala 436:16S2(
_T_463R


cyc_B4


valid_PA&$DivSqrtRecF64_mulAddZ31.scala 437:26U2*
_T_465 R

	sqrtOp_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 437:41Q2&
_T_466R


_T_463


_T_465&$DivSqrtRecF64_mulAddZ31.scala 437:38Gz



cyc_B4_div


_T_466&$DivSqrtRecF64_mulAddZ31.scala 437:16U2*
_T_468 R

	sqrtOp_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 438:29Q2&
_T_469R


cyc_B3


_T_468&$DivSqrtRecF64_mulAddZ31.scala 438:26Gz



cyc_B3_div


_T_469&$DivSqrtRecF64_mulAddZ31.scala 438:16U2*
_T_471 R

	sqrtOp_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 439:29Q2&
_T_472R


cyc_B2


_T_471&$DivSqrtRecF64_mulAddZ31.scala 439:26Gz



cyc_B2_div


_T_472&$DivSqrtRecF64_mulAddZ31.scala 439:16U2*
_T_474 R

	sqrtOp_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 440:29Q2&
_T_475R


cyc_B1


_T_474&$DivSqrtRecF64_mulAddZ31.scala 440:26Gz



cyc_B1_div


_T_475&$DivSqrtRecF64_mulAddZ31.scala 440:16S2(
_T_476R


cyc_B6


valid_PB&$DivSqrtRecF64_mulAddZ31.scala 442:27T2)
_T_477R


_T_476

	sqrtOp_PB&$DivSqrtRecF64_mulAddZ31.scala 442:39Hz


cyc_B6_sqrt


_T_477&$DivSqrtRecF64_mulAddZ31.scala 442:17S2(
_T_478R


cyc_B5


valid_PB&$DivSqrtRecF64_mulAddZ31.scala 443:27T2)
_T_479R


_T_478

	sqrtOp_PB&$DivSqrtRecF64_mulAddZ31.scala 443:39Hz


cyc_B5_sqrt


_T_479&$DivSqrtRecF64_mulAddZ31.scala 443:17S2(
_T_480R


cyc_B4


valid_PB&$DivSqrtRecF64_mulAddZ31.scala 444:27T2)
_T_481R


_T_480

	sqrtOp_PB&$DivSqrtRecF64_mulAddZ31.scala 444:39Hz


cyc_B4_sqrt


_T_481&$DivSqrtRecF64_mulAddZ31.scala 444:17T2)
_T_482R


cyc_B3

	sqrtOp_PB&$DivSqrtRecF64_mulAddZ31.scala 445:27Hz


cyc_B3_sqrt


_T_482&$DivSqrtRecF64_mulAddZ31.scala 445:17T2)
_T_483R


cyc_B2

	sqrtOp_PB&$DivSqrtRecF64_mulAddZ31.scala 446:27Hz


cyc_B2_sqrt


_T_483&$DivSqrtRecF64_mulAddZ31.scala 446:17T2)
_T_484R


cyc_B1

	sqrtOp_PB&$DivSqrtRecF64_mulAddZ31.scala 447:27Hz


cyc_B1_sqrt


_T_484&$DivSqrtRecF64_mulAddZ31.scala 447:17V2+
_T_486!R


cycleNum_C	

0&$DivSqrtRecF64_mulAddZ31.scala 449:33Q2&
_T_487R


cyc_B1


_T_486&$DivSqrtRecF64_mulAddZ31.scala 449:18«:ÿ



_T_487`25
_T_490+2)


	sqrtOp_PB	

6	

5&$DivSqrtRecF64_mulAddZ31.scala 451:28V2+
_T_492!R


cycleNum_C	

1&$DivSqrtRecF64_mulAddZ31.scala 451:70E2
_T_493R


_T_492&$DivSqrtRecF64_mulAddZ31.scala 451:70J2
_T_494R


_T_493
1&$DivSqrtRecF64_mulAddZ31.scala 451:70[20
_T_495&2$



cyc_B1


_T_490


_T_494&$DivSqrtRecF64_mulAddZ31.scala 451:16Gz



cycleNum_C


_T_495&$DivSqrtRecF64_mulAddZ31.scala 450:20&$DivSqrtRecF64_mulAddZ31.scala 449:47[20
cyc_C6_sqrt!R


cycleNum_C	

6&$DivSqrtRecF64_mulAddZ31.scala 454:35V2+
_T_498!R


cycleNum_C	

5&$DivSqrtRecF64_mulAddZ31.scala 456:27Cz



cyc_C5


_T_498&$DivSqrtRecF64_mulAddZ31.scala 456:12V2+
_T_500!R


cycleNum_C	

4&$DivSqrtRecF64_mulAddZ31.scala 457:27Cz



cyc_C4


_T_500&$DivSqrtRecF64_mulAddZ31.scala 457:12V2+
_T_502!R


cycleNum_C	

3&$DivSqrtRecF64_mulAddZ31.scala 458:27Cz



cyc_C3


_T_502&$DivSqrtRecF64_mulAddZ31.scala 458:12V2+
_T_504!R


cycleNum_C	

2&$DivSqrtRecF64_mulAddZ31.scala 459:27Cz



cyc_C2


_T_504&$DivSqrtRecF64_mulAddZ31.scala 459:12V2+
_T_506!R


cycleNum_C	

1&$DivSqrtRecF64_mulAddZ31.scala 460:27Cz



cyc_C1


_T_506&$DivSqrtRecF64_mulAddZ31.scala 460:12U2*
_T_508 R

	sqrtOp_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 462:32U2*

cyc_C5_divR


cyc_C5


_T_508&$DivSqrtRecF64_mulAddZ31.scala 462:29U2*
_T_510 R

	sqrtOp_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 463:32U2*

cyc_C4_divR


cyc_C4


_T_510&$DivSqrtRecF64_mulAddZ31.scala 463:29U2*
_T_512 R

	sqrtOp_PB	

0&$DivSqrtRecF64_mulAddZ31.scala 464:32U2*

cyc_C3_divR


cyc_C3


_T_512&$DivSqrtRecF64_mulAddZ31.scala 464:29U2*
_T_514 R

	sqrtOp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 465:32U2*

cyc_C2_divR


cyc_C2


_T_514&$DivSqrtRecF64_mulAddZ31.scala 465:29U2*
_T_516 R

	sqrtOp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 466:32U2*

cyc_C1_divR


cyc_C1


_T_516&$DivSqrtRecF64_mulAddZ31.scala 466:29Y2.
cyc_C5_sqrtR


cyc_C5

	sqrtOp_PB&$DivSqrtRecF64_mulAddZ31.scala 468:30Y2.
cyc_C4_sqrtR


cyc_C4

	sqrtOp_PB&$DivSqrtRecF64_mulAddZ31.scala 469:30Y2.
cyc_C3_sqrtR


cyc_C3

	sqrtOp_PB&$DivSqrtRecF64_mulAddZ31.scala 470:30Y2.
cyc_C2_sqrtR


cyc_C2

	sqrtOp_PC&$DivSqrtRecF64_mulAddZ31.scala 471:30Y2.
cyc_C1_sqrtR


cyc_C1

	sqrtOp_PC&$DivSqrtRecF64_mulAddZ31.scala 472:30V2+
_T_518!R


cycleNum_E	

0&$DivSqrtRecF64_mulAddZ31.scala 474:33Q2&
_T_519R


cyc_C1


_T_518&$DivSqrtRecF64_mulAddZ31.scala 474:18Ê:



_T_519V2+
_T_522!R


cycleNum_E	

1&$DivSqrtRecF64_mulAddZ31.scala 475:55E2
_T_523R


_T_522&$DivSqrtRecF64_mulAddZ31.scala 475:55J2
_T_524R


_T_523
1&$DivSqrtRecF64_mulAddZ31.scala 475:55\21
_T_525'2%



cyc_C1	

4


_T_524&$DivSqrtRecF64_mulAddZ31.scala 475:26Gz



cycleNum_E


_T_525&$DivSqrtRecF64_mulAddZ31.scala 475:20&$DivSqrtRecF64_mulAddZ31.scala 474:47V2+
_T_527!R


cycleNum_E	

4&$DivSqrtRecF64_mulAddZ31.scala 478:27Cz



cyc_E4


_T_527&$DivSqrtRecF64_mulAddZ31.scala 478:12V2+
_T_529!R


cycleNum_E	

3&$DivSqrtRecF64_mulAddZ31.scala 479:27Cz



cyc_E3


_T_529&$DivSqrtRecF64_mulAddZ31.scala 479:12V2+
_T_531!R


cycleNum_E	

2&$DivSqrtRecF64_mulAddZ31.scala 480:27Cz



cyc_E2


_T_531&$DivSqrtRecF64_mulAddZ31.scala 480:12V2+
_T_533!R


cycleNum_E	

1&$DivSqrtRecF64_mulAddZ31.scala 481:27Cz



cyc_E1


_T_533&$DivSqrtRecF64_mulAddZ31.scala 481:12U2*
_T_535 R

	sqrtOp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 483:32U2*

cyc_E4_divR


cyc_E4


_T_535&$DivSqrtRecF64_mulAddZ31.scala 483:29U2*
_T_537 R

	sqrtOp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 484:32U2*

cyc_E3_divR


cyc_E3


_T_537&$DivSqrtRecF64_mulAddZ31.scala 484:29U2*
_T_539 R

	sqrtOp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 485:32U2*

cyc_E2_divR


cyc_E2


_T_539&$DivSqrtRecF64_mulAddZ31.scala 485:29U2*
_T_541 R

	sqrtOp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 486:32U2*

cyc_E1_divR


cyc_E1


_T_541&$DivSqrtRecF64_mulAddZ31.scala 486:29Y2.
cyc_E4_sqrtR


cyc_E4

	sqrtOp_PC&$DivSqrtRecF64_mulAddZ31.scala 488:30Y2.
cyc_E3_sqrtR


cyc_E3

	sqrtOp_PC&$DivSqrtRecF64_mulAddZ31.scala 489:30Y2.
cyc_E2_sqrtR


cyc_E2

	sqrtOp_PC&$DivSqrtRecF64_mulAddZ31.scala 490:30Y2.
cyc_E1_sqrtR


cyc_E1

	sqrtOp_PC&$DivSqrtRecF64_mulAddZ31.scala 491:30j2?
zFractB_A4_div-2+



cyc_A4_div


fractB_S	

0&$DivSqrtRecF64_mulAddZ31.scala 496:29S2(
_T_543R


fractB_S
51
49&$DivSqrtRecF64_mulAddZ31.scala 498:53R2'
_T_545R


_T_543	

0&$DivSqrtRecF64_mulAddZ31.scala 498:62a26
zLinPiece_0_A4_div R


cyc_A4_div


_T_545&$DivSqrtRecF64_mulAddZ31.scala 498:41S2(
_T_546R


fractB_S
51
49&$DivSqrtRecF64_mulAddZ31.scala 499:53R2'
_T_548R


_T_546	

1&$DivSqrtRecF64_mulAddZ31.scala 499:62a26
zLinPiece_1_A4_div R


cyc_A4_div


_T_548&$DivSqrtRecF64_mulAddZ31.scala 499:41S2(
_T_549R


fractB_S
51
49&$DivSqrtRecF64_mulAddZ31.scala 500:53R2'
_T_551R


_T_549	

2&$DivSqrtRecF64_mulAddZ31.scala 500:62a26
zLinPiece_2_A4_div R


cyc_A4_div


_T_551&$DivSqrtRecF64_mulAddZ31.scala 500:41S2(
_T_552R


fractB_S
51
49&$DivSqrtRecF64_mulAddZ31.scala 501:53R2'
_T_554R


_T_552	

3&$DivSqrtRecF64_mulAddZ31.scala 501:62a26
zLinPiece_3_A4_div R


cyc_A4_div


_T_554&$DivSqrtRecF64_mulAddZ31.scala 501:41S2(
_T_555R


fractB_S
51
49&$DivSqrtRecF64_mulAddZ31.scala 502:53R2'
_T_557R


_T_555	

4&$DivSqrtRecF64_mulAddZ31.scala 502:62a26
zLinPiece_4_A4_div R


cyc_A4_div


_T_557&$DivSqrtRecF64_mulAddZ31.scala 502:41S2(
_T_558R


fractB_S
51
49&$DivSqrtRecF64_mulAddZ31.scala 503:53R2'
_T_560R


_T_558	

5&$DivSqrtRecF64_mulAddZ31.scala 503:62a26
zLinPiece_5_A4_div R


cyc_A4_div


_T_560&$DivSqrtRecF64_mulAddZ31.scala 503:41S2(
_T_561R


fractB_S
51
49&$DivSqrtRecF64_mulAddZ31.scala 504:53R2'
_T_563R


_T_561	

6&$DivSqrtRecF64_mulAddZ31.scala 504:62a26
zLinPiece_6_A4_div R


cyc_A4_div


_T_563&$DivSqrtRecF64_mulAddZ31.scala 504:41S2(
_T_564R


fractB_S
51
49&$DivSqrtRecF64_mulAddZ31.scala 505:53R2'
_T_566R


_T_564	

7&$DivSqrtRecF64_mulAddZ31.scala 505:62a26
zLinPiece_7_A4_div R


cyc_A4_div


_T_566&$DivSqrtRecF64_mulAddZ31.scala 505:41k2@
_T_569624


zLinPiece_0_A4_div

455		

0&$DivSqrtRecF64_mulAddZ31.scala 507:12k2@
_T_572624


zLinPiece_1_A4_div

364		

0&$DivSqrtRecF64_mulAddZ31.scala 508:12Q2&
_T_573R


_T_569


_T_572&$DivSqrtRecF64_mulAddZ31.scala 507:59k2@
_T_576624


zLinPiece_2_A4_div

298		

0&$DivSqrtRecF64_mulAddZ31.scala 509:12Q2&
_T_577R


_T_573


_T_576&$DivSqrtRecF64_mulAddZ31.scala 508:59k2@
_T_580624


zLinPiece_3_A4_div

248		

0&$DivSqrtRecF64_mulAddZ31.scala 510:12Q2&
_T_581R


_T_577


_T_580&$DivSqrtRecF64_mulAddZ31.scala 509:59k2@
_T_584624


zLinPiece_4_A4_div

210		

0&$DivSqrtRecF64_mulAddZ31.scala 511:12Q2&
_T_585R


_T_581


_T_584&$DivSqrtRecF64_mulAddZ31.scala 510:59k2@
_T_588624


zLinPiece_5_A4_div

180		

0&$DivSqrtRecF64_mulAddZ31.scala 512:12Q2&
_T_589R


_T_585


_T_588&$DivSqrtRecF64_mulAddZ31.scala 511:59k2@
_T_592624


zLinPiece_6_A4_div

156		

0&$DivSqrtRecF64_mulAddZ31.scala 513:12Q2&
_T_593R


_T_589


_T_592&$DivSqrtRecF64_mulAddZ31.scala 512:59k2@
_T_596624


zLinPiece_7_A4_div

137		

0&$DivSqrtRecF64_mulAddZ31.scala 514:12U2*

zK1_A4_divR


_T_593


_T_596&$DivSqrtRecF64_mulAddZ31.scala 513:59I2
_T_598R

4067&$DivSqrtRecF64_mulAddZ31.scala 516:33h2=
_T_600321


zLinPiece_0_A4_div


_T_598	

0&$DivSqrtRecF64_mulAddZ31.scala 516:12I2
_T_602R

3165&$DivSqrtRecF64_mulAddZ31.scala 517:33h2=
_T_604321


zLinPiece_1_A4_div


_T_602	

0&$DivSqrtRecF64_mulAddZ31.scala 517:12Q2&
_T_605R


_T_600


_T_604&$DivSqrtRecF64_mulAddZ31.scala 516:61I2
_T_607R

2442&$DivSqrtRecF64_mulAddZ31.scala 518:33h2=
_T_609321


zLinPiece_2_A4_div


_T_607	

0&$DivSqrtRecF64_mulAddZ31.scala 518:12Q2&
_T_610R


_T_605


_T_609&$DivSqrtRecF64_mulAddZ31.scala 517:61I2
_T_612R

1849&$DivSqrtRecF64_mulAddZ31.scala 519:33h2=
_T_614321


zLinPiece_3_A4_div


_T_612	

0&$DivSqrtRecF64_mulAddZ31.scala 519:12Q2&
_T_615R


_T_610


_T_614&$DivSqrtRecF64_mulAddZ31.scala 518:61I2
_T_617R

1355&$DivSqrtRecF64_mulAddZ31.scala 520:33h2=
_T_619321


zLinPiece_4_A4_div


_T_617	

0&$DivSqrtRecF64_mulAddZ31.scala 520:12Q2&
_T_620R


_T_615


_T_619&$DivSqrtRecF64_mulAddZ31.scala 519:61H2
_T_622R

937&$DivSqrtRecF64_mulAddZ31.scala 521:33h2=
_T_624321


zLinPiece_5_A4_div


_T_622	

0&$DivSqrtRecF64_mulAddZ31.scala 521:12Q2&
_T_625R


_T_620


_T_624&$DivSqrtRecF64_mulAddZ31.scala 520:61H2
_T_627R

578&$DivSqrtRecF64_mulAddZ31.scala 522:33h2=
_T_629321


zLinPiece_6_A4_div


_T_627	

0&$DivSqrtRecF64_mulAddZ31.scala 522:12Q2&
_T_630R


_T_625


_T_629&$DivSqrtRecF64_mulAddZ31.scala 521:61H2
_T_632R

267&$DivSqrtRecF64_mulAddZ31.scala 523:33h2=
_T_634321


zLinPiece_7_A4_div


_T_632	

0&$DivSqrtRecF64_mulAddZ31.scala 523:12_24
zComplFractK0_A4_divR


_T_630


_T_634&$DivSqrtRecF64_mulAddZ31.scala 522:61l2A
zFractB_A7_sqrt.2,


cyc_A7_sqrt


fractB_S	

0&$DivSqrtRecF64_mulAddZ31.scala 525:30O2$
_T_636R


expB_S
0
0&$DivSqrtRecF64_mulAddZ31.scala 527:55R2'
_T_638R


_T_636	

0&$DivSqrtRecF64_mulAddZ31.scala 527:47V2+
_T_639!R

cyc_A7_sqrt


_T_638&$DivSqrtRecF64_mulAddZ31.scala 527:44S2(
_T_640R


fractB_S
51
51&$DivSqrtRecF64_mulAddZ31.scala 527:72R2'
_T_642R


_T_640	

0&$DivSqrtRecF64_mulAddZ31.scala 527:62_24
zQuadPiece_0_A7_sqrtR


_T_639


_T_642&$DivSqrtRecF64_mulAddZ31.scala 527:59O2$
_T_643R


expB_S
0
0&$DivSqrtRecF64_mulAddZ31.scala 528:55R2'
_T_645R


_T_643	

0&$DivSqrtRecF64_mulAddZ31.scala 528:47V2+
_T_646!R

cyc_A7_sqrt


_T_645&$DivSqrtRecF64_mulAddZ31.scala 528:44S2(
_T_647R


fractB_S
51
51&$DivSqrtRecF64_mulAddZ31.scala 528:72_24
zQuadPiece_1_A7_sqrtR


_T_646


_T_647&$DivSqrtRecF64_mulAddZ31.scala 528:59O2$
_T_648R


expB_S
0
0&$DivSqrtRecF64_mulAddZ31.scala 529:55V2+
_T_649!R

cyc_A7_sqrt


_T_648&$DivSqrtRecF64_mulAddZ31.scala 529:44S2(
_T_650R


fractB_S
51
51&$DivSqrtRecF64_mulAddZ31.scala 529:72R2'
_T_652R


_T_650	

0&$DivSqrtRecF64_mulAddZ31.scala 529:62_24
zQuadPiece_2_A7_sqrtR


_T_649


_T_652&$DivSqrtRecF64_mulAddZ31.scala 529:59O2$
_T_653R


expB_S
0
0&$DivSqrtRecF64_mulAddZ31.scala 530:55V2+
_T_654!R

cyc_A7_sqrt


_T_653&$DivSqrtRecF64_mulAddZ31.scala 530:44S2(
_T_655R


fractB_S
51
51&$DivSqrtRecF64_mulAddZ31.scala 530:72_24
zQuadPiece_3_A7_sqrtR


_T_654


_T_655&$DivSqrtRecF64_mulAddZ31.scala 530:59m2B
_T_658826


zQuadPiece_0_A7_sqrt

456		

0&$DivSqrtRecF64_mulAddZ31.scala 532:12m2B
_T_661826


zQuadPiece_1_A7_sqrt

193		

0&$DivSqrtRecF64_mulAddZ31.scala 533:12Q2&
_T_662R


_T_658


_T_661&$DivSqrtRecF64_mulAddZ31.scala 532:61m2B
_T_665826


zQuadPiece_2_A7_sqrt

323		

0&$DivSqrtRecF64_mulAddZ31.scala 534:12Q2&
_T_666R


_T_662


_T_665&$DivSqrtRecF64_mulAddZ31.scala 533:61m2B
_T_669826


zQuadPiece_3_A7_sqrt

137		

0&$DivSqrtRecF64_mulAddZ31.scala 535:12V2+
zK2_A7_sqrtR


_T_666


_T_669&$DivSqrtRecF64_mulAddZ31.scala 534:61H2
_T_671R

976
&$DivSqrtRecF64_mulAddZ31.scala 537:35j2?
_T_673523


zQuadPiece_0_A7_sqrt


_T_671	

0&$DivSqrtRecF64_mulAddZ31.scala 537:12H2
_T_675R

544
&$DivSqrtRecF64_mulAddZ31.scala 538:35j2?
_T_677523


zQuadPiece_1_A7_sqrt


_T_675	

0&$DivSqrtRecF64_mulAddZ31.scala 538:12Q2&
_T_678R


_T_673


_T_677&$DivSqrtRecF64_mulAddZ31.scala 537:63H2
_T_680R

690
&$DivSqrtRecF64_mulAddZ31.scala 539:35j2?
_T_682523


zQuadPiece_2_A7_sqrt


_T_680	

0&$DivSqrtRecF64_mulAddZ31.scala 539:12Q2&
_T_683R


_T_678


_T_682&$DivSqrtRecF64_mulAddZ31.scala 538:63H2
_T_685R

385
&$DivSqrtRecF64_mulAddZ31.scala 540:35j2?
_T_687523


zQuadPiece_3_A7_sqrt


_T_685	

0&$DivSqrtRecF64_mulAddZ31.scala 540:12[20
zComplK1_A7_sqrtR


_T_683


_T_687&$DivSqrtRecF64_mulAddZ31.scala 539:63O2$
_T_688R


exp_PA
0
0&$DivSqrtRecF64_mulAddZ31.scala 542:55R2'
_T_690R


_T_688	

0&$DivSqrtRecF64_mulAddZ31.scala 542:47V2+
_T_691!R

cyc_A6_sqrt


_T_690&$DivSqrtRecF64_mulAddZ31.scala 542:44R2'
_T_692R
	
sigB_PA
51
51&$DivSqrtRecF64_mulAddZ31.scala 542:71R2'
_T_694R


_T_692	

0&$DivSqrtRecF64_mulAddZ31.scala 542:62_24
zQuadPiece_0_A6_sqrtR


_T_691


_T_694&$DivSqrtRecF64_mulAddZ31.scala 542:59O2$
_T_695R


exp_PA
0
0&$DivSqrtRecF64_mulAddZ31.scala 543:55R2'
_T_697R


_T_695	

0&$DivSqrtRecF64_mulAddZ31.scala 543:47V2+
_T_698!R

cyc_A6_sqrt


_T_697&$DivSqrtRecF64_mulAddZ31.scala 543:44R2'
_T_699R
	
sigB_PA
51
51&$DivSqrtRecF64_mulAddZ31.scala 543:71_24
zQuadPiece_1_A6_sqrtR


_T_698


_T_699&$DivSqrtRecF64_mulAddZ31.scala 543:59O2$
_T_700R


exp_PA
0
0&$DivSqrtRecF64_mulAddZ31.scala 544:55V2+
_T_701!R

cyc_A6_sqrt


_T_700&$DivSqrtRecF64_mulAddZ31.scala 544:44R2'
_T_702R
	
sigB_PA
51
51&$DivSqrtRecF64_mulAddZ31.scala 544:71R2'
_T_704R


_T_702	

0&$DivSqrtRecF64_mulAddZ31.scala 544:62_24
zQuadPiece_2_A6_sqrtR


_T_701


_T_704&$DivSqrtRecF64_mulAddZ31.scala 544:59O2$
_T_705R


exp_PA
0
0&$DivSqrtRecF64_mulAddZ31.scala 545:55V2+
_T_706!R

cyc_A6_sqrt


_T_705&$DivSqrtRecF64_mulAddZ31.scala 545:44R2'
_T_707R
	
sigB_PA
51
51&$DivSqrtRecF64_mulAddZ31.scala 545:71_24
zQuadPiece_3_A6_sqrtR


_T_706


_T_707&$DivSqrtRecF64_mulAddZ31.scala 545:59I2
_T_709R

8165&$DivSqrtRecF64_mulAddZ31.scala 547:35j2?
_T_711523


zQuadPiece_0_A6_sqrt


_T_709	

0&$DivSqrtRecF64_mulAddZ31.scala 547:12I2
_T_713R

5173&$DivSqrtRecF64_mulAddZ31.scala 548:35j2?
_T_715523


zQuadPiece_1_A6_sqrt


_T_713	

0&$DivSqrtRecF64_mulAddZ31.scala 548:12Q2&
_T_716R


_T_711


_T_715&$DivSqrtRecF64_mulAddZ31.scala 547:64I2
_T_718R

3372&$DivSqrtRecF64_mulAddZ31.scala 549:35j2?
_T_720523


zQuadPiece_2_A6_sqrt


_T_718	

0&$DivSqrtRecF64_mulAddZ31.scala 549:12Q2&
_T_721R


_T_716


_T_720&$DivSqrtRecF64_mulAddZ31.scala 548:64I2
_T_723R

1256&$DivSqrtRecF64_mulAddZ31.scala 550:35j2?
_T_725523


zQuadPiece_3_A6_sqrt


_T_723	

0&$DivSqrtRecF64_mulAddZ31.scala 550:12`25
zComplFractK0_A6_sqrtR


_T_721


_T_725&$DivSqrtRecF64_mulAddZ31.scala 549:64Y2.
_T_726$R"

zFractB_A4_div
48
40&$DivSqrtRecF64_mulAddZ31.scala 553:23V2+
_T_727!R


_T_726

zK2_A7_sqrt&$DivSqrtRecF64_mulAddZ31.scala 553:32Q2&
_T_729R	

cyc_S	

0&$DivSqrtRecF64_mulAddZ31.scala 554:17d29
_T_731/2-



_T_729

nextMulAdd9A_A	

0&$DivSqrtRecF64_mulAddZ31.scala 554:16U2*

mulAdd9A_AR


_T_727


_T_731&$DivSqrtRecF64_mulAddZ31.scala 553:46Z2/
_T_732%R#

zFractB_A7_sqrt
50
42&$DivSqrtRecF64_mulAddZ31.scala 556:37U2*
_T_733 R


zK1_A4_div


_T_732&$DivSqrtRecF64_mulAddZ31.scala 556:20Q2&
_T_735R	

cyc_S	

0&$DivSqrtRecF64_mulAddZ31.scala 557:17d29
_T_737/2-



_T_735

nextMulAdd9B_A	

0&$DivSqrtRecF64_mulAddZ31.scala 557:16U2*

mulAdd9B_AR


_T_733


_T_737&$DivSqrtRecF64_mulAddZ31.scala 556:46C2)
_T_738R

cyc_A7_sqrt
0
0Bitwise.scala 71:15O25
_T_741+2)



_T_738

1023
	

0
Bitwise.scala 71:12F20
_T_742&R$

zComplK1_A7_sqrt


_T_741Cat.scala 30:58C2)
_T_743R

cyc_A6_sqrt
0
0Bitwise.scala 71:15M23
_T_746)2'



_T_743


63	

0Bitwise.scala 71:12P2:
_T_7470R.

cyc_A6_sqrt

zComplFractK0_A6_sqrtCat.scala 30:58<2&
_T_748R


_T_747


_T_746Cat.scala 30:58Q2&
_T_749R


_T_742


_T_748&$DivSqrtRecF64_mulAddZ31.scala 559:71B2(
_T_750R


cyc_A4_div
0
0Bitwise.scala 71:15N24
_T_753*2(



_T_750

255	

0Bitwise.scala 71:12N28
_T_754.R,


cyc_A4_div

zComplFractK0_A4_divCat.scala 30:58<2&
_T_755R


_T_754


_T_753Cat.scala 30:58Q2&
_T_756R


_T_749


_T_755&$DivSqrtRecF64_mulAddZ31.scala 560:71N2#
_T_758R

	fractR0_A
10&$DivSqrtRecF64_mulAddZ31.scala 563:54W2,
_T_759"R 

262144


_T_758&$DivSqrtRecF64_mulAddZ31.scala 563:42J2
_T_760R


_T_759
1&$DivSqrtRecF64_mulAddZ31.scala 563:42a26
_T_762,2*


cyc_A5_sqrt


_T_760	

0&$DivSqrtRecF64_mulAddZ31.scala 563:12Q2&
_T_763R


_T_756


_T_762&$DivSqrtRecF64_mulAddZ31.scala 561:71W2,
_T_764"R 

hiSqrR0_A_sqrt
9
9&$DivSqrtRecF64_mulAddZ31.scala 564:44R2'
_T_766R


_T_764	

0&$DivSqrtRecF64_mulAddZ31.scala 564:28V2+
_T_767!R

cyc_A4_sqrt


_T_766&$DivSqrtRecF64_mulAddZ31.scala 564:25`25
_T_770+2)



_T_767

1024	

0&$DivSqrtRecF64_mulAddZ31.scala 564:12Q2&
_T_771R


_T_763


_T_770&$DivSqrtRecF64_mulAddZ31.scala 563:70W2,
_T_772"R 

hiSqrR0_A_sqrt
9
9&$DivSqrtRecF64_mulAddZ31.scala 565:43V2+
_T_773!R

cyc_A4_sqrt


_T_772&$DivSqrtRecF64_mulAddZ31.scala 565:26U2*
_T_774 R


_T_773


cyc_A3_div&$DivSqrtRecF64_mulAddZ31.scala 565:48R2'
_T_775R
	
sigB_PA
46
26&$DivSqrtRecF64_mulAddZ31.scala 566:20U2*
_T_777 R


_T_775

1024&$DivSqrtRecF64_mulAddZ31.scala 566:29J2
_T_778R


_T_777
1&$DivSqrtRecF64_mulAddZ31.scala 566:29\21
_T_780'2%



_T_774


_T_778	

0&$DivSqrtRecF64_mulAddZ31.scala 565:12Q2&
_T_781R


_T_771


_T_780&$DivSqrtRecF64_mulAddZ31.scala 564:71V2+
_T_782!R

cyc_A3_sqrt


cyc_A2&$DivSqrtRecF64_mulAddZ31.scala 569:25e2:
_T_78402.



_T_782

partNegSigma0_A	

0&$DivSqrtRecF64_mulAddZ31.scala 569:12Q2&
_T_785R


_T_781


_T_784&$DivSqrtRecF64_mulAddZ31.scala 568:11N2#
_T_786R

	fractR0_A
16&$DivSqrtRecF64_mulAddZ31.scala 570:45a26
_T_788,2*


cyc_A1_sqrt


_T_786	

0&$DivSqrtRecF64_mulAddZ31.scala 570:12Q2&
_T_789R


_T_785


_T_788&$DivSqrtRecF64_mulAddZ31.scala 569:62N2#
_T_790R

	fractR0_A
15&$DivSqrtRecF64_mulAddZ31.scala 571:45`25
_T_792+2)



cyc_A1_div


_T_790	

0&$DivSqrtRecF64_mulAddZ31.scala 571:12U2*

mulAdd9C_AR


_T_789


_T_792&$DivSqrtRecF64_mulAddZ31.scala 570:62Y2.
_T_793$R"


mulAdd9A_A


mulAdd9B_A&$DivSqrtRecF64_mulAddZ31.scala 573:20T2)
_T_795R


mulAdd9C_A
17
0&$DivSqrtRecF64_mulAddZ31.scala 573:61=2'
_T_796R	

0


_T_795Cat.scala 30:58Q2&
_T_797R


_T_793


_T_796&$DivSqrtRecF64_mulAddZ31.scala 573:33R2'
loMulAdd9Out_AR


_T_797
1&$DivSqrtRecF64_mulAddZ31.scala 573:33Y2.
_T_798$R"

loMulAdd9Out_A
18
18&$DivSqrtRecF64_mulAddZ31.scala 575:31U2*
_T_799 R


mulAdd9C_A
24
18&$DivSqrtRecF64_mulAddZ31.scala 576:27R2'
_T_801R


_T_799	

1&$DivSqrtRecF64_mulAddZ31.scala 576:36J2
_T_802R


_T_801
1&$DivSqrtRecF64_mulAddZ31.scala 576:36U2*
_T_803 R


mulAdd9C_A
24
18&$DivSqrtRecF64_mulAddZ31.scala 577:27[20
_T_804&2$



_T_798


_T_802


_T_803&$DivSqrtRecF64_mulAddZ31.scala 575:16X2-
_T_805#R!

loMulAdd9Out_A
17
0&$DivSqrtRecF64_mulAddZ31.scala 579:27B2,
mulAdd9Out_AR


_T_804


_T_805Cat.scala 30:58W2,
_T_806"R 

mulAdd9Out_A
19
19&$DivSqrtRecF64_mulAddZ31.scala 583:40V2+
_T_807!R

cyc_A6_sqrt


_T_806&$DivSqrtRecF64_mulAddZ31.scala 583:25K2 
_T_808R

mulAdd9Out_A&$DivSqrtRecF64_mulAddZ31.scala 584:13K2 
_T_809R	


_T_808
10&$DivSqrtRecF64_mulAddZ31.scala 584:26\21
_T_811'2%



_T_807


_T_809	

0&$DivSqrtRecF64_mulAddZ31.scala 583:12Y2.
zFractR0_A6_sqrtR


_T_811
8
0&$DivSqrtRecF64_mulAddZ31.scala 586:10O2$
_T_812R


exp_PA
0
0&$DivSqrtRecF64_mulAddZ31.scala 590:35P2%
_T_813R

mulAdd9Out_A
1&$DivSqrtRecF64_mulAddZ31.scala 590:52h2=
sqrR0_A5_sqrt,2*



_T_812


_T_813

mulAdd9Out_A&$DivSqrtRecF64_mulAddZ31.scala 590:28W2,
_T_814"R 

mulAdd9Out_A
20
20&$DivSqrtRecF64_mulAddZ31.scala 592:39U2*
_T_815 R


cyc_A4_div


_T_814&$DivSqrtRecF64_mulAddZ31.scala 592:24K2 
_T_816R

mulAdd9Out_A&$DivSqrtRecF64_mulAddZ31.scala 593:13K2 
_T_817R	


_T_816
11&$DivSqrtRecF64_mulAddZ31.scala 593:26\21
_T_819'2%



_T_815


_T_817	

0&$DivSqrtRecF64_mulAddZ31.scala 592:12X2-
zFractR0_A4_divR


_T_819
8
0&$DivSqrtRecF64_mulAddZ31.scala 595:10W2,
_T_820"R 

mulAdd9Out_A
11
11&$DivSqrtRecF64_mulAddZ31.scala 598:35Q2&
_T_821R


cyc_A2


_T_820&$DivSqrtRecF64_mulAddZ31.scala 598:20K2 
_T_822R

mulAdd9Out_A&$DivSqrtRecF64_mulAddZ31.scala 598:41J2
_T_823R	


_T_822
2&$DivSqrtRecF64_mulAddZ31.scala 598:54\21
_T_825'2%



_T_821


_T_823	

0&$DivSqrtRecF64_mulAddZ31.scala 598:12S2(

zSigma0_A2R


_T_825
8
0&$DivSqrtRecF64_mulAddZ31.scala 598:67Q2&
_T_826R	

mulAdd9Out_A
10&$DivSqrtRecF64_mulAddZ31.scala 601:36P2%
_T_827R	

mulAdd9Out_A
9&$DivSqrtRecF64_mulAddZ31.scala 601:54^23
_T_828)2'


	sqrtOp_PA


_T_826


_T_827&$DivSqrtRecF64_mulAddZ31.scala 601:12T2)

fractR1_A1R


_T_828
14
0&$DivSqrtRecF64_mulAddZ31.scala 601:58@2*
r1_A1!R	

1


fractR1_A1Cat.scala 30:58O2$
_T_830R


exp_PA
0
0&$DivSqrtRecF64_mulAddZ31.scala 603:33I2
_T_831R	

r1_A1
1&$DivSqrtRecF64_mulAddZ31.scala 603:43_24
ER1_A1_sqrt%2#



_T_830


_T_831	

r1_A1&$DivSqrtRecF64_mulAddZ31.scala 603:26Z2/
_T_832%R#

cyc_A6_sqrt


cyc_A4_div&$DivSqrtRecF64_mulAddZ31.scala 605:23æ:º



_T_832d29
_T_833/R-

zFractR0_A6_sqrt

zFractR0_A4_div&$DivSqrtRecF64_mulAddZ31.scala 606:39Fz


	fractR0_A


_T_833&$DivSqrtRecF64_mulAddZ31.scala 606:19&$DivSqrtRecF64_mulAddZ31.scala 605:38Þ:²


cyc_A5_sqrtR2'
_T_834R	

sqrR0_A5_sqrt
10&$DivSqrtRecF64_mulAddZ31.scala 610:40Kz 


hiSqrR0_A_sqrt


_T_834&$DivSqrtRecF64_mulAddZ31.scala 610:24&$DivSqrtRecF64_mulAddZ31.scala 609:24V2+
_T_835!R

cyc_A4_sqrt


cyc_A3&$DivSqrtRecF64_mulAddZ31.scala 613:23:æ



_T_835P2%
_T_836R	

mulAdd9Out_A
9&$DivSqrtRecF64_mulAddZ31.scala 616:56f2;
_T_83712/


cyc_A4_sqrt

mulAdd9Out_A


_T_836&$DivSqrtRecF64_mulAddZ31.scala 616:16P2%
_T_838R


_T_837
20
0&$DivSqrtRecF64_mulAddZ31.scala 616:60Lz!


partNegSigma0_A


_T_838&$DivSqrtRecF64_mulAddZ31.scala 615:25&$DivSqrtRecF64_mulAddZ31.scala 613:34[20
_T_839&R$

cyc_A7_sqrt

cyc_A6_sqrt&$DivSqrtRecF64_mulAddZ31.scala 620:21V2+
_T_840!R


_T_839

cyc_A5_sqrt&$DivSqrtRecF64_mulAddZ31.scala 620:36Q2&
_T_841R


_T_840


cyc_A4&$DivSqrtRecF64_mulAddZ31.scala 620:51Q2&
_T_842R


_T_841


cyc_A3&$DivSqrtRecF64_mulAddZ31.scala 620:61Q2&
_T_843R


_T_842


cyc_A2&$DivSqrtRecF64_mulAddZ31.scala 620:71Ê
:




_T_843K2 
_T_844R

mulAdd9Out_A&$DivSqrtRecF64_mulAddZ31.scala 623:40K2 
_T_845R	


_T_844
11&$DivSqrtRecF64_mulAddZ31.scala 623:53a26
_T_847,2*


cyc_A7_sqrt


_T_845	

0&$DivSqrtRecF64_mulAddZ31.scala 623:16[20
_T_848&R$


_T_847

zFractR0_A6_sqrt&$DivSqrtRecF64_mulAddZ31.scala 623:68R2'
_T_849R
	
sigB_PA
43
35&$DivSqrtRecF64_mulAddZ31.scala 625:47a26
_T_851,2*


cyc_A4_sqrt


_T_849	

0&$DivSqrtRecF64_mulAddZ31.scala 625:16Q2&
_T_852R


_T_848


_T_851&$DivSqrtRecF64_mulAddZ31.scala 624:68Y2.
_T_853$R"

zFractB_A4_div
43
35&$DivSqrtRecF64_mulAddZ31.scala 626:27Q2&
_T_854R


_T_852


_T_853&$DivSqrtRecF64_mulAddZ31.scala 625:68V2+
_T_855!R

cyc_A5_sqrt


cyc_A3&$DivSqrtRecF64_mulAddZ31.scala 627:29R2'
_T_856R
	
sigB_PA
52
44&$DivSqrtRecF64_mulAddZ31.scala 627:47\21
_T_858'2%



_T_855


_T_856	

0&$DivSqrtRecF64_mulAddZ31.scala 627:16Q2&
_T_859R


_T_854


_T_858&$DivSqrtRecF64_mulAddZ31.scala 626:68U2*
_T_860 R


_T_859


zSigma0_A2&$DivSqrtRecF64_mulAddZ31.scala 627:68Kz 


nextMulAdd9A_A


_T_860&$DivSqrtRecF64_mulAddZ31.scala 622:24%#DivSqrtRecF64_mulAddZ31.scala 621:7[20
_T_861&R$

cyc_A7_sqrt

cyc_A6_sqrt&$DivSqrtRecF64_mulAddZ31.scala 630:23V2+
_T_862!R


_T_861

cyc_A5_sqrt&$DivSqrtRecF64_mulAddZ31.scala 630:38Q2&
_T_863R


_T_862


cyc_A4&$DivSqrtRecF64_mulAddZ31.scala 630:53Q2&
_T_864R


_T_863


cyc_A2&$DivSqrtRecF64_mulAddZ31.scala 630:63û	:Ï	



_T_864Z2/
_T_865%R#

zFractB_A7_sqrt
50
42&$DivSqrtRecF64_mulAddZ31.scala 632:28[20
_T_866&R$


_T_865

zFractR0_A6_sqrt&$DivSqrtRecF64_mulAddZ31.scala 632:73V2+
_T_867!R

sqrR0_A5_sqrt
9
1&$DivSqrtRecF64_mulAddZ31.scala 634:43a26
_T_869,2*


cyc_A5_sqrt


_T_867	

0&$DivSqrtRecF64_mulAddZ31.scala 634:16Q2&
_T_870R


_T_866


_T_869&$DivSqrtRecF64_mulAddZ31.scala 633:73Z2/
_T_871%R#


_T_870

zFractR0_A4_div&$DivSqrtRecF64_mulAddZ31.scala 634:73W2,
_T_872"R 

hiSqrR0_A_sqrt
8
0&$DivSqrtRecF64_mulAddZ31.scala 636:44a26
_T_874,2*


cyc_A4_sqrt


_T_872	

0&$DivSqrtRecF64_mulAddZ31.scala 636:16Q2&
_T_875R


_T_871


_T_874&$DivSqrtRecF64_mulAddZ31.scala 635:73R2'
_T_877R

	fractR0_A
8
1&$DivSqrtRecF64_mulAddZ31.scala 637:55=2'
_T_878R	

1


_T_877Cat.scala 30:58\21
_T_880'2%



cyc_A2


_T_878	

0&$DivSqrtRecF64_mulAddZ31.scala 637:16Q2&
_T_881R


_T_875


_T_880&$DivSqrtRecF64_mulAddZ31.scala 636:73Kz 


nextMulAdd9B_A


_T_881&$DivSqrtRecF64_mulAddZ31.scala 631:24&$DivSqrtRecF64_mulAddZ31.scala 630:74:_


cyc_A1_sqrtLz!



ER1_B_sqrt

ER1_A1_sqrt&$DivSqrtRecF64_mulAddZ31.scala 641:20&$DivSqrtRecF64_mulAddZ31.scala 640:24V2+
_T_882!R


cyc_A1

cyc_B7_sqrt&$DivSqrtRecF64_mulAddZ31.scala 647:16U2*
_T_883 R


_T_882


cyc_B6_div&$DivSqrtRecF64_mulAddZ31.scala 647:31Q2&
_T_884R


_T_883


cyc_B4&$DivSqrtRecF64_mulAddZ31.scala 647:45Q2&
_T_885R


_T_884


cyc_B3&$DivSqrtRecF64_mulAddZ31.scala 647:55V2+
_T_886!R


_T_885

cyc_C6_sqrt&$DivSqrtRecF64_mulAddZ31.scala 647:65Q2&
_T_887R


_T_886


cyc_C4&$DivSqrtRecF64_mulAddZ31.scala 648:25Q2&
_T_888R


_T_887


cyc_C1&$DivSqrtRecF64_mulAddZ31.scala 648:35Sz(
:


iolatchMulAddA_0


_T_888&$DivSqrtRecF64_mulAddZ31.scala 646:23P2%
_T_889R

ER1_A1_sqrt
36&$DivSqrtRecF64_mulAddZ31.scala 650:51a26
_T_891,2*


cyc_A1_sqrt


_T_889	

0&$DivSqrtRecF64_mulAddZ31.scala 650:12Z2/
_T_892%R#

cyc_B7_sqrt


cyc_A1_div&$DivSqrtRecF64_mulAddZ31.scala 651:25]22
_T_894(2&



_T_892
	
sigB_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 651:12Q2&
_T_895R


_T_891


_T_894&$DivSqrtRecF64_mulAddZ31.scala 650:67a26
_T_897,2*



cyc_B6_div
	
sigA_PA	

0&$DivSqrtRecF64_mulAddZ31.scala 652:12Q2&
_T_898R


_T_895


_T_897&$DivSqrtRecF64_mulAddZ31.scala 651:67U2*
_T_899 R


zSigma1_B4
45
12&$DivSqrtRecF64_mulAddZ31.scala 653:19Q2&
_T_900R


_T_898


_T_899&$DivSqrtRecF64_mulAddZ31.scala 652:67V2+
_T_901!R


cyc_B3

cyc_C6_sqrt&$DivSqrtRecF64_mulAddZ31.scala 655:20W2,
_T_902"R 

sigXNU_B3_CX
57
12&$DivSqrtRecF64_mulAddZ31.scala 655:48\21
_T_904'2%



_T_901


_T_902	

0&$DivSqrtRecF64_mulAddZ31.scala 655:12Q2&
_T_905R


_T_900


_T_904&$DivSqrtRecF64_mulAddZ31.scala 653:67R2'
_T_906R
	
sigXN_C
57
25&$DivSqrtRecF64_mulAddZ31.scala 656:43K2 
_T_907R


_T_906
13&$DivSqrtRecF64_mulAddZ31.scala 656:51`25
_T_909+2)



cyc_C4_div


_T_907	

0&$DivSqrtRecF64_mulAddZ31.scala 656:12Q2&
_T_910R


_T_905


_T_909&$DivSqrtRecF64_mulAddZ31.scala 655:67M2"
_T_911R


u_C_sqrt
15&$DivSqrtRecF64_mulAddZ31.scala 657:44a26
_T_913,2*


cyc_C4_sqrt


_T_911	

0&$DivSqrtRecF64_mulAddZ31.scala 657:12Q2&
_T_914R


_T_910


_T_913&$DivSqrtRecF64_mulAddZ31.scala 656:67a26
_T_916,2*



cyc_C1_div
	
sigB_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 658:12Q2&
_T_917R


_T_914


_T_916&$DivSqrtRecF64_mulAddZ31.scala 657:67]22
_T_918(R&


_T_917

zComplSigT_C1_sqrt&$DivSqrtRecF64_mulAddZ31.scala 658:67Nz#
:


io	mulAddA_0


_T_918&$DivSqrtRecF64_mulAddZ31.scala 649:18V2+
_T_919!R


cyc_A1

cyc_B7_sqrt&$DivSqrtRecF64_mulAddZ31.scala 661:16V2+
_T_920!R


_T_919

cyc_B6_sqrt&$DivSqrtRecF64_mulAddZ31.scala 661:31Q2&
_T_921R


_T_920


cyc_B4&$DivSqrtRecF64_mulAddZ31.scala 661:46V2+
_T_922!R


_T_921

cyc_C6_sqrt&$DivSqrtRecF64_mulAddZ31.scala 661:56Q2&
_T_923R


_T_922


cyc_C4&$DivSqrtRecF64_mulAddZ31.scala 662:25Q2&
_T_924R


_T_923


cyc_C1&$DivSqrtRecF64_mulAddZ31.scala 662:35Sz(
:


iolatchMulAddB_0


_T_924&$DivSqrtRecF64_mulAddZ31.scala 660:23J2
_T_925R	

r1_A1
36&$DivSqrtRecF64_mulAddZ31.scala 664:31\21
_T_927'2%



cyc_A1


_T_925	

0&$DivSqrtRecF64_mulAddZ31.scala 664:12R2'
_T_928R

ESqrR1_B_sqrt
19&$DivSqrtRecF64_mulAddZ31.scala 665:39a26
_T_930,2*


cyc_B7_sqrt


_T_928	

0&$DivSqrtRecF64_mulAddZ31.scala 665:12Q2&
_T_931R


_T_927


_T_930&$DivSqrtRecF64_mulAddZ31.scala 664:55O2$
_T_932R


ER1_B_sqrt
36&$DivSqrtRecF64_mulAddZ31.scala 666:36a26
_T_934,2*


cyc_B6_sqrt


_T_932	

0&$DivSqrtRecF64_mulAddZ31.scala 666:12Q2&
_T_935R


_T_931


_T_934&$DivSqrtRecF64_mulAddZ31.scala 665:55U2*
_T_936 R


_T_935


zSigma1_B4&$DivSqrtRecF64_mulAddZ31.scala 666:55U2*
_T_937 R

sqrSigma1_C
30
1&$DivSqrtRecF64_mulAddZ31.scala 668:37a26
_T_939,2*


cyc_C6_sqrt


_T_937	

0&$DivSqrtRecF64_mulAddZ31.scala 668:12Q2&
_T_940R


_T_936


_T_939&$DivSqrtRecF64_mulAddZ31.scala 667:55a26
_T_942,2*



cyc_C4

sqrSigma1_C	

0&$DivSqrtRecF64_mulAddZ31.scala 669:12Q2&
_T_943R


_T_940


_T_942&$DivSqrtRecF64_mulAddZ31.scala 668:55X2-
_T_944#R!


_T_943

zComplSigT_C1&$DivSqrtRecF64_mulAddZ31.scala 669:55Nz#
:


io	mulAddB_0


_T_944&$DivSqrtRecF64_mulAddZ31.scala 663:18U2*
_T_945 R


cyc_A4


cyc_A3_div&$DivSqrtRecF64_mulAddZ31.scala 672:20U2*
_T_946 R


_T_945


cyc_A1_div&$DivSqrtRecF64_mulAddZ31.scala 672:34W2,
_T_947"R 


_T_946

cyc_B10_sqrt&$DivSqrtRecF64_mulAddZ31.scala 672:48V2+
_T_948!R


_T_947

cyc_B9_sqrt&$DivSqrtRecF64_mulAddZ31.scala 673:30V2+
_T_949!R


_T_948

cyc_B7_sqrt&$DivSqrtRecF64_mulAddZ31.scala 673:45Q2&
_T_950R


_T_949


cyc_B6&$DivSqrtRecF64_mulAddZ31.scala 673:60V2+
_T_951!R


_T_950

cyc_B5_sqrt&$DivSqrtRecF64_mulAddZ31.scala 673:70V2+
_T_952!R


_T_951

cyc_B3_sqrt&$DivSqrtRecF64_mulAddZ31.scala 674:29U2*
_T_953 R


_T_952


cyc_B2_div&$DivSqrtRecF64_mulAddZ31.scala 674:44V2+
_T_954!R


_T_953

cyc_B1_sqrt&$DivSqrtRecF64_mulAddZ31.scala 674:58Q2&
_T_955R


_T_954


cyc_C4&$DivSqrtRecF64_mulAddZ31.scala 674:73U2*
_T_956 R


cyc_A3


cyc_A2_div&$DivSqrtRecF64_mulAddZ31.scala 676:20V2+
_T_957!R


_T_956

cyc_B9_sqrt&$DivSqrtRecF64_mulAddZ31.scala 676:34V2+
_T_958!R


_T_957

cyc_B8_sqrt&$DivSqrtRecF64_mulAddZ31.scala 677:29Q2&
_T_959R


_T_958


cyc_B6&$DivSqrtRecF64_mulAddZ31.scala 677:44Q2&
_T_960R


_T_959


cyc_B5&$DivSqrtRecF64_mulAddZ31.scala 677:54V2+
_T_961!R


_T_960

cyc_B4_sqrt&$DivSqrtRecF64_mulAddZ31.scala 677:64V2+
_T_962!R


_T_961

cyc_B2_sqrt&$DivSqrtRecF64_mulAddZ31.scala 678:29U2*
_T_963 R


_T_962


cyc_B1_div&$DivSqrtRecF64_mulAddZ31.scala 678:44V2+
_T_964!R


_T_963

cyc_C6_sqrt&$DivSqrtRecF64_mulAddZ31.scala 678:58Q2&
_T_965R


_T_964


cyc_C3&$DivSqrtRecF64_mulAddZ31.scala 678:73U2*
_T_966 R


cyc_A2


cyc_A1_div&$DivSqrtRecF64_mulAddZ31.scala 680:20V2+
_T_967!R


_T_966

cyc_B8_sqrt&$DivSqrtRecF64_mulAddZ31.scala 680:34V2+
_T_968!R


_T_967

cyc_B7_sqrt&$DivSqrtRecF64_mulAddZ31.scala 681:29Q2&
_T_969R


_T_968


cyc_B5&$DivSqrtRecF64_mulAddZ31.scala 681:44Q2&
_T_970R


_T_969


cyc_B4&$DivSqrtRecF64_mulAddZ31.scala 681:54V2+
_T_971!R


_T_970

cyc_B3_sqrt&$DivSqrtRecF64_mulAddZ31.scala 681:64V2+
_T_972!R


_T_971

cyc_B1_sqrt&$DivSqrtRecF64_mulAddZ31.scala 682:29Q2&
_T_973R


_T_972


cyc_C5&$DivSqrtRecF64_mulAddZ31.scala 682:44Q2&
_T_974R


_T_973


cyc_C2&$DivSqrtRecF64_mulAddZ31.scala 682:54a26
_T_975,R*:


iolatchMulAddA_0


cyc_B6&$DivSqrtRecF64_mulAddZ31.scala 684:31V2+
_T_976!R


_T_975

cyc_B2_sqrt&$DivSqrtRecF64_mulAddZ31.scala 684:41<2&
_T_977R


_T_974


_T_976Cat.scala 30:58<2&
_T_978R


_T_955


_T_965Cat.scala 30:58<2&
_T_979R


_T_978


_T_977Cat.scala 30:58Pz%
:


iousingMulAdd


_T_979&$DivSqrtRecF64_mulAddZ31.scala 671:20L2!
_T_980R
	
sigX1_B
47&$DivSqrtRecF64_mulAddZ31.scala 688:45\21
_T_982'2%



cyc_B1


_T_980	

0&$DivSqrtRecF64_mulAddZ31.scala 688:12L2!
_T_983R
	
sigX1_B
46&$DivSqrtRecF64_mulAddZ31.scala 689:45a26
_T_985,2*


cyc_C6_sqrt


_T_983	

0&$DivSqrtRecF64_mulAddZ31.scala 689:12Q2&
_T_986R


_T_982


_T_985&$DivSqrtRecF64_mulAddZ31.scala 688:64V2+
_T_987!R

cyc_C4_sqrt


cyc_C2&$DivSqrtRecF64_mulAddZ31.scala 690:25L2!
_T_988R
	
sigXN_C
47&$DivSqrtRecF64_mulAddZ31.scala 690:45\21
_T_990'2%



_T_987


_T_988	

0&$DivSqrtRecF64_mulAddZ31.scala 690:12Q2&
_T_991R


_T_986


_T_990&$DivSqrtRecF64_mulAddZ31.scala 689:64S2(
_T_993R
	
E_E_div	

0&$DivSqrtRecF64_mulAddZ31.scala 691:27U2*
_T_994 R


cyc_E3_div


_T_993&$DivSqrtRecF64_mulAddZ31.scala 691:24P2%
_T_995R

fractA_0_PC
53&$DivSqrtRecF64_mulAddZ31.scala 691:49\21
_T_997'2%



_T_994


_T_995	

0&$DivSqrtRecF64_mulAddZ31.scala 691:12Q2&
_T_998R


_T_991


_T_997&$DivSqrtRecF64_mulAddZ31.scala 690:64O2$
_T_999R


exp_PC
0
0&$DivSqrtRecF64_mulAddZ31.scala 693:24Q2&
_T_1000R
	
sigB_PC
0
0&$DivSqrtRecF64_mulAddZ31.scala 694:29?2)
_T_1002R
	
_T_1000	

0Cat.scala 30:58Q2&
_T_1003R
	
sigB_PC
1
1&$DivSqrtRecF64_mulAddZ31.scala 695:29Q2&
_T_1004R
	
sigB_PC
0
0&$DivSqrtRecF64_mulAddZ31.scala 695:42T2)
_T_1005R
	
_T_1003
	
_T_1004&$DivSqrtRecF64_mulAddZ31.scala 695:33Q2&
_T_1006R
	
sigB_PC
0
0&$DivSqrtRecF64_mulAddZ31.scala 695:54?2)
_T_1007R
	
_T_1005
	
_T_1006Cat.scala 30:58^23
_T_1008(2&



_T_999
	
_T_1002
	
_T_1007&$DivSqrtRecF64_mulAddZ31.scala 693:17U2*
_T_1010R


extraT_E	

0&$DivSqrtRecF64_mulAddZ31.scala 696:22?2)
_T_1012R
	
_T_1010	

0Cat.scala 30:58T2)
_T_1013R
	
_T_1008
	
_T_1012&$DivSqrtRecF64_mulAddZ31.scala 696:16M2"
_T_1014R
	
_T_1013
54&$DivSqrtRecF64_mulAddZ31.scala 697:14c28
_T_1016-2+


cyc_E3_sqrt
	
_T_1014	

0&$DivSqrtRecF64_mulAddZ31.scala 692:12S2(
_T_1017R


_T_998
	
_T_1016&$DivSqrtRecF64_mulAddZ31.scala 691:64Oz$
:


io	mulAddC_2
	
_T_1017&$DivSqrtRecF64_mulAddZ31.scala 687:18j2?
ESqrR1_B8_sqrt-R+:


iomulAddResult_3
103
72&$DivSqrtRecF64_mulAddZ31.scala 701:43b27
_T_1018,R*:


iomulAddResult_3
90
45&$DivSqrtRecF64_mulAddZ31.scala 702:49G2
_T_1019R
	
_T_1018&$DivSqrtRecF64_mulAddZ31.scala 702:31^23
_T_1021(2&



cyc_B4
	
_T_1019	

0&$DivSqrtRecF64_mulAddZ31.scala 702:22Hz



zSigma1_B4
	
_T_1021&$DivSqrtRecF64_mulAddZ31.scala 702:16g2<
sqrSigma1_B1,R*:


iomulAddResult_3
79
47&$DivSqrtRecF64_mulAddZ31.scala 703:41c28
_T_1022-R+:


iomulAddResult_3
104
47&$DivSqrtRecF64_mulAddZ31.scala 704:38Jz


sigXNU_B3_CX
	
_T_1022&$DivSqrtRecF64_mulAddZ31.scala 704:18d29
_T_1023.R,:


iomulAddResult_3
104
104&$DivSqrtRecF64_mulAddZ31.scala 705:39U2*
E_C1_divR
	
_T_1023	

0&$DivSqrtRecF64_mulAddZ31.scala 705:20U2*
_T_1026R


E_C1_div	

0&$DivSqrtRecF64_mulAddZ31.scala 707:28W2,
_T_1027!R


cyc_C1_div
	
_T_1026&$DivSqrtRecF64_mulAddZ31.scala 707:25X2-
_T_1028"R 
	
_T_1027

cyc_C1_sqrt&$DivSqrtRecF64_mulAddZ31.scala 707:40c28
_T_1029-R+:


iomulAddResult_3
104
51&$DivSqrtRecF64_mulAddZ31.scala 708:31G2
_T_1030R
	
_T_1029&$DivSqrtRecF64_mulAddZ31.scala 708:13_24
_T_1032)2'

	
_T_1028
	
_T_1030	

0&$DivSqrtRecF64_mulAddZ31.scala 707:12X2-
_T_1033"R 


cyc_C1_div


E_C1_div&$DivSqrtRecF64_mulAddZ31.scala 711:24c28
_T_1035-R+:


iomulAddResult_3
102
50&$DivSqrtRecF64_mulAddZ31.scala 712:47G2
_T_1036R
	
_T_1035&$DivSqrtRecF64_mulAddZ31.scala 712:29?2)
_T_1037R	

0
	
_T_1036Cat.scala 30:58_24
_T_1039)2'

	
_T_1033
	
_T_1037	

0&$DivSqrtRecF64_mulAddZ31.scala 711:12T2)
_T_1040R
	
_T_1032
	
_T_1039&$DivSqrtRecF64_mulAddZ31.scala 710:11Kz 


zComplSigT_C1
	
_T_1040&$DivSqrtRecF64_mulAddZ31.scala 706:19c28
_T_1041-R+:


iomulAddResult_3
104
51&$DivSqrtRecF64_mulAddZ31.scala 716:44G2
_T_1042R
	
_T_1041&$DivSqrtRecF64_mulAddZ31.scala 716:26c28
_T_1044-2+


cyc_C1_sqrt
	
_T_1042	

0&$DivSqrtRecF64_mulAddZ31.scala 716:12Pz%


zComplSigT_C1_sqrt
	
_T_1044&$DivSqrtRecF64_mulAddZ31.scala 715:24M2"
sigT_C1R

zComplSigT_C1&$DivSqrtRecF64_mulAddZ31.scala 720:19a26
remT_E2+R):


iomulAddResult_3
55
0&$DivSqrtRecF64_mulAddZ31.scala 721:36:e


cyc_B8_sqrtRz'


ESqrR1_B_sqrt

ESqrR1_B8_sqrt&$DivSqrtRecF64_mulAddZ31.scala 724:23&$DivSqrtRecF64_mulAddZ31.scala 723:24:X



cyc_B3Jz

	
sigX1_B

sigXNU_B3_CX&$DivSqrtRecF64_mulAddZ31.scala 727:17&$DivSqrtRecF64_mulAddZ31.scala 726:19:\



cyc_B1Nz#


sqrSigma1_C

sqrSigma1_B1&$DivSqrtRecF64_mulAddZ31.scala 730:21&$DivSqrtRecF64_mulAddZ31.scala 729:19[20
_T_1045%R#

cyc_C6_sqrt


cyc_C5_div&$DivSqrtRecF64_mulAddZ31.scala 733:23X2-
_T_1046"R 
	
_T_1045

cyc_C3_sqrt&$DivSqrtRecF64_mulAddZ31.scala 733:37:Y

	
_T_1046Jz

	
sigXN_C

sigXNU_B3_CX&$DivSqrtRecF64_mulAddZ31.scala 734:17&$DivSqrtRecF64_mulAddZ31.scala 733:53ß:³


cyc_C5_sqrtX2-
_T_1047"R 

sigXNU_B3_CX
56
26&$DivSqrtRecF64_mulAddZ31.scala 737:33Fz



u_C_sqrt
	
_T_1047&$DivSqrtRecF64_mulAddZ31.scala 737:18&$DivSqrtRecF64_mulAddZ31.scala 736:24µ:



cyc_C1Fz

	
E_E_div


E_C1_div&$DivSqrtRecF64_mulAddZ31.scala 740:18R2'
_T_1048R
	
sigT_C1
53
1&$DivSqrtRecF64_mulAddZ31.scala 741:28Dz



sigT_E
	
_T_1048&$DivSqrtRecF64_mulAddZ31.scala 741:18Q2&
_T_1049R
	
sigT_C1
0
0&$DivSqrtRecF64_mulAddZ31.scala 742:28Fz



extraT_E
	
_T_1049&$DivSqrtRecF64_mulAddZ31.scala 742:18&$DivSqrtRecF64_mulAddZ31.scala 739:19µ:



cyc_E2S2(
_T_1050R
	
remT_E2
55
55&$DivSqrtRecF64_mulAddZ31.scala 746:47S2(
_T_1051R
	
remT_E2
53
53&$DivSqrtRecF64_mulAddZ31.scala 746:61a26
_T_1052+2)


	sqrtOp_PC
	
_T_1050
	
_T_1051&$DivSqrtRecF64_mulAddZ31.scala 746:27Iz


isNegRemT_E
	
_T_1052&$DivSqrtRecF64_mulAddZ31.scala 746:21R2'
_T_1053R
	
remT_E2
53
0&$DivSqrtRecF64_mulAddZ31.scala 748:21T2)
_T_1055R
	
_T_1053	

0&$DivSqrtRecF64_mulAddZ31.scala 748:29V2+
_T_1057 R

	sqrtOp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 749:18S2(
_T_1058R
	
remT_E2
55
54&$DivSqrtRecF64_mulAddZ31.scala 749:41T2)
_T_1060R
	
_T_1058	

0&$DivSqrtRecF64_mulAddZ31.scala 749:50T2)
_T_1061R
	
_T_1057
	
_T_1060&$DivSqrtRecF64_mulAddZ31.scala 749:30T2)
_T_1062R
	
_T_1055
	
_T_1061&$DivSqrtRecF64_mulAddZ31.scala 748:42Jz


isZeroRemT_E
	
_T_1062&$DivSqrtRecF64_mulAddZ31.scala 747:22&$DivSqrtRecF64_mulAddZ31.scala 745:19V2+
_T_1064 R

	sqrtOp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 755:13T2)
_T_1065R
	
_T_1064
	
E_E_div&$DivSqrtRecF64_mulAddZ31.scala 755:25^23
_T_1067(2&

	
_T_1065


exp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 755:12V2+
_T_1069 R

	sqrtOp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 756:13T2)
_T_1071R
	
E_E_div	

0&$DivSqrtRecF64_mulAddZ31.scala 756:28T2)
_T_1072R
	
_T_1069
	
_T_1071&$DivSqrtRecF64_mulAddZ31.scala 756:25`25
_T_1074*2(

	
_T_1072


expP1_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 756:12T2)
_T_1075R
	
_T_1067
	
_T_1074&$DivSqrtRecF64_mulAddZ31.scala 755:76K2 
_T_1076R	


exp_PC
1&$DivSqrtRecF64_mulAddZ31.scala 757:42W2,
_T_1078!R
	
_T_1076

1024&$DivSqrtRecF64_mulAddZ31.scala 757:47L2!
_T_1079R
	
_T_1078
1&$DivSqrtRecF64_mulAddZ31.scala 757:47a26
_T_1081+2)


	sqrtOp_PC
	
_T_1079	

0&$DivSqrtRecF64_mulAddZ31.scala 757:12T2)
sExpX_ER
	
_T_1075
	
_T_1081&$DivSqrtRecF64_mulAddZ31.scala 756:76T2)
	posExpX_ER
	
sExpX_E
12
0&$DivSqrtRecF64_mulAddZ31.scala 759:28;2
_T_1082R

	posExpX_Eprimitives.scala 50:21E2(
_T_1083R
	
_T_1082
12
12primitives.scala 56:25D2'
_T_1084R
	
_T_1082
11
0primitives.scala 57:26E2(
_T_1085R
	
_T_1084
11
11primitives.scala 56:25D2'
_T_1086R
	
_T_1084
10
0primitives.scala 57:26E2(
_T_1087R
	
_T_1086
10
10primitives.scala 56:25C2&
_T_1088R
	
_T_1086
9
0primitives.scala 57:26C2&
_T_1089R
	
_T_1088
9
9primitives.scala 56:25C2&
_T_1090R
	
_T_1088
8
0primitives.scala 57:26C2&
_T_1092R
	
_T_1090
8
8primitives.scala 56:25C2&
_T_1093R
	
_T_1090
7
0primitives.scala 57:26C2&
_T_1095R
	
_T_1093
7
7primitives.scala 56:25C2&
_T_1096R
	
_T_1093
6
0primitives.scala 57:26C2&
_T_1098R
	
_T_1096
6
6primitives.scala 56:25C2&
_T_1099R
	
_T_1096
5
0primitives.scala 57:26_2B
_T_11027R5$R"

18446744073709551616A
	
_T_1099primitives.scala 68:52E2(
_T_1103R
	
_T_1102
63
14primitives.scala 69:26B2'
_T_1104R
	
_T_1103
31
0Bitwise.scala 108:18A2&
_T_1107R

65535
16Bitwise.scala 101:47M22
_T_1108'R%


4294967295 
	
_T_1107Bitwise.scala 101:21=2"
_T_1109R	
	
_T_1104
16Bitwise.scala 102:21D2)
_T_1110R
	
_T_1109
	
_T_1108Bitwise.scala 102:31B2'
_T_1111R
	
_T_1104
15
0Bitwise.scala 102:46=2"
_T_1112R
	
_T_1111
16Bitwise.scala 102:6572
_T_1113R
	
_T_1108Bitwise.scala 102:77D2)
_T_1114R
	
_T_1112
	
_T_1113Bitwise.scala 102:75D2)
_T_1115R
	
_T_1110
	
_T_1114Bitwise.scala 102:39B2'
_T_1116R
	
_T_1108
23
0Bitwise.scala 101:28<2!
_T_1117R
	
_T_1116
8Bitwise.scala 101:47D2)
_T_1118R
	
_T_1108
	
_T_1117Bitwise.scala 101:21<2!
_T_1119R	
	
_T_1115
8Bitwise.scala 102:21D2)
_T_1120R
	
_T_1119
	
_T_1118Bitwise.scala 102:31B2'
_T_1121R
	
_T_1115
23
0Bitwise.scala 102:46<2!
_T_1122R
	
_T_1121
8Bitwise.scala 102:6572
_T_1123R
	
_T_1118Bitwise.scala 102:77D2)
_T_1124R
	
_T_1122
	
_T_1123Bitwise.scala 102:75D2)
_T_1125R
	
_T_1120
	
_T_1124Bitwise.scala 102:39B2'
_T_1126R
	
_T_1118
27
0Bitwise.scala 101:28<2!
_T_1127R
	
_T_1126
4Bitwise.scala 101:47D2)
_T_1128R
	
_T_1118
	
_T_1127Bitwise.scala 101:21<2!
_T_1129R	
	
_T_1125
4Bitwise.scala 102:21D2)
_T_1130R
	
_T_1129
	
_T_1128Bitwise.scala 102:31B2'
_T_1131R
	
_T_1125
27
0Bitwise.scala 102:46<2!
_T_1132R
	
_T_1131
4Bitwise.scala 102:6572
_T_1133R
	
_T_1128Bitwise.scala 102:77D2)
_T_1134R
	
_T_1132
	
_T_1133Bitwise.scala 102:75D2)
_T_1135R
	
_T_1130
	
_T_1134Bitwise.scala 102:39B2'
_T_1136R
	
_T_1128
29
0Bitwise.scala 101:28<2!
_T_1137R
	
_T_1136
2Bitwise.scala 101:47D2)
_T_1138R
	
_T_1128
	
_T_1137Bitwise.scala 101:21<2!
_T_1139R	
	
_T_1135
2Bitwise.scala 102:21D2)
_T_1140R
	
_T_1139
	
_T_1138Bitwise.scala 102:31B2'
_T_1141R
	
_T_1135
29
0Bitwise.scala 102:46<2!
_T_1142R
	
_T_1141
2Bitwise.scala 102:6572
_T_1143R
	
_T_1138Bitwise.scala 102:77D2)
_T_1144R
	
_T_1142
	
_T_1143Bitwise.scala 102:75D2)
_T_1145R
	
_T_1140
	
_T_1144Bitwise.scala 102:39B2'
_T_1146R
	
_T_1138
30
0Bitwise.scala 101:28<2!
_T_1147R
	
_T_1146
1Bitwise.scala 101:47D2)
_T_1148R
	
_T_1138
	
_T_1147Bitwise.scala 101:21<2!
_T_1149R	
	
_T_1145
1Bitwise.scala 102:21D2)
_T_1150R
	
_T_1149
	
_T_1148Bitwise.scala 102:31B2'
_T_1151R
	
_T_1145
30
0Bitwise.scala 102:46<2!
_T_1152R
	
_T_1151
1Bitwise.scala 102:6572
_T_1153R
	
_T_1148Bitwise.scala 102:77D2)
_T_1154R
	
_T_1152
	
_T_1153Bitwise.scala 102:75D2)
_T_1155R
	
_T_1150
	
_T_1154Bitwise.scala 102:39C2(
_T_1156R
	
_T_1103
49
32Bitwise.scala 108:44B2'
_T_1157R
	
_T_1156
15
0Bitwise.scala 108:18>2#
_T_1160R

255
8Bitwise.scala 101:47H2-
_T_1161"R 

65535
	
_T_1160Bitwise.scala 101:21<2!
_T_1162R	
	
_T_1157
8Bitwise.scala 102:21D2)
_T_1163R
	
_T_1162
	
_T_1161Bitwise.scala 102:31A2&
_T_1164R
	
_T_1157
7
0Bitwise.scala 102:46<2!
_T_1165R
	
_T_1164
8Bitwise.scala 102:6572
_T_1166R
	
_T_1161Bitwise.scala 102:77D2)
_T_1167R
	
_T_1165
	
_T_1166Bitwise.scala 102:75D2)
_T_1168R
	
_T_1163
	
_T_1167Bitwise.scala 102:39B2'
_T_1169R
	
_T_1161
11
0Bitwise.scala 101:28<2!
_T_1170R
	
_T_1169
4Bitwise.scala 101:47D2)
_T_1171R
	
_T_1161
	
_T_1170Bitwise.scala 101:21<2!
_T_1172R	
	
_T_1168
4Bitwise.scala 102:21D2)
_T_1173R
	
_T_1172
	
_T_1171Bitwise.scala 102:31B2'
_T_1174R
	
_T_1168
11
0Bitwise.scala 102:46<2!
_T_1175R
	
_T_1174
4Bitwise.scala 102:6572
_T_1176R
	
_T_1171Bitwise.scala 102:77D2)
_T_1177R
	
_T_1175
	
_T_1176Bitwise.scala 102:75D2)
_T_1178R
	
_T_1173
	
_T_1177Bitwise.scala 102:39B2'
_T_1179R
	
_T_1171
13
0Bitwise.scala 101:28<2!
_T_1180R
	
_T_1179
2Bitwise.scala 101:47D2)
_T_1181R
	
_T_1171
	
_T_1180Bitwise.scala 101:21<2!
_T_1182R	
	
_T_1178
2Bitwise.scala 102:21D2)
_T_1183R
	
_T_1182
	
_T_1181Bitwise.scala 102:31B2'
_T_1184R
	
_T_1178
13
0Bitwise.scala 102:46<2!
_T_1185R
	
_T_1184
2Bitwise.scala 102:6572
_T_1186R
	
_T_1181Bitwise.scala 102:77D2)
_T_1187R
	
_T_1185
	
_T_1186Bitwise.scala 102:75D2)
_T_1188R
	
_T_1183
	
_T_1187Bitwise.scala 102:39B2'
_T_1189R
	
_T_1181
14
0Bitwise.scala 101:28<2!
_T_1190R
	
_T_1189
1Bitwise.scala 101:47D2)
_T_1191R
	
_T_1181
	
_T_1190Bitwise.scala 101:21<2!
_T_1192R	
	
_T_1188
1Bitwise.scala 102:21D2)
_T_1193R
	
_T_1192
	
_T_1191Bitwise.scala 102:31B2'
_T_1194R
	
_T_1188
14
0Bitwise.scala 102:46<2!
_T_1195R
	
_T_1194
1Bitwise.scala 102:6572
_T_1196R
	
_T_1191Bitwise.scala 102:77D2)
_T_1197R
	
_T_1195
	
_T_1196Bitwise.scala 102:75D2)
_T_1198R
	
_T_1193
	
_T_1197Bitwise.scala 102:39C2(
_T_1199R
	
_T_1156
17
16Bitwise.scala 108:44A2&
_T_1200R
	
_T_1199
0
0Bitwise.scala 108:18A2&
_T_1201R
	
_T_1199
1
1Bitwise.scala 108:44?2)
_T_1202R
	
_T_1200
	
_T_1201Cat.scala 30:58?2)
_T_1203R
	
_T_1198
	
_T_1202Cat.scala 30:58?2)
_T_1204R
	
_T_1155
	
_T_1203Cat.scala 30:5892
_T_1205R
	
_T_1204primitives.scala 65:36Q24
_T_1206)2'

	
_T_1098	

0
	
_T_1205primitives.scala 65:2192
_T_1207R
	
_T_1206primitives.scala 65:1792
_T_1208R
	
_T_1207primitives.scala 65:36Q24
_T_1209)2'

	
_T_1095	

0
	
_T_1208primitives.scala 65:2192
_T_1210R
	
_T_1209primitives.scala 65:1792
_T_1211R
	
_T_1210primitives.scala 65:36Q24
_T_1212)2'

	
_T_1092	

0
	
_T_1211primitives.scala 65:2192
_T_1213R
	
_T_1212primitives.scala 65:1792
_T_1214R
	
_T_1213primitives.scala 65:36Q24
_T_1215)2'

	
_T_1089	

0
	
_T_1214primitives.scala 65:2192
_T_1216R
	
_T_1215primitives.scala 65:17?2)
_T_1218R
	
_T_1216	

7Cat.scala 30:58C2&
_T_1219R
	
_T_1088
9
9primitives.scala 56:25C2&
_T_1220R
	
_T_1088
8
0primitives.scala 57:26C2&
_T_1221R
	
_T_1220
8
8primitives.scala 56:25C2&
_T_1222R
	
_T_1220
7
0primitives.scala 57:26C2&
_T_1223R
	
_T_1222
7
7primitives.scala 56:25C2&
_T_1224R
	
_T_1222
6
0primitives.scala 57:26C2&
_T_1225R
	
_T_1224
6
6primitives.scala 56:25C2&
_T_1226R
	
_T_1224
5
0primitives.scala 57:26_2B
_T_12287R5$R"

18446744073709551616A
	
_T_1226primitives.scala 68:52C2&
_T_1229R
	
_T_1228
2
0primitives.scala 69:26A2&
_T_1230R
	
_T_1229
1
0Bitwise.scala 108:18A2&
_T_1231R
	
_T_1230
0
0Bitwise.scala 108:18A2&
_T_1232R
	
_T_1230
1
1Bitwise.scala 108:44?2)
_T_1233R
	
_T_1231
	
_T_1232Cat.scala 30:58A2&
_T_1234R
	
_T_1229
2
2Bitwise.scala 108:44?2)
_T_1235R
	
_T_1233
	
_T_1234Cat.scala 30:58Q24
_T_1237)2'

	
_T_1225
	
_T_1235	

0primitives.scala 59:20Q24
_T_1239)2'

	
_T_1223
	
_T_1237	

0primitives.scala 59:20Q24
_T_1241)2'

	
_T_1221
	
_T_1239	

0primitives.scala 59:20Q24
_T_1243)2'

	
_T_1219
	
_T_1241	

0primitives.scala 59:20Q24
_T_1244)2'

	
_T_1087
	
_T_1218
	
_T_1243primitives.scala 61:20Q24
_T_1246)2'

	
_T_1085
	
_T_1244	

0primitives.scala 59:20U28
roundMask_E)2'

	
_T_1083
	
_T_1246	

0primitives.scala 59:20C2-
_T_1249"R 	

0

roundMask_ECat.scala 30:58F2
_T_1250R
	
_T_1249%#DivSqrtRecF64_mulAddZ31.scala 763:9C2-
_T_1252"R 

roundMask_E	

1Cat.scala 30:58Z2/
incrPosMask_ER
	
_T_1250
	
_T_1252&$DivSqrtRecF64_mulAddZ31.scala 763:39R2'
_T_1253R	

incrPosMask_E
1&$DivSqrtRecF64_mulAddZ31.scala 765:51S2(
_T_1254R


sigT_E
	
_T_1253&$DivSqrtRecF64_mulAddZ31.scala 765:36]22
hiRoundPosBitT_ER
	
_T_1254	

0&$DivSqrtRecF64_mulAddZ31.scala 765:56P2%
_T_1256R	

roundMask_E
1&$DivSqrtRecF64_mulAddZ31.scala 766:55S2(
_T_1257R


sigT_E
	
_T_1256&$DivSqrtRecF64_mulAddZ31.scala 766:42a26
all0sHiRoundExtraT_ER
	
_T_1257	

0&$DivSqrtRecF64_mulAddZ31.scala 766:60F2
_T_1259R


sigT_E&$DivSqrtRecF64_mulAddZ31.scala 767:34P2%
_T_1260R	

roundMask_E
1&$DivSqrtRecF64_mulAddZ31.scala 767:55T2)
_T_1261R
	
_T_1259
	
_T_1260&$DivSqrtRecF64_mulAddZ31.scala 767:42a26
all1sHiRoundExtraT_ER
	
_T_1261	

0&$DivSqrtRecF64_mulAddZ31.scala 767:60U2*
_T_1263R

roundMask_E
0
0&$DivSqrtRecF64_mulAddZ31.scala 769:23T2)
_T_1265R
	
_T_1263	

0&$DivSqrtRecF64_mulAddZ31.scala 769:10]22
_T_1266'R%
	
_T_1265

hiRoundPosBitT_E&$DivSqrtRecF64_mulAddZ31.scala 769:27i2>
all1sHiRoundT_E+R)
	
_T_1266

all1sHiRoundExtraT_E&$DivSqrtRecF64_mulAddZ31.scala 769:48S2(
_T_1268R	

06


sigT_E&$DivSqrtRecF64_mulAddZ31.scala 773:33L2!
_T_1269R
	
_T_1268
1&$DivSqrtRecF64_mulAddZ31.scala 773:33Z2/
_T_1270$R"
	
_T_1269

roundMagUp_PC&$DivSqrtRecF64_mulAddZ31.scala 773:42N2#
	sigAdjT_ER
	
_T_1270
1&$DivSqrtRecF64_mulAddZ31.scala 773:42K2 
_T_1272R

roundMask_E&$DivSqrtRecF64_mulAddZ31.scala 774:47?2)
_T_1273R	

1
	
_T_1272Cat.scala 30:58V2+
sigY0_E R

	sigAdjT_E
	
_T_1273&$DivSqrtRecF64_mulAddZ31.scala 774:29C2-
_T_1275"R 	

0

roundMask_ECat.scala 30:58V2+
_T_1276 R

	sigAdjT_E
	
_T_1275&$DivSqrtRecF64_mulAddZ31.scala 775:30T2)
_T_1278R
	
_T_1276	

1&$DivSqrtRecF64_mulAddZ31.scala 775:62L2!
sigY1_ER
	
_T_1278
1&$DivSqrtRecF64_mulAddZ31.scala 775:62X2-
_T_1280"R 

isNegRemT_E	

0&$DivSqrtRecF64_mulAddZ31.scala 783:24Y2.
_T_1282#R!

isZeroRemT_E	

0&$DivSqrtRecF64_mulAddZ31.scala 783:41T2)
_T_1283R
	
_T_1280
	
_T_1282&$DivSqrtRecF64_mulAddZ31.scala 783:38h2=

trueLtX_E1/2-


	sqrtOp_PC
	
_T_1283

isNegRemT_E&$DivSqrtRecF64_mulAddZ31.scala 783:12U2*
_T_1284R

roundMask_E
0
0&$DivSqrtRecF64_mulAddZ31.scala 793:25W2,
_T_1286!R


trueLtX_E1	

0&$DivSqrtRecF64_mulAddZ31.scala 793:32T2)
_T_1287R
	
_T_1284
	
_T_1286&$DivSqrtRecF64_mulAddZ31.scala 793:29a26
_T_1288+R)
	
_T_1287

all1sHiRoundExtraT_E&$DivSqrtRecF64_mulAddZ31.scala 793:45U2*
_T_1289R
	
_T_1288


extraT_E&$DivSqrtRecF64_mulAddZ31.scala 793:69f2;
hiRoundPosBit_E1'R%

hiRoundPosBitT_E
	
_T_1289&$DivSqrtRecF64_mulAddZ31.scala 792:26Y2.
_T_1291#R!

isZeroRemT_E	

0&$DivSqrtRecF64_mulAddZ31.scala 795:28U2*
_T_1293R


extraT_E	

0&$DivSqrtRecF64_mulAddZ31.scala 795:44T2)
_T_1294R
	
_T_1291
	
_T_1293&$DivSqrtRecF64_mulAddZ31.scala 795:41a26
_T_1296+R)

all1sHiRoundExtraT_E	

0&$DivSqrtRecF64_mulAddZ31.scala 795:58]22
anyRoundExtra_E1R
	
_T_1294
	
_T_1296&$DivSqrtRecF64_mulAddZ31.scala 795:55o2D
_T_12979R7

roundingMode_near_even_PC

hiRoundPosBit_E1&$DivSqrtRecF64_mulAddZ31.scala 797:39]22
_T_1299'R%

anyRoundExtra_E1	

0&$DivSqrtRecF64_mulAddZ31.scala 798:17T2)
_T_1300R
	
_T_1297
	
_T_1299&$DivSqrtRecF64_mulAddZ31.scala 797:59n2C
roundEvenMask_E1/2-

	
_T_1300

incrPosMask_E	

0&$DivSqrtRecF64_mulAddZ31.scala 797:12]22
_T_1302'R%

roundMagDown_PC


extraT_E&$DivSqrtRecF64_mulAddZ31.scala 804:30W2,
_T_1304!R


trueLtX_E1	

0&$DivSqrtRecF64_mulAddZ31.scala 804:45T2)
_T_1305R
	
_T_1302
	
_T_1304&$DivSqrtRecF64_mulAddZ31.scala 804:42\21
_T_1306&R$
	
_T_1305

all1sHiRoundT_E&$DivSqrtRecF64_mulAddZ31.scala 804:58W2,
_T_1308!R


trueLtX_E1	

0&$DivSqrtRecF64_mulAddZ31.scala 806:32U2*
_T_1309R


extraT_E
	
_T_1308&$DivSqrtRecF64_mulAddZ31.scala 806:29Y2.
_T_1311#R!

isZeroRemT_E	

0&$DivSqrtRecF64_mulAddZ31.scala 806:48T2)
_T_1312R
	
_T_1309
	
_T_1311&$DivSqrtRecF64_mulAddZ31.scala 806:45\21
_T_1314&R$

all1sHiRoundT_E	

0&$DivSqrtRecF64_mulAddZ31.scala 807:23T2)
_T_1315R
	
_T_1312
	
_T_1314&$DivSqrtRecF64_mulAddZ31.scala 806:62Z2/
_T_1316$R"

roundMagUp_PC
	
_T_1315&$DivSqrtRecF64_mulAddZ31.scala 805:28T2)
_T_1317R
	
_T_1306
	
_T_1316&$DivSqrtRecF64_mulAddZ31.scala 804:78W2,
_T_1319!R


trueLtX_E1	

0&$DivSqrtRecF64_mulAddZ31.scala 810:37U2*
_T_1320R


extraT_E
	
_T_1319&$DivSqrtRecF64_mulAddZ31.scala 810:34U2*
_T_1321R

roundMask_E
0
0&$DivSqrtRecF64_mulAddZ31.scala 810:67T2)
_T_1323R
	
_T_1321	

0&$DivSqrtRecF64_mulAddZ31.scala 810:54T2)
_T_1324R
	
_T_1320
	
_T_1323&$DivSqrtRecF64_mulAddZ31.scala 810:51]22
_T_1325'R%

hiRoundPosBitT_E
	
_T_1324&$DivSqrtRecF64_mulAddZ31.scala 809:36W2,
_T_1327!R


trueLtX_E1	

0&$DivSqrtRecF64_mulAddZ31.scala 811:36U2*
_T_1328R


extraT_E
	
_T_1327&$DivSqrtRecF64_mulAddZ31.scala 811:33a26
_T_1329+R)
	
_T_1328

all1sHiRoundExtraT_E&$DivSqrtRecF64_mulAddZ31.scala 811:49T2)
_T_1330R
	
_T_1325
	
_T_1329&$DivSqrtRecF64_mulAddZ31.scala 810:72f2;
_T_13310R.

roundingMode_near_even_PC
	
_T_1330&$DivSqrtRecF64_mulAddZ31.scala 808:40T2)
_T_1332R
	
_T_1317
	
_T_1331&$DivSqrtRecF64_mulAddZ31.scala 807:43_24
_T_1333)2'

	
_T_1332
	
sigY1_E
	
sigY0_E&$DivSqrtRecF64_mulAddZ31.scala 804:12P2%
_T_1334R

roundEvenMask_E1&$DivSqrtRecF64_mulAddZ31.scala 814:13T2)
sigY_E1R
	
_T_1333
	
_T_1334&$DivSqrtRecF64_mulAddZ31.scala 814:11T2)
	fractY_E1R
	
sigY_E1
51
0&$DivSqrtRecF64_mulAddZ31.scala 815:28j2?
inexactY_E10R.

hiRoundPosBit_E1

anyRoundExtra_E1&$DivSqrtRecF64_mulAddZ31.scala 816:40S2(
_T_1335R
	
sigY_E1
53
53&$DivSqrtRecF64_mulAddZ31.scala 818:22T2)
_T_1337R
	
_T_1335	

0&$DivSqrtRecF64_mulAddZ31.scala 818:13_24
_T_1339)2'

	
_T_1337
	
sExpX_E	

0&$DivSqrtRecF64_mulAddZ31.scala 818:12S2(
_T_1340R
	
sigY_E1
53
53&$DivSqrtRecF64_mulAddZ31.scala 819:20V2+
_T_1342 R

	sqrtOp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 819:28T2)
_T_1343R
	
_T_1340
	
_T_1342&$DivSqrtRecF64_mulAddZ31.scala 819:25T2)
_T_1344R
	
_T_1343
	
E_E_div&$DivSqrtRecF64_mulAddZ31.scala 819:40`25
_T_1346*2(

	
_T_1344


expP1_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 819:12T2)
_T_1347R
	
_T_1339
	
_T_1346&$DivSqrtRecF64_mulAddZ31.scala 818:73S2(
_T_1348R
	
sigY_E1
53
53&$DivSqrtRecF64_mulAddZ31.scala 820:20V2+
_T_1350 R

	sqrtOp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 820:28T2)
_T_1351R
	
_T_1348
	
_T_1350&$DivSqrtRecF64_mulAddZ31.scala 820:25T2)
_T_1353R
	
E_E_div	

0&$DivSqrtRecF64_mulAddZ31.scala 820:43T2)
_T_1354R
	
_T_1351
	
_T_1353&$DivSqrtRecF64_mulAddZ31.scala 820:40`25
_T_1356*2(

	
_T_1354


expP2_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 820:12T2)
_T_1357R
	
_T_1347
	
_T_1356&$DivSqrtRecF64_mulAddZ31.scala 819:73S2(
_T_1358R
	
sigY_E1
53
53&$DivSqrtRecF64_mulAddZ31.scala 821:20V2+
_T_1359 R
	
_T_1358

	sqrtOp_PC&$DivSqrtRecF64_mulAddZ31.scala 821:25M2"
_T_1360R	


expP2_PC
1&$DivSqrtRecF64_mulAddZ31.scala 822:22W2,
_T_1362!R
	
_T_1360

1024&$DivSqrtRecF64_mulAddZ31.scala 822:27L2!
_T_1363R
	
_T_1362
1&$DivSqrtRecF64_mulAddZ31.scala 822:27_24
_T_1365)2'

	
_T_1359
	
_T_1363	

0&$DivSqrtRecF64_mulAddZ31.scala 821:12U2*
sExpY_E1R
	
_T_1357
	
_T_1365&$DivSqrtRecF64_mulAddZ31.scala 820:73S2(
expY_E1R


sExpY_E1
11
0&$DivSqrtRecF64_mulAddZ31.scala 825:27T2)
_T_1366R


sExpY_E1
13
13&$DivSqrtRecF64_mulAddZ31.scala 827:34T2)
_T_1368R
	
_T_1366	

0&$DivSqrtRecF64_mulAddZ31.scala 827:24T2)
_T_1370R


sExpY_E1
12
10&$DivSqrtRecF64_mulAddZ31.scala 827:70T2)
_T_1371R	

3
	
_T_1370&$DivSqrtRecF64_mulAddZ31.scala 827:59Y2.
overflowY_E1R
	
_T_1368
	
_T_1371&$DivSqrtRecF64_mulAddZ31.scala 827:39T2)
_T_1372R


sExpY_E1
13
13&$DivSqrtRecF64_mulAddZ31.scala 830:17S2(
_T_1373R


sExpY_E1
12
0&$DivSqrtRecF64_mulAddZ31.scala 830:34V2+
_T_1375 R
	
_T_1373

974&$DivSqrtRecF64_mulAddZ31.scala 830:42_24
totalUnderflowY_E1R
	
_T_1372
	
_T_1375&$DivSqrtRecF64_mulAddZ31.scala 830:22Y2.
_T_1377#R!

	posExpX_E

1025&$DivSqrtRecF64_mulAddZ31.scala 833:25X2-
_T_1378"R 
	
_T_1377

inexactY_E1&$DivSqrtRecF64_mulAddZ31.scala 833:56e2:
underflowY_E1)R'

totalUnderflowY_E1
	
_T_1378&$DivSqrtRecF64_mulAddZ31.scala 832:28V2+
_T_1380 R

	isNaNB_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 839:13W2,
_T_1382!R


isZeroB_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 839:28T2)
_T_1383R
	
_T_1380
	
_T_1382&$DivSqrtRecF64_mulAddZ31.scala 839:25T2)
_T_1384R
	
_T_1383
	
sign_PC&$DivSqrtRecF64_mulAddZ31.scala 839:41Z2/
_T_1385$R"


isZeroA_PC


isZeroB_PC&$DivSqrtRecF64_mulAddZ31.scala 840:25X2-
_T_1386"R 

	isInfA_PC

	isInfB_PC&$DivSqrtRecF64_mulAddZ31.scala 840:54T2)
_T_1387R
	
_T_1385
	
_T_1386&$DivSqrtRecF64_mulAddZ31.scala 840:40n2C
notSigNaN_invalid_PC+2)


	sqrtOp_PC
	
_T_1384
	
_T_1387&$DivSqrtRecF64_mulAddZ31.scala 838:12V2+
_T_1389 R

	sqrtOp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 843:10Y2.
_T_1390#R!
	
_T_1389

isSigNaNA_PC&$DivSqrtRecF64_mulAddZ31.scala 843:22Y2.
_T_1391#R!
	
_T_1390

isSigNaNB_PC&$DivSqrtRecF64_mulAddZ31.scala 843:39d29

invalid_PC+R)
	
_T_1391

notSigNaN_invalid_PC&$DivSqrtRecF64_mulAddZ31.scala 843:55U2+
_T_1393 R

	sqrtOp_PC	

0%#DivSqrtRecF64_mulAddZ31.scala 845:9Z2/
_T_1395$R"

isSpecialA_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 845:24T2)
_T_1396R
	
_T_1393
	
_T_1395&$DivSqrtRecF64_mulAddZ31.scala 845:21W2,
_T_1398!R


isZeroA_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 845:43T2)
_T_1399R
	
_T_1396
	
_T_1398&$DivSqrtRecF64_mulAddZ31.scala 845:40[20
infinity_PC!R
	
_T_1399


isZeroB_PC&$DivSqrtRecF64_mulAddZ31.scala 845:56c28
overflow_E1)R'

normalCase_PC

overflowY_E1&$DivSqrtRecF64_mulAddZ31.scala 847:37e2:
underflow_E1*R(

normalCase_PC

underflowY_E1&$DivSqrtRecF64_mulAddZ31.scala 848:38]22
_T_1400'R%

overflow_E1

underflow_E1&$DivSqrtRecF64_mulAddZ31.scala 852:21^23
_T_1401(R&

normalCase_PC

inexactY_E1&$DivSqrtRecF64_mulAddZ31.scala 852:55W2,

inexact_E1R
	
_T_1400
	
_T_1401&$DivSqrtRecF64_mulAddZ31.scala 852:37Y2.
_T_1402#R!


isZeroA_PC

	isInfB_PC&$DivSqrtRecF64_mulAddZ31.scala 857:24Z2/
_T_1404$R"

roundMagUp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 857:63_24
_T_1405)R'

totalUnderflowY_E1
	
_T_1404&$DivSqrtRecF64_mulAddZ31.scala 857:60T2)
_T_1406R
	
_T_1402
	
_T_1405&$DivSqrtRecF64_mulAddZ31.scala 857:37t2I
notSpecial_isZeroOut_E1.2,


	sqrtOp_PC


isZeroB_PC
	
_T_1406&$DivSqrtRecF64_mulAddZ31.scala 855:12e2:
_T_1407/R-

normalCase_PC

totalUnderflowY_E1&$DivSqrtRecF64_mulAddZ31.scala 860:23h2=
pegMinFiniteMagOut_E1$R"
	
_T_1407

roundMagUp_PC&$DivSqrtRecF64_mulAddZ31.scala 860:45d29
_T_1409.R,

overflowY_roundMagUp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 861:48f2;
pegMaxFiniteMagOut_E1"R 

overflow_E1
	
_T_1409&$DivSqrtRecF64_mulAddZ31.scala 861:45Y2.
_T_1410#R!

	isInfA_PC


isZeroB_PC&$DivSqrtRecF64_mulAddZ31.scala 865:23h2=
_T_14112R0

overflow_E1

overflowY_roundMagUp_PC&$DivSqrtRecF64_mulAddZ31.scala 865:53T2)
_T_1412R
	
_T_1410
	
_T_1411&$DivSqrtRecF64_mulAddZ31.scala 865:37n2C
notNaN_isInfOut_E1-2+


	sqrtOp_PC

	isInfB_PC
	
_T_1412&$DivSqrtRecF64_mulAddZ31.scala 863:12V2+
_T_1414 R

	sqrtOp_PC	

0&$DivSqrtRecF64_mulAddZ31.scala 868:10V2+
_T_1415 R
	
_T_1414

	isNaNA_PC&$DivSqrtRecF64_mulAddZ31.scala 868:22V2+
_T_1416 R
	
_T_1415

	isNaNB_PC&$DivSqrtRecF64_mulAddZ31.scala 868:36e2:
isNaNOut_PC+R)
	
_T_1416

notSigNaN_invalid_PC&$DivSqrtRecF64_mulAddZ31.scala 868:49W2-
_T_1418"R 

isNaNOut_PC	

0%#DivSqrtRecF64_mulAddZ31.scala 871:9W2,
_T_1419!R


isZeroB_PC
	
sign_PC&$DivSqrtRecF64_mulAddZ31.scala 871:52a26
_T_1420+2)


	sqrtOp_PC
	
_T_1419
	
sign_PC&$DivSqrtRecF64_mulAddZ31.scala 871:29W2,

signOut_PCR
	
_T_1418
	
_T_1420&$DivSqrtRecF64_mulAddZ31.scala 871:23I2
_T_1422R

511&$DivSqrtRecF64_mulAddZ31.scala 875:19o2D
_T_1424927


notSpecial_isZeroOut_E1
	
_T_1422	

0&$DivSqrtRecF64_mulAddZ31.scala 874:18G2
_T_1425R
	
_T_1424&$DivSqrtRecF64_mulAddZ31.scala 874:14T2)
_T_1426R
	
expY_E1
	
_T_1425&$DivSqrtRecF64_mulAddZ31.scala 873:18I2
_T_1428R

974&$DivSqrtRecF64_mulAddZ31.scala 879:19m2B
_T_1430725


pegMinFiniteMagOut_E1
	
_T_1428	

0&$DivSqrtRecF64_mulAddZ31.scala 878:18G2
_T_1431R
	
_T_1430&$DivSqrtRecF64_mulAddZ31.scala 878:14T2)
_T_1432R
	
_T_1426
	
_T_1431&$DivSqrtRecF64_mulAddZ31.scala 877:16J2
_T_1434R

3071&$DivSqrtRecF64_mulAddZ31.scala 883:19m2B
_T_1436725


pegMaxFiniteMagOut_E1
	
_T_1434	

0&$DivSqrtRecF64_mulAddZ31.scala 882:18G2
_T_1437R
	
_T_1436&$DivSqrtRecF64_mulAddZ31.scala 882:14T2)
_T_1438R
	
_T_1432
	
_T_1437&$DivSqrtRecF64_mulAddZ31.scala 881:16J2
_T_1440R

3583&$DivSqrtRecF64_mulAddZ31.scala 887:19j2?
_T_1442422


notNaN_isInfOut_E1
	
_T_1440	

0&$DivSqrtRecF64_mulAddZ31.scala 886:18G2
_T_1443R
	
_T_1442&$DivSqrtRecF64_mulAddZ31.scala 886:14T2)
_T_1444R
	
_T_1438
	
_T_1443&$DivSqrtRecF64_mulAddZ31.scala 885:16o2D
_T_1447927


pegMinFiniteMagOut_E1

974	

0&$DivSqrtRecF64_mulAddZ31.scala 890:16T2)
_T_1448R
	
_T_1444
	
_T_1447&$DivSqrtRecF64_mulAddZ31.scala 889:17p2E
_T_1451:28


pegMaxFiniteMagOut_E1

3071	

0&$DivSqrtRecF64_mulAddZ31.scala 891:16T2)
_T_1452R
	
_T_1448
	
_T_1451&$DivSqrtRecF64_mulAddZ31.scala 890:76m2B
_T_1455725


notNaN_isInfOut_E1

3072	

0&$DivSqrtRecF64_mulAddZ31.scala 892:16T2)
_T_1456R
	
_T_1452
	
_T_1455&$DivSqrtRecF64_mulAddZ31.scala 891:76f2;
_T_145902.


isNaNOut_PC

3584	

0&$DivSqrtRecF64_mulAddZ31.scala 893:16V2+
	expOut_E1R
	
_T_1456
	
_T_1459&$DivSqrtRecF64_mulAddZ31.scala 892:76o2D
_T_14609R7

notSpecial_isZeroOut_E1

totalUnderflowY_E1&$DivSqrtRecF64_mulAddZ31.scala 895:37X2-
_T_1461"R 
	
_T_1460

isNaNOut_PC&$DivSqrtRecF64_mulAddZ31.scala 895:59M2"
_T_1463R	

1
51&$DivSqrtRecF64_mulAddZ31.scala 896:37c28
_T_1465-2+


isNaNOut_PC
	
_T_1463	

0&$DivSqrtRecF64_mulAddZ31.scala 896:16a26
_T_1466+2)

	
_T_1461
	
_T_1465

	fractY_E1&$DivSqrtRecF64_mulAddZ31.scala 895:12N24
_T_1467)R'

pegMaxFiniteMagOut_E1
0
0Bitwise.scala 71:15]2C
_T_1470826

	
_T_1467

45035996273704954	

04Bitwise.scala 71:12X2-
fractOut_E1R
	
_T_1466
	
_T_1470&$DivSqrtRecF64_mulAddZ31.scala 898:11D2.
_T_1471#R!


signOut_PC

	expOut_E1Cat.scala 30:58C2-
_T_1472"R 
	
_T_1471

fractOut_E1Cat.scala 30:58Iz
:


ioout
	
_T_1472&$DivSqrtRecF64_mulAddZ31.scala 900:12G21
_T_1473&R$

underflow_E1


inexact_E1Cat.scala 30:58F20
_T_1474%R#


invalid_PC

infinity_PCCat.scala 30:58C2-
_T_1475"R 
	
_T_1474

overflow_E1Cat.scala 30:58?2)
_T_1476R
	
_T_1475
	
_T_1473Cat.scala 30:58Tz)
:


ioexceptionFlags
	
_T_1476&$DivSqrtRecF64_mulAddZ31.scala 902:23
ª§
Mul54
clock" 
reset

io*
val_s0


latch_a_s0

a_s0
6

latch_b_s0

b_s0
6
c_s2
i
	result_s3
i


io
 


io
 T4
val_s1
	

clock"	

0*


val_s1DivSqrtRecF64.scala 96:21T4
val_s2
	

clock"	

0*


val_s2DivSqrtRecF64.scala 97:21X8
reg_a_s1
6	

clock"	

0*


reg_a_s1DivSqrtRecF64.scala 98:23X8
reg_b_s1
6	

clock"	

0*


reg_b_s1DivSqrtRecF64.scala 99:23Y8
reg_a_s2
6	

clock"	

0*


reg_a_s2DivSqrtRecF64.scala 100:23Y8
reg_b_s2
6	

clock"	

0*


reg_b_s2DivSqrtRecF64.scala 101:23cB
reg_result_s3
i	

clock"	

0*

reg_result_s3DivSqrtRecF64.scala 102:28Az 



val_s1:


ioval_s0DivSqrtRecF64.scala 104:129z



val_s2


val_s1DivSqrtRecF64.scala 105:12²:
:


ioval_s0|:[
:


io
latch_a_s0Az 



reg_a_s1:


ioa_s0DivSqrtRecF64.scala 109:22DivSqrtRecF64.scala 108:30|:[
:


io
latch_b_s0Az 



reg_b_s1:


iob_s0DivSqrtRecF64.scala 112:22DivSqrtRecF64.scala 111:30DivSqrtRecF64.scala 107:22¬:



val_s1=z



reg_a_s2


reg_a_s1DivSqrtRecF64.scala 117:18=z



reg_b_s2


reg_b_s1DivSqrtRecF64.scala 118:18DivSqrtRecF64.scala 116:19:í



val_s2J2)
_T_23 R


reg_a_s2


reg_b_s2DivSqrtRecF64.scala 122:36E2$
_T_24R	

_T_23
104
0DivSqrtRecF64.scala 122:47K2*
_T_25!R	

_T_24:


ioc_s2DivSqrtRecF64.scala 122:55>2
_T_26R	

_T_25
1DivSqrtRecF64.scala 122:55?z


reg_result_s3	

_T_26DivSqrtRecF64.scala 122:23DivSqrtRecF64.scala 121:19Kz*
:


io	result_s3

reg_result_s3DivSqrtRecF64.scala 125:18
øvõv
RoundRawFNToRecFN_1
clock" 
reset
ï
ioæ*ã

invalidExc

infiniteExc

mine*c
sign

isNaN

isInf

isZero

sExp


sig

roundingMode

out
!
exceptionFlags



io
 


io
 l2H
roundingMode_nearest_even+R):


ioroundingMode	

0RoundRawFNToRecFN.scala 88:54f2B
roundingMode_minMag+R):


ioroundingMode	

1RoundRawFNToRecFN.scala 89:54c2?
roundingMode_min+R):


ioroundingMode	

2RoundRawFNToRecFN.scala 90:54c2?
roundingMode_max+R):


ioroundingMode	

3RoundRawFNToRecFN.scala 91:54a2=
_T_264R2

roundingMode_min:
:


ioinsignRoundRawFNToRecFN.scala 94:27X24
_T_28+R):
:


ioinsign	

0RoundRawFNToRecFN.scala 94:66R2.
_T_29%R#

roundingMode_max	

_T_28RoundRawFNToRecFN.scala 94:63L2(

roundMagUpR	

_T_26	

_T_29RoundRawFNToRecFN.scala 94:42`2<
doShiftSigDown1)R':
:


ioinsig
26
26RoundRawFNToRecFN.scala 98:36a2=
isNegExp1R/:
:


ioinsExpR	

0RoundRawFNToRecFN.scala 99:32?2%
_T_31R


isNegExp
0
0Bitwise.scala 71:15Q27
_T_34.2,
	

_T_31


33554431	

0Bitwise.scala 71:12V21
_T_35(R&:
:


ioinsExp
8
0 RoundRawFNToRecFN.scala 103:3152
_T_36R	

_T_35primitives.scala 50:21?2"
_T_37R	

_T_36
8
8primitives.scala 56:25?2"
_T_38R	

_T_36
7
0primitives.scala 57:26?2"
_T_39R	

_T_38
7
7primitives.scala 56:25?2"
_T_40R	

_T_38
6
0primitives.scala 57:26?2"
_T_41R	

_T_40
6
6primitives.scala 56:25?2"
_T_42R	

_T_40
5
0primitives.scala 57:26[2>
_T_455R3$R"

18446744073709551616A	

_T_42primitives.scala 68:52A2$
_T_46R	

_T_45
63
42primitives.scala 69:26>2#
_T_47R	

_T_46
15
0Bitwise.scala 108:18<2!
_T_50R

255
8Bitwise.scala 101:47D2)
_T_51 R

65535	

_T_50Bitwise.scala 101:2182
_T_52R		

_T_47
8Bitwise.scala 102:21>2#
_T_53R	

_T_52	

_T_51Bitwise.scala 102:31=2"
_T_54R	

_T_47
7
0Bitwise.scala 102:4682
_T_55R	

_T_54
8Bitwise.scala 102:6532
_T_56R	

_T_51Bitwise.scala 102:77>2#
_T_57R	

_T_55	

_T_56Bitwise.scala 102:75>2#
_T_58R	

_T_53	

_T_57Bitwise.scala 102:39>2#
_T_59R	

_T_51
11
0Bitwise.scala 101:2882
_T_60R	

_T_59
4Bitwise.scala 101:47>2#
_T_61R	

_T_51	

_T_60Bitwise.scala 101:2182
_T_62R		

_T_58
4Bitwise.scala 102:21>2#
_T_63R	

_T_62	

_T_61Bitwise.scala 102:31>2#
_T_64R	

_T_58
11
0Bitwise.scala 102:4682
_T_65R	

_T_64
4Bitwise.scala 102:6532
_T_66R	

_T_61Bitwise.scala 102:77>2#
_T_67R	

_T_65	

_T_66Bitwise.scala 102:75>2#
_T_68R	

_T_63	

_T_67Bitwise.scala 102:39>2#
_T_69R	

_T_61
13
0Bitwise.scala 101:2882
_T_70R	

_T_69
2Bitwise.scala 101:47>2#
_T_71R	

_T_61	

_T_70Bitwise.scala 101:2182
_T_72R		

_T_68
2Bitwise.scala 102:21>2#
_T_73R	

_T_72	

_T_71Bitwise.scala 102:31>2#
_T_74R	

_T_68
13
0Bitwise.scala 102:4682
_T_75R	

_T_74
2Bitwise.scala 102:6532
_T_76R	

_T_71Bitwise.scala 102:77>2#
_T_77R	

_T_75	

_T_76Bitwise.scala 102:75>2#
_T_78R	

_T_73	

_T_77Bitwise.scala 102:39>2#
_T_79R	

_T_71
14
0Bitwise.scala 101:2882
_T_80R	

_T_79
1Bitwise.scala 101:47>2#
_T_81R	

_T_71	

_T_80Bitwise.scala 101:2182
_T_82R		

_T_78
1Bitwise.scala 102:21>2#
_T_83R	

_T_82	

_T_81Bitwise.scala 102:31>2#
_T_84R	

_T_78
14
0Bitwise.scala 102:4682
_T_85R	

_T_84
1Bitwise.scala 102:6532
_T_86R	

_T_81Bitwise.scala 102:77>2#
_T_87R	

_T_85	

_T_86Bitwise.scala 102:75>2#
_T_88R	

_T_83	

_T_87Bitwise.scala 102:39?2$
_T_89R	

_T_46
21
16Bitwise.scala 108:44=2"
_T_90R	

_T_89
3
0Bitwise.scala 108:18=2"
_T_91R	

_T_90
1
0Bitwise.scala 108:18=2"
_T_92R	

_T_91
0
0Bitwise.scala 108:18=2"
_T_93R	

_T_91
1
1Bitwise.scala 108:4492#
_T_94R	

_T_92	

_T_93Cat.scala 30:58=2"
_T_95R	

_T_90
3
2Bitwise.scala 108:44=2"
_T_96R	

_T_95
0
0Bitwise.scala 108:18=2"
_T_97R	

_T_95
1
1Bitwise.scala 108:4492#
_T_98R	

_T_96	

_T_97Cat.scala 30:5892#
_T_99R	

_T_94	

_T_98Cat.scala 30:58>2#
_T_100R	

_T_89
5
4Bitwise.scala 108:44?2$
_T_101R


_T_100
0
0Bitwise.scala 108:18?2$
_T_102R


_T_100
1
1Bitwise.scala 108:44<2&
_T_103R


_T_101


_T_102Cat.scala 30:58;2%
_T_104R	

_T_99


_T_103Cat.scala 30:58;2%
_T_105R	

_T_88


_T_104Cat.scala 30:5872
_T_106R


_T_105primitives.scala 65:36M20
_T_107&2$
	

_T_41	

0


_T_106primitives.scala 65:2172
_T_108R


_T_107primitives.scala 65:17=2'
_T_110R


_T_108	

7Cat.scala 30:58@2#
_T_111R	

_T_40
6
6primitives.scala 56:25@2#
_T_112R	

_T_40
5
0primitives.scala 57:26]2@
_T_1146R4$R"

18446744073709551616A


_T_112primitives.scala 68:52A2$
_T_115R


_T_114
2
0primitives.scala 69:26?2$
_T_116R


_T_115
1
0Bitwise.scala 108:18?2$
_T_117R


_T_116
0
0Bitwise.scala 108:18?2$
_T_118R


_T_116
1
1Bitwise.scala 108:44<2&
_T_119R


_T_117


_T_118Cat.scala 30:58?2$
_T_120R


_T_115
2
2Bitwise.scala 108:44<2&
_T_121R


_T_119


_T_120Cat.scala 30:58N21
_T_123'2%



_T_111


_T_121	

0primitives.scala 59:20L2/
_T_124%2#
	

_T_39


_T_110


_T_123primitives.scala 61:20M20
_T_126&2$
	

_T_37


_T_124	

0primitives.scala 59:20J2%
_T_127R	

_T_34


_T_126 RoundRawFNToRecFN.scala 101:42T2/
_T_128%R#


_T_127

doShiftSigDown1 RoundRawFNToRecFN.scala 106:19@2*
	roundMaskR


_T_128	

3Cat.scala 30:58A2+
_T_130!R


isNegExp

	roundMaskCat.scala 30:58N2)
shiftedRoundMaskR	


_T_130
1 RoundRawFNToRecFN.scala 109:52I2$
_T_131R

shiftedRoundMask RoundRawFNToRecFN.scala 110:24T2/
roundPosMaskR


_T_131

	roundMask RoundRawFNToRecFN.scala 110:42^29
_T_132/R-:
:


ioinsig

roundPosMask RoundRawFNToRecFN.scala 111:34Q2,
roundPosBitR


_T_132	

0 RoundRawFNToRecFN.scala 111:50b2=
_T_1343R1:
:


ioinsig

shiftedRoundMask RoundRawFNToRecFN.scala 112:36S2.
anyRoundExtraR


_T_134	

0 RoundRawFNToRecFN.scala 112:56Y24
anyRound(R&

roundPosBit

anyRoundExtra RoundRawFNToRecFN.scala 113:32c2>
_T_1364R2

roundingMode_nearest_even

roundPosBit RoundRawFNToRecFN.scala 116:40Q2,
_T_137"R 


roundMagUp


anyRound RoundRawFNToRecFN.scala 117:29K2&
_T_138R


_T_136


_T_137 RoundRawFNToRecFN.scala 116:56[26
_T_139,R*:
:


ioinsig

	roundMask RoundRawFNToRecFN.scala 118:26D2
_T_140R	


_T_139
2 RoundRawFNToRecFN.scala 118:38L2'
_T_142R


_T_140	

1 RoundRawFNToRecFN.scala 118:43c2>
_T_1434R2

roundingMode_nearest_even

roundPosBit RoundRawFNToRecFN.scala 119:48S2.
_T_145$R"

anyRoundExtra	

0 RoundRawFNToRecFN.scala 120:26K2&
_T_146R


_T_143


_T_145 RoundRawFNToRecFN.scala 119:63G2"
_T_147R	

	roundMask
1 RoundRawFNToRecFN.scala 121:31V21
_T_149'2%



_T_146


_T_147	

0 RoundRawFNToRecFN.scala 119:21?2
_T_150R


_T_149 RoundRawFNToRecFN.scala 119:17K2&
_T_151R


_T_142


_T_150 RoundRawFNToRecFN.scala 118:55B2
_T_152R

	roundMask RoundRawFNToRecFN.scala 124:26X23
_T_153)R':
:


ioinsig


_T_152 RoundRawFNToRecFN.scala 124:24D2
_T_154R	


_T_153
2 RoundRawFNToRecFN.scala 124:37Y24

roundedSig&2$



_T_138


_T_151


_T_154 RoundRawFNToRecFN.scala 116:12I2$
_T_155R	


roundedSig
24 RoundRawFNToRecFN.scala 127:48?2
_T_156R


_T_155 RoundRawFNToRecFN.scala 127:60^29
sRoundedExp*R(:
:


ioinsExp


_T_156 RoundRawFNToRecFN.scala 127:34U20
common_expOutR

sRoundedExp
8
0 RoundRawFNToRecFN.scala 129:36N2)
_T_157R


roundedSig
23
1 RoundRawFNToRecFN.scala 132:23N2)
_T_158R


roundedSig
22
0 RoundRawFNToRecFN.scala 133:23g2B
common_fractOut/2-


doShiftSigDown1


_T_157


_T_158 RoundRawFNToRecFN.scala 131:12I2$
_T_159R	

sRoundedExp
7 RoundRawFNToRecFN.scala 136:39[26
common_overflow#R!


_T_159R	

3 RoundRawFNToRecFN.scala 136:56h2C
common_totalUnderflow*R(

sRoundedExpR

107 RoundRawFNToRecFN.scala 138:46p2K
_T_164A2?


doShiftSigDown1R

129	R

130	 RoundRawFNToRecFN.scala 142:21Y24
_T_165*R(:
:


ioinsExp


_T_164 RoundRawFNToRecFN.scala 141:25W22
common_underflowR


anyRound


_T_165 RoundRawFNToRecFN.scala 140:18h2C
isNaNOut7R5:


io
invalidExc:
:


ioinisNaN RoundRawFNToRecFN.scala 147:34w2R
notNaN_isSpecialInfOut8R6:


ioinfiniteExc:
:


ioinisInf RoundRawFNToRecFN.scala 148:49N2)
_T_167R


isNaNOut	

0 RoundRawFNToRecFN.scala 149:22\27
_T_169-R+

notNaN_isSpecialInfOut	

0 RoundRawFNToRecFN.scala 149:36K2&
_T_170R


_T_167


_T_169 RoundRawFNToRecFN.scala 149:33\27
_T_172-R+:
:


ioinisZero	

0 RoundRawFNToRecFN.scala 149:64O2*

commonCaseR


_T_170


_T_172 RoundRawFNToRecFN.scala 149:61Z25
overflow)R'


commonCase

common_overflow RoundRawFNToRecFN.scala 150:32\27
	underflow*R(


commonCase

common_underflow RoundRawFNToRecFN.scala 151:32Q2,
_T_173"R 


commonCase


anyRound RoundRawFNToRecFN.scala 152:43N2)
inexactR


overflow


_T_173 RoundRawFNToRecFN.scala 152:28o2J
overflow_roundMagUp3R1

roundingMode_nearest_even


roundMagUp RoundRawFNToRecFN.scala 154:57^29
_T_174/R-


commonCase

common_totalUnderflow RoundRawFNToRecFN.scala 155:42\27
pegMinNonzeroMagOut R


_T_174


roundMagUp RoundRawFNToRecFN.scala 155:67Q2,
_T_175"R 


commonCase


overflow RoundRawFNToRecFN.scala 156:41Y24
_T_177*R(

overflow_roundMagUp	

0 RoundRawFNToRecFN.scala 156:56W22
pegMaxFiniteMagOutR


_T_175


_T_177 RoundRawFNToRecFN.scala 156:53Z25
_T_178+R)


overflow

overflow_roundMagUp RoundRawFNToRecFN.scala 158:45d2?
notNaN_isInfOut,R*

notNaN_isSpecialInfOut


_T_178 RoundRawFNToRecFN.scala 158:32g2B
signOut725



isNaNOut	

0:
:


ioinsign RoundRawFNToRecFN.scala 160:22j2E
_T_180;R9:
:


ioinisZero

common_totalUnderflow RoundRawFNToRecFN.scala 163:32Y24
_T_183*2(



_T_180

448		

0 RoundRawFNToRecFN.scala 163:18?2
_T_184R


_T_183 RoundRawFNToRecFN.scala 163:14R2-
_T_185#R!

common_expOut


_T_184 RoundRawFNToRecFN.scala 162:24B2
_T_187R

107	 RoundRawFNToRecFN.scala 168:19c2>
_T_189422


pegMinNonzeroMagOut


_T_187	

0 RoundRawFNToRecFN.scala 167:18?2
_T_190R


_T_189 RoundRawFNToRecFN.scala 167:14K2&
_T_191R


_T_185


_T_190 RoundRawFNToRecFN.scala 166:17e2@
_T_194624


pegMaxFiniteMagOut

128		

0 RoundRawFNToRecFN.scala 171:18?2
_T_195R


_T_194 RoundRawFNToRecFN.scala 171:14K2&
_T_196R


_T_191


_T_195 RoundRawFNToRecFN.scala 170:17a2<
_T_199220


notNaN_isInfOut


64		

0 RoundRawFNToRecFN.scala 175:18?2
_T_200R


_T_199 RoundRawFNToRecFN.scala 175:14K2&
_T_201R


_T_196


_T_200 RoundRawFNToRecFN.scala 174:17f2A
_T_204725


pegMinNonzeroMagOut

107		

0 RoundRawFNToRecFN.scala 179:16K2&
_T_205R


_T_201


_T_204 RoundRawFNToRecFN.scala 178:18e2@
_T_208624


pegMaxFiniteMagOut

383		

0 RoundRawFNToRecFN.scala 183:16K2&
_T_209R


_T_205


_T_208 RoundRawFNToRecFN.scala 182:15b2=
_T_212321


notNaN_isInfOut

384		

0 RoundRawFNToRecFN.scala 187:16K2&
_T_213R


_T_209


_T_212 RoundRawFNToRecFN.scala 186:15[26
_T_216,2*



isNaNOut

448		

0 RoundRawFNToRecFN.scala 188:16K2&
expOutR


_T_213


_T_216 RoundRawFNToRecFN.scala 187:71\27
_T_217-R+

common_totalUnderflow


isNaNOut RoundRawFNToRecFN.scala 190:35F2!
_T_219R	

1
22 RoundRawFNToRecFN.scala 191:34X23
_T_221)2'



isNaNOut


_T_219	

0 RoundRawFNToRecFN.scala 191:16^29
_T_222/2-



_T_217


_T_221

common_fractOut RoundRawFNToRecFN.scala 190:12J20
_T_223&R$

pegMaxFiniteMagOut
0
0Bitwise.scala 71:15R28
_T_226.2,



_T_223
	
8388607	

0Bitwise.scala 71:12M2(
fractOutR


_T_222


_T_226 RoundRawFNToRecFN.scala 193:11=2'
_T_227R
	
signOut


expOutCat.scala 30:58>2(
_T_228R


_T_227


fractOutCat.scala 30:58Bz
:


ioout


_T_228 RoundRawFNToRecFN.scala 196:12@2*
_T_229 R

	underflow
	
inexactCat.scala 30:58U2?
_T_2305R3:


io
invalidExc:


ioinfiniteExcCat.scala 30:58>2(
_T_231R


_T_230


overflowCat.scala 30:58<2&
_T_232R


_T_231


_T_229Cat.scala 30:58Mz(
:


ioexceptionFlags


_T_232 RoundRawFNToRecFN.scala 197:23
ökók
MulAddRecFN_preMul
clock" 
reset
¬
io£* 
op

a
!
b
!
c
!
roundingMode

mulAddA

mulAddB

mulAddC
0
	toPostMulÿ*ü
highExpA

isNaN_isQuietNaNA

highExpB

isNaN_isQuietNaNB

signProd


isZeroProd

opSignC

highExpC

isNaN_isQuietNaNC

isCDominant

CAlignDist_0


CAlignDist

bit0AlignedNegSigC

highAlignedNegSigC

sExpSum

roundingMode



io
 


io
 G2(
signAR:


ioa
32
32MulAddRecFN.scala 102:22F2'
expAR:


ioa
31
23MulAddRecFN.scala 103:22G2(
fractAR:


ioa
22
0MulAddRecFN.scala 104:22@2!
_T_52R

expA
8
6MulAddRecFN.scala 105:24F2'
isZeroAR	

_T_52	

0MulAddRecFN.scala 105:49F2'
_T_55R
	
isZeroA	

0MulAddRecFN.scala 106:2092#
sigAR	

_T_55


fractACat.scala 30:58G2(
signBR:


iob
32
32MulAddRecFN.scala 108:22F2'
expBR:


iob
31
23MulAddRecFN.scala 109:22G2(
fractBR:


iob
22
0MulAddRecFN.scala 110:22@2!
_T_56R

expB
8
6MulAddRecFN.scala 111:24F2'
isZeroBR	

_T_56	

0MulAddRecFN.scala 111:49F2'
_T_59R
	
isZeroB	

0MulAddRecFN.scala 112:2092#
sigBR	

_T_59


fractBCat.scala 30:58G2(
_T_60R:


ioc
32
32MulAddRecFN.scala 114:23F2'
_T_61R:


ioop
0
0MulAddRecFN.scala 114:52D2%
opSignCR	

_T_60	

_T_61MulAddRecFN.scala 114:45F2'
expCR:


ioc
31
23MulAddRecFN.scala 115:22G2(
fractCR:


ioc
22
0MulAddRecFN.scala 116:22@2!
_T_62R

expC
8
6MulAddRecFN.scala 117:24F2'
isZeroCR	

_T_62	

0MulAddRecFN.scala 117:49F2'
_T_65R
	
isZeroC	

0MulAddRecFN.scala 118:2092#
sigCR	

_T_65


fractCCat.scala 30:58B2#
_T_66R	

signA	

signBMulAddRecFN.scala 122:26F2'
_T_67R:


ioop
1
1MulAddRecFN.scala 122:41E2&
signProdR	

_T_66	

_T_67MulAddRecFN.scala 122:34K2,

isZeroProdR
	
isZeroA
	
isZeroBMulAddRecFN.scala 123:30@2!
_T_68R

expB
8
8MulAddRecFN.scala 125:34D2%
_T_70R	

_T_68	

0MulAddRecFN.scala 125:28<2"
_T_71R	

_T_70
0
0Bitwise.scala 71:15J20
_T_74'2%
	

_T_71	

7	

0Bitwise.scala 71:12@2!
_T_75R

expB
7
0MulAddRecFN.scala 125:5192#
_T_76R	

_T_74	

_T_75Cat.scala 30:58A2"
_T_77R

expA	

_T_76MulAddRecFN.scala 125:14<2
_T_78R	

_T_77
1MulAddRecFN.scala 125:14E2&
_T_80R	

_T_78


27MulAddRecFN.scala 125:70F2'
sExpAlignedProdR	

_T_80
1MulAddRecFN.scala 125:70K2,
	doSubMagsR


signProd
	
opSignCMulAddRecFN.scala 130:30K2,
_T_81#R!

sExpAlignedProd

expCMulAddRecFN.scala 132:4272
_T_82R	

_T_81MulAddRecFN.scala 132:42E2&
sNatCAlignDistR	

_T_82
1MulAddRecFN.scala 132:42L2-
_T_83$R"

sNatCAlignDist
10
10MulAddRecFN.scala 133:56R23
CAlignDist_floorR


isZeroProd	

_T_83MulAddRecFN.scala 133:39J2+
_T_84"R 

sNatCAlignDist
9
0MulAddRecFN.scala 135:44D2%
_T_86R	

_T_84	

0MulAddRecFN.scala 135:62T25
CAlignDist_0%R#

CAlignDist_floor	

_T_86MulAddRecFN.scala 135:26E2'
_T_88R
	
isZeroC	

0MulAddRecFN.scala 137:9J2+
_T_89"R 

sNatCAlignDist
9
0MulAddRecFN.scala 139:33E2&
_T_91R	

_T_89


25MulAddRecFN.scala 139:51M2.
_T_92%R#

CAlignDist_floor	

_T_91MulAddRecFN.scala 138:31H2)
isCDominantR	

_T_88	

_T_92MulAddRecFN.scala 137:19J2+
_T_94"R 

sNatCAlignDist
9
0MulAddRecFN.scala 143:31E2&
_T_96R	

_T_94


74MulAddRecFN.scala 143:49J2+
_T_97"R 

sNatCAlignDist
6
0MulAddRecFN.scala 144:31N2/
_T_99&2$
	

_T_96	

_T_97


74MulAddRecFN.scala 143:16]2>

CAlignDist02.


CAlignDist_floor	

0	

_T_99MulAddRecFN.scala 141:12a2B
sExpSum725


CAlignDist_floor

expC

sExpAlignedProdMulAddRecFN.scala 148:22E2(
_T_100R


CAlignDist
6
6primitives.scala 56:25E2(
_T_101R


CAlignDist
5
0primitives.scala 57:26]2@
_T_1036R4$R"

18446744073709551616A


_T_101primitives.scala 68:52C2&
_T_104R


_T_103
63
54primitives.scala 69:26?2$
_T_105R


_T_104
7
0Bitwise.scala 108:18<2!
_T_108R


15
4Bitwise.scala 101:47D2)
_T_109R

255


_T_108Bitwise.scala 101:21:2
_T_110R	


_T_105
4Bitwise.scala 102:21A2&
_T_111R


_T_110


_T_109Bitwise.scala 102:31?2$
_T_112R


_T_105
3
0Bitwise.scala 102:46:2
_T_113R


_T_112
4Bitwise.scala 102:6552
_T_114R


_T_109Bitwise.scala 102:77A2&
_T_115R


_T_113


_T_114Bitwise.scala 102:75A2&
_T_116R


_T_111


_T_115Bitwise.scala 102:39?2$
_T_117R


_T_109
5
0Bitwise.scala 101:28:2
_T_118R


_T_117
2Bitwise.scala 101:47A2&
_T_119R


_T_109


_T_118Bitwise.scala 101:21:2
_T_120R	


_T_116
2Bitwise.scala 102:21A2&
_T_121R


_T_120


_T_119Bitwise.scala 102:31?2$
_T_122R


_T_116
5
0Bitwise.scala 102:46:2
_T_123R


_T_122
2Bitwise.scala 102:6552
_T_124R


_T_119Bitwise.scala 102:77A2&
_T_125R


_T_123


_T_124Bitwise.scala 102:75A2&
_T_126R


_T_121


_T_125Bitwise.scala 102:39?2$
_T_127R


_T_119
6
0Bitwise.scala 101:28:2
_T_128R


_T_127
1Bitwise.scala 101:47A2&
_T_129R


_T_119


_T_128Bitwise.scala 101:21:2
_T_130R	


_T_126
1Bitwise.scala 102:21A2&
_T_131R


_T_130


_T_129Bitwise.scala 102:31?2$
_T_132R


_T_126
6
0Bitwise.scala 102:46:2
_T_133R


_T_132
1Bitwise.scala 102:6552
_T_134R


_T_129Bitwise.scala 102:77A2&
_T_135R


_T_133


_T_134Bitwise.scala 102:75A2&
_T_136R


_T_131


_T_135Bitwise.scala 102:39?2$
_T_137R


_T_104
9
8Bitwise.scala 108:44?2$
_T_138R


_T_137
0
0Bitwise.scala 108:18?2$
_T_139R


_T_137
1
1Bitwise.scala 108:44<2&
_T_140R


_T_138


_T_139Cat.scala 30:58<2&
_T_141R


_T_136


_T_140Cat.scala 30:58A2+
_T_143!R


_T_141

16383Cat.scala 30:58]2@
_T_1456R4$R"

18446744073709551616A


_T_101primitives.scala 68:52B2%
_T_146R


_T_145
13
0primitives.scala 69:26?2$
_T_147R


_T_146
7
0Bitwise.scala 108:18<2!
_T_150R


15
4Bitwise.scala 101:47D2)
_T_151R

255


_T_150Bitwise.scala 101:21:2
_T_152R	


_T_147
4Bitwise.scala 102:21A2&
_T_153R


_T_152


_T_151Bitwise.scala 102:31?2$
_T_154R


_T_147
3
0Bitwise.scala 102:46:2
_T_155R


_T_154
4Bitwise.scala 102:6552
_T_156R


_T_151Bitwise.scala 102:77A2&
_T_157R


_T_155


_T_156Bitwise.scala 102:75A2&
_T_158R


_T_153


_T_157Bitwise.scala 102:39?2$
_T_159R


_T_151
5
0Bitwise.scala 101:28:2
_T_160R


_T_159
2Bitwise.scala 101:47A2&
_T_161R


_T_151


_T_160Bitwise.scala 101:21:2
_T_162R	


_T_158
2Bitwise.scala 102:21A2&
_T_163R


_T_162


_T_161Bitwise.scala 102:31?2$
_T_164R


_T_158
5
0Bitwise.scala 102:46:2
_T_165R


_T_164
2Bitwise.scala 102:6552
_T_166R


_T_161Bitwise.scala 102:77A2&
_T_167R


_T_165


_T_166Bitwise.scala 102:75A2&
_T_168R


_T_163


_T_167Bitwise.scala 102:39?2$
_T_169R


_T_161
6
0Bitwise.scala 101:28:2
_T_170R


_T_169
1Bitwise.scala 101:47A2&
_T_171R


_T_161


_T_170Bitwise.scala 101:21:2
_T_172R	


_T_168
1Bitwise.scala 102:21A2&
_T_173R


_T_172


_T_171Bitwise.scala 102:31?2$
_T_174R


_T_168
6
0Bitwise.scala 102:46:2
_T_175R


_T_174
1Bitwise.scala 102:6552
_T_176R


_T_171Bitwise.scala 102:77A2&
_T_177R


_T_175


_T_176Bitwise.scala 102:75A2&
_T_178R


_T_173


_T_177Bitwise.scala 102:39@2%
_T_179R


_T_146
13
8Bitwise.scala 108:44?2$
_T_180R


_T_179
3
0Bitwise.scala 108:18?2$
_T_181R


_T_180
1
0Bitwise.scala 108:18?2$
_T_182R


_T_181
0
0Bitwise.scala 108:18?2$
_T_183R


_T_181
1
1Bitwise.scala 108:44<2&
_T_184R


_T_182


_T_183Cat.scala 30:58?2$
_T_185R


_T_180
3
2Bitwise.scala 108:44?2$
_T_186R


_T_185
0
0Bitwise.scala 108:18?2$
_T_187R


_T_185
1
1Bitwise.scala 108:44<2&
_T_188R


_T_186


_T_187Cat.scala 30:58<2&
_T_189R


_T_184


_T_188Cat.scala 30:58?2$
_T_190R


_T_179
5
4Bitwise.scala 108:44?2$
_T_191R


_T_190
0
0Bitwise.scala 108:18?2$
_T_192R


_T_190
1
1Bitwise.scala 108:44<2&
_T_193R


_T_191


_T_192Cat.scala 30:58<2&
_T_194R


_T_189


_T_193Cat.scala 30:58<2&
_T_195R


_T_178


_T_194Cat.scala 30:58Q24

CExtraMask&2$



_T_100


_T_143


_T_195primitives.scala 61:2072
_T_196R

sigCMulAddRecFN.scala 151:34Q22
negSigC'2%


	doSubMags


_T_196

sigCMulAddRecFN.scala 151:22A2'
_T_197R

	doSubMags
0
0Bitwise.scala 71:15[2A
_T_200725



_T_197

11258999068426232	

02Bitwise.scala 71:12@2*
_T_201 R

	doSubMags
	
negSigCCat.scala 30:58<2&
_T_202R


_T_201


_T_200Cat.scala 30:5892
_T_203R


_T_202MulAddRecFN.scala 154:64I2*
_T_204 R


_T_203


CAlignDistMulAddRecFN.scala 154:70G2(
_T_205R

sigC


CExtraMaskMulAddRecFN.scala 156:19F2'
_T_207R


_T_205	

0MulAddRecFN.scala 156:33H2)
_T_208R


_T_207

	doSubMagsMulAddRecFN.scala 156:3702
_T_209R


_T_204Cat.scala 30:58<2&
_T_210R


_T_209


_T_208Cat.scala 30:58L2-
alignedNegSigCR


_T_210
74
0MulAddRecFN.scala 157:10>z
:


iomulAddA

sigAMulAddRecFN.scala 159:16>z
:


iomulAddB

sigBMulAddRecFN.scala 160:16L2-
_T_211#R!

alignedNegSigC
48
1MulAddRecFN.scala 161:33@z!
:


iomulAddC


_T_211MulAddRecFN.scala 161:16A2"
_T_212R

expA
8
6MulAddRecFN.scala 163:44Pz1
#:!
:


io	toPostMulhighExpA


_T_212MulAddRecFN.scala 163:37E2&
_T_213R


fractA
22
22MulAddRecFN.scala 164:46Yz:
,:*
:


io	toPostMulisNaN_isQuietNaNA


_T_213MulAddRecFN.scala 164:37A2"
_T_214R

expB
8
6MulAddRecFN.scala 165:44Pz1
#:!
:


io	toPostMulhighExpB


_T_214MulAddRecFN.scala 165:37E2&
_T_215R


fractB
22
22MulAddRecFN.scala 166:46Yz:
,:*
:


io	toPostMulisNaN_isQuietNaNB


_T_215MulAddRecFN.scala 166:37Rz3
#:!
:


io	toPostMulsignProd


signProdMulAddRecFN.scala 167:37Vz7
%:#
:


io	toPostMul
isZeroProd


isZeroProdMulAddRecFN.scala 168:37Pz1
": 
:


io	toPostMulopSignC
	
opSignCMulAddRecFN.scala 169:37A2"
_T_216R

expC
8
6MulAddRecFN.scala 170:44Pz1
#:!
:


io	toPostMulhighExpC


_T_216MulAddRecFN.scala 170:37E2&
_T_217R


fractC
22
22MulAddRecFN.scala 171:46Yz:
,:*
:


io	toPostMulisNaN_isQuietNaNC


_T_217MulAddRecFN.scala 171:37Xz9
&:$
:


io	toPostMulisCDominant

isCDominantMulAddRecFN.scala 172:37Zz;
':%
:


io	toPostMulCAlignDist_0

CAlignDist_0MulAddRecFN.scala 173:37Vz7
%:#
:


io	toPostMul
CAlignDist


CAlignDistMulAddRecFN.scala 174:37K2,
_T_218"R 

alignedNegSigC
0
0MulAddRecFN.scala 175:54Zz;
-:+
:


io	toPostMulbit0AlignedNegSigC


_T_218MulAddRecFN.scala 175:37M2.
_T_219$R"

alignedNegSigC
74
49MulAddRecFN.scala 177:23Zz;
-:+
:


io	toPostMulhighAlignedNegSigC


_T_219MulAddRecFN.scala 176:37Pz1
": 
:


io	toPostMulsExpSum
	
sExpSumMulAddRecFN.scala 178:37bzC
':%
:


io	toPostMulroundingMode:


ioroundingModeMulAddRecFN.scala 179:37
ÉïÅï
MulAddRecFN_postMul
clock" 
reset
â
ioÙ*Ö

fromPreMulÿ*ü
highExpA

isNaN_isQuietNaNA

highExpB

isNaN_isQuietNaNB

signProd


isZeroProd

opSignC

highExpC

isNaN_isQuietNaNC

isCDominant

CAlignDist_0


CAlignDist

bit0AlignedNegSigC

highAlignedNegSigC

sExpSum

roundingMode

mulAddResult
1
out
!
exceptionFlags



io
 


io
 a2B
isZeroA7R5$:"
:


io
fromPreMulhighExpA	

0MulAddRecFN.scala 207:46\2=
_T_434R2$:"
:


io
fromPreMulhighExpA
2
1MulAddRecFN.scala 208:45I2*

isSpecialAR	

_T_43	

3MulAddRecFN.scala 208:52\2=
_T_454R2$:"
:


io
fromPreMulhighExpA
0
0MulAddRecFN.scala 209:56D2%
_T_47R	

_T_45	

0MulAddRecFN.scala 209:32H2)
isInfAR


isSpecialA	

_T_47MulAddRecFN.scala 209:29\2=
_T_484R2$:"
:


io
fromPreMulhighExpA
0
0MulAddRecFN.scala 210:56H2)
isNaNAR


isSpecialA	

_T_48MulAddRecFN.scala 210:29h2I
_T_50@R>-:+
:


io
fromPreMulisNaN_isQuietNaNA	

0MulAddRecFN.scala 211:31G2(
	isSigNaNAR


isNaNA	

_T_50MulAddRecFN.scala 211:28a2B
isZeroB7R5$:"
:


io
fromPreMulhighExpB	

0MulAddRecFN.scala 213:46\2=
_T_524R2$:"
:


io
fromPreMulhighExpB
2
1MulAddRecFN.scala 214:45I2*

isSpecialBR	

_T_52	

3MulAddRecFN.scala 214:52\2=
_T_544R2$:"
:


io
fromPreMulhighExpB
0
0MulAddRecFN.scala 215:56D2%
_T_56R	

_T_54	

0MulAddRecFN.scala 215:32H2)
isInfBR


isSpecialB	

_T_56MulAddRecFN.scala 215:29\2=
_T_574R2$:"
:


io
fromPreMulhighExpB
0
0MulAddRecFN.scala 216:56H2)
isNaNBR


isSpecialB	

_T_57MulAddRecFN.scala 216:29h2I
_T_59@R>-:+
:


io
fromPreMulisNaN_isQuietNaNB	

0MulAddRecFN.scala 217:31G2(
	isSigNaNBR


isNaNB	

_T_59MulAddRecFN.scala 217:28a2B
isZeroC7R5$:"
:


io
fromPreMulhighExpC	

0MulAddRecFN.scala 219:46\2=
_T_614R2$:"
:


io
fromPreMulhighExpC
2
1MulAddRecFN.scala 220:45I2*

isSpecialCR	

_T_61	

3MulAddRecFN.scala 220:52\2=
_T_634R2$:"
:


io
fromPreMulhighExpC
0
0MulAddRecFN.scala 221:56D2%
_T_65R	

_T_63	

0MulAddRecFN.scala 221:32H2)
isInfCR


isSpecialC	

_T_65MulAddRecFN.scala 221:29\2=
_T_664R2$:"
:


io
fromPreMulhighExpC
0
0MulAddRecFN.scala 222:56H2)
isNaNCR


isSpecialC	

_T_66MulAddRecFN.scala 222:29h2I
_T_68@R>-:+
:


io
fromPreMulisNaN_isQuietNaNC	

0MulAddRecFN.scala 223:31G2(
	isSigNaNCR


isNaNC	

_T_68MulAddRecFN.scala 223:28w2X
roundingMode_nearest_even;R9(:&
:


io
fromPreMulroundingMode	

0MulAddRecFN.scala 226:37q2R
roundingMode_minMag;R9(:&
:


io
fromPreMulroundingMode	

1MulAddRecFN.scala 227:59n2O
roundingMode_min;R9(:&
:


io
fromPreMulroundingMode	

2MulAddRecFN.scala 228:59n2O
roundingMode_max;R9(:&
:


io
fromPreMulroundingMode	

3MulAddRecFN.scala 229:59i2J
signZeroNotEqOpSigns220


roundingMode_min	

1	

0MulAddRecFN.scala 231:35{2\
	doSubMagsORM$:"
:


io
fromPreMulsignProd#:!
:


io
fromPreMulopSignCMulAddRecFN.scala 232:44R23
_T_71*R(:


iomulAddResult
48
48MulAddRecFN.scala 237:32i2J
_T_73AR?.:,
:


io
fromPreMulhighAlignedNegSigC	

1MulAddRecFN.scala 238:50<2
_T_74R	

_T_73
1MulAddRecFN.scala 238:50p2Q
_T_75H2F
	

_T_71	

_T_74.:,
:


io
fromPreMulhighAlignedNegSigCMulAddRecFN.scala 237:16Q22
_T_76)R':


iomulAddResult
47
0MulAddRecFN.scala 241:2892#
_T_77R	

_T_75	

_T_76Cat.scala 30:58_2I
sigSum?R=	

_T_77.:,
:


io
fromPreMulbit0AlignedNegSigCCat.scala 30:58C2$
_T_79R


sigSum
50
1MulAddRecFN.scala 248:38D2%
_T_80R	

02	

_T_79MulAddRecFN.scala 191:27D2%
_T_81R	

02	

_T_79MulAddRecFN.scala 191:37<2
_T_82R	

_T_81
1MulAddRecFN.scala 191:41B2#
_T_83R	

_T_80	

_T_82MulAddRecFN.scala 191:32@2#
_T_85R	

_T_83
49
0primitives.scala 79:35B2$
_T_86R	

_T_85
49
32CircuitMath.scala 35:17A2#
_T_87R	

_T_85
31
0CircuitMath.scala 36:17C2%
_T_89R	

_T_86	

0CircuitMath.scala 37:22B2$
_T_90R	

_T_86
17
16CircuitMath.scala 35:17A2#
_T_91R	

_T_86
15
0CircuitMath.scala 36:17C2%
_T_93R	

_T_90	

0CircuitMath.scala 37:22?2"
_T_94R	

_T_90
1
1CircuitMath.scala 30:8A2#
_T_95R	

_T_91
15
8CircuitMath.scala 35:17@2"
_T_96R	

_T_91
7
0CircuitMath.scala 36:17C2%
_T_98R	

_T_95	

0CircuitMath.scala 37:22@2"
_T_99R	

_T_95
7
4CircuitMath.scala 35:17A2#
_T_100R	

_T_95
3
0CircuitMath.scala 36:17D2&
_T_102R	

_T_99	

0CircuitMath.scala 37:22A2#
_T_103R	

_T_99
3
3CircuitMath.scala 32:12A2#
_T_105R	

_T_99
2
2CircuitMath.scala 32:12@2#
_T_107R	

_T_99
1
1CircuitMath.scala 30:8O21
_T_108'2%



_T_105	

2


_T_107CircuitMath.scala 32:10O21
_T_109'2%



_T_103	

3


_T_108CircuitMath.scala 32:10B2$
_T_110R


_T_100
3
3CircuitMath.scala 32:12B2$
_T_112R


_T_100
2
2CircuitMath.scala 32:12A2$
_T_114R


_T_100
1
1CircuitMath.scala 30:8O21
_T_115'2%



_T_112	

2


_T_114CircuitMath.scala 32:10O21
_T_116'2%



_T_110	

3


_T_115CircuitMath.scala 32:10N20
_T_117&2$



_T_102


_T_109


_T_116CircuitMath.scala 38:21<2&
_T_118R


_T_102


_T_117Cat.scala 30:58A2#
_T_119R	

_T_96
7
4CircuitMath.scala 35:17A2#
_T_120R	

_T_96
3
0CircuitMath.scala 36:17E2'
_T_122R


_T_119	

0CircuitMath.scala 37:22B2$
_T_123R


_T_119
3
3CircuitMath.scala 32:12B2$
_T_125R


_T_119
2
2CircuitMath.scala 32:12A2$
_T_127R


_T_119
1
1CircuitMath.scala 30:8O21
_T_128'2%



_T_125	

2


_T_127CircuitMath.scala 32:10O21
_T_129'2%



_T_123	

3


_T_128CircuitMath.scala 32:10B2$
_T_130R


_T_120
3
3CircuitMath.scala 32:12B2$
_T_132R


_T_120
2
2CircuitMath.scala 32:12A2$
_T_134R


_T_120
1
1CircuitMath.scala 30:8O21
_T_135'2%



_T_132	

2


_T_134CircuitMath.scala 32:10O21
_T_136'2%



_T_130	

3


_T_135CircuitMath.scala 32:10N20
_T_137&2$



_T_122


_T_129


_T_136CircuitMath.scala 38:21<2&
_T_138R


_T_122


_T_137Cat.scala 30:58M2/
_T_139%2#
	

_T_98


_T_118


_T_138CircuitMath.scala 38:21;2%
_T_140R	

_T_98


_T_139Cat.scala 30:58L2.
_T_141$2"
	

_T_93	

_T_94


_T_140CircuitMath.scala 38:21;2%
_T_142R	

_T_93


_T_141Cat.scala 30:58C2%
_T_143R	

_T_87
31
16CircuitMath.scala 35:17B2$
_T_144R	

_T_87
15
0CircuitMath.scala 36:17E2'
_T_146R


_T_143	

0CircuitMath.scala 37:22C2%
_T_147R


_T_143
15
8CircuitMath.scala 35:17B2$
_T_148R


_T_143
7
0CircuitMath.scala 36:17E2'
_T_150R


_T_147	

0CircuitMath.scala 37:22B2$
_T_151R


_T_147
7
4CircuitMath.scala 35:17B2$
_T_152R


_T_147
3
0CircuitMath.scala 36:17E2'
_T_154R


_T_151	

0CircuitMath.scala 37:22B2$
_T_155R


_T_151
3
3CircuitMath.scala 32:12B2$
_T_157R


_T_151
2
2CircuitMath.scala 32:12A2$
_T_159R


_T_151
1
1CircuitMath.scala 30:8O21
_T_160'2%



_T_157	

2


_T_159CircuitMath.scala 32:10O21
_T_161'2%



_T_155	

3


_T_160CircuitMath.scala 32:10B2$
_T_162R


_T_152
3
3CircuitMath.scala 32:12B2$
_T_164R


_T_152
2
2CircuitMath.scala 32:12A2$
_T_166R


_T_152
1
1CircuitMath.scala 30:8O21
_T_167'2%



_T_164	

2


_T_166CircuitMath.scala 32:10O21
_T_168'2%



_T_162	

3


_T_167CircuitMath.scala 32:10N20
_T_169&2$



_T_154


_T_161


_T_168CircuitMath.scala 38:21<2&
_T_170R


_T_154


_T_169Cat.scala 30:58B2$
_T_171R


_T_148
7
4CircuitMath.scala 35:17B2$
_T_172R


_T_148
3
0CircuitMath.scala 36:17E2'
_T_174R


_T_171	

0CircuitMath.scala 37:22B2$
_T_175R


_T_171
3
3CircuitMath.scala 32:12B2$
_T_177R


_T_171
2
2CircuitMath.scala 32:12A2$
_T_179R


_T_171
1
1CircuitMath.scala 30:8O21
_T_180'2%



_T_177	

2


_T_179CircuitMath.scala 32:10O21
_T_181'2%



_T_175	

3


_T_180CircuitMath.scala 32:10B2$
_T_182R


_T_172
3
3CircuitMath.scala 32:12B2$
_T_184R


_T_172
2
2CircuitMath.scala 32:12A2$
_T_186R


_T_172
1
1CircuitMath.scala 30:8O21
_T_187'2%



_T_184	

2


_T_186CircuitMath.scala 32:10O21
_T_188'2%



_T_182	

3


_T_187CircuitMath.scala 32:10N20
_T_189&2$



_T_174


_T_181


_T_188CircuitMath.scala 38:21<2&
_T_190R


_T_174


_T_189Cat.scala 30:58N20
_T_191&2$



_T_150


_T_170


_T_190CircuitMath.scala 38:21<2&
_T_192R


_T_150


_T_191Cat.scala 30:58C2%
_T_193R


_T_144
15
8CircuitMath.scala 35:17B2$
_T_194R


_T_144
7
0CircuitMath.scala 36:17E2'
_T_196R


_T_193	

0CircuitMath.scala 37:22B2$
_T_197R


_T_193
7
4CircuitMath.scala 35:17B2$
_T_198R


_T_193
3
0CircuitMath.scala 36:17E2'
_T_200R


_T_197	

0CircuitMath.scala 37:22B2$
_T_201R


_T_197
3
3CircuitMath.scala 32:12B2$
_T_203R


_T_197
2
2CircuitMath.scala 32:12A2$
_T_205R


_T_197
1
1CircuitMath.scala 30:8O21
_T_206'2%



_T_203	

2


_T_205CircuitMath.scala 32:10O21
_T_207'2%



_T_201	

3


_T_206CircuitMath.scala 32:10B2$
_T_208R


_T_198
3
3CircuitMath.scala 32:12B2$
_T_210R


_T_198
2
2CircuitMath.scala 32:12A2$
_T_212R


_T_198
1
1CircuitMath.scala 30:8O21
_T_213'2%



_T_210	

2


_T_212CircuitMath.scala 32:10O21
_T_214'2%



_T_208	

3


_T_213CircuitMath.scala 32:10N20
_T_215&2$



_T_200


_T_207


_T_214CircuitMath.scala 38:21<2&
_T_216R


_T_200


_T_215Cat.scala 30:58B2$
_T_217R


_T_194
7
4CircuitMath.scala 35:17B2$
_T_218R


_T_194
3
0CircuitMath.scala 36:17E2'
_T_220R


_T_217	

0CircuitMath.scala 37:22B2$
_T_221R


_T_217
3
3CircuitMath.scala 32:12B2$
_T_223R


_T_217
2
2CircuitMath.scala 32:12A2$
_T_225R


_T_217
1
1CircuitMath.scala 30:8O21
_T_226'2%



_T_223	

2


_T_225CircuitMath.scala 32:10O21
_T_227'2%



_T_221	

3


_T_226CircuitMath.scala 32:10B2$
_T_228R


_T_218
3
3CircuitMath.scala 32:12B2$
_T_230R


_T_218
2
2CircuitMath.scala 32:12A2$
_T_232R


_T_218
1
1CircuitMath.scala 30:8O21
_T_233'2%



_T_230	

2


_T_232CircuitMath.scala 32:10O21
_T_234'2%



_T_228	

3


_T_233CircuitMath.scala 32:10N20
_T_235&2$



_T_220


_T_227


_T_234CircuitMath.scala 38:21<2&
_T_236R


_T_220


_T_235Cat.scala 30:58N20
_T_237&2$



_T_196


_T_216


_T_236CircuitMath.scala 38:21<2&
_T_238R


_T_196


_T_237Cat.scala 30:58N20
_T_239&2$



_T_146


_T_192


_T_238CircuitMath.scala 38:21<2&
_T_240R


_T_146


_T_239Cat.scala 30:58M2/
_T_241%2#
	

_T_89


_T_142


_T_240CircuitMath.scala 38:21;2%
_T_242R	

_T_89


_T_241Cat.scala 30:58E2(
_T_243R


73


_T_242primitives.scala 79:2572
_T_244R


_T_243primitives.scala 79:25E2(
estNormNeg_distR


_T_244
1primitives.scala 79:25E2&
_T_245R


sigSum
33
18MulAddRecFN.scala 252:19F2'
_T_247R


_T_245	

0MulAddRecFN.scala 254:15D2%
_T_248R


sigSum
17
0MulAddRecFN.scala 255:19F2'
_T_250R


_T_248	

0MulAddRecFN.scala 255:57G21
firstReduceSigSumR


_T_247


_T_250Cat.scala 30:58>2
complSigSumR


sigSumMulAddRecFN.scala 257:23J2+
_T_251!R

complSigSum
33
18MulAddRecFN.scala 259:24F2'
_T_253R


_T_251	

0MulAddRecFN.scala 261:15I2*
_T_254 R

complSigSum
17
0MulAddRecFN.scala 262:24F2'
_T_256R


_T_254	

0MulAddRecFN.scala 262:62L26
firstReduceComplSigSumR


_T_253


_T_256Cat.scala 30:58f2G
_T_257=R;(:&
:


io
fromPreMulCAlignDist_0

	doSubMagsMulAddRecFN.scala 266:40b2C
_T_2599R7&:$
:


io
fromPreMul
CAlignDist	

1MulAddRecFN.scala 268:3992
_T_260R


_T_259MulAddRecFN.scala 268:39>2
_T_261R


_T_260
1MulAddRecFN.scala 268:39C2$
_T_262R


_T_261
4
0MulAddRecFN.scala 268:49u2V
CDom_estNormDistB2@



_T_257&:$
:


io
fromPreMul
CAlignDist


_T_262MulAddRecFN.scala 266:12I2*
_T_264 R

	doSubMags	

0MulAddRecFN.scala 271:13M2.
_T_265$R"

CDom_estNormDist
4
4MulAddRecFN.scala 271:46F2'
_T_267R


_T_265	

0MulAddRecFN.scala 271:28E2&
_T_268R


_T_264


_T_267MulAddRecFN.scala 271:25E2&
_T_269R


sigSum
74
34MulAddRecFN.scala 272:23Q22
_T_271(R&

firstReduceSigSum	

0MulAddRecFN.scala 273:35<2&
_T_272R


_T_269


_T_271Cat.scala 30:58P21
_T_274'2%



_T_268


_T_272	

0MulAddRecFN.scala 271:12I2*
_T_276 R

	doSubMags	

0MulAddRecFN.scala 277:13M2.
_T_277$R"

CDom_estNormDist
4
4MulAddRecFN.scala 277:44E2&
_T_278R


_T_276


_T_277MulAddRecFN.scala 277:25E2&
_T_279R


sigSum
58
18MulAddRecFN.scala 278:23N2/
_T_280%R#

firstReduceSigSum
0
0MulAddRecFN.scala 282:34<2&
_T_281R


_T_279


_T_280Cat.scala 30:58P21
_T_283'2%



_T_278


_T_281	

0MulAddRecFN.scala 277:12E2&
_T_284R


_T_274


_T_283MulAddRecFN.scala 276:11M2.
_T_285$R"

CDom_estNormDist
4
4MulAddRecFN.scala 286:44F2'
_T_287R


_T_285	

0MulAddRecFN.scala 286:26H2)
_T_288R

	doSubMags


_T_287MulAddRecFN.scala 286:23J2+
_T_289!R

complSigSum
74
34MulAddRecFN.scala 287:28V27
_T_291-R+

firstReduceComplSigSum	

0MulAddRecFN.scala 288:40<2&
_T_292R


_T_289


_T_291Cat.scala 30:58P21
_T_294'2%



_T_288


_T_292	

0MulAddRecFN.scala 286:12E2&
_T_295R


_T_284


_T_294MulAddRecFN.scala 285:11M2.
_T_296$R"

CDom_estNormDist
4
4MulAddRecFN.scala 292:42H2)
_T_297R

	doSubMags


_T_296MulAddRecFN.scala 292:23J2+
_T_298!R

complSigSum
58
18MulAddRecFN.scala 293:28S24
_T_299*R(

firstReduceComplSigSum
0
0MulAddRecFN.scala 297:39<2&
_T_300R


_T_298


_T_299Cat.scala 30:58P21
_T_302'2%



_T_297


_T_300	

0MulAddRecFN.scala 292:12V27
CDom_firstNormAbsSigSumR


_T_295


_T_302MulAddRecFN.scala 291:11E2&
_T_303R


sigSum
50
18MulAddRecFN.scala 308:23S24
_T_304*R(

firstReduceComplSigSum
0
0MulAddRecFN.scala 310:45F2'
_T_306R


_T_304	

0MulAddRecFN.scala 310:21N2/
_T_307%R#

firstReduceSigSum
0
0MulAddRecFN.scala 311:38R23
_T_308)2'


	doSubMags


_T_306


_T_307MulAddRecFN.scala 309:20<2&
_T_309R


_T_303


_T_308Cat.scala 30:58D2%
_T_310R


sigSum
42
1MulAddRecFN.scala 314:24L2-
_T_311#R!

estNormNeg_dist
5
5MulAddRecFN.scala 338:28L2-
_T_312#R!

estNormNeg_dist
4
4MulAddRecFN.scala 339:33D2%
_T_313R


sigSum
26
1MulAddRecFN.scala 340:28A2'
_T_314R

	doSubMags
0
0Bitwise.scala 71:15P26
_T_317,2*



_T_314

65535	

0Bitwise.scala 71:12<2&
_T_318R


_T_313


_T_317Cat.scala 30:58O20
_T_319&2$



_T_312


_T_318


_T_310MulAddRecFN.scala 339:17L2-
_T_320#R!

estNormNeg_dist
4
4MulAddRecFN.scala 345:33D2%
_T_321R


sigSum
10
1MulAddRecFN.scala 347:28A2'
_T_322R

	doSubMags
0
0Bitwise.scala 71:15U2;
_T_32512/



_T_322


4294967295 	

0 Bitwise.scala 71:12<2&
_T_326R


_T_321


_T_325Cat.scala 30:58O20
_T_327&2$



_T_320


_T_309


_T_326MulAddRecFN.scala 345:17g2H
notCDom_pos_firstNormAbsSigSum&2$



_T_311


_T_319


_T_327MulAddRecFN.scala 338:12J2+
_T_328!R

complSigSum
49
18MulAddRecFN.scala 360:28S24
_T_329*R(

firstReduceComplSigSum
0
0MulAddRecFN.scala 361:39<2&
_T_330R


_T_328


_T_329Cat.scala 30:58I2*
_T_331 R

complSigSum
42
1MulAddRecFN.scala 363:29L2-
_T_332#R!

estNormNeg_dist
5
5MulAddRecFN.scala 379:28L2-
_T_333#R!

estNormNeg_dist
4
4MulAddRecFN.scala 380:33I2*
_T_334 R

complSigSum
27
1MulAddRecFN.scala 381:29?2 
_T_335R


_T_334
16MulAddRecFN.scala 381:64O20
_T_336&2$



_T_333


_T_335


_T_331MulAddRecFN.scala 380:17L2-
_T_337#R!

estNormNeg_dist
4
4MulAddRecFN.scala 385:33I2*
_T_338 R

complSigSum
11
1MulAddRecFN.scala 387:29?2 
_T_339R


_T_338
32MulAddRecFN.scala 387:64O20
_T_340&2$



_T_337


_T_330


_T_339MulAddRecFN.scala 385:17h2I
notCDom_neg_cFirstNormAbsSigSum&2$



_T_332


_T_336


_T_340MulAddRecFN.scala 379:12Q22
notCDom_signSigSumR


sigSum
51
51MulAddRecFN.scala 392:36G2(
_T_342R
	
isZeroC	

0MulAddRecFN.scala 395:26H2)
_T_343R

	doSubMags


_T_342MulAddRecFN.scala 395:23~2_
doNegSignSumO2M
':%
:


io
fromPreMulisCDominant


_T_343

notCDom_signSigSumMulAddRecFN.scala 394:12m2N
_T_344D2B


notCDom_signSigSum

estNormNeg_dist

estNormNeg_distMulAddRecFN.scala 401:16{2\
estNormDistM2K
':%
:


io
fromPreMulisCDominant

CDom_estNormDist


_T_344MulAddRecFN.scala 399:122w
_T_345m2k
':%
:


io
fromPreMulisCDominant

CDom_firstNormAbsSigSum#
!
notCDom_neg_cFirstNormAbsSigSumMulAddRecFN.scala 408:162v
_T_346l2j
':%
:


io
fromPreMulisCDominant

CDom_firstNormAbsSigSum"
 
notCDom_pos_firstNormAbsSigSumMulAddRecFN.scala 412:16h2I
cFirstNormAbsSigSum220


notCDom_signSigSum


_T_345


_T_346MulAddRecFN.scala 407:12b2D
_T_348:R8':%
:


io
fromPreMulisCDominant	

0MulAddRecFN.scala 418:9R23
_T_350)R'

notCDom_signSigSum	

0MulAddRecFN.scala 418:40E2&
_T_351R


_T_348


_T_350MulAddRecFN.scala 418:37K2,
	doIncrSigR


_T_351

	doSubMagsMulAddRecFN.scala 418:61O20
estNormDist_5R

estNormDist
3
0MulAddRecFN.scala 419:36J2+
normTo2ShiftDistR

estNormDist_5MulAddRecFN.scala 420:28X2;
_T_3531R/R

65536

normTo2ShiftDistprimitives.scala 68:52B2%
_T_354R


_T_353
15
1primitives.scala 69:26?2$
_T_355R


_T_354
7
0Bitwise.scala 108:18<2!
_T_358R


15
4Bitwise.scala 101:47D2)
_T_359R

255


_T_358Bitwise.scala 101:21:2
_T_360R	


_T_355
4Bitwise.scala 102:21A2&
_T_361R


_T_360


_T_359Bitwise.scala 102:31?2$
_T_362R


_T_355
3
0Bitwise.scala 102:46:2
_T_363R


_T_362
4Bitwise.scala 102:6552
_T_364R


_T_359Bitwise.scala 102:77A2&
_T_365R


_T_363


_T_364Bitwise.scala 102:75A2&
_T_366R


_T_361


_T_365Bitwise.scala 102:39?2$
_T_367R


_T_359
5
0Bitwise.scala 101:28:2
_T_368R


_T_367
2Bitwise.scala 101:47A2&
_T_369R


_T_359


_T_368Bitwise.scala 101:21:2
_T_370R	


_T_366
2Bitwise.scala 102:21A2&
_T_371R


_T_370


_T_369Bitwise.scala 102:31?2$
_T_372R


_T_366
5
0Bitwise.scala 102:46:2
_T_373R


_T_372
2Bitwise.scala 102:6552
_T_374R


_T_369Bitwise.scala 102:77A2&
_T_375R


_T_373


_T_374Bitwise.scala 102:75A2&
_T_376R


_T_371


_T_375Bitwise.scala 102:39?2$
_T_377R


_T_369
6
0Bitwise.scala 101:28:2
_T_378R


_T_377
1Bitwise.scala 101:47A2&
_T_379R


_T_369


_T_378Bitwise.scala 101:21:2
_T_380R	


_T_376
1Bitwise.scala 102:21A2&
_T_381R


_T_380


_T_379Bitwise.scala 102:31?2$
_T_382R


_T_376
6
0Bitwise.scala 102:46:2
_T_383R


_T_382
1Bitwise.scala 102:6552
_T_384R


_T_379Bitwise.scala 102:77A2&
_T_385R


_T_383


_T_384Bitwise.scala 102:75A2&
_T_386R


_T_381


_T_385Bitwise.scala 102:39@2%
_T_387R


_T_354
14
8Bitwise.scala 108:44?2$
_T_388R


_T_387
3
0Bitwise.scala 108:18?2$
_T_389R


_T_388
1
0Bitwise.scala 108:18?2$
_T_390R


_T_389
0
0Bitwise.scala 108:18?2$
_T_391R


_T_389
1
1Bitwise.scala 108:44<2&
_T_392R


_T_390


_T_391Cat.scala 30:58?2$
_T_393R


_T_388
3
2Bitwise.scala 108:44?2$
_T_394R


_T_393
0
0Bitwise.scala 108:18?2$
_T_395R


_T_393
1
1Bitwise.scala 108:44<2&
_T_396R


_T_394


_T_395Cat.scala 30:58<2&
_T_397R


_T_392


_T_396Cat.scala 30:58?2$
_T_398R


_T_387
6
4Bitwise.scala 108:44?2$
_T_399R


_T_398
1
0Bitwise.scala 108:18?2$
_T_400R


_T_399
0
0Bitwise.scala 108:18?2$
_T_401R


_T_399
1
1Bitwise.scala 108:44<2&
_T_402R


_T_400


_T_401Cat.scala 30:58?2$
_T_403R


_T_398
2
2Bitwise.scala 108:44<2&
_T_404R


_T_402


_T_403Cat.scala 30:58<2&
_T_405R


_T_397


_T_404Cat.scala 30:58<2&
_T_406R


_T_386


_T_405Cat.scala 30:58I23
absSigSumExtraMaskR


_T_406	

1Cat.scala 30:58Q22
_T_408(R&

cFirstNormAbsSigSum
42
1MulAddRecFN.scala 424:32O20
_T_409&R$


_T_408

normTo2ShiftDistMulAddRecFN.scala 424:65Q22
_T_410(R&

cFirstNormAbsSigSum
15
0MulAddRecFN.scala 427:3992
_T_411R


_T_410MulAddRecFN.scala 427:19Q22
_T_412(R&


_T_411

absSigSumExtraMaskMulAddRecFN.scala 427:62F2'
_T_414R


_T_412	

0MulAddRecFN.scala 428:43Q22
_T_415(R&

cFirstNormAbsSigSum
15
0MulAddRecFN.scala 430:38Q22
_T_416(R&


_T_415

absSigSumExtraMaskMulAddRecFN.scala 430:61F2'
_T_418R


_T_416	

0MulAddRecFN.scala 431:43R23
_T_419)2'


	doIncrSig


_T_414


_T_418MulAddRecFN.scala 426:16<2&
_T_420R


_T_409


_T_419Cat.scala 30:58C2$
sigX3R


_T_420
27
0MulAddRecFN.scala 434:10D2%
_T_421R	

sigX3
27
26MulAddRecFN.scala 436:29K2,
sigX3Shift1R


_T_421	

0MulAddRecFN.scala 436:58c2D
_T_423:R8#:!
:


io
fromPreMulsExpSum

estNormDistMulAddRecFN.scala 437:4092
_T_424R


_T_423MulAddRecFN.scala 437:40>2
sExpX3R


_T_424
1MulAddRecFN.scala 437:40D2%
_T_425R	

sigX3
27
25MulAddRecFN.scala 439:25G2(
isZeroYR


_T_425	

0MulAddRecFN.scala 439:54e2F
_T_427<R:$:"
:


io
fromPreMulsignProd

doNegSignSumMulAddRecFN.scala 444:36]2>
signY523

	
isZeroY

signZeroNotEqOpSigns


_T_427MulAddRecFN.scala 442:12F2'
	sExpX3_13R


sExpX3
9
0MulAddRecFN.scala 446:27E2&
_T_428R


sExpX3
10
10MulAddRecFN.scala 448:34>2$
_T_429R


_T_428
0
0Bitwise.scala 71:15T2:
_T_43202.



_T_429

	134217727	

0Bitwise.scala 71:12:2
_T_433R

	sExpX3_13primitives.scala 50:21A2$
_T_434R


_T_433
9
9primitives.scala 56:25A2$
_T_435R


_T_433
8
0primitives.scala 57:26A2$
_T_436R


_T_435
8
8primitives.scala 56:25A2$
_T_437R


_T_435
7
0primitives.scala 57:26A2$
_T_438R


_T_437
7
7primitives.scala 56:25A2$
_T_439R


_T_437
6
0primitives.scala 57:26A2$
_T_440R


_T_439
6
6primitives.scala 56:25A2$
_T_441R


_T_439
5
0primitives.scala 57:26]2@
_T_4446R4$R"

18446744073709551616A


_T_441primitives.scala 68:52C2&
_T_445R


_T_444
63
43primitives.scala 69:26@2%
_T_446R


_T_445
15
0Bitwise.scala 108:18=2"
_T_449R

255
8Bitwise.scala 101:47F2+
_T_450!R

65535


_T_449Bitwise.scala 101:21:2
_T_451R	


_T_446
8Bitwise.scala 102:21A2&
_T_452R


_T_451


_T_450Bitwise.scala 102:31?2$
_T_453R


_T_446
7
0Bitwise.scala 102:46:2
_T_454R


_T_453
8Bitwise.scala 102:6552
_T_455R


_T_450Bitwise.scala 102:77A2&
_T_456R


_T_454


_T_455Bitwise.scala 102:75A2&
_T_457R


_T_452


_T_456Bitwise.scala 102:39@2%
_T_458R


_T_450
11
0Bitwise.scala 101:28:2
_T_459R


_T_458
4Bitwise.scala 101:47A2&
_T_460R


_T_450


_T_459Bitwise.scala 101:21:2
_T_461R	


_T_457
4Bitwise.scala 102:21A2&
_T_462R


_T_461


_T_460Bitwise.scala 102:31@2%
_T_463R


_T_457
11
0Bitwise.scala 102:46:2
_T_464R


_T_463
4Bitwise.scala 102:6552
_T_465R


_T_460Bitwise.scala 102:77A2&
_T_466R


_T_464


_T_465Bitwise.scala 102:75A2&
_T_467R


_T_462


_T_466Bitwise.scala 102:39@2%
_T_468R


_T_460
13
0Bitwise.scala 101:28:2
_T_469R


_T_468
2Bitwise.scala 101:47A2&
_T_470R


_T_460


_T_469Bitwise.scala 101:21:2
_T_471R	


_T_467
2Bitwise.scala 102:21A2&
_T_472R


_T_471


_T_470Bitwise.scala 102:31@2%
_T_473R


_T_467
13
0Bitwise.scala 102:46:2
_T_474R


_T_473
2Bitwise.scala 102:6552
_T_475R


_T_470Bitwise.scala 102:77A2&
_T_476R


_T_474


_T_475Bitwise.scala 102:75A2&
_T_477R


_T_472


_T_476Bitwise.scala 102:39@2%
_T_478R


_T_470
14
0Bitwise.scala 101:28:2
_T_479R


_T_478
1Bitwise.scala 101:47A2&
_T_480R


_T_470


_T_479Bitwise.scala 101:21:2
_T_481R	


_T_477
1Bitwise.scala 102:21A2&
_T_482R


_T_481


_T_480Bitwise.scala 102:31@2%
_T_483R


_T_477
14
0Bitwise.scala 102:46:2
_T_484R


_T_483
1Bitwise.scala 102:6552
_T_485R


_T_480Bitwise.scala 102:77A2&
_T_486R


_T_484


_T_485Bitwise.scala 102:75A2&
_T_487R


_T_482


_T_486Bitwise.scala 102:39A2&
_T_488R


_T_445
20
16Bitwise.scala 108:44?2$
_T_489R


_T_488
3
0Bitwise.scala 108:18?2$
_T_490R


_T_489
1
0Bitwise.scala 108:18?2$
_T_491R


_T_490
0
0Bitwise.scala 108:18?2$
_T_492R


_T_490
1
1Bitwise.scala 108:44<2&
_T_493R


_T_491


_T_492Cat.scala 30:58?2$
_T_494R


_T_489
3
2Bitwise.scala 108:44?2$
_T_495R


_T_494
0
0Bitwise.scala 108:18?2$
_T_496R


_T_494
1
1Bitwise.scala 108:44<2&
_T_497R


_T_495


_T_496Cat.scala 30:58<2&
_T_498R


_T_493


_T_497Cat.scala 30:58?2$
_T_499R


_T_488
4
4Bitwise.scala 108:44<2&
_T_500R


_T_498


_T_499Cat.scala 30:58<2&
_T_501R


_T_487


_T_500Cat.scala 30:5872
_T_502R


_T_501primitives.scala 65:36N21
_T_503'2%



_T_440	

0


_T_502primitives.scala 65:2172
_T_504R


_T_503primitives.scala 65:17>2(
_T_506R


_T_504


15Cat.scala 30:58A2$
_T_507R


_T_439
6
6primitives.scala 56:25A2$
_T_508R


_T_439
5
0primitives.scala 57:26]2@
_T_5106R4$R"

18446744073709551616A


_T_508primitives.scala 68:52A2$
_T_511R


_T_510
3
0primitives.scala 69:26?2$
_T_512R


_T_511
1
0Bitwise.scala 108:18?2$
_T_513R


_T_512
0
0Bitwise.scala 108:18?2$
_T_514R


_T_512
1
1Bitwise.scala 108:44<2&
_T_515R


_T_513


_T_514Cat.scala 30:58?2$
_T_516R


_T_511
3
2Bitwise.scala 108:44?2$
_T_517R


_T_516
0
0Bitwise.scala 108:18?2$
_T_518R


_T_516
1
1Bitwise.scala 108:44<2&
_T_519R


_T_517


_T_518Cat.scala 30:58<2&
_T_520R


_T_515


_T_519Cat.scala 30:58N21
_T_522'2%



_T_507


_T_520	

0primitives.scala 59:20M20
_T_523&2$



_T_438


_T_506


_T_522primitives.scala 61:20N21
_T_525'2%



_T_436


_T_523	

0primitives.scala 59:20N21
_T_527'2%



_T_434


_T_525	

0primitives.scala 59:20D2%
_T_528R	

sigX3
26
26MulAddRecFN.scala 450:26E2&
_T_529R


_T_527


_T_528MulAddRecFN.scala 449:75=2'
_T_531R


_T_529	

3Cat.scala 30:58H2)
	roundMaskR


_T_432


_T_531MulAddRecFN.scala 448:50A2"
_T_532R	

	roundMask
1MulAddRecFN.scala 454:3592
_T_533R


_T_532MulAddRecFN.scala 454:24N2/
roundPosMaskR


_T_533

	roundMaskMulAddRecFN.scala 454:40J2+
_T_534!R	

sigX3

roundPosMaskMulAddRecFN.scala 455:30K2,
roundPosBitR


_T_534	

0MulAddRecFN.scala 455:46A2"
_T_536R	

	roundMask
1MulAddRecFN.scala 456:45D2%
_T_537R	

sigX3


_T_536MulAddRecFN.scala 456:34M2.
anyRoundExtraR


_T_537	

0MulAddRecFN.scala 456:5082
_T_539R	

sigX3MulAddRecFN.scala 457:27A2"
_T_540R	

	roundMask
1MulAddRecFN.scala 457:45E2&
_T_541R


_T_539


_T_540MulAddRecFN.scala 457:34M2.
allRoundExtraR


_T_541	

0MulAddRecFN.scala 457:50S24
anyRound(R&

roundPosBit

anyRoundExtraMulAddRecFN.scala 458:32S24
allRound(R&

roundPosBit

allRoundExtraMulAddRecFN.scala 459:32i2J
roundDirectUp927
	

signY

roundingMode_min

roundingMode_maxMulAddRecFN.scala 460:28I2*
_T_544 R

	doIncrSig	

0MulAddRecFN.scala 462:10X29
_T_545/R-


_T_544

roundingMode_nearest_evenMulAddRecFN.scala 462:22J2+
_T_546!R


_T_545

roundPosBitMulAddRecFN.scala 462:51L2-
_T_547#R!


_T_546

anyRoundExtraMulAddRecFN.scala 463:60I2*
_T_549 R

	doIncrSig	

0MulAddRecFN.scala 464:10L2-
_T_550#R!


_T_549

roundDirectUpMulAddRecFN.scala 464:22G2(
_T_551R


_T_550


anyRoundMulAddRecFN.scala 464:49E2&
_T_552R


_T_547


_T_551MulAddRecFN.scala 463:78J2+
_T_553!R

	doIncrSig


allRoundMulAddRecFN.scala 465:49E2&
_T_554R


_T_552


_T_553MulAddRecFN.scala 464:65[2<
_T_5552R0

	doIncrSig

roundingMode_nearest_evenMulAddRecFN.scala 466:20J2+
_T_556!R


_T_555

roundPosBitMulAddRecFN.scala 466:49E2&
_T_557R


_T_554


_T_556MulAddRecFN.scala 465:65O20
_T_558&R$

	doIncrSig

roundDirectUpMulAddRecFN.scala 467:20F2'
_T_560R


_T_558	

1MulAddRecFN.scala 467:49F2'
roundUpR


_T_557


_T_560MulAddRecFN.scala 466:65K2,
_T_562"R 

roundPosBit	

0MulAddRecFN.scala 470:42X29
_T_563/R-

roundingMode_nearest_even


_T_562MulAddRecFN.scala 470:39L2-
_T_564#R!


_T_563

allRoundExtraMulAddRecFN.scala 470:56]2>
_T_5654R2

roundingMode_nearest_even

roundPosBitMulAddRecFN.scala 471:39M2.
_T_567$R"

anyRoundExtra	

0MulAddRecFN.scala 471:59E2&
_T_568R


_T_565


_T_567MulAddRecFN.scala 471:56U26
	roundEven)2'


	doIncrSig


_T_564


_T_568MulAddRecFN.scala 469:12H2)
_T_570R


allRound	

0MulAddRecFN.scala 473:39V27
inexactY+2)


	doIncrSig


_T_570


anyRoundMulAddRecFN.scala 473:27G2(
_T_571R	

sigX3

	roundMaskMulAddRecFN.scala 475:18>2
_T_572R	


_T_571
2MulAddRecFN.scala 475:30F2'
_T_574R


_T_572	

1MulAddRecFN.scala 475:35>2
_T_575R


_T_574
1MulAddRecFN.scala 475:35K2,
roundUp_sigY3R


_T_575
25
0MulAddRecFN.scala 475:45G2(
_T_577R
	
roundUp	

0MulAddRecFN.scala 477:13I2*
_T_579 R

	roundEven	

0MulAddRecFN.scala 477:26E2&
_T_580R


_T_577


_T_579MulAddRecFN.scala 477:23<2
_T_581R

	roundMaskMulAddRecFN.scala 477:48D2%
_T_582R	

sigX3


_T_581MulAddRecFN.scala 477:46>2
_T_583R	


_T_582
2MulAddRecFN.scala 477:59P21
_T_585'2%



_T_580


_T_583	

0MulAddRecFN.scala 477:12X29
_T_587/2-

	
roundUp

roundUp_sigY3	

0MulAddRecFN.scala 478:12E2&
_T_588R


_T_585


_T_587MulAddRecFN.scala 477:79A2"
_T_589R	

	roundMask
1MulAddRecFN.scala 479:6492
_T_590R


_T_589MulAddRecFN.scala 479:53L2-
_T_591#R!

roundUp_sigY3


_T_590MulAddRecFN.scala 479:51S24
_T_593*2(


	roundEven


_T_591	

0MulAddRecFN.scala 479:12D2%
sigY3R


_T_588


_T_593MulAddRecFN.scala 478:79D2%
_T_594R	

sigY3
25
25MulAddRecFN.scala 482:18F2'
_T_596R


sExpX3	

1MulAddRecFN.scala 482:41>2
_T_597R


_T_596
1MulAddRecFN.scala 482:41P21
_T_599'2%



_T_594


_T_597	

0MulAddRecFN.scala 482:12D2%
_T_600R	

sigY3
24
24MulAddRecFN.scala 483:18P21
_T_602'2%



_T_600


sExpX3	

0MulAddRecFN.scala 483:12E2&
_T_603R


_T_599


_T_602MulAddRecFN.scala 482:61D2%
_T_604R	

sigY3
25
24MulAddRecFN.scala 484:19F2'
_T_606R


_T_604	

0MulAddRecFN.scala 484:44F2'
_T_608R


sExpX3	

1MulAddRecFN.scala 485:2092
_T_609R


_T_608MulAddRecFN.scala 485:20>2
_T_610R


_T_609
1MulAddRecFN.scala 485:20P21
_T_612'2%



_T_606


_T_610	

0MulAddRecFN.scala 484:12D2%
sExpYR


_T_603


_T_612MulAddRecFN.scala 483:61@2!
expYR	

sExpY
8
0MulAddRecFN.scala 488:21C2$
_T_613R	

sigY3
22
0MulAddRecFN.scala 490:31C2$
_T_614R	

sigY3
23
1MulAddRecFN.scala 490:55T25
fractY+2)


sigX3Shift1


_T_613


_T_614MulAddRecFN.scala 490:12B2#
_T_615R	

sExpY
9
7MulAddRecFN.scala 492:27I2*
	overflowYR


_T_615	

3MulAddRecFN.scala 492:56F2(
_T_618R
	
isZeroY	

0MulAddRecFN.scala 495:9B2#
_T_619R	

sExpY
9
9MulAddRecFN.scala 496:19B2#
_T_620R	

sExpY
8
0MulAddRecFN.scala 496:43H2)
_T_622R


_T_620

107MulAddRecFN.scala 496:57E2&
_T_623R


_T_619


_T_622MulAddRecFN.scala 496:34N2/
totalUnderflowYR


_T_618


_T_623MulAddRecFN.scala 495:19E2&
_T_624R


sExpX3
10
10MulAddRecFN.scala 499:20Z2;
_T_62712/


sigX3Shift1

130

129MulAddRecFN.scala 501:26H2)
_T_628R

	sExpX3_13


_T_627MulAddRecFN.scala 500:29E2&
_T_629R


_T_624


_T_628MulAddRecFN.scala 499:35K2,

underflowYR


inexactY


_T_629MulAddRecFN.scala 498:22N2/
_T_630%R#

roundingMode_min	

signYMulAddRecFN.scala 506:27E2&
_T_632R	

signY	

0MulAddRecFN.scala 506:61O20
_T_633&R$

roundingMode_max


_T_632MulAddRecFN.scala 506:58I2*

roundMagUpR


_T_630


_T_633MulAddRecFN.scala 506:37j2K
overflowY_roundMagUp3R1

roundingMode_nearest_even


roundMagUpMulAddRecFN.scala 507:58Q22

mulSpecial$R"


isSpecialA


isSpecialBMulAddRecFN.scala 511:33Q22

addSpecial$R"


mulSpecial


isSpecialCMulAddRecFN.scala 512:33o2P
notSpecial_addZeros9R7&:$
:


io
fromPreMul
isZeroProd
	
isZeroCMulAddRecFN.scala 513:56J2+
_T_635!R


addSpecial	

0MulAddRecFN.scala 514:22S24
_T_637*R(

notSpecial_addZeros	

0MulAddRecFN.scala 514:38I2*

commonCaseR


_T_635


_T_637MulAddRecFN.scala 514:35F2'
_T_638R


isInfA
	
isZeroBMulAddRecFN.scala 517:17F2'
_T_639R
	
isZeroA


isInfBMulAddRecFN.scala 517:41E2&
_T_640R


_T_638


_T_639MulAddRecFN.scala 517:29F2'
_T_642R


isNaNA	

0MulAddRecFN.scala 518:14F2'
_T_644R


isNaNB	

0MulAddRecFN.scala 518:26E2&
_T_645R


_T_642


_T_644MulAddRecFN.scala 518:23E2&
_T_646R


isInfA


isInfBMulAddRecFN.scala 518:46E2&
_T_647R


_T_645


_T_646MulAddRecFN.scala 518:35E2&
_T_648R


_T_647


isInfCMulAddRecFN.scala 518:57H2)
_T_649R


_T_648

	doSubMagsMulAddRecFN.scala 518:67P21
notSigNaN_invalidR


_T_640


_T_649MulAddRecFN.scala 517:52K2,
_T_650"R 

	isSigNaNA

	isSigNaNBMulAddRecFN.scala 519:29H2)
_T_651R


_T_650

	isSigNaNCMulAddRecFN.scala 519:42Q22
invalid'R%


_T_651

notSigNaN_invalidMulAddRecFN.scala 519:55N2/
overflow#R!


commonCase

	overflowYMulAddRecFN.scala 520:32P21
	underflow$R"


commonCase


underflowYMulAddRecFN.scala 521:32K2,
_T_652"R 


commonCase


inexactYMulAddRecFN.scala 522:43H2)
inexactR


overflow


_T_652MulAddRecFN.scala 522:28S24
_T_653*R(

notSpecial_addZeros
	
isZeroYMulAddRecFN.scala 525:29\2=
notSpecial_isZeroOut%R#


_T_653

totalUnderflowYMulAddRecFN.scala 525:40R23
_T_654)R'


commonCase

totalUnderflowYMulAddRecFN.scala 526:41U26
pegMinFiniteMagOut R


_T_654


roundMagUpMulAddRecFN.scala 526:60T25
_T_656+R)

overflowY_roundMagUp	

0MulAddRecFN.scala 527:42S24
pegMaxFiniteMagOutR


overflow


_T_656MulAddRecFN.scala 527:39E2&
_T_657R


isInfA


isInfBMulAddRecFN.scala 529:16E2&
_T_658R


_T_657


isInfCMulAddRecFN.scala 529:26U26
_T_659,R*


overflow

overflowY_roundMagUpMulAddRecFN.scala 529:49N2/
notNaN_isInfOutR


_T_658


_T_659MulAddRecFN.scala 529:36E2&
_T_660R


isNaNA


isNaNBMulAddRecFN.scala 530:27E2&
_T_661R


_T_660


isNaNCMulAddRecFN.scala 530:37R23
isNaNOut'R%


_T_661

notSigNaN_invalidMulAddRecFN.scala 530:47I2*
_T_663 R

	doSubMags	

0MulAddRecFN.scala 533:10^2?
_T_6645R3


_T_663#:!
:


io
fromPreMulopSignCMulAddRecFN.scala 533:51J2+
_T_666!R


isSpecialC	

0MulAddRecFN.scala 534:24I2*
_T_667 R


mulSpecial


_T_666MulAddRecFN.scala 534:21_2@
_T_6686R4


_T_667$:"
:


io
fromPreMulsignProdMulAddRecFN.scala 534:51E2&
_T_669R


_T_664


_T_668MulAddRecFN.scala 533:78J2+
_T_671!R


mulSpecial	

0MulAddRecFN.scala 535:10I2*
_T_672 R


_T_671


isSpecialCMulAddRecFN.scala 535:23^2?
_T_6735R3


_T_672#:!
:


io
fromPreMulopSignCMulAddRecFN.scala 535:51E2&
_T_674R


_T_669


_T_673MulAddRecFN.scala 534:78J2+
_T_676!R


mulSpecial	

0MulAddRecFN.scala 536:10R23
_T_677)R'


_T_676

notSpecial_addZerosMulAddRecFN.scala 536:23H2)
_T_678R


_T_677

	doSubMagsMulAddRecFN.scala 536:46S24
_T_679*R(


_T_678

signZeroNotEqOpSignsMulAddRecFN.scala 536:59R23
uncommonCaseSignOutR


_T_674


_T_679MulAddRecFN.scala 535:78H2)
_T_681R


isNaNOut	

0MulAddRecFN.scala 538:20R23
_T_682)R'


_T_681

uncommonCaseSignOutMulAddRecFN.scala 538:31H2)
_T_683R


commonCase	

signYMulAddRecFN.scala 538:70F2'
signOutR


_T_682


_T_683MulAddRecFN.scala 538:55a2B
_T_686826


notSpecial_isZeroOut

448		

0	MulAddRecFN.scala 541:1892
_T_687R


_T_686MulAddRecFN.scala 541:14C2$
_T_688R

expY


_T_687MulAddRecFN.scala 540:15<2
_T_690R

107	MulAddRecFN.scala 546:19\2=
_T_692321


pegMinFiniteMagOut


_T_690	

0	MulAddRecFN.scala 545:1892
_T_693R


_T_692MulAddRecFN.scala 545:14E2&
_T_694R


_T_688


_T_693MulAddRecFN.scala 544:17_2@
_T_697624


pegMaxFiniteMagOut

128		

0	MulAddRecFN.scala 549:1892
_T_698R


_T_697MulAddRecFN.scala 549:14E2&
_T_699R


_T_694


_T_698MulAddRecFN.scala 548:17[2<
_T_702220


notNaN_isInfOut


64	

0	MulAddRecFN.scala 553:1892
_T_703R


_T_702MulAddRecFN.scala 553:14E2&
_T_704R


_T_699


_T_703MulAddRecFN.scala 552:17_2@
_T_707624


pegMinFiniteMagOut

107	

0	MulAddRecFN.scala 557:16E2&
_T_708R


_T_704


_T_707MulAddRecFN.scala 556:18_2@
_T_711624


pegMaxFiniteMagOut

383		

0	MulAddRecFN.scala 558:16E2&
_T_712R


_T_708


_T_711MulAddRecFN.scala 557:74\2=
_T_715321


notNaN_isInfOut

384		

0	MulAddRecFN.scala 562:16E2&
_T_716R


_T_712


_T_715MulAddRecFN.scala 561:15U26
_T_719,2*



isNaNOut

448		

0	MulAddRecFN.scala 566:16E2&
expOutR


_T_716


_T_719MulAddRecFN.scala 565:15R23
_T_720)R'

totalUnderflowY


roundMagUpMulAddRecFN.scala 568:30G2(
_T_721R


_T_720


isNaNOutMulAddRecFN.scala 568:45@2!
_T_723R	

1
22MulAddRecFN.scala 569:34R23
_T_725)2'



isNaNOut


_T_723	

0MulAddRecFN.scala 569:16O20
_T_726&2$



_T_721


_T_725


fractYMulAddRecFN.scala 568:12J20
_T_727&R$

pegMaxFiniteMagOut
0
0Bitwise.scala 71:15R28
_T_730.2,



_T_727
	
8388607	

0Bitwise.scala 71:12G2(
fractOutR


_T_726


_T_730MulAddRecFN.scala 571:11=2'
_T_731R
	
signOut


expOutCat.scala 30:58>2(
_T_732R


_T_731


fractOutCat.scala 30:58<z
:


ioout


_T_732MulAddRecFN.scala 574:12@2*
_T_734 R

	underflow
	
inexactCat.scala 30:58>2(
_T_735R
	
invalid	

0Cat.scala 30:58>2(
_T_736R


_T_735


overflowCat.scala 30:58<2&
_T_737R


_T_736


_T_734Cat.scala 30:58Gz(
:


ioexceptionFlags


_T_737MulAddRecFN.scala 575:23
{{
MulAddRecFN_preMul_1
clock" 
reset
¬
io£* 
op

a
A
b
A
c
A
roundingMode

mulAddA
5
mulAddB
5
mulAddC
j
	toPostMulÿ*ü
highExpA

isNaN_isQuietNaNA

highExpB

isNaN_isQuietNaNB

signProd


isZeroProd

opSignC

highExpC

isNaN_isQuietNaNC

isCDominant

CAlignDist_0


CAlignDist

bit0AlignedNegSigC

highAlignedNegSigC
7
sExpSum

roundingMode



io
 


io
 G2(
signAR:


ioa
64
64MulAddRecFN.scala 102:22F2'
expAR:


ioa
63
52MulAddRecFN.scala 103:22G2(
fractAR:


ioa
51
0MulAddRecFN.scala 104:22A2"
_T_52R

expA
11
9MulAddRecFN.scala 105:24F2'
isZeroAR	

_T_52	

0MulAddRecFN.scala 105:49F2'
_T_55R
	
isZeroA	

0MulAddRecFN.scala 106:2092#
sigAR	

_T_55


fractACat.scala 30:58G2(
signBR:


iob
64
64MulAddRecFN.scala 108:22F2'
expBR:


iob
63
52MulAddRecFN.scala 109:22G2(
fractBR:


iob
51
0MulAddRecFN.scala 110:22A2"
_T_56R

expB
11
9MulAddRecFN.scala 111:24F2'
isZeroBR	

_T_56	

0MulAddRecFN.scala 111:49F2'
_T_59R
	
isZeroB	

0MulAddRecFN.scala 112:2092#
sigBR	

_T_59


fractBCat.scala 30:58G2(
_T_60R:


ioc
64
64MulAddRecFN.scala 114:23F2'
_T_61R:


ioop
0
0MulAddRecFN.scala 114:52D2%
opSignCR	

_T_60	

_T_61MulAddRecFN.scala 114:45F2'
expCR:


ioc
63
52MulAddRecFN.scala 115:22G2(
fractCR:


ioc
51
0MulAddRecFN.scala 116:22A2"
_T_62R

expC
11
9MulAddRecFN.scala 117:24F2'
isZeroCR	

_T_62	

0MulAddRecFN.scala 117:49F2'
_T_65R
	
isZeroC	

0MulAddRecFN.scala 118:2092#
sigCR	

_T_65


fractCCat.scala 30:58B2#
_T_66R	

signA	

signBMulAddRecFN.scala 122:26F2'
_T_67R:


ioop
1
1MulAddRecFN.scala 122:41E2&
signProdR	

_T_66	

_T_67MulAddRecFN.scala 122:34K2,

isZeroProdR
	
isZeroA
	
isZeroBMulAddRecFN.scala 123:30B2#
_T_68R

expB
11
11MulAddRecFN.scala 125:34D2%
_T_70R	

_T_68	

0MulAddRecFN.scala 125:28<2"
_T_71R	

_T_70
0
0Bitwise.scala 71:15J20
_T_74'2%
	

_T_71	

7	

0Bitwise.scala 71:12A2"
_T_75R

expB
10
0MulAddRecFN.scala 125:5192#
_T_76R	

_T_74	

_T_75Cat.scala 30:58A2"
_T_77R

expA	

_T_76MulAddRecFN.scala 125:14<2
_T_78R	

_T_77
1MulAddRecFN.scala 125:14E2&
_T_80R	

_T_78


56MulAddRecFN.scala 125:70F2'
sExpAlignedProdR	

_T_80
1MulAddRecFN.scala 125:70K2,
	doSubMagsR


signProd
	
opSignCMulAddRecFN.scala 130:30K2,
_T_81#R!

sExpAlignedProd

expCMulAddRecFN.scala 132:4272
_T_82R	

_T_81MulAddRecFN.scala 132:42E2&
sNatCAlignDistR	

_T_82
1MulAddRecFN.scala 132:42L2-
_T_83$R"

sNatCAlignDist
13
13MulAddRecFN.scala 133:56R23
CAlignDist_floorR


isZeroProd	

_T_83MulAddRecFN.scala 133:39K2,
_T_84#R!

sNatCAlignDist
12
0MulAddRecFN.scala 135:44D2%
_T_86R	

_T_84	

0MulAddRecFN.scala 135:62T25
CAlignDist_0%R#

CAlignDist_floor	

_T_86MulAddRecFN.scala 135:26E2'
_T_88R
	
isZeroC	

0MulAddRecFN.scala 137:9K2,
_T_89#R!

sNatCAlignDist
12
0MulAddRecFN.scala 139:33E2&
_T_91R	

_T_89


54MulAddRecFN.scala 139:51M2.
_T_92%R#

CAlignDist_floor	

_T_91MulAddRecFN.scala 138:31H2)
isCDominantR	

_T_88	

_T_92MulAddRecFN.scala 137:19K2,
_T_94#R!

sNatCAlignDist
12
0MulAddRecFN.scala 143:31F2'
_T_96R	

_T_94

161MulAddRecFN.scala 143:49J2+
_T_97"R 

sNatCAlignDist
7
0MulAddRecFN.scala 144:31O20
_T_99'2%
	

_T_96	

_T_97

161MulAddRecFN.scala 143:16]2>

CAlignDist02.


CAlignDist_floor	

0	

_T_99MulAddRecFN.scala 141:12a2B
sExpSum725


CAlignDist_floor

expC

sExpAlignedProdMulAddRecFN.scala 148:22E2(
_T_100R


CAlignDist
7
7primitives.scala 56:25E2(
_T_101R


CAlignDist
6
0primitives.scala 57:26A2$
_T_102R


_T_101
6
6primitives.scala 56:25A2$
_T_103R


_T_101
5
0primitives.scala 57:26]2@
_T_1066R4$R"

18446744073709551616A


_T_103primitives.scala 68:52C2&
_T_107R


_T_106
63
31primitives.scala 69:26@2%
_T_108R


_T_107
31
0Bitwise.scala 108:18@2%
_T_111R

65535
16Bitwise.scala 101:47K20
_T_112&R$


4294967295 


_T_111Bitwise.scala 101:21;2 
_T_113R	


_T_108
16Bitwise.scala 102:21A2&
_T_114R


_T_113


_T_112Bitwise.scala 102:31@2%
_T_115R


_T_108
15
0Bitwise.scala 102:46;2 
_T_116R


_T_115
16Bitwise.scala 102:6552
_T_117R


_T_112Bitwise.scala 102:77A2&
_T_118R


_T_116


_T_117Bitwise.scala 102:75A2&
_T_119R


_T_114


_T_118Bitwise.scala 102:39@2%
_T_120R


_T_112
23
0Bitwise.scala 101:28:2
_T_121R


_T_120
8Bitwise.scala 101:47A2&
_T_122R


_T_112


_T_121Bitwise.scala 101:21:2
_T_123R	


_T_119
8Bitwise.scala 102:21A2&
_T_124R


_T_123


_T_122Bitwise.scala 102:31@2%
_T_125R


_T_119
23
0Bitwise.scala 102:46:2
_T_126R


_T_125
8Bitwise.scala 102:6552
_T_127R


_T_122Bitwise.scala 102:77A2&
_T_128R


_T_126


_T_127Bitwise.scala 102:75A2&
_T_129R


_T_124


_T_128Bitwise.scala 102:39@2%
_T_130R


_T_122
27
0Bitwise.scala 101:28:2
_T_131R


_T_130
4Bitwise.scala 101:47A2&
_T_132R


_T_122


_T_131Bitwise.scala 101:21:2
_T_133R	


_T_129
4Bitwise.scala 102:21A2&
_T_134R


_T_133


_T_132Bitwise.scala 102:31@2%
_T_135R


_T_129
27
0Bitwise.scala 102:46:2
_T_136R


_T_135
4Bitwise.scala 102:6552
_T_137R


_T_132Bitwise.scala 102:77A2&
_T_138R


_T_136


_T_137Bitwise.scala 102:75A2&
_T_139R


_T_134


_T_138Bitwise.scala 102:39@2%
_T_140R


_T_132
29
0Bitwise.scala 101:28:2
_T_141R


_T_140
2Bitwise.scala 101:47A2&
_T_142R


_T_132


_T_141Bitwise.scala 101:21:2
_T_143R	


_T_139
2Bitwise.scala 102:21A2&
_T_144R


_T_143


_T_142Bitwise.scala 102:31@2%
_T_145R


_T_139
29
0Bitwise.scala 102:46:2
_T_146R


_T_145
2Bitwise.scala 102:6552
_T_147R


_T_142Bitwise.scala 102:77A2&
_T_148R


_T_146


_T_147Bitwise.scala 102:75A2&
_T_149R


_T_144


_T_148Bitwise.scala 102:39@2%
_T_150R


_T_142
30
0Bitwise.scala 101:28:2
_T_151R


_T_150
1Bitwise.scala 101:47A2&
_T_152R


_T_142


_T_151Bitwise.scala 101:21:2
_T_153R	


_T_149
1Bitwise.scala 102:21A2&
_T_154R


_T_153


_T_152Bitwise.scala 102:31@2%
_T_155R


_T_149
30
0Bitwise.scala 102:46:2
_T_156R


_T_155
1Bitwise.scala 102:6552
_T_157R


_T_152Bitwise.scala 102:77A2&
_T_158R


_T_156


_T_157Bitwise.scala 102:75A2&
_T_159R


_T_154


_T_158Bitwise.scala 102:39A2&
_T_160R


_T_107
32
32Bitwise.scala 108:44<2&
_T_161R


_T_159


_T_160Cat.scala 30:5872
_T_162R


_T_161primitives.scala 65:36N21
_T_163'2%



_T_102	

0


_T_162primitives.scala 65:2172
_T_164R


_T_163primitives.scala 65:17C2-
_T_166#R!


_T_164
	
1048575Cat.scala 30:58A2$
_T_167R


_T_101
6
6primitives.scala 56:25A2$
_T_168R


_T_101
5
0primitives.scala 57:26]2@
_T_1706R4$R"

18446744073709551616A


_T_168primitives.scala 68:52B2%
_T_171R


_T_170
19
0primitives.scala 69:26@2%
_T_172R


_T_171
15
0Bitwise.scala 108:18=2"
_T_175R

255
8Bitwise.scala 101:47F2+
_T_176!R

65535


_T_175Bitwise.scala 101:21:2
_T_177R	


_T_172
8Bitwise.scala 102:21A2&
_T_178R


_T_177


_T_176Bitwise.scala 102:31?2$
_T_179R


_T_172
7
0Bitwise.scala 102:46:2
_T_180R


_T_179
8Bitwise.scala 102:6552
_T_181R


_T_176Bitwise.scala 102:77A2&
_T_182R


_T_180


_T_181Bitwise.scala 102:75A2&
_T_183R


_T_178


_T_182Bitwise.scala 102:39@2%
_T_184R


_T_176
11
0Bitwise.scala 101:28:2
_T_185R


_T_184
4Bitwise.scala 101:47A2&
_T_186R


_T_176


_T_185Bitwise.scala 101:21:2
_T_187R	


_T_183
4Bitwise.scala 102:21A2&
_T_188R


_T_187


_T_186Bitwise.scala 102:31@2%
_T_189R


_T_183
11
0Bitwise.scala 102:46:2
_T_190R


_T_189
4Bitwise.scala 102:6552
_T_191R


_T_186Bitwise.scala 102:77A2&
_T_192R


_T_190


_T_191Bitwise.scala 102:75A2&
_T_193R


_T_188


_T_192Bitwise.scala 102:39@2%
_T_194R


_T_186
13
0Bitwise.scala 101:28:2
_T_195R


_T_194
2Bitwise.scala 101:47A2&
_T_196R


_T_186


_T_195Bitwise.scala 101:21:2
_T_197R	


_T_193
2Bitwise.scala 102:21A2&
_T_198R


_T_197


_T_196Bitwise.scala 102:31@2%
_T_199R


_T_193
13
0Bitwise.scala 102:46:2
_T_200R


_T_199
2Bitwise.scala 102:6552
_T_201R


_T_196Bitwise.scala 102:77A2&
_T_202R


_T_200


_T_201Bitwise.scala 102:75A2&
_T_203R


_T_198


_T_202Bitwise.scala 102:39@2%
_T_204R


_T_196
14
0Bitwise.scala 101:28:2
_T_205R


_T_204
1Bitwise.scala 101:47A2&
_T_206R


_T_196


_T_205Bitwise.scala 101:21:2
_T_207R	


_T_203
1Bitwise.scala 102:21A2&
_T_208R


_T_207


_T_206Bitwise.scala 102:31@2%
_T_209R


_T_203
14
0Bitwise.scala 102:46:2
_T_210R


_T_209
1Bitwise.scala 102:6552
_T_211R


_T_206Bitwise.scala 102:77A2&
_T_212R


_T_210


_T_211Bitwise.scala 102:75A2&
_T_213R


_T_208


_T_212Bitwise.scala 102:39A2&
_T_214R


_T_171
19
16Bitwise.scala 108:44?2$
_T_215R


_T_214
1
0Bitwise.scala 108:18?2$
_T_216R


_T_215
0
0Bitwise.scala 108:18?2$
_T_217R


_T_215
1
1Bitwise.scala 108:44<2&
_T_218R


_T_216


_T_217Cat.scala 30:58?2$
_T_219R


_T_214
3
2Bitwise.scala 108:44?2$
_T_220R


_T_219
0
0Bitwise.scala 108:18?2$
_T_221R


_T_219
1
1Bitwise.scala 108:44<2&
_T_222R


_T_220


_T_221Cat.scala 30:58<2&
_T_223R


_T_218


_T_222Cat.scala 30:58<2&
_T_224R


_T_213


_T_223Cat.scala 30:58N21
_T_226'2%



_T_167


_T_224	

0primitives.scala 59:20Q24

CExtraMask&2$



_T_100


_T_166


_T_226primitives.scala 61:2072
_T_227R

sigCMulAddRecFN.scala 151:34Q22
negSigC'2%


	doSubMags


_T_227

sigCMulAddRecFN.scala 151:22A2'
_T_228R

	doSubMags
0
0Bitwise.scala 71:15l2R
_T_231H2F



_T_228+)
#
!324518553658426726783156020576255l	

0lBitwise.scala 71:12@2*
_T_232 R

	doSubMags
	
negSigCCat.scala 30:58<2&
_T_233R


_T_232


_T_231Cat.scala 30:5892
_T_234R


_T_233MulAddRecFN.scala 154:64I2*
_T_235 R


_T_234


CAlignDistMulAddRecFN.scala 154:70G2(
_T_236R

sigC


CExtraMaskMulAddRecFN.scala 156:19F2'
_T_238R


_T_236	

0MulAddRecFN.scala 156:33H2)
_T_239R


_T_238

	doSubMagsMulAddRecFN.scala 156:3702
_T_240R


_T_235Cat.scala 30:58<2&
_T_241R


_T_240


_T_239Cat.scala 30:58M2.
alignedNegSigCR


_T_241
161
0MulAddRecFN.scala 157:10>z
:


iomulAddA

sigAMulAddRecFN.scala 159:16>z
:


iomulAddB

sigBMulAddRecFN.scala 160:16M2.
_T_242$R"

alignedNegSigC
106
1MulAddRecFN.scala 161:33@z!
:


iomulAddC


_T_242MulAddRecFN.scala 161:16B2#
_T_243R

expA
11
9MulAddRecFN.scala 163:44Pz1
#:!
:


io	toPostMulhighExpA


_T_243MulAddRecFN.scala 163:37E2&
_T_244R


fractA
51
51MulAddRecFN.scala 164:46Yz:
,:*
:


io	toPostMulisNaN_isQuietNaNA


_T_244MulAddRecFN.scala 164:37B2#
_T_245R

expB
11
9MulAddRecFN.scala 165:44Pz1
#:!
:


io	toPostMulhighExpB


_T_245MulAddRecFN.scala 165:37E2&
_T_246R


fractB
51
51MulAddRecFN.scala 166:46Yz:
,:*
:


io	toPostMulisNaN_isQuietNaNB


_T_246MulAddRecFN.scala 166:37Rz3
#:!
:


io	toPostMulsignProd


signProdMulAddRecFN.scala 167:37Vz7
%:#
:


io	toPostMul
isZeroProd


isZeroProdMulAddRecFN.scala 168:37Pz1
": 
:


io	toPostMulopSignC
	
opSignCMulAddRecFN.scala 169:37B2#
_T_247R

expC
11
9MulAddRecFN.scala 170:44Pz1
#:!
:


io	toPostMulhighExpC


_T_247MulAddRecFN.scala 170:37E2&
_T_248R


fractC
51
51MulAddRecFN.scala 171:46Yz:
,:*
:


io	toPostMulisNaN_isQuietNaNC


_T_248MulAddRecFN.scala 171:37Xz9
&:$
:


io	toPostMulisCDominant

isCDominantMulAddRecFN.scala 172:37Zz;
':%
:


io	toPostMulCAlignDist_0

CAlignDist_0MulAddRecFN.scala 173:37Vz7
%:#
:


io	toPostMul
CAlignDist


CAlignDistMulAddRecFN.scala 174:37K2,
_T_249"R 

alignedNegSigC
0
0MulAddRecFN.scala 175:54Zz;
-:+
:


io	toPostMulbit0AlignedNegSigC


_T_249MulAddRecFN.scala 175:37O20
_T_250&R$

alignedNegSigC
161
107MulAddRecFN.scala 177:23Zz;
-:+
:


io	toPostMulhighAlignedNegSigC


_T_250MulAddRecFN.scala 176:37Pz1
": 
:


io	toPostMulsExpSum
	
sExpSumMulAddRecFN.scala 178:37bzC
':%
:


io	toPostMulroundingMode:


ioroundingModeMulAddRecFN.scala 179:37
ÉÅ
MulAddRecFN_postMul_1
clock" 
reset
â
ioÙ*Ö

fromPreMulÿ*ü
highExpA

isNaN_isQuietNaNA

highExpB

isNaN_isQuietNaNB

signProd


isZeroProd

opSignC

highExpC

isNaN_isQuietNaNC

isCDominant

CAlignDist_0


CAlignDist

bit0AlignedNegSigC

highAlignedNegSigC
7
sExpSum

roundingMode

mulAddResult
k
out
A
exceptionFlags



io
 


io
 a2B
isZeroA7R5$:"
:


io
fromPreMulhighExpA	

0MulAddRecFN.scala 207:46\2=
_T_434R2$:"
:


io
fromPreMulhighExpA
2
1MulAddRecFN.scala 208:45I2*

isSpecialAR	

_T_43	

3MulAddRecFN.scala 208:52\2=
_T_454R2$:"
:


io
fromPreMulhighExpA
0
0MulAddRecFN.scala 209:56D2%
_T_47R	

_T_45	

0MulAddRecFN.scala 209:32H2)
isInfAR


isSpecialA	

_T_47MulAddRecFN.scala 209:29\2=
_T_484R2$:"
:


io
fromPreMulhighExpA
0
0MulAddRecFN.scala 210:56H2)
isNaNAR


isSpecialA	

_T_48MulAddRecFN.scala 210:29h2I
_T_50@R>-:+
:


io
fromPreMulisNaN_isQuietNaNA	

0MulAddRecFN.scala 211:31G2(
	isSigNaNAR


isNaNA	

_T_50MulAddRecFN.scala 211:28a2B
isZeroB7R5$:"
:


io
fromPreMulhighExpB	

0MulAddRecFN.scala 213:46\2=
_T_524R2$:"
:


io
fromPreMulhighExpB
2
1MulAddRecFN.scala 214:45I2*

isSpecialBR	

_T_52	

3MulAddRecFN.scala 214:52\2=
_T_544R2$:"
:


io
fromPreMulhighExpB
0
0MulAddRecFN.scala 215:56D2%
_T_56R	

_T_54	

0MulAddRecFN.scala 215:32H2)
isInfBR


isSpecialB	

_T_56MulAddRecFN.scala 215:29\2=
_T_574R2$:"
:


io
fromPreMulhighExpB
0
0MulAddRecFN.scala 216:56H2)
isNaNBR


isSpecialB	

_T_57MulAddRecFN.scala 216:29h2I
_T_59@R>-:+
:


io
fromPreMulisNaN_isQuietNaNB	

0MulAddRecFN.scala 217:31G2(
	isSigNaNBR


isNaNB	

_T_59MulAddRecFN.scala 217:28a2B
isZeroC7R5$:"
:


io
fromPreMulhighExpC	

0MulAddRecFN.scala 219:46\2=
_T_614R2$:"
:


io
fromPreMulhighExpC
2
1MulAddRecFN.scala 220:45I2*

isSpecialCR	

_T_61	

3MulAddRecFN.scala 220:52\2=
_T_634R2$:"
:


io
fromPreMulhighExpC
0
0MulAddRecFN.scala 221:56D2%
_T_65R	

_T_63	

0MulAddRecFN.scala 221:32H2)
isInfCR


isSpecialC	

_T_65MulAddRecFN.scala 221:29\2=
_T_664R2$:"
:


io
fromPreMulhighExpC
0
0MulAddRecFN.scala 222:56H2)
isNaNCR


isSpecialC	

_T_66MulAddRecFN.scala 222:29h2I
_T_68@R>-:+
:


io
fromPreMulisNaN_isQuietNaNC	

0MulAddRecFN.scala 223:31G2(
	isSigNaNCR


isNaNC	

_T_68MulAddRecFN.scala 223:28w2X
roundingMode_nearest_even;R9(:&
:


io
fromPreMulroundingMode	

0MulAddRecFN.scala 226:37q2R
roundingMode_minMag;R9(:&
:


io
fromPreMulroundingMode	

1MulAddRecFN.scala 227:59n2O
roundingMode_min;R9(:&
:


io
fromPreMulroundingMode	

2MulAddRecFN.scala 228:59n2O
roundingMode_max;R9(:&
:


io
fromPreMulroundingMode	

3MulAddRecFN.scala 229:59i2J
signZeroNotEqOpSigns220


roundingMode_min	

1	

0MulAddRecFN.scala 231:35{2\
	doSubMagsORM$:"
:


io
fromPreMulsignProd#:!
:


io
fromPreMulopSignCMulAddRecFN.scala 232:44T25
_T_71,R*:


iomulAddResult
106
106MulAddRecFN.scala 237:32i2J
_T_73AR?.:,
:


io
fromPreMulhighAlignedNegSigC	

1MulAddRecFN.scala 238:50<2
_T_74R	

_T_73
1MulAddRecFN.scala 238:50p2Q
_T_75H2F
	

_T_71	

_T_74.:,
:


io
fromPreMulhighAlignedNegSigCMulAddRecFN.scala 237:16R23
_T_76*R(:


iomulAddResult
105
0MulAddRecFN.scala 241:2892#
_T_77R	

_T_75	

_T_76Cat.scala 30:58_2I
sigSum?R=	

_T_77.:,
:


io
fromPreMulbit0AlignedNegSigCCat.scala 30:58D2%
_T_79R


sigSum
108
1MulAddRecFN.scala 248:38D2%
_T_80R	

0l	

_T_79MulAddRecFN.scala 191:27D2%
_T_81R	

0l	

_T_79MulAddRecFN.scala 191:37<2
_T_82R	

_T_81
1MulAddRecFN.scala 191:41B2#
_T_83R	

_T_80	

_T_82MulAddRecFN.scala 191:32A2$
_T_85R	

_T_83
107
0primitives.scala 79:35C2%
_T_86R	

_T_85
107
64CircuitMath.scala 35:17A2#
_T_87R	

_T_85
63
0CircuitMath.scala 36:17C2%
_T_89R	

_T_86	

0CircuitMath.scala 37:22B2$
_T_90R	

_T_86
43
32CircuitMath.scala 35:17A2#
_T_91R	

_T_86
31
0CircuitMath.scala 36:17C2%
_T_93R	

_T_90	

0CircuitMath.scala 37:22A2#
_T_94R	

_T_90
11
8CircuitMath.scala 35:17@2"
_T_95R	

_T_90
7
0CircuitMath.scala 36:17C2%
_T_97R	

_T_94	

0CircuitMath.scala 37:22@2"
_T_98R	

_T_94
3
3CircuitMath.scala 32:12A2#
_T_100R	

_T_94
2
2CircuitMath.scala 32:12@2#
_T_102R	

_T_94
1
1CircuitMath.scala 30:8O21
_T_103'2%



_T_100	

2


_T_102CircuitMath.scala 32:10N20
_T_104&2$
	

_T_98	

3


_T_103CircuitMath.scala 32:10A2#
_T_105R	

_T_95
7
4CircuitMath.scala 35:17A2#
_T_106R	

_T_95
3
0CircuitMath.scala 36:17E2'
_T_108R


_T_105	

0CircuitMath.scala 37:22B2$
_T_109R


_T_105
3
3CircuitMath.scala 32:12B2$
_T_111R


_T_105
2
2CircuitMath.scala 32:12A2$
_T_113R


_T_105
1
1CircuitMath.scala 30:8O21
_T_114'2%



_T_111	

2


_T_113CircuitMath.scala 32:10O21
_T_115'2%



_T_109	

3


_T_114CircuitMath.scala 32:10B2$
_T_116R


_T_106
3
3CircuitMath.scala 32:12B2$
_T_118R


_T_106
2
2CircuitMath.scala 32:12A2$
_T_120R


_T_106
1
1CircuitMath.scala 30:8O21
_T_121'2%



_T_118	

2


_T_120CircuitMath.scala 32:10O21
_T_122'2%



_T_116	

3


_T_121CircuitMath.scala 32:10N20
_T_123&2$



_T_108


_T_115


_T_122CircuitMath.scala 38:21<2&
_T_124R


_T_108


_T_123Cat.scala 30:58M2/
_T_125%2#
	

_T_97


_T_104


_T_124CircuitMath.scala 38:21;2%
_T_126R	

_T_97


_T_125Cat.scala 30:58C2%
_T_127R	

_T_91
31
16CircuitMath.scala 35:17B2$
_T_128R	

_T_91
15
0CircuitMath.scala 36:17E2'
_T_130R


_T_127	

0CircuitMath.scala 37:22C2%
_T_131R


_T_127
15
8CircuitMath.scala 35:17B2$
_T_132R


_T_127
7
0CircuitMath.scala 36:17E2'
_T_134R


_T_131	

0CircuitMath.scala 37:22B2$
_T_135R


_T_131
7
4CircuitMath.scala 35:17B2$
_T_136R


_T_131
3
0CircuitMath.scala 36:17E2'
_T_138R


_T_135	

0CircuitMath.scala 37:22B2$
_T_139R


_T_135
3
3CircuitMath.scala 32:12B2$
_T_141R


_T_135
2
2CircuitMath.scala 32:12A2$
_T_143R


_T_135
1
1CircuitMath.scala 30:8O21
_T_144'2%



_T_141	

2


_T_143CircuitMath.scala 32:10O21
_T_145'2%



_T_139	

3


_T_144CircuitMath.scala 32:10B2$
_T_146R


_T_136
3
3CircuitMath.scala 32:12B2$
_T_148R


_T_136
2
2CircuitMath.scala 32:12A2$
_T_150R


_T_136
1
1CircuitMath.scala 30:8O21
_T_151'2%



_T_148	

2


_T_150CircuitMath.scala 32:10O21
_T_152'2%



_T_146	

3


_T_151CircuitMath.scala 32:10N20
_T_153&2$



_T_138


_T_145


_T_152CircuitMath.scala 38:21<2&
_T_154R


_T_138


_T_153Cat.scala 30:58B2$
_T_155R


_T_132
7
4CircuitMath.scala 35:17B2$
_T_156R


_T_132
3
0CircuitMath.scala 36:17E2'
_T_158R


_T_155	

0CircuitMath.scala 37:22B2$
_T_159R


_T_155
3
3CircuitMath.scala 32:12B2$
_T_161R


_T_155
2
2CircuitMath.scala 32:12A2$
_T_163R


_T_155
1
1CircuitMath.scala 30:8O21
_T_164'2%



_T_161	

2


_T_163CircuitMath.scala 32:10O21
_T_165'2%



_T_159	

3


_T_164CircuitMath.scala 32:10B2$
_T_166R


_T_156
3
3CircuitMath.scala 32:12B2$
_T_168R


_T_156
2
2CircuitMath.scala 32:12A2$
_T_170R


_T_156
1
1CircuitMath.scala 30:8O21
_T_171'2%



_T_168	

2


_T_170CircuitMath.scala 32:10O21
_T_172'2%



_T_166	

3


_T_171CircuitMath.scala 32:10N20
_T_173&2$



_T_158


_T_165


_T_172CircuitMath.scala 38:21<2&
_T_174R


_T_158


_T_173Cat.scala 30:58N20
_T_175&2$



_T_134


_T_154


_T_174CircuitMath.scala 38:21<2&
_T_176R


_T_134


_T_175Cat.scala 30:58C2%
_T_177R


_T_128
15
8CircuitMath.scala 35:17B2$
_T_178R


_T_128
7
0CircuitMath.scala 36:17E2'
_T_180R


_T_177	

0CircuitMath.scala 37:22B2$
_T_181R


_T_177
7
4CircuitMath.scala 35:17B2$
_T_182R


_T_177
3
0CircuitMath.scala 36:17E2'
_T_184R


_T_181	

0CircuitMath.scala 37:22B2$
_T_185R


_T_181
3
3CircuitMath.scala 32:12B2$
_T_187R


_T_181
2
2CircuitMath.scala 32:12A2$
_T_189R


_T_181
1
1CircuitMath.scala 30:8O21
_T_190'2%



_T_187	

2


_T_189CircuitMath.scala 32:10O21
_T_191'2%



_T_185	

3


_T_190CircuitMath.scala 32:10B2$
_T_192R


_T_182
3
3CircuitMath.scala 32:12B2$
_T_194R


_T_182
2
2CircuitMath.scala 32:12A2$
_T_196R


_T_182
1
1CircuitMath.scala 30:8O21
_T_197'2%



_T_194	

2


_T_196CircuitMath.scala 32:10O21
_T_198'2%



_T_192	

3


_T_197CircuitMath.scala 32:10N20
_T_199&2$



_T_184


_T_191


_T_198CircuitMath.scala 38:21<2&
_T_200R


_T_184


_T_199Cat.scala 30:58B2$
_T_201R


_T_178
7
4CircuitMath.scala 35:17B2$
_T_202R


_T_178
3
0CircuitMath.scala 36:17E2'
_T_204R


_T_201	

0CircuitMath.scala 37:22B2$
_T_205R


_T_201
3
3CircuitMath.scala 32:12B2$
_T_207R


_T_201
2
2CircuitMath.scala 32:12A2$
_T_209R


_T_201
1
1CircuitMath.scala 30:8O21
_T_210'2%



_T_207	

2


_T_209CircuitMath.scala 32:10O21
_T_211'2%



_T_205	

3


_T_210CircuitMath.scala 32:10B2$
_T_212R


_T_202
3
3CircuitMath.scala 32:12B2$
_T_214R


_T_202
2
2CircuitMath.scala 32:12A2$
_T_216R


_T_202
1
1CircuitMath.scala 30:8O21
_T_217'2%



_T_214	

2


_T_216CircuitMath.scala 32:10O21
_T_218'2%



_T_212	

3


_T_217CircuitMath.scala 32:10N20
_T_219&2$



_T_204


_T_211


_T_218CircuitMath.scala 38:21<2&
_T_220R


_T_204


_T_219Cat.scala 30:58N20
_T_221&2$



_T_180


_T_200


_T_220CircuitMath.scala 38:21<2&
_T_222R


_T_180


_T_221Cat.scala 30:58N20
_T_223&2$



_T_130


_T_176


_T_222CircuitMath.scala 38:21<2&
_T_224R


_T_130


_T_223Cat.scala 30:58M2/
_T_225%2#
	

_T_93


_T_126


_T_224CircuitMath.scala 38:21;2%
_T_226R	

_T_93


_T_225Cat.scala 30:58C2%
_T_227R	

_T_87
63
32CircuitMath.scala 35:17B2$
_T_228R	

_T_87
31
0CircuitMath.scala 36:17E2'
_T_230R


_T_227	

0CircuitMath.scala 37:22D2&
_T_231R


_T_227
31
16CircuitMath.scala 35:17C2%
_T_232R


_T_227
15
0CircuitMath.scala 36:17E2'
_T_234R


_T_231	

0CircuitMath.scala 37:22C2%
_T_235R


_T_231
15
8CircuitMath.scala 35:17B2$
_T_236R


_T_231
7
0CircuitMath.scala 36:17E2'
_T_238R


_T_235	

0CircuitMath.scala 37:22B2$
_T_239R


_T_235
7
4CircuitMath.scala 35:17B2$
_T_240R


_T_235
3
0CircuitMath.scala 36:17E2'
_T_242R


_T_239	

0CircuitMath.scala 37:22B2$
_T_243R


_T_239
3
3CircuitMath.scala 32:12B2$
_T_245R


_T_239
2
2CircuitMath.scala 32:12A2$
_T_247R


_T_239
1
1CircuitMath.scala 30:8O21
_T_248'2%



_T_245	

2


_T_247CircuitMath.scala 32:10O21
_T_249'2%



_T_243	

3


_T_248CircuitMath.scala 32:10B2$
_T_250R


_T_240
3
3CircuitMath.scala 32:12B2$
_T_252R


_T_240
2
2CircuitMath.scala 32:12A2$
_T_254R


_T_240
1
1CircuitMath.scala 30:8O21
_T_255'2%



_T_252	

2


_T_254CircuitMath.scala 32:10O21
_T_256'2%



_T_250	

3


_T_255CircuitMath.scala 32:10N20
_T_257&2$



_T_242


_T_249


_T_256CircuitMath.scala 38:21<2&
_T_258R


_T_242


_T_257Cat.scala 30:58B2$
_T_259R


_T_236
7
4CircuitMath.scala 35:17B2$
_T_260R


_T_236
3
0CircuitMath.scala 36:17E2'
_T_262R


_T_259	

0CircuitMath.scala 37:22B2$
_T_263R


_T_259
3
3CircuitMath.scala 32:12B2$
_T_265R


_T_259
2
2CircuitMath.scala 32:12A2$
_T_267R


_T_259
1
1CircuitMath.scala 30:8O21
_T_268'2%



_T_265	

2


_T_267CircuitMath.scala 32:10O21
_T_269'2%



_T_263	

3


_T_268CircuitMath.scala 32:10B2$
_T_270R


_T_260
3
3CircuitMath.scala 32:12B2$
_T_272R


_T_260
2
2CircuitMath.scala 32:12A2$
_T_274R


_T_260
1
1CircuitMath.scala 30:8O21
_T_275'2%



_T_272	

2


_T_274CircuitMath.scala 32:10O21
_T_276'2%



_T_270	

3


_T_275CircuitMath.scala 32:10N20
_T_277&2$



_T_262


_T_269


_T_276CircuitMath.scala 38:21<2&
_T_278R


_T_262


_T_277Cat.scala 30:58N20
_T_279&2$



_T_238


_T_258


_T_278CircuitMath.scala 38:21<2&
_T_280R


_T_238


_T_279Cat.scala 30:58C2%
_T_281R


_T_232
15
8CircuitMath.scala 35:17B2$
_T_282R


_T_232
7
0CircuitMath.scala 36:17E2'
_T_284R


_T_281	

0CircuitMath.scala 37:22B2$
_T_285R


_T_281
7
4CircuitMath.scala 35:17B2$
_T_286R


_T_281
3
0CircuitMath.scala 36:17E2'
_T_288R


_T_285	

0CircuitMath.scala 37:22B2$
_T_289R


_T_285
3
3CircuitMath.scala 32:12B2$
_T_291R


_T_285
2
2CircuitMath.scala 32:12A2$
_T_293R


_T_285
1
1CircuitMath.scala 30:8O21
_T_294'2%



_T_291	

2


_T_293CircuitMath.scala 32:10O21
_T_295'2%



_T_289	

3


_T_294CircuitMath.scala 32:10B2$
_T_296R


_T_286
3
3CircuitMath.scala 32:12B2$
_T_298R


_T_286
2
2CircuitMath.scala 32:12A2$
_T_300R


_T_286
1
1CircuitMath.scala 30:8O21
_T_301'2%



_T_298	

2


_T_300CircuitMath.scala 32:10O21
_T_302'2%



_T_296	

3


_T_301CircuitMath.scala 32:10N20
_T_303&2$



_T_288


_T_295


_T_302CircuitMath.scala 38:21<2&
_T_304R


_T_288


_T_303Cat.scala 30:58B2$
_T_305R


_T_282
7
4CircuitMath.scala 35:17B2$
_T_306R


_T_282
3
0CircuitMath.scala 36:17E2'
_T_308R


_T_305	

0CircuitMath.scala 37:22B2$
_T_309R


_T_305
3
3CircuitMath.scala 32:12B2$
_T_311R


_T_305
2
2CircuitMath.scala 32:12A2$
_T_313R


_T_305
1
1CircuitMath.scala 30:8O21
_T_314'2%



_T_311	

2


_T_313CircuitMath.scala 32:10O21
_T_315'2%



_T_309	

3


_T_314CircuitMath.scala 32:10B2$
_T_316R


_T_306
3
3CircuitMath.scala 32:12B2$
_T_318R


_T_306
2
2CircuitMath.scala 32:12A2$
_T_320R


_T_306
1
1CircuitMath.scala 30:8O21
_T_321'2%



_T_318	

2


_T_320CircuitMath.scala 32:10O21
_T_322'2%



_T_316	

3


_T_321CircuitMath.scala 32:10N20
_T_323&2$



_T_308


_T_315


_T_322CircuitMath.scala 38:21<2&
_T_324R


_T_308


_T_323Cat.scala 30:58N20
_T_325&2$



_T_284


_T_304


_T_324CircuitMath.scala 38:21<2&
_T_326R


_T_284


_T_325Cat.scala 30:58N20
_T_327&2$



_T_234


_T_280


_T_326CircuitMath.scala 38:21<2&
_T_328R


_T_234


_T_327Cat.scala 30:58D2&
_T_329R


_T_228
31
16CircuitMath.scala 35:17C2%
_T_330R


_T_228
15
0CircuitMath.scala 36:17E2'
_T_332R


_T_329	

0CircuitMath.scala 37:22C2%
_T_333R


_T_329
15
8CircuitMath.scala 35:17B2$
_T_334R


_T_329
7
0CircuitMath.scala 36:17E2'
_T_336R


_T_333	

0CircuitMath.scala 37:22B2$
_T_337R


_T_333
7
4CircuitMath.scala 35:17B2$
_T_338R


_T_333
3
0CircuitMath.scala 36:17E2'
_T_340R


_T_337	

0CircuitMath.scala 37:22B2$
_T_341R


_T_337
3
3CircuitMath.scala 32:12B2$
_T_343R


_T_337
2
2CircuitMath.scala 32:12A2$
_T_345R


_T_337
1
1CircuitMath.scala 30:8O21
_T_346'2%



_T_343	

2


_T_345CircuitMath.scala 32:10O21
_T_347'2%



_T_341	

3


_T_346CircuitMath.scala 32:10B2$
_T_348R


_T_338
3
3CircuitMath.scala 32:12B2$
_T_350R


_T_338
2
2CircuitMath.scala 32:12A2$
_T_352R


_T_338
1
1CircuitMath.scala 30:8O21
_T_353'2%



_T_350	

2


_T_352CircuitMath.scala 32:10O21
_T_354'2%



_T_348	

3


_T_353CircuitMath.scala 32:10N20
_T_355&2$



_T_340


_T_347


_T_354CircuitMath.scala 38:21<2&
_T_356R


_T_340


_T_355Cat.scala 30:58B2$
_T_357R


_T_334
7
4CircuitMath.scala 35:17B2$
_T_358R


_T_334
3
0CircuitMath.scala 36:17E2'
_T_360R


_T_357	

0CircuitMath.scala 37:22B2$
_T_361R


_T_357
3
3CircuitMath.scala 32:12B2$
_T_363R


_T_357
2
2CircuitMath.scala 32:12A2$
_T_365R


_T_357
1
1CircuitMath.scala 30:8O21
_T_366'2%



_T_363	

2


_T_365CircuitMath.scala 32:10O21
_T_367'2%



_T_361	

3


_T_366CircuitMath.scala 32:10B2$
_T_368R


_T_358
3
3CircuitMath.scala 32:12B2$
_T_370R


_T_358
2
2CircuitMath.scala 32:12A2$
_T_372R


_T_358
1
1CircuitMath.scala 30:8O21
_T_373'2%



_T_370	

2


_T_372CircuitMath.scala 32:10O21
_T_374'2%



_T_368	

3


_T_373CircuitMath.scala 32:10N20
_T_375&2$



_T_360


_T_367


_T_374CircuitMath.scala 38:21<2&
_T_376R


_T_360


_T_375Cat.scala 30:58N20
_T_377&2$



_T_336


_T_356


_T_376CircuitMath.scala 38:21<2&
_T_378R


_T_336


_T_377Cat.scala 30:58C2%
_T_379R


_T_330
15
8CircuitMath.scala 35:17B2$
_T_380R


_T_330
7
0CircuitMath.scala 36:17E2'
_T_382R


_T_379	

0CircuitMath.scala 37:22B2$
_T_383R


_T_379
7
4CircuitMath.scala 35:17B2$
_T_384R


_T_379
3
0CircuitMath.scala 36:17E2'
_T_386R


_T_383	

0CircuitMath.scala 37:22B2$
_T_387R


_T_383
3
3CircuitMath.scala 32:12B2$
_T_389R


_T_383
2
2CircuitMath.scala 32:12A2$
_T_391R


_T_383
1
1CircuitMath.scala 30:8O21
_T_392'2%



_T_389	

2


_T_391CircuitMath.scala 32:10O21
_T_393'2%



_T_387	

3


_T_392CircuitMath.scala 32:10B2$
_T_394R


_T_384
3
3CircuitMath.scala 32:12B2$
_T_396R


_T_384
2
2CircuitMath.scala 32:12A2$
_T_398R


_T_384
1
1CircuitMath.scala 30:8O21
_T_399'2%



_T_396	

2


_T_398CircuitMath.scala 32:10O21
_T_400'2%



_T_394	

3


_T_399CircuitMath.scala 32:10N20
_T_401&2$



_T_386


_T_393


_T_400CircuitMath.scala 38:21<2&
_T_402R


_T_386


_T_401Cat.scala 30:58B2$
_T_403R


_T_380
7
4CircuitMath.scala 35:17B2$
_T_404R


_T_380
3
0CircuitMath.scala 36:17E2'
_T_406R


_T_403	

0CircuitMath.scala 37:22B2$
_T_407R


_T_403
3
3CircuitMath.scala 32:12B2$
_T_409R


_T_403
2
2CircuitMath.scala 32:12A2$
_T_411R


_T_403
1
1CircuitMath.scala 30:8O21
_T_412'2%



_T_409	

2


_T_411CircuitMath.scala 32:10O21
_T_413'2%



_T_407	

3


_T_412CircuitMath.scala 32:10B2$
_T_414R


_T_404
3
3CircuitMath.scala 32:12B2$
_T_416R


_T_404
2
2CircuitMath.scala 32:12A2$
_T_418R


_T_404
1
1CircuitMath.scala 30:8O21
_T_419'2%



_T_416	

2


_T_418CircuitMath.scala 32:10O21
_T_420'2%



_T_414	

3


_T_419CircuitMath.scala 32:10N20
_T_421&2$



_T_406


_T_413


_T_420CircuitMath.scala 38:21<2&
_T_422R


_T_406


_T_421Cat.scala 30:58N20
_T_423&2$



_T_382


_T_402


_T_422CircuitMath.scala 38:21<2&
_T_424R


_T_382


_T_423Cat.scala 30:58N20
_T_425&2$



_T_332


_T_378


_T_424CircuitMath.scala 38:21<2&
_T_426R


_T_332


_T_425Cat.scala 30:58N20
_T_427&2$



_T_230


_T_328


_T_426CircuitMath.scala 38:21<2&
_T_428R


_T_230


_T_427Cat.scala 30:58M2/
_T_429%2#
	

_T_89


_T_226


_T_428CircuitMath.scala 38:21;2%
_T_430R	

_T_89


_T_429Cat.scala 30:58F2)
_T_431R

160


_T_430primitives.scala 79:2572
_T_432R


_T_431primitives.scala 79:25E2(
estNormNeg_distR


_T_432
1primitives.scala 79:25E2&
_T_433R


sigSum
75
44MulAddRecFN.scala 252:19F2'
_T_435R


_T_433	

0MulAddRecFN.scala 254:15D2%
_T_436R


sigSum
43
0MulAddRecFN.scala 255:19F2'
_T_438R


_T_436	

0MulAddRecFN.scala 255:57G21
firstReduceSigSumR


_T_435


_T_438Cat.scala 30:58>2
complSigSumR


sigSumMulAddRecFN.scala 257:23J2+
_T_439!R

complSigSum
75
44MulAddRecFN.scala 259:24F2'
_T_441R


_T_439	

0MulAddRecFN.scala 261:15I2*
_T_442 R

complSigSum
43
0MulAddRecFN.scala 262:24F2'
_T_444R


_T_442	

0MulAddRecFN.scala 262:62L26
firstReduceComplSigSumR


_T_441


_T_444Cat.scala 30:58f2G
_T_445=R;(:&
:


io
fromPreMulCAlignDist_0

	doSubMagsMulAddRecFN.scala 266:40b2C
_T_4479R7&:$
:


io
fromPreMul
CAlignDist	

1MulAddRecFN.scala 268:3992
_T_448R


_T_447MulAddRecFN.scala 268:39>2
_T_449R


_T_448
1MulAddRecFN.scala 268:39C2$
_T_450R


_T_449
5
0MulAddRecFN.scala 268:49u2V
CDom_estNormDistB2@



_T_445&:$
:


io
fromPreMul
CAlignDist


_T_450MulAddRecFN.scala 266:12I2*
_T_452 R

	doSubMags	

0MulAddRecFN.scala 271:13M2.
_T_453$R"

CDom_estNormDist
5
5MulAddRecFN.scala 271:46F2'
_T_455R


_T_453	

0MulAddRecFN.scala 271:28E2&
_T_456R


_T_452


_T_455MulAddRecFN.scala 271:25F2'
_T_457R


sigSum
161
76MulAddRecFN.scala 272:23Q22
_T_459(R&

firstReduceSigSum	

0MulAddRecFN.scala 273:35<2&
_T_460R


_T_457


_T_459Cat.scala 30:58P21
_T_462'2%



_T_456


_T_460	

0MulAddRecFN.scala 271:12I2*
_T_464 R

	doSubMags	

0MulAddRecFN.scala 277:13M2.
_T_465$R"

CDom_estNormDist
5
5MulAddRecFN.scala 277:44E2&
_T_466R


_T_464


_T_465MulAddRecFN.scala 277:25F2'
_T_467R


sigSum
129
44MulAddRecFN.scala 278:23N2/
_T_468%R#

firstReduceSigSum
0
0MulAddRecFN.scala 282:34<2&
_T_469R


_T_467


_T_468Cat.scala 30:58P21
_T_471'2%



_T_466


_T_469	

0MulAddRecFN.scala 277:12E2&
_T_472R


_T_462


_T_471MulAddRecFN.scala 276:11M2.
_T_473$R"

CDom_estNormDist
5
5MulAddRecFN.scala 286:44F2'
_T_475R


_T_473	

0MulAddRecFN.scala 286:26H2)
_T_476R

	doSubMags


_T_475MulAddRecFN.scala 286:23K2,
_T_477"R 

complSigSum
161
76MulAddRecFN.scala 287:28V27
_T_479-R+

firstReduceComplSigSum	

0MulAddRecFN.scala 288:40<2&
_T_480R


_T_477


_T_479Cat.scala 30:58P21
_T_482'2%



_T_476


_T_480	

0MulAddRecFN.scala 286:12E2&
_T_483R


_T_472


_T_482MulAddRecFN.scala 285:11M2.
_T_484$R"

CDom_estNormDist
5
5MulAddRecFN.scala 292:42H2)
_T_485R

	doSubMags


_T_484MulAddRecFN.scala 292:23K2,
_T_486"R 

complSigSum
129
44MulAddRecFN.scala 293:28S24
_T_487*R(

firstReduceComplSigSum
0
0MulAddRecFN.scala 297:39<2&
_T_488R


_T_486


_T_487Cat.scala 30:58P21
_T_490'2%



_T_485


_T_488	

0MulAddRecFN.scala 292:12V27
CDom_firstNormAbsSigSumR


_T_483


_T_490MulAddRecFN.scala 291:11F2'
_T_491R


sigSum
108
44MulAddRecFN.scala 308:23S24
_T_492*R(

firstReduceComplSigSum
0
0MulAddRecFN.scala 310:45F2'
_T_494R


_T_492	

0MulAddRecFN.scala 310:21N2/
_T_495%R#

firstReduceSigSum
0
0MulAddRecFN.scala 311:38R23
_T_496)2'


	doSubMags


_T_494


_T_495MulAddRecFN.scala 309:20<2&
_T_497R


_T_491


_T_496Cat.scala 30:58D2%
_T_498R


sigSum
97
1MulAddRecFN.scala 314:24L2-
_T_499#R!

estNormNeg_dist
4
4MulAddRecFN.scala 316:37C2$
_T_500R


sigSum
1
1MulAddRecFN.scala 318:32A2'
_T_501R

	doSubMags
0
0Bitwise.scala 71:15e2K
_T_504A2?



_T_501$"

77371252455336267181195263V	

0VBitwise.scala 71:12<2&
_T_505R


_T_500


_T_504Cat.scala 30:58O20
_T_506&2$



_T_499


_T_497


_T_505MulAddRecFN.scala 316:21E2&
_T_507R


sigSum
97
12MulAddRecFN.scala 324:28I2*
_T_508 R

complSigSum
11
1MulAddRecFN.scala 329:39F2'
_T_510R


_T_508	

0MulAddRecFN.scala 329:77D2%
_T_511R


sigSum
11
1MulAddRecFN.scala 331:34F2'
_T_513R


_T_511	

0MulAddRecFN.scala 331:72R23
_T_514)2'


	doSubMags


_T_510


_T_513MulAddRecFN.scala 328:26<2&
_T_515R


_T_507


_T_514Cat.scala 30:58L2-
_T_516#R!

estNormNeg_dist
6
6MulAddRecFN.scala 338:28L2-
_T_517#R!

estNormNeg_dist
5
5MulAddRecFN.scala 339:33D2%
_T_518R


sigSum
65
1MulAddRecFN.scala 340:28A2'
_T_519R

	doSubMags
0
0Bitwise.scala 71:15R28
_T_522.2,



_T_519
	
4194303	

0Bitwise.scala 71:12<2&
_T_523R


_T_518


_T_522Cat.scala 30:58O20
_T_524&2$



_T_517


_T_523


_T_515MulAddRecFN.scala 339:17L2-
_T_525#R!

estNormNeg_dist
5
5MulAddRecFN.scala 345:33D2%
_T_526R


sigSum
33
1MulAddRecFN.scala 347:28A2'
_T_527R

	doSubMags
0
0Bitwise.scala 71:15\2B
_T_530826



_T_527

180143985094819836	

06Bitwise.scala 71:12<2&
_T_531R


_T_526


_T_530Cat.scala 30:58O20
_T_532&2$



_T_525


_T_506


_T_531MulAddRecFN.scala 345:17g2H
notCDom_pos_firstNormAbsSigSum&2$



_T_516


_T_524


_T_532MulAddRecFN.scala 338:12K2,
_T_533"R 

complSigSum
107
44MulAddRecFN.scala 360:28S24
_T_534*R(

firstReduceComplSigSum
0
0MulAddRecFN.scala 361:39<2&
_T_535R


_T_533


_T_534Cat.scala 30:58I2*
_T_536 R

complSigSum
97
1MulAddRecFN.scala 363:29L2-
_T_537#R!

estNormNeg_dist
4
4MulAddRecFN.scala 365:37H2)
_T_538R

complSigSum
2
1MulAddRecFN.scala 367:33?2 
_T_539R


_T_538
86MulAddRecFN.scala 367:68O20
_T_540&2$



_T_537


_T_535


_T_539MulAddRecFN.scala 365:21J2+
_T_541!R

complSigSum
98
12MulAddRecFN.scala 372:33I2*
_T_542 R

complSigSum
11
1MulAddRecFN.scala 376:33F2'
_T_544R


_T_542	

0MulAddRecFN.scala 376:71<2&
_T_545R


_T_541


_T_544Cat.scala 30:58L2-
_T_546#R!

estNormNeg_dist
6
6MulAddRecFN.scala 379:28L2-
_T_547#R!

estNormNeg_dist
5
5MulAddRecFN.scala 380:33I2*
_T_548 R

complSigSum
66
1MulAddRecFN.scala 381:29?2 
_T_549R


_T_548
22MulAddRecFN.scala 381:64O20
_T_550&2$



_T_547


_T_549


_T_545MulAddRecFN.scala 380:17L2-
_T_551#R!

estNormNeg_dist
5
5MulAddRecFN.scala 385:33I2*
_T_552 R

complSigSum
34
1MulAddRecFN.scala 387:29?2 
_T_553R


_T_552
54MulAddRecFN.scala 387:64O20
_T_554&2$



_T_551


_T_540


_T_553MulAddRecFN.scala 385:17h2I
notCDom_neg_cFirstNormAbsSigSum&2$



_T_546


_T_550


_T_554MulAddRecFN.scala 379:12S24
notCDom_signSigSumR


sigSum
109
109MulAddRecFN.scala 392:36G2(
_T_556R
	
isZeroC	

0MulAddRecFN.scala 395:26H2)
_T_557R

	doSubMags


_T_556MulAddRecFN.scala 395:23~2_
doNegSignSumO2M
':%
:


io
fromPreMulisCDominant


_T_557

notCDom_signSigSumMulAddRecFN.scala 394:12m2N
_T_558D2B


notCDom_signSigSum

estNormNeg_dist

estNormNeg_distMulAddRecFN.scala 401:16{2\
estNormDistM2K
':%
:


io
fromPreMulisCDominant

CDom_estNormDist


_T_558MulAddRecFN.scala 399:122w
_T_559m2k
':%
:


io
fromPreMulisCDominant

CDom_firstNormAbsSigSum#
!
notCDom_neg_cFirstNormAbsSigSumMulAddRecFN.scala 408:162v
_T_560l2j
':%
:


io
fromPreMulisCDominant

CDom_firstNormAbsSigSum"
 
notCDom_pos_firstNormAbsSigSumMulAddRecFN.scala 412:16h2I
cFirstNormAbsSigSum220


notCDom_signSigSum


_T_559


_T_560MulAddRecFN.scala 407:12b2D
_T_562:R8':%
:


io
fromPreMulisCDominant	

0MulAddRecFN.scala 418:9R23
_T_564)R'

notCDom_signSigSum	

0MulAddRecFN.scala 418:40E2&
_T_565R


_T_562


_T_564MulAddRecFN.scala 418:37K2,
	doIncrSigR


_T_565

	doSubMagsMulAddRecFN.scala 418:61O20
estNormDist_5R

estNormDist
4
0MulAddRecFN.scala 419:36J2+
normTo2ShiftDistR

estNormDist_5MulAddRecFN.scala 420:28]2@
_T_5676R4R


4294967296!

normTo2ShiftDistprimitives.scala 68:52B2%
_T_568R


_T_567
31
1primitives.scala 69:26@2%
_T_569R


_T_568
15
0Bitwise.scala 108:18=2"
_T_572R

255
8Bitwise.scala 101:47F2+
_T_573!R

65535


_T_572Bitwise.scala 101:21:2
_T_574R	


_T_569
8Bitwise.scala 102:21A2&
_T_575R


_T_574


_T_573Bitwise.scala 102:31?2$
_T_576R


_T_569
7
0Bitwise.scala 102:46:2
_T_577R


_T_576
8Bitwise.scala 102:6552
_T_578R


_T_573Bitwise.scala 102:77A2&
_T_579R


_T_577


_T_578Bitwise.scala 102:75A2&
_T_580R


_T_575


_T_579Bitwise.scala 102:39@2%
_T_581R


_T_573
11
0Bitwise.scala 101:28:2
_T_582R


_T_581
4Bitwise.scala 101:47A2&
_T_583R


_T_573


_T_582Bitwise.scala 101:21:2
_T_584R	


_T_580
4Bitwise.scala 102:21A2&
_T_585R


_T_584


_T_583Bitwise.scala 102:31@2%
_T_586R


_T_580
11
0Bitwise.scala 102:46:2
_T_587R


_T_586
4Bitwise.scala 102:6552
_T_588R


_T_583Bitwise.scala 102:77A2&
_T_589R


_T_587


_T_588Bitwise.scala 102:75A2&
_T_590R


_T_585


_T_589Bitwise.scala 102:39@2%
_T_591R


_T_583
13
0Bitwise.scala 101:28:2
_T_592R


_T_591
2Bitwise.scala 101:47A2&
_T_593R


_T_583


_T_592Bitwise.scala 101:21:2
_T_594R	


_T_590
2Bitwise.scala 102:21A2&
_T_595R


_T_594


_T_593Bitwise.scala 102:31@2%
_T_596R


_T_590
13
0Bitwise.scala 102:46:2
_T_597R


_T_596
2Bitwise.scala 102:6552
_T_598R


_T_593Bitwise.scala 102:77A2&
_T_599R


_T_597


_T_598Bitwise.scala 102:75A2&
_T_600R


_T_595


_T_599Bitwise.scala 102:39@2%
_T_601R


_T_593
14
0Bitwise.scala 101:28:2
_T_602R


_T_601
1Bitwise.scala 101:47A2&
_T_603R


_T_593


_T_602Bitwise.scala 101:21:2
_T_604R	


_T_600
1Bitwise.scala 102:21A2&
_T_605R


_T_604


_T_603Bitwise.scala 102:31@2%
_T_606R


_T_600
14
0Bitwise.scala 102:46:2
_T_607R


_T_606
1Bitwise.scala 102:6552
_T_608R


_T_603Bitwise.scala 102:77A2&
_T_609R


_T_607


_T_608Bitwise.scala 102:75A2&
_T_610R


_T_605


_T_609Bitwise.scala 102:39A2&
_T_611R


_T_568
30
16Bitwise.scala 108:44?2$
_T_612R


_T_611
7
0Bitwise.scala 108:18<2!
_T_615R


15
4Bitwise.scala 101:47D2)
_T_616R

255


_T_615Bitwise.scala 101:21:2
_T_617R	


_T_612
4Bitwise.scala 102:21A2&
_T_618R


_T_617


_T_616Bitwise.scala 102:31?2$
_T_619R


_T_612
3
0Bitwise.scala 102:46:2
_T_620R


_T_619
4Bitwise.scala 102:6552
_T_621R


_T_616Bitwise.scala 102:77A2&
_T_622R


_T_620


_T_621Bitwise.scala 102:75A2&
_T_623R


_T_618


_T_622Bitwise.scala 102:39?2$
_T_624R


_T_616
5
0Bitwise.scala 101:28:2
_T_625R


_T_624
2Bitwise.scala 101:47A2&
_T_626R


_T_616


_T_625Bitwise.scala 101:21:2
_T_627R	


_T_623
2Bitwise.scala 102:21A2&
_T_628R


_T_627


_T_626Bitwise.scala 102:31?2$
_T_629R


_T_623
5
0Bitwise.scala 102:46:2
_T_630R


_T_629
2Bitwise.scala 102:6552
_T_631R


_T_626Bitwise.scala 102:77A2&
_T_632R


_T_630


_T_631Bitwise.scala 102:75A2&
_T_633R


_T_628


_T_632Bitwise.scala 102:39?2$
_T_634R


_T_626
6
0Bitwise.scala 101:28:2
_T_635R


_T_634
1Bitwise.scala 101:47A2&
_T_636R


_T_626


_T_635Bitwise.scala 101:21:2
_T_637R	


_T_633
1Bitwise.scala 102:21A2&
_T_638R


_T_637


_T_636Bitwise.scala 102:31?2$
_T_639R


_T_633
6
0Bitwise.scala 102:46:2
_T_640R


_T_639
1Bitwise.scala 102:6552
_T_641R


_T_636Bitwise.scala 102:77A2&
_T_642R


_T_640


_T_641Bitwise.scala 102:75A2&
_T_643R


_T_638


_T_642Bitwise.scala 102:39@2%
_T_644R


_T_611
14
8Bitwise.scala 108:44?2$
_T_645R


_T_644
3
0Bitwise.scala 108:18?2$
_T_646R


_T_645
1
0Bitwise.scala 108:18?2$
_T_647R


_T_646
0
0Bitwise.scala 108:18?2$
_T_648R


_T_646
1
1Bitwise.scala 108:44<2&
_T_649R


_T_647


_T_648Cat.scala 30:58?2$
_T_650R


_T_645
3
2Bitwise.scala 108:44?2$
_T_651R


_T_650
0
0Bitwise.scala 108:18?2$
_T_652R


_T_650
1
1Bitwise.scala 108:44<2&
_T_653R


_T_651


_T_652Cat.scala 30:58<2&
_T_654R


_T_649


_T_653Cat.scala 30:58?2$
_T_655R


_T_644
6
4Bitwise.scala 108:44?2$
_T_656R


_T_655
1
0Bitwise.scala 108:18?2$
_T_657R


_T_656
0
0Bitwise.scala 108:18?2$
_T_658R


_T_656
1
1Bitwise.scala 108:44<2&
_T_659R


_T_657


_T_658Cat.scala 30:58?2$
_T_660R


_T_655
2
2Bitwise.scala 108:44<2&
_T_661R


_T_659


_T_660Cat.scala 30:58<2&
_T_662R


_T_654


_T_661Cat.scala 30:58<2&
_T_663R


_T_643


_T_662Cat.scala 30:58<2&
_T_664R


_T_610


_T_663Cat.scala 30:58I23
absSigSumExtraMaskR


_T_664	

1Cat.scala 30:58Q22
_T_666(R&

cFirstNormAbsSigSum
87
1MulAddRecFN.scala 424:32O20
_T_667&R$


_T_666

normTo2ShiftDistMulAddRecFN.scala 424:65Q22
_T_668(R&

cFirstNormAbsSigSum
31
0MulAddRecFN.scala 427:3992
_T_669R


_T_668MulAddRecFN.scala 427:19Q22
_T_670(R&


_T_669

absSigSumExtraMaskMulAddRecFN.scala 427:62F2'
_T_672R


_T_670	

0MulAddRecFN.scala 428:43Q22
_T_673(R&

cFirstNormAbsSigSum
31
0MulAddRecFN.scala 430:38Q22
_T_674(R&


_T_673

absSigSumExtraMaskMulAddRecFN.scala 430:61F2'
_T_676R


_T_674	

0MulAddRecFN.scala 431:43R23
_T_677)2'


	doIncrSig


_T_672


_T_676MulAddRecFN.scala 426:16<2&
_T_678R


_T_667


_T_677Cat.scala 30:58C2$
sigX3R


_T_678
56
0MulAddRecFN.scala 434:10D2%
_T_679R	

sigX3
56
55MulAddRecFN.scala 436:29K2,
sigX3Shift1R


_T_679	

0MulAddRecFN.scala 436:58c2D
_T_681:R8#:!
:


io
fromPreMulsExpSum

estNormDistMulAddRecFN.scala 437:4092
_T_682R


_T_681MulAddRecFN.scala 437:40>2
sExpX3R


_T_682
1MulAddRecFN.scala 437:40D2%
_T_683R	

sigX3
56
54MulAddRecFN.scala 439:25G2(
isZeroYR


_T_683	

0MulAddRecFN.scala 439:54e2F
_T_685<R:$:"
:


io
fromPreMulsignProd

doNegSignSumMulAddRecFN.scala 444:36]2>
signY523

	
isZeroY

signZeroNotEqOpSigns


_T_685MulAddRecFN.scala 442:12G2(
	sExpX3_13R


sExpX3
12
0MulAddRecFN.scala 446:27E2&
_T_686R


sExpX3
13
13MulAddRecFN.scala 448:34>2$
_T_687R


_T_686
0
0Bitwise.scala 71:15\2B
_T_690826



_T_687

720575940379279358	

08Bitwise.scala 71:12:2
_T_691R

	sExpX3_13primitives.scala 50:21C2&
_T_692R


_T_691
12
12primitives.scala 56:25B2%
_T_693R


_T_691
11
0primitives.scala 57:26C2&
_T_694R


_T_693
11
11primitives.scala 56:25B2%
_T_695R


_T_693
10
0primitives.scala 57:26C2&
_T_696R


_T_695
10
10primitives.scala 56:25A2$
_T_697R


_T_695
9
0primitives.scala 57:26A2$
_T_698R


_T_697
9
9primitives.scala 56:25A2$
_T_699R


_T_697
8
0primitives.scala 57:26A2$
_T_701R


_T_699
8
8primitives.scala 56:25A2$
_T_702R


_T_699
7
0primitives.scala 57:26A2$
_T_704R


_T_702
7
7primitives.scala 56:25A2$
_T_705R


_T_702
6
0primitives.scala 57:26A2$
_T_707R


_T_705
6
6primitives.scala 56:25A2$
_T_708R


_T_705
5
0primitives.scala 57:26]2@
_T_7116R4$R"

18446744073709551616A


_T_708primitives.scala 68:52C2&
_T_712R


_T_711
63
14primitives.scala 69:26@2%
_T_713R


_T_712
31
0Bitwise.scala 108:18@2%
_T_716R

65535
16Bitwise.scala 101:47K20
_T_717&R$


4294967295 


_T_716Bitwise.scala 101:21;2 
_T_718R	


_T_713
16Bitwise.scala 102:21A2&
_T_719R


_T_718


_T_717Bitwise.scala 102:31@2%
_T_720R


_T_713
15
0Bitwise.scala 102:46;2 
_T_721R


_T_720
16Bitwise.scala 102:6552
_T_722R


_T_717Bitwise.scala 102:77A2&
_T_723R


_T_721


_T_722Bitwise.scala 102:75A2&
_T_724R


_T_719


_T_723Bitwise.scala 102:39@2%
_T_725R


_T_717
23
0Bitwise.scala 101:28:2
_T_726R


_T_725
8Bitwise.scala 101:47A2&
_T_727R


_T_717


_T_726Bitwise.scala 101:21:2
_T_728R	


_T_724
8Bitwise.scala 102:21A2&
_T_729R


_T_728


_T_727Bitwise.scala 102:31@2%
_T_730R


_T_724
23
0Bitwise.scala 102:46:2
_T_731R


_T_730
8Bitwise.scala 102:6552
_T_732R


_T_727Bitwise.scala 102:77A2&
_T_733R


_T_731


_T_732Bitwise.scala 102:75A2&
_T_734R


_T_729


_T_733Bitwise.scala 102:39@2%
_T_735R


_T_727
27
0Bitwise.scala 101:28:2
_T_736R


_T_735
4Bitwise.scala 101:47A2&
_T_737R


_T_727


_T_736Bitwise.scala 101:21:2
_T_738R	


_T_734
4Bitwise.scala 102:21A2&
_T_739R


_T_738


_T_737Bitwise.scala 102:31@2%
_T_740R


_T_734
27
0Bitwise.scala 102:46:2
_T_741R


_T_740
4Bitwise.scala 102:6552
_T_742R


_T_737Bitwise.scala 102:77A2&
_T_743R


_T_741


_T_742Bitwise.scala 102:75A2&
_T_744R


_T_739


_T_743Bitwise.scala 102:39@2%
_T_745R


_T_737
29
0Bitwise.scala 101:28:2
_T_746R


_T_745
2Bitwise.scala 101:47A2&
_T_747R


_T_737


_T_746Bitwise.scala 101:21:2
_T_748R	


_T_744
2Bitwise.scala 102:21A2&
_T_749R


_T_748


_T_747Bitwise.scala 102:31@2%
_T_750R


_T_744
29
0Bitwise.scala 102:46:2
_T_751R


_T_750
2Bitwise.scala 102:6552
_T_752R


_T_747Bitwise.scala 102:77A2&
_T_753R


_T_751


_T_752Bitwise.scala 102:75A2&
_T_754R


_T_749


_T_753Bitwise.scala 102:39@2%
_T_755R


_T_747
30
0Bitwise.scala 101:28:2
_T_756R


_T_755
1Bitwise.scala 101:47A2&
_T_757R


_T_747


_T_756Bitwise.scala 101:21:2
_T_758R	


_T_754
1Bitwise.scala 102:21A2&
_T_759R


_T_758


_T_757Bitwise.scala 102:31@2%
_T_760R


_T_754
30
0Bitwise.scala 102:46:2
_T_761R


_T_760
1Bitwise.scala 102:6552
_T_762R


_T_757Bitwise.scala 102:77A2&
_T_763R


_T_761


_T_762Bitwise.scala 102:75A2&
_T_764R


_T_759


_T_763Bitwise.scala 102:39A2&
_T_765R


_T_712
49
32Bitwise.scala 108:44@2%
_T_766R


_T_765
15
0Bitwise.scala 108:18=2"
_T_769R

255
8Bitwise.scala 101:47F2+
_T_770!R

65535


_T_769Bitwise.scala 101:21:2
_T_771R	


_T_766
8Bitwise.scala 102:21A2&
_T_772R


_T_771


_T_770Bitwise.scala 102:31?2$
_T_773R


_T_766
7
0Bitwise.scala 102:46:2
_T_774R


_T_773
8Bitwise.scala 102:6552
_T_775R


_T_770Bitwise.scala 102:77A2&
_T_776R


_T_774


_T_775Bitwise.scala 102:75A2&
_T_777R


_T_772


_T_776Bitwise.scala 102:39@2%
_T_778R


_T_770
11
0Bitwise.scala 101:28:2
_T_779R


_T_778
4Bitwise.scala 101:47A2&
_T_780R


_T_770


_T_779Bitwise.scala 101:21:2
_T_781R	


_T_777
4Bitwise.scala 102:21A2&
_T_782R


_T_781


_T_780Bitwise.scala 102:31@2%
_T_783R


_T_777
11
0Bitwise.scala 102:46:2
_T_784R


_T_783
4Bitwise.scala 102:6552
_T_785R


_T_780Bitwise.scala 102:77A2&
_T_786R


_T_784


_T_785Bitwise.scala 102:75A2&
_T_787R


_T_782


_T_786Bitwise.scala 102:39@2%
_T_788R


_T_780
13
0Bitwise.scala 101:28:2
_T_789R


_T_788
2Bitwise.scala 101:47A2&
_T_790R


_T_780


_T_789Bitwise.scala 101:21:2
_T_791R	


_T_787
2Bitwise.scala 102:21A2&
_T_792R


_T_791


_T_790Bitwise.scala 102:31@2%
_T_793R


_T_787
13
0Bitwise.scala 102:46:2
_T_794R


_T_793
2Bitwise.scala 102:6552
_T_795R


_T_790Bitwise.scala 102:77A2&
_T_796R


_T_794


_T_795Bitwise.scala 102:75A2&
_T_797R


_T_792


_T_796Bitwise.scala 102:39@2%
_T_798R


_T_790
14
0Bitwise.scala 101:28:2
_T_799R


_T_798
1Bitwise.scala 101:47A2&
_T_800R


_T_790


_T_799Bitwise.scala 101:21:2
_T_801R	


_T_797
1Bitwise.scala 102:21A2&
_T_802R


_T_801


_T_800Bitwise.scala 102:31@2%
_T_803R


_T_797
14
0Bitwise.scala 102:46:2
_T_804R


_T_803
1Bitwise.scala 102:6552
_T_805R


_T_800Bitwise.scala 102:77A2&
_T_806R


_T_804


_T_805Bitwise.scala 102:75A2&
_T_807R


_T_802


_T_806Bitwise.scala 102:39A2&
_T_808R


_T_765
17
16Bitwise.scala 108:44?2$
_T_809R


_T_808
0
0Bitwise.scala 108:18?2$
_T_810R


_T_808
1
1Bitwise.scala 108:44<2&
_T_811R


_T_809


_T_810Cat.scala 30:58<2&
_T_812R


_T_807


_T_811Cat.scala 30:58<2&
_T_813R


_T_764


_T_812Cat.scala 30:5872
_T_814R


_T_813primitives.scala 65:36N21
_T_815'2%



_T_707	

0


_T_814primitives.scala 65:2172
_T_816R


_T_815primitives.scala 65:1772
_T_817R


_T_816primitives.scala 65:36N21
_T_818'2%



_T_704	

0


_T_817primitives.scala 65:2172
_T_819R


_T_818primitives.scala 65:1772
_T_820R


_T_819primitives.scala 65:36N21
_T_821'2%



_T_701	

0


_T_820primitives.scala 65:2172
_T_822R


_T_821primitives.scala 65:1772
_T_823R


_T_822primitives.scala 65:36N21
_T_824'2%



_T_698	

0


_T_823primitives.scala 65:2172
_T_825R


_T_824primitives.scala 65:17>2(
_T_827R


_T_825


15Cat.scala 30:58A2$
_T_828R


_T_697
9
9primitives.scala 56:25A2$
_T_829R


_T_697
8
0primitives.scala 57:26A2$
_T_830R


_T_829
8
8primitives.scala 56:25A2$
_T_831R


_T_829
7
0primitives.scala 57:26A2$
_T_832R


_T_831
7
7primitives.scala 56:25A2$
_T_833R


_T_831
6
0primitives.scala 57:26A2$
_T_834R


_T_833
6
6primitives.scala 56:25A2$
_T_835R


_T_833
5
0primitives.scala 57:26]2@
_T_8376R4$R"

18446744073709551616A


_T_835primitives.scala 68:52A2$
_T_838R


_T_837
3
0primitives.scala 69:26?2$
_T_839R


_T_838
1
0Bitwise.scala 108:18?2$
_T_840R


_T_839
0
0Bitwise.scala 108:18?2$
_T_841R


_T_839
1
1Bitwise.scala 108:44<2&
_T_842R


_T_840


_T_841Cat.scala 30:58?2$
_T_843R


_T_838
3
2Bitwise.scala 108:44?2$
_T_844R


_T_843
0
0Bitwise.scala 108:18?2$
_T_845R


_T_843
1
1Bitwise.scala 108:44<2&
_T_846R


_T_844


_T_845Cat.scala 30:58<2&
_T_847R


_T_842


_T_846Cat.scala 30:58N21
_T_849'2%



_T_834


_T_847	

0primitives.scala 59:20N21
_T_851'2%



_T_832


_T_849	

0primitives.scala 59:20N21
_T_853'2%



_T_830


_T_851	

0primitives.scala 59:20N21
_T_855'2%



_T_828


_T_853	

0primitives.scala 59:20M20
_T_856&2$



_T_696


_T_827


_T_855primitives.scala 61:20N21
_T_858'2%



_T_694


_T_856	

0primitives.scala 59:20N21
_T_860'2%



_T_692


_T_858	

0primitives.scala 59:20D2%
_T_861R	

sigX3
55
55MulAddRecFN.scala 450:26E2&
_T_862R


_T_860


_T_861MulAddRecFN.scala 449:75=2'
_T_864R


_T_862	

3Cat.scala 30:58H2)
	roundMaskR


_T_690


_T_864MulAddRecFN.scala 448:50A2"
_T_865R	

	roundMask
1MulAddRecFN.scala 454:3592
_T_866R


_T_865MulAddRecFN.scala 454:24N2/
roundPosMaskR


_T_866

	roundMaskMulAddRecFN.scala 454:40J2+
_T_867!R	

sigX3

roundPosMaskMulAddRecFN.scala 455:30K2,
roundPosBitR


_T_867	

0MulAddRecFN.scala 455:46A2"
_T_869R	

	roundMask
1MulAddRecFN.scala 456:45D2%
_T_870R	

sigX3


_T_869MulAddRecFN.scala 456:34M2.
anyRoundExtraR


_T_870	

0MulAddRecFN.scala 456:5082
_T_872R	

sigX3MulAddRecFN.scala 457:27A2"
_T_873R	

	roundMask
1MulAddRecFN.scala 457:45E2&
_T_874R


_T_872


_T_873MulAddRecFN.scala 457:34M2.
allRoundExtraR


_T_874	

0MulAddRecFN.scala 457:50S24
anyRound(R&

roundPosBit

anyRoundExtraMulAddRecFN.scala 458:32S24
allRound(R&

roundPosBit

allRoundExtraMulAddRecFN.scala 459:32i2J
roundDirectUp927
	

signY

roundingMode_min

roundingMode_maxMulAddRecFN.scala 460:28I2*
_T_877 R

	doIncrSig	

0MulAddRecFN.scala 462:10X29
_T_878/R-


_T_877

roundingMode_nearest_evenMulAddRecFN.scala 462:22J2+
_T_879!R


_T_878

roundPosBitMulAddRecFN.scala 462:51L2-
_T_880#R!


_T_879

anyRoundExtraMulAddRecFN.scala 463:60I2*
_T_882 R

	doIncrSig	

0MulAddRecFN.scala 464:10L2-
_T_883#R!


_T_882

roundDirectUpMulAddRecFN.scala 464:22G2(
_T_884R


_T_883


anyRoundMulAddRecFN.scala 464:49E2&
_T_885R


_T_880


_T_884MulAddRecFN.scala 463:78J2+
_T_886!R

	doIncrSig


allRoundMulAddRecFN.scala 465:49E2&
_T_887R


_T_885


_T_886MulAddRecFN.scala 464:65[2<
_T_8882R0

	doIncrSig

roundingMode_nearest_evenMulAddRecFN.scala 466:20J2+
_T_889!R


_T_888

roundPosBitMulAddRecFN.scala 466:49E2&
_T_890R


_T_887


_T_889MulAddRecFN.scala 465:65O20
_T_891&R$

	doIncrSig

roundDirectUpMulAddRecFN.scala 467:20F2'
_T_893R


_T_891	

1MulAddRecFN.scala 467:49F2'
roundUpR


_T_890


_T_893MulAddRecFN.scala 466:65K2,
_T_895"R 

roundPosBit	

0MulAddRecFN.scala 470:42X29
_T_896/R-

roundingMode_nearest_even


_T_895MulAddRecFN.scala 470:39L2-
_T_897#R!


_T_896

allRoundExtraMulAddRecFN.scala 470:56]2>
_T_8984R2

roundingMode_nearest_even

roundPosBitMulAddRecFN.scala 471:39M2.
_T_900$R"

anyRoundExtra	

0MulAddRecFN.scala 471:59E2&
_T_901R


_T_898


_T_900MulAddRecFN.scala 471:56U26
	roundEven)2'


	doIncrSig


_T_897


_T_901MulAddRecFN.scala 469:12H2)
_T_903R


allRound	

0MulAddRecFN.scala 473:39V27
inexactY+2)


	doIncrSig


_T_903


anyRoundMulAddRecFN.scala 473:27G2(
_T_904R	

sigX3

	roundMaskMulAddRecFN.scala 475:18>2
_T_905R	


_T_904
2MulAddRecFN.scala 475:30F2'
_T_907R


_T_905	

1MulAddRecFN.scala 475:35>2
_T_908R


_T_907
1MulAddRecFN.scala 475:35K2,
roundUp_sigY3R


_T_908
54
0MulAddRecFN.scala 475:45G2(
_T_910R
	
roundUp	

0MulAddRecFN.scala 477:13I2*
_T_912 R

	roundEven	

0MulAddRecFN.scala 477:26E2&
_T_913R


_T_910


_T_912MulAddRecFN.scala 477:23<2
_T_914R

	roundMaskMulAddRecFN.scala 477:48D2%
_T_915R	

sigX3


_T_914MulAddRecFN.scala 477:46>2
_T_916R	


_T_915
2MulAddRecFN.scala 477:59P21
_T_918'2%



_T_913


_T_916	

0MulAddRecFN.scala 477:12X29
_T_920/2-

	
roundUp

roundUp_sigY3	

0MulAddRecFN.scala 478:12E2&
_T_921R


_T_918


_T_920MulAddRecFN.scala 477:79A2"
_T_922R	

	roundMask
1MulAddRecFN.scala 479:6492
_T_923R


_T_922MulAddRecFN.scala 479:53L2-
_T_924#R!

roundUp_sigY3


_T_923MulAddRecFN.scala 479:51S24
_T_926*2(


	roundEven


_T_924	

0MulAddRecFN.scala 479:12D2%
sigY3R


_T_921


_T_926MulAddRecFN.scala 478:79D2%
_T_927R	

sigY3
54
54MulAddRecFN.scala 482:18F2'
_T_929R


sExpX3	

1MulAddRecFN.scala 482:41>2
_T_930R


_T_929
1MulAddRecFN.scala 482:41P21
_T_932'2%



_T_927


_T_930	

0MulAddRecFN.scala 482:12D2%
_T_933R	

sigY3
53
53MulAddRecFN.scala 483:18P21
_T_935'2%



_T_933


sExpX3	

0MulAddRecFN.scala 483:12E2&
_T_936R


_T_932


_T_935MulAddRecFN.scala 482:61D2%
_T_937R	

sigY3
54
53MulAddRecFN.scala 484:19F2'
_T_939R


_T_937	

0MulAddRecFN.scala 484:44F2'
_T_941R


sExpX3	

1MulAddRecFN.scala 485:2092
_T_942R


_T_941MulAddRecFN.scala 485:20>2
_T_943R


_T_942
1MulAddRecFN.scala 485:20P21
_T_945'2%



_T_939


_T_943	

0MulAddRecFN.scala 484:12D2%
sExpYR


_T_936


_T_945MulAddRecFN.scala 483:61A2"
expYR	

sExpY
11
0MulAddRecFN.scala 488:21C2$
_T_946R	

sigY3
51
0MulAddRecFN.scala 490:31C2$
_T_947R	

sigY3
52
1MulAddRecFN.scala 490:55T25
fractY+2)


sigX3Shift1


_T_946


_T_947MulAddRecFN.scala 490:12D2%
_T_948R	

sExpY
12
10MulAddRecFN.scala 492:27I2*
	overflowYR


_T_948	

3MulAddRecFN.scala 492:56F2(
_T_951R
	
isZeroY	

0MulAddRecFN.scala 495:9D2%
_T_952R	

sExpY
12
12MulAddRecFN.scala 496:19C2$
_T_953R	

sExpY
11
0MulAddRecFN.scala 496:43H2)
_T_955R


_T_953

974
MulAddRecFN.scala 496:57E2&
_T_956R


_T_952


_T_955MulAddRecFN.scala 496:34N2/
totalUnderflowYR


_T_951


_T_956MulAddRecFN.scala 495:19E2&
_T_957R


sExpX3
13
13MulAddRecFN.scala 499:20\2=
_T_960321


sigX3Shift1

1026

1025MulAddRecFN.scala 501:26H2)
_T_961R

	sExpX3_13


_T_960MulAddRecFN.scala 500:29E2&
_T_962R


_T_957


_T_961MulAddRecFN.scala 499:35K2,

underflowYR


inexactY


_T_962MulAddRecFN.scala 498:22N2/
_T_963%R#

roundingMode_min	

signYMulAddRecFN.scala 506:27E2&
_T_965R	

signY	

0MulAddRecFN.scala 506:61O20
_T_966&R$

roundingMode_max


_T_965MulAddRecFN.scala 506:58I2*

roundMagUpR


_T_963


_T_966MulAddRecFN.scala 506:37j2K
overflowY_roundMagUp3R1

roundingMode_nearest_even


roundMagUpMulAddRecFN.scala 507:58Q22

mulSpecial$R"


isSpecialA


isSpecialBMulAddRecFN.scala 511:33Q22

addSpecial$R"


mulSpecial


isSpecialCMulAddRecFN.scala 512:33o2P
notSpecial_addZeros9R7&:$
:


io
fromPreMul
isZeroProd
	
isZeroCMulAddRecFN.scala 513:56J2+
_T_968!R


addSpecial	

0MulAddRecFN.scala 514:22S24
_T_970*R(

notSpecial_addZeros	

0MulAddRecFN.scala 514:38I2*

commonCaseR


_T_968


_T_970MulAddRecFN.scala 514:35F2'
_T_971R


isInfA
	
isZeroBMulAddRecFN.scala 517:17F2'
_T_972R
	
isZeroA


isInfBMulAddRecFN.scala 517:41E2&
_T_973R


_T_971


_T_972MulAddRecFN.scala 517:29F2'
_T_975R


isNaNA	

0MulAddRecFN.scala 518:14F2'
_T_977R


isNaNB	

0MulAddRecFN.scala 518:26E2&
_T_978R


_T_975


_T_977MulAddRecFN.scala 518:23E2&
_T_979R


isInfA


isInfBMulAddRecFN.scala 518:46E2&
_T_980R


_T_978


_T_979MulAddRecFN.scala 518:35E2&
_T_981R


_T_980


isInfCMulAddRecFN.scala 518:57H2)
_T_982R


_T_981

	doSubMagsMulAddRecFN.scala 518:67P21
notSigNaN_invalidR


_T_973


_T_982MulAddRecFN.scala 517:52K2,
_T_983"R 

	isSigNaNA

	isSigNaNBMulAddRecFN.scala 519:29H2)
_T_984R


_T_983

	isSigNaNCMulAddRecFN.scala 519:42Q22
invalid'R%


_T_984

notSigNaN_invalidMulAddRecFN.scala 519:55N2/
overflow#R!


commonCase

	overflowYMulAddRecFN.scala 520:32P21
	underflow$R"


commonCase


underflowYMulAddRecFN.scala 521:32K2,
_T_985"R 


commonCase


inexactYMulAddRecFN.scala 522:43H2)
inexactR


overflow


_T_985MulAddRecFN.scala 522:28S24
_T_986*R(

notSpecial_addZeros
	
isZeroYMulAddRecFN.scala 525:29\2=
notSpecial_isZeroOut%R#


_T_986

totalUnderflowYMulAddRecFN.scala 525:40R23
_T_987)R'


commonCase

totalUnderflowYMulAddRecFN.scala 526:41U26
pegMinFiniteMagOut R


_T_987


roundMagUpMulAddRecFN.scala 526:60T25
_T_989+R)

overflowY_roundMagUp	

0MulAddRecFN.scala 527:42S24
pegMaxFiniteMagOutR


overflow


_T_989MulAddRecFN.scala 527:39E2&
_T_990R


isInfA


isInfBMulAddRecFN.scala 529:16E2&
_T_991R


_T_990


isInfCMulAddRecFN.scala 529:26U26
_T_992,R*


overflow

overflowY_roundMagUpMulAddRecFN.scala 529:49N2/
notNaN_isInfOutR


_T_991


_T_992MulAddRecFN.scala 529:36E2&
_T_993R


isNaNA


isNaNBMulAddRecFN.scala 530:27E2&
_T_994R


_T_993


isNaNCMulAddRecFN.scala 530:37R23
isNaNOut'R%


_T_994

notSigNaN_invalidMulAddRecFN.scala 530:47I2*
_T_996 R

	doSubMags	

0MulAddRecFN.scala 533:10^2?
_T_9975R3


_T_996#:!
:


io
fromPreMulopSignCMulAddRecFN.scala 533:51J2+
_T_999!R


isSpecialC	

0MulAddRecFN.scala 534:24J2+
_T_1000 R


mulSpecial


_T_999MulAddRecFN.scala 534:21a2B
_T_10017R5
	
_T_1000$:"
:


io
fromPreMulsignProdMulAddRecFN.scala 534:51G2(
_T_1002R


_T_997
	
_T_1001MulAddRecFN.scala 533:78K2,
_T_1004!R


mulSpecial	

0MulAddRecFN.scala 535:10K2,
_T_1005!R
	
_T_1004


isSpecialCMulAddRecFN.scala 535:23`2A
_T_10066R4
	
_T_1005#:!
:


io
fromPreMulopSignCMulAddRecFN.scala 535:51H2)
_T_1007R
	
_T_1002
	
_T_1006MulAddRecFN.scala 534:78K2,
_T_1009!R


mulSpecial	

0MulAddRecFN.scala 536:10T25
_T_1010*R(
	
_T_1009

notSpecial_addZerosMulAddRecFN.scala 536:23J2+
_T_1011 R
	
_T_1010

	doSubMagsMulAddRecFN.scala 536:46U26
_T_1012+R)
	
_T_1011

signZeroNotEqOpSignsMulAddRecFN.scala 536:59T25
uncommonCaseSignOutR
	
_T_1007
	
_T_1012MulAddRecFN.scala 535:78I2*
_T_1014R


isNaNOut	

0MulAddRecFN.scala 538:20T25
_T_1015*R(
	
_T_1014

uncommonCaseSignOutMulAddRecFN.scala 538:31I2*
_T_1016R


commonCase	

signYMulAddRecFN.scala 538:70H2)
signOutR
	
_T_1015
	
_T_1016MulAddRecFN.scala 538:55c2D
_T_1019927


notSpecial_isZeroOut

3584	

0MulAddRecFN.scala 541:18;2
_T_1020R
	
_T_1019MulAddRecFN.scala 541:14E2&
_T_1021R

expY
	
_T_1020MulAddRecFN.scala 540:15=2
_T_1023R

974MulAddRecFN.scala 546:19^2?
_T_1025422


pegMinFiniteMagOut
	
_T_1023	

0MulAddRecFN.scala 545:18;2
_T_1026R
	
_T_1025MulAddRecFN.scala 545:14H2)
_T_1027R
	
_T_1021
	
_T_1026MulAddRecFN.scala 544:17a2B
_T_1030725


pegMaxFiniteMagOut

1024	

0MulAddRecFN.scala 549:18;2
_T_1031R
	
_T_1030MulAddRecFN.scala 549:14H2)
_T_1032R
	
_T_1027
	
_T_1031MulAddRecFN.scala 548:17]2>
_T_1035321


notNaN_isInfOut

512
	

0MulAddRecFN.scala 553:18;2
_T_1036R
	
_T_1035MulAddRecFN.scala 553:14H2)
_T_1037R
	
_T_1032
	
_T_1036MulAddRecFN.scala 552:17`2A
_T_1040624


pegMinFiniteMagOut

974
	

0MulAddRecFN.scala 557:16H2)
_T_1041R
	
_T_1037
	
_T_1040MulAddRecFN.scala 556:18a2B
_T_1044725


pegMaxFiniteMagOut

3071	

0MulAddRecFN.scala 558:16H2)
_T_1045R
	
_T_1041
	
_T_1044MulAddRecFN.scala 557:74^2?
_T_1048422


notNaN_isInfOut

3072	

0MulAddRecFN.scala 562:16H2)
_T_1049R
	
_T_1045
	
_T_1048MulAddRecFN.scala 561:15W28
_T_1052-2+



isNaNOut

3584	

0MulAddRecFN.scala 566:16G2(
expOutR
	
_T_1049
	
_T_1052MulAddRecFN.scala 565:15S24
_T_1053)R'

totalUnderflowY


roundMagUpMulAddRecFN.scala 568:30I2*
_T_1054R
	
_T_1053


isNaNOutMulAddRecFN.scala 568:45A2"
_T_1056R	

1
51MulAddRecFN.scala 569:34T25
_T_1058*2(



isNaNOut
	
_T_1056	

0MulAddRecFN.scala 569:16R23
_T_1059(2&

	
_T_1054
	
_T_1058


fractYMulAddRecFN.scala 568:12K21
_T_1060&R$

pegMaxFiniteMagOut
0
0Bitwise.scala 71:15]2C
_T_1063826

	
_T_1060

45035996273704954	

04Bitwise.scala 71:12I2*
fractOutR
	
_T_1059
	
_T_1063MulAddRecFN.scala 571:11>2(
_T_1064R
	
signOut


expOutCat.scala 30:58@2*
_T_1065R
	
_T_1064


fractOutCat.scala 30:58=z
:


ioout
	
_T_1065MulAddRecFN.scala 574:12A2+
_T_1067 R

	underflow
	
inexactCat.scala 30:58?2)
_T_1068R
	
invalid	

0Cat.scala 30:58@2*
_T_1069R
	
_T_1068


overflowCat.scala 30:58?2)
_T_1070R
	
_T_1069
	
_T_1067Cat.scala 30:58Hz)
:


ioexceptionFlags
	
_T_1070MulAddRecFN.scala 575:23
FPU