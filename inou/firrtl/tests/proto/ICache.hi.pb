
๑ฏ
ใฏ฿ฏ
ICache
clock" 
reset
ฮ
ioล*ย
8req/*-
valid

bits*
addr
'
s1_paddr
 
s1_kill

s2_kill

_respW*U
ready

valid

/bits'*%
data

	datablock
@

invalidate

ฬmemฤ*ม
พ0ธ*ต
ฒaฌ*ฉ
ready

valid

bitsz*x
opcode

param

size

source

address
 
mask

data
@
ดbฌ*ฉ
ready

valid

bitsz*x
opcode

param

size

source

address
 
mask

data
@
ณcญ*ช
ready

valid

bits{*y
opcode

param

size

source

address
 
data
@
error

วdฟ*ผ
ready

valid

bits*
opcode

param

size

source

sink

addr_lo

data
@
error

GeB*@
ready

valid

bits*
sink



io
 


io
 K2
state
	

clock"	

reset*	

0ICache.scala 67:18W>
invalidated
	

clock"	

0*

invalidatedICache.scala 68:24P27
stall.R,:
:


iorespready	

0ICache.scala 69:15W>
refill_addr
 	

clock"	

0*

refill_addrICache.scala 71:241

s1_any_tag_hit
ICache.scala 72:28.


s1_any_tag_hitICache.scala 72:28N5
s1_valid
	

clock"	

reset*	

0ICache.scala 74:21I20
_T_221&R$:


ios1_kill	

0ICache.scala 75:31A2(
_T_222R


s1_valid


_T_221ICache.scala 75:28?2&
_T_223R	

state	

0ICache.scala 75:52B2)
	out_validR


_T_222


_T_223ICache.scala 75:43H2/
s1_idx%R#:


ios1_paddr
11
6ICache.scala 76:27I20
s1_tag&R$:


ios1_paddr
31
12ICache.scala 77:27J21
s1_hit'R%

	out_valid

s1_any_tag_hitICache.scala 78:26H2/
_T_225%R#

s1_any_tag_hit	

0ICache.scala 79:30C2*
s1_missR

	out_valid


_T_225ICache.scala 79:27?2&
_T_226R	

state	

0ICache.scala 81:40O26
_T_227,R*:
:


ioreqvalid


_T_226ICache.scala 81:31A2(
_T_228R

	out_valid	

stallICache.scala 81:67@2'
_T_230R


_T_228	

0ICache.scala 81:55A2(
s0_validR


_T_227


_T_230ICache.scala 81:52A2(
_T_231R

	out_valid	

stallICache.scala 84:37A2(
_T_232R


s0_valid


_T_231ICache.scala 84:243z



s1_valid


_T_232ICache.scala 84:12?2&
_T_233R	

state	

0ICache.scala 86:26@2'
_T_234R
	
s1_miss


_T_233ICache.scala 86:17g:N



_T_234@z'


refill_addr:


ios1_paddrICache.scala 87:17ICache.scala 86:39H2/

refill_tag!R

refill_addr
31
12ICache.scala 89:31G2.

refill_idx R

refill_addr
11
6ICache.scala 90:31~2b
_T_235XRV(:&
:
:
:


iomem0dready(:&
:
:
:


iomem0dvalidDecoupled.scala 30:37=2#
_T_237RR

255package.scala 19:64g2M
_T_238CRA



_T_2371:/
':%
:
:
:


iomem0dbitssizepackage.scala 19:71>2$
_T_239R


_T_238
7
0package.scala 19:7642
_T_240R


_T_239package.scala 19:4082
_T_241R	


_T_240
3Edges.scala 198:59e2M
_T_242CRA3:1
':%
:
:
:


iomem0dbitsopcode
0
0Edges.scala 90:36J21
_T_244'2%



_T_242


_T_241	

0Edges.scala 199:14I3
_T_246
	

clock"	

reset*	

0Reg.scala 26:44@2'
_T_248R


_T_246	

1Edges.scala 208:2832
_T_249R


_T_248Edges.scala 208:2882
_T_250R


_T_249
1Edges.scala 208:28@2'
_T_252R


_T_246	

0Edges.scala 209:25@2'
_T_254R


_T_246	

1Edges.scala 210:25@2'
_T_256R


_T_244	

0Edges.scala 210:47?2&
_T_257R


_T_254


_T_256Edges.scala 210:37D2+
refill_doneR


_T_257


_T_235Edges.scala 211:2232
_T_258R


_T_250Edges.scala 212:27C2*

refill_cntR


_T_244


_T_258Edges.scala 212:25ค:



_T_235I20
_T_259&2$



_T_252


_T_244


_T_250Edges.scala 214:211z



_T_246


_T_259Edges.scala 214:15Edges.scala 213:17Pz7
(:&
:
:
:


iomem0dready	

1ICache.scala 92:18J3
_T_262
	

clock"	

reset*	

1LFSR.scala 22:19:๋

	
s1_miss;2$
_T_263R


_T_262
0
0LFSR.scala 23:40;2$
_T_264R


_T_262
2
2LFSR.scala 23:48=2&
_T_265R


_T_263


_T_264LFSR.scala 23:43;2$
_T_266R


_T_262
3
3LFSR.scala 23:56=2&
_T_267R


_T_265


_T_266LFSR.scala 23:51;2$
_T_268R


_T_262
5
5LFSR.scala 23:64=2&
_T_269R


_T_267


_T_268LFSR.scala 23:59<2%
_T_270R


_T_262
15
1LFSR.scala 23:73<2&
_T_271R


_T_269


_T_270Cat.scala 30:58/z



_T_262


_T_271LFSR.scala 23:29LFSR.scala 23:22?2&
repl_wayR


_T_262
1
0ICache.scala 95:56N5
	tag_array2


 (2	tag_rdata:_T_328J
@ICache.scala 97:25B(
&:$
:


	tag_array	tag_rdataaddrICache.scala 97:25A'
%:#
:


	tag_array	tag_rdataclkICache.scala 97:25Lz3
$:"
:


	tag_array	tag_rdataen	

0ICache.scala 97:25?%
#:!
:


	tag_array_T_328addrICache.scala 97:25>$
": 
:


	tag_array_T_328clkICache.scala 97:25Iz0
!:
:


	tag_array_T_328en	

0ICache.scala 97:25?%
#:!
:


	tag_array_T_328dataICache.scala 97:25?%
#:!
:


	tag_array_T_328maskICache.scala 97:25W2>
_T_2824R2#:!
:
:


ioreqbitsaddr
11
6ICache.scala 98:42E2,
_T_284"R 

refill_done	

0ICache.scala 98:70A2(
_T_285R


_T_284


s0_validICache.scala 98:83

_T_287

 



_T_287
 ฦ:พ



_T_285z



_T_287


_T_282
 .2'
_T_289R


_T_287	

0
 +2$
_T_290R


_T_289
5
0
 :z3
$:"
:


	tag_array	tag_rdataen	

1
 ;z4
&:$
:


	tag_array	tag_rdataaddr


_T_290
 9z2
%:#
:


	tag_array	tag_rdataclk	

clock
 
 ์:า


refill_done0

_T_3042


ICache.scala 101:48'



_T_304ICache.scala 101:48?z%
B



_T_304
0


refill_tagICache.scala 101:48?z%
B



_T_304
1


refill_tagICache.scala 101:48?z%
B



_T_304
2


refill_tagICache.scala 101:48?z%
B



_T_304
3


refill_tagICache.scala 101:48C2)
_T_312R


repl_way	

0ICache.scala 101:84C2)
_T_314R


repl_way	

1ICache.scala 101:84C2)
_T_316R


repl_way	

2ICache.scala 101:84C2)
_T_318R


repl_way	

3ICache.scala 101:840

_T_3212


ICache.scala 101:74'



_T_321ICache.scala 101:74;z!
B



_T_321
0


_T_312ICache.scala 101:74;z!
B



_T_321
1


_T_314ICache.scala 101:74;z!
B



_T_321
2


_T_316ICache.scala 101:74;z!
B



_T_321
3


_T_318ICache.scala 101:74<z5
#:!
:


	tag_array_T_328addr


refill_idx
 6z/
": 
:


	tag_array_T_328clk	

clock
 7z0
!:
:


	tag_array_T_328en	

1
 Bz;
,B*
#:!
:


	tag_array_T_328mask
0	

0
 Bz;
,B*
#:!
:


	tag_array_T_328mask
1	

0
 Bz;
,B*
#:!
:


	tag_array_T_328mask
2	

0
 Bz;
,B*
#:!
:


	tag_array_T_328mask
3	

0
 ญ:ฅ
B



_T_321
0JzC
,B*
#:!
:


	tag_array_T_328data
0B



_T_304
0
 Bz;
,B*
#:!
:


	tag_array_T_328mask
0	

1
 
 ญ:ฅ
B



_T_321
1JzC
,B*
#:!
:


	tag_array_T_328data
1B



_T_304
1
 Bz;
,B*
#:!
:


	tag_array_T_328mask
1	

1
 
 ญ:ฅ
B



_T_321
2JzC
,B*
#:!
:


	tag_array_T_328data
2B



_T_304
2
 Bz;
,B*
#:!
:


	tag_array_T_328mask
2	

1
 
 ญ:ฅ
B



_T_321
3JzC
,B*
#:!
:


	tag_array_T_328data
3B



_T_304
3
 Bz;
,B*
#:!
:


	tag_array_T_328mask
3	

1
 
 ICache.scala 99:22Q7
vb_array
	

clock"	

reset*


0ICache.scala 104:21F2,
_T_342"R 

invalidated	

0ICache.scala 105:24E2+
_T_343!R

refill_done


_T_342ICache.scala 105:21ฅ:



_T_343B2,
_T_344"R 


repl_way


refill_idxCat.scala 30:58A2'
_T_347R
	

1


_T_344ICache.scala 106:32B2(
_T_348R


vb_array


_T_347ICache.scala 106:3262
_T_349R


vb_arrayICache.scala 106:32@2&
_T_350R


_T_349


_T_347ICache.scala 106:3242
_T_351R


_T_350ICache.scala 106:32K21
_T_352'2%
	

1


_T_348


_T_351ICache.scala 106:324z



vb_array


_T_352ICache.scala 106:14ICache.scala 105:38ค:
:


io
invalidate5z



vb_array	

0ICache.scala 109:148z


invalidated	

1ICache.scala 110:17ICache.scala 108:246

s1_disparity2


ICache.scala 112:26-


s1_disparityICache.scala 112:26Q27
_T_364-R+


s1_validB


s1_disparity
0ICache.scala 114:20 :



_T_364=2'
_T_366R	

0


s1_idxCat.scala 30:58A2'
_T_369R
	

1


_T_366ICache.scala 114:69B2(
_T_370R


vb_array


_T_369ICache.scala 114:6962
_T_371R


vb_arrayICache.scala 114:69@2&
_T_372R


_T_371


_T_369ICache.scala 114:6942
_T_373R


_T_372ICache.scala 114:69K21
_T_374'2%
	

0


_T_370


_T_373ICache.scala 114:694z



vb_array


_T_374ICache.scala 114:51ICache.scala 114:40Q27
_T_375-R+


s1_validB


s1_disparity
1ICache.scala 114:20 :



_T_375=2'
_T_377R	

1


s1_idxCat.scala 30:58A2'
_T_380R
	

1


_T_377ICache.scala 114:69B2(
_T_381R


vb_array


_T_380ICache.scala 114:6962
_T_382R


vb_arrayICache.scala 114:69@2&
_T_383R


_T_382


_T_380ICache.scala 114:6942
_T_384R


_T_383ICache.scala 114:69K21
_T_385'2%
	

0


_T_381


_T_384ICache.scala 114:694z



vb_array


_T_385ICache.scala 114:51ICache.scala 114:40Q27
_T_386-R+


s1_validB


s1_disparity
2ICache.scala 114:20 :



_T_386=2'
_T_388R	

2


s1_idxCat.scala 30:58A2'
_T_391R
	

1


_T_388ICache.scala 114:69B2(
_T_392R


vb_array


_T_391ICache.scala 114:6962
_T_393R


vb_arrayICache.scala 114:69@2&
_T_394R


_T_393


_T_391ICache.scala 114:6942
_T_395R


_T_394ICache.scala 114:69K21
_T_396'2%
	

0


_T_392


_T_395ICache.scala 114:694z



vb_array


_T_396ICache.scala 114:51ICache.scala 114:40Q27
_T_397-R+


s1_validB


s1_disparity
3ICache.scala 114:20 :



_T_397=2'
_T_399R	

3


s1_idxCat.scala 30:58A2'
_T_402R
	

1


_T_399ICache.scala 114:69B2(
_T_403R


vb_array


_T_402ICache.scala 114:6962
_T_404R


vb_arrayICache.scala 114:69@2&
_T_405R


_T_404


_T_402ICache.scala 114:6942
_T_406R


_T_405ICache.scala 114:69K21
_T_407'2%
	

0


_T_403


_T_406ICache.scala 114:694z



vb_array


_T_407ICache.scala 114:51ICache.scala 114:406

s1_tag_match2


ICache.scala 116:26-


s1_tag_matchICache.scala 116:264


s1_tag_hit2


ICache.scala 117:24+



s1_tag_hitICache.scala 117:241

s1_dout2


@ICache.scala 118:21(

	
s1_doutICache.scala 118:21XB
s1_dout_valid
	

clock"	

0*

s1_dout_validReg.scala 14:447z!


s1_dout_valid


s0_validReg.scala 14:44M23
_T_436)R':


io
invalidate	

0ICache.scala 122:17I2/
_T_438%R#:


ios1_paddr
11
6ICache.scala 122:68=2'
_T_439R	

0


_T_438Cat.scala 30:58B2(
_T_440R


vb_array


_T_439ICache.scala 122:43>2$
_T_441R


_T_440
0
0ICache.scala 122:43>2$
_T_442R


_T_441
0
0ICache.scala 122:97@2&
_T_443R


_T_436


_T_442ICache.scala 122:32>2(
_T_446R	

0	

0Ecc.scala 14:27J4
_T_448
	

clock"	

0*


_T_448Reg.scala 34:16Y:C


s1_dout_valid.z



_T_448


_T_446Reg.scala 35:23Reg.scala 35:19Q27
_T_449-2+


s1_dout_valid


_T_446


_T_448Package.scala 27:42d2J
_T_450@R>/B-
&:$
:


	tag_array	tag_rdatadata
0
19
0ICache.scala 125:32@2&
_T_451R


_T_450


s1_tagICache.scala 125:46J4
_T_453
	

clock"	

0*


_T_453Reg.scala 34:16Y:C


s1_dout_valid.z



_T_453


_T_451Reg.scala 35:23Reg.scala 35:19Q27
_T_454-2+


s1_dout_valid


_T_451


_T_453Package.scala 27:42Az'
B


s1_tag_match
0


_T_454ICache.scala 125:21O25
_T_455+R)


_T_443B


s1_tag_match
0ICache.scala 126:28?z%
B



s1_tag_hit
0


_T_455ICache.scala 126:19>2(
_T_458R	

0	

0Ecc.scala 14:27@2&
_T_459R


_T_449


_T_458ICache.scala 127:51@2&
_T_460R


_T_443


_T_459ICache.scala 127:30Az'
B


s1_disparity
0


_T_460ICache.scala 127:21M23
_T_462)R':


io
invalidate	

0ICache.scala 122:17I2/
_T_464%R#:


ios1_paddr
11
6ICache.scala 122:68=2'
_T_465R	

1


_T_464Cat.scala 30:58B2(
_T_466R


vb_array


_T_465ICache.scala 122:43>2$
_T_467R


_T_466
0
0ICache.scala 122:43>2$
_T_468R


_T_467
0
0ICache.scala 122:97@2&
_T_469R


_T_462


_T_468ICache.scala 122:32>2(
_T_472R	

0	

0Ecc.scala 14:27J4
_T_474
	

clock"	

0*


_T_474Reg.scala 34:16Y:C


s1_dout_valid.z



_T_474


_T_472Reg.scala 35:23Reg.scala 35:19Q27
_T_475-2+


s1_dout_valid


_T_472


_T_474Package.scala 27:42d2J
_T_476@R>/B-
&:$
:


	tag_array	tag_rdatadata
1
19
0ICache.scala 125:32@2&
_T_477R


_T_476


s1_tagICache.scala 125:46J4
_T_479
	

clock"	

0*


_T_479Reg.scala 34:16Y:C


s1_dout_valid.z



_T_479


_T_477Reg.scala 35:23Reg.scala 35:19Q27
_T_480-2+


s1_dout_valid


_T_477


_T_479Package.scala 27:42Az'
B


s1_tag_match
1


_T_480ICache.scala 125:21O25
_T_481+R)


_T_469B


s1_tag_match
1ICache.scala 126:28?z%
B



s1_tag_hit
1


_T_481ICache.scala 126:19>2(
_T_484R	

0	

0Ecc.scala 14:27@2&
_T_485R


_T_475


_T_484ICache.scala 127:51@2&
_T_486R


_T_469


_T_485ICache.scala 127:30Az'
B


s1_disparity
1


_T_486ICache.scala 127:21M23
_T_488)R':


io
invalidate	

0ICache.scala 122:17I2/
_T_490%R#:


ios1_paddr
11
6ICache.scala 122:68=2'
_T_491R	

2


_T_490Cat.scala 30:58B2(
_T_492R


vb_array


_T_491ICache.scala 122:43>2$
_T_493R


_T_492
0
0ICache.scala 122:43>2$
_T_494R


_T_493
0
0ICache.scala 122:97@2&
_T_495R


_T_488


_T_494ICache.scala 122:32>2(
_T_498R	

0	

0Ecc.scala 14:27J4
_T_500
	

clock"	

0*


_T_500Reg.scala 34:16Y:C


s1_dout_valid.z



_T_500


_T_498Reg.scala 35:23Reg.scala 35:19Q27
_T_501-2+


s1_dout_valid


_T_498


_T_500Package.scala 27:42d2J
_T_502@R>/B-
&:$
:


	tag_array	tag_rdatadata
2
19
0ICache.scala 125:32@2&
_T_503R


_T_502


s1_tagICache.scala 125:46J4
_T_505
	

clock"	

0*


_T_505Reg.scala 34:16Y:C


s1_dout_valid.z



_T_505


_T_503Reg.scala 35:23Reg.scala 35:19Q27
_T_506-2+


s1_dout_valid


_T_503


_T_505Package.scala 27:42Az'
B


s1_tag_match
2


_T_506ICache.scala 125:21O25
_T_507+R)


_T_495B


s1_tag_match
2ICache.scala 126:28?z%
B



s1_tag_hit
2


_T_507ICache.scala 126:19>2(
_T_510R	

0	

0Ecc.scala 14:27@2&
_T_511R


_T_501


_T_510ICache.scala 127:51@2&
_T_512R


_T_495


_T_511ICache.scala 127:30Az'
B


s1_disparity
2


_T_512ICache.scala 127:21M23
_T_514)R':


io
invalidate	

0ICache.scala 122:17I2/
_T_516%R#:


ios1_paddr
11
6ICache.scala 122:68=2'
_T_517R	

3


_T_516Cat.scala 30:58B2(
_T_518R


vb_array


_T_517ICache.scala 122:43>2$
_T_519R


_T_518
0
0ICache.scala 122:43>2$
_T_520R


_T_519
0
0ICache.scala 122:97@2&
_T_521R


_T_514


_T_520ICache.scala 122:32>2(
_T_524R	

0	

0Ecc.scala 14:27J4
_T_526
	

clock"	

0*


_T_526Reg.scala 34:16Y:C


s1_dout_valid.z



_T_526


_T_524Reg.scala 35:23Reg.scala 35:19Q27
_T_527-2+


s1_dout_valid


_T_524


_T_526Package.scala 27:42d2J
_T_528@R>/B-
&:$
:


	tag_array	tag_rdatadata
3
19
0ICache.scala 125:32@2&
_T_529R


_T_528


s1_tagICache.scala 125:46J4
_T_531
	

clock"	

0*


_T_531Reg.scala 34:16Y:C


s1_dout_valid.z



_T_531


_T_529Reg.scala 35:23Reg.scala 35:19Q27
_T_532-2+


s1_dout_valid


_T_529


_T_531Package.scala 27:42Az'
B


s1_tag_match
3


_T_532ICache.scala 125:21O25
_T_533+R)


_T_521B


s1_tag_match
3ICache.scala 126:28?z%
B



s1_tag_hit
3


_T_533ICache.scala 126:19>2(
_T_536R	

0	

0Ecc.scala 14:27@2&
_T_537R


_T_527


_T_536ICache.scala 127:51@2&
_T_538R


_T_521


_T_537ICache.scala 127:30Az'
B


s1_disparity
3


_T_538ICache.scala 127:21Z2@
_T_5396R4B



s1_tag_hit
0B



s1_tag_hit
1ICache.scala 129:44M23
_T_540)R'


_T_539B



s1_tag_hit
2ICache.scala 129:44M23
_T_541)R'


_T_540B



s1_tag_hit
3ICache.scala 129:44^2D
_T_542:R8B


s1_disparity
0B


s1_disparity
1ICache.scala 129:78O25
_T_543+R)


_T_542B


s1_disparity
2ICache.scala 129:78O25
_T_544+R)


_T_543B


s1_disparity
3ICache.scala 129:78A2'
_T_546R


_T_544	

0ICache.scala 129:52@2&
_T_547R


_T_541


_T_546ICache.scala 129:49:z 


s1_any_tag_hit


_T_547ICache.scala 129:18D*
_T_550
@ (2_T_566:_T_556J
 ICache.scala 132:28="
 :
:



_T_550_T_566addrICache.scala 132:28<!
:
:



_T_550_T_566clkICache.scala 132:28Gz-
:
:



_T_550_T_566en	

0ICache.scala 132:28="
 :
:



_T_550_T_556addrICache.scala 132:28<!
:
:



_T_550_T_556clkICache.scala 132:28Gz-
:
:



_T_550_T_556en	

0ICache.scala 132:28="
 :
:



_T_550_T_556dataICache.scala 132:28="
 :
:



_T_550_T_556maskICache.scala 132:28C2)
_T_552R


repl_way	

0ICache.scala 133:42^2D
_T_553:R8(:&
:
:
:


iomem0dvalid


_T_552ICache.scala 133:30:



_T_553=2#
_T_554R


refill_idx
3ICache.scala 136:36D2*
_T_555 R


_T_554


refill_cntICache.scala 136:635z.
 :
:



_T_550_T_556addr


_T_555
 3z,
:
:



_T_550_T_556clk	

clock
 4z-
:
:



_T_550_T_556en	

1
 6z/
 :
:



_T_550_T_556mask	

0
 \zU
 :
:



_T_550_T_556data1:/
':%
:
:
:


iomem0dbitsdata
 6z/
 :
:



_T_550_T_556mask	

1
 ICache.scala 134:16X2>
_T_5574R2#:!
:
:


ioreqbitsaddr
11
3ICache.scala 138:28A2'
_T_559R


_T_553	

0ICache.scala 139:45B2(
_T_560R


_T_559


s0_validICache.scala 139:50

_T_562
	
 



_T_562
 ด:ฌ



_T_560z



_T_562


_T_557
 .2'
_T_564R


_T_562	

0	
 +2$
_T_565R


_T_564
8
0
 4z-
:
:



_T_550_T_566en	

1
 5z.
 :
:



_T_550_T_566addr


_T_565
 3z,
:
:



_T_550_T_566clk	

clock
 
 J4
_T_568
@	

clock"	

0*


_T_568Reg.scala 34:16o:Y


s1_dout_validDz.



_T_568 :
:



_T_550_T_566dataReg.scala 35:23Reg.scala 35:19g2M
_T_569C2A


s1_dout_valid :
:



_T_550_T_566data


_T_568Package.scala 27:42<z"
B

	
s1_dout
0


_T_569ICache.scala 139:16D*
_T_572
@ (2_T_588:_T_578J
 ICache.scala 132:28="
 :
:



_T_572_T_588addrICache.scala 132:28<!
:
:



_T_572_T_588clkICache.scala 132:28Gz-
:
:



_T_572_T_588en	

0ICache.scala 132:28="
 :
:



_T_572_T_578addrICache.scala 132:28<!
:
:



_T_572_T_578clkICache.scala 132:28Gz-
:
:



_T_572_T_578en	

0ICache.scala 132:28="
 :
:



_T_572_T_578dataICache.scala 132:28="
 :
:



_T_572_T_578maskICache.scala 132:28C2)
_T_574R


repl_way	

1ICache.scala 133:42^2D
_T_575:R8(:&
:
:
:


iomem0dvalid


_T_574ICache.scala 133:30:



_T_575=2#
_T_576R


refill_idx
3ICache.scala 136:36D2*
_T_577 R


_T_576


refill_cntICache.scala 136:635z.
 :
:



_T_572_T_578addr


_T_577
 3z,
:
:



_T_572_T_578clk	

clock
 4z-
:
:



_T_572_T_578en	

1
 6z/
 :
:



_T_572_T_578mask	

0
 \zU
 :
:



_T_572_T_578data1:/
':%
:
:
:


iomem0dbitsdata
 6z/
 :
:



_T_572_T_578mask	

1
 ICache.scala 134:16X2>
_T_5794R2#:!
:
:


ioreqbitsaddr
11
3ICache.scala 138:28A2'
_T_581R


_T_575	

0ICache.scala 139:45B2(
_T_582R


_T_581


s0_validICache.scala 139:50

_T_584
	
 



_T_584
 ด:ฌ



_T_582z



_T_584


_T_579
 .2'
_T_586R


_T_584	

0	
 +2$
_T_587R


_T_586
8
0
 4z-
:
:



_T_572_T_588en	

1
 5z.
 :
:



_T_572_T_588addr


_T_587
 3z,
:
:



_T_572_T_588clk	

clock
 
 J4
_T_590
@	

clock"	

0*


_T_590Reg.scala 34:16o:Y


s1_dout_validDz.



_T_590 :
:



_T_572_T_588dataReg.scala 35:23Reg.scala 35:19g2M
_T_591C2A


s1_dout_valid :
:



_T_572_T_588data


_T_590Package.scala 27:42<z"
B

	
s1_dout
1


_T_591ICache.scala 139:16D*
_T_594
@ (2_T_610:_T_600J
 ICache.scala 132:28="
 :
:



_T_594_T_610addrICache.scala 132:28<!
:
:



_T_594_T_610clkICache.scala 132:28Gz-
:
:



_T_594_T_610en	

0ICache.scala 132:28="
 :
:



_T_594_T_600addrICache.scala 132:28<!
:
:



_T_594_T_600clkICache.scala 132:28Gz-
:
:



_T_594_T_600en	

0ICache.scala 132:28="
 :
:



_T_594_T_600dataICache.scala 132:28="
 :
:



_T_594_T_600maskICache.scala 132:28C2)
_T_596R


repl_way	

2ICache.scala 133:42^2D
_T_597:R8(:&
:
:
:


iomem0dvalid


_T_596ICache.scala 133:30:



_T_597=2#
_T_598R


refill_idx
3ICache.scala 136:36D2*
_T_599 R


_T_598


refill_cntICache.scala 136:635z.
 :
:



_T_594_T_600addr


_T_599
 3z,
:
:



_T_594_T_600clk	

clock
 4z-
:
:



_T_594_T_600en	

1
 6z/
 :
:



_T_594_T_600mask	

0
 \zU
 :
:



_T_594_T_600data1:/
':%
:
:
:


iomem0dbitsdata
 6z/
 :
:



_T_594_T_600mask	

1
 ICache.scala 134:16X2>
_T_6014R2#:!
:
:


ioreqbitsaddr
11
3ICache.scala 138:28A2'
_T_603R


_T_597	

0ICache.scala 139:45B2(
_T_604R


_T_603


s0_validICache.scala 139:50

_T_606
	
 



_T_606
 ด:ฌ



_T_604z



_T_606


_T_601
 .2'
_T_608R


_T_606	

0	
 +2$
_T_609R


_T_608
8
0
 4z-
:
:



_T_594_T_610en	

1
 5z.
 :
:



_T_594_T_610addr


_T_609
 3z,
:
:



_T_594_T_610clk	

clock
 
 J4
_T_612
@	

clock"	

0*


_T_612Reg.scala 34:16o:Y


s1_dout_validDz.



_T_612 :
:



_T_594_T_610dataReg.scala 35:23Reg.scala 35:19g2M
_T_613C2A


s1_dout_valid :
:



_T_594_T_610data


_T_612Package.scala 27:42<z"
B

	
s1_dout
2


_T_613ICache.scala 139:16D*
_T_616
@ (2_T_632:_T_622J
 ICache.scala 132:28="
 :
:



_T_616_T_632addrICache.scala 132:28<!
:
:



_T_616_T_632clkICache.scala 132:28Gz-
:
:



_T_616_T_632en	

0ICache.scala 132:28="
 :
:



_T_616_T_622addrICache.scala 132:28<!
:
:



_T_616_T_622clkICache.scala 132:28Gz-
:
:



_T_616_T_622en	

0ICache.scala 132:28="
 :
:



_T_616_T_622dataICache.scala 132:28="
 :
:



_T_616_T_622maskICache.scala 132:28C2)
_T_618R


repl_way	

3ICache.scala 133:42^2D
_T_619:R8(:&
:
:
:


iomem0dvalid


_T_618ICache.scala 133:30:



_T_619=2#
_T_620R


refill_idx
3ICache.scala 136:36D2*
_T_621 R


_T_620


refill_cntICache.scala 136:635z.
 :
:



_T_616_T_622addr


_T_621
 3z,
:
:



_T_616_T_622clk	

clock
 4z-
:
:



_T_616_T_622en	

1
 6z/
 :
:



_T_616_T_622mask	

0
 \zU
 :
:



_T_616_T_622data1:/
':%
:
:
:


iomem0dbitsdata
 6z/
 :
:



_T_616_T_622mask	

1
 ICache.scala 134:16X2>
_T_6234R2#:!
:
:


ioreqbitsaddr
11
3ICache.scala 138:28A2'
_T_625R


_T_619	

0ICache.scala 139:45B2(
_T_626R


_T_625


s0_validICache.scala 139:50

_T_628
	
 



_T_628
 ด:ฌ



_T_626z



_T_628


_T_623
 .2'
_T_630R


_T_628	

0	
 +2$
_T_631R


_T_630
8
0
 4z-
:
:



_T_616_T_632en	

1
 5z.
 :
:



_T_616_T_632addr


_T_631
 3z,
:
:



_T_616_T_632clk	

clock
 
 J4
_T_634
@	

clock"	

0*


_T_634Reg.scala 34:16o:Y


s1_dout_validDz.



_T_634 :
:



_T_616_T_632dataReg.scala 35:23Reg.scala 35:19g2M
_T_635C2A


s1_dout_valid :
:



_T_616_T_632data


_T_634Package.scala 27:42<z"
B

	
s1_dout
3


_T_635ICache.scala 139:16@2&
_T_638R	

stall	

0ICache.scala 148:51I3
_T_639
	

clock"	

reset*	

0Reg.scala 26:44R:<



_T_638.z



_T_639


s1_hitReg.scala 43:23Reg.scala 43:19@2&
_T_641R	

stall	

0ICache.scala 149:46P:
_T_6542


	

clock"	

0*


_T_654Reg.scala 34:16W:A



_T_6413



_T_654


s1_tag_hitReg.scala 35:23Reg.scala 35:19@2&
_T_672R	

stall	

0ICache.scala 150:40P:
_T_6852


@	

clock"	

0*


_T_685Reg.scala 34:16T:>



_T_6720



_T_685
	
s1_doutReg.scala 35:23Reg.scala 35:19Y2C
_T_703927
B



_T_654
0B



_T_685
0	

0Mux.scala 19:72Y2C
_T_705927
B



_T_654
1B



_T_685
1	

0Mux.scala 19:72Y2C
_T_707927
B



_T_654
2B



_T_685
2	

0Mux.scala 19:72Y2C
_T_709927
B



_T_654
3B



_T_685
3	

0Mux.scala 19:72<2&
_T_711R


_T_703


_T_705Mux.scala 19:72<2&
_T_712R


_T_711


_T_707Mux.scala 19:72<2&
_T_713R


_T_712


_T_709Mux.scala 19:72&

_T_715
@Mux.scala 19:72#



_T_715Mux.scala 19:72.z



_T_715


_T_713Mux.scala 19:72Qz7
):'
:
:


iorespbits	datablock


_T_715ICache.scala 151:30Cz)
:
:


iorespvalid


_T_639ICache.scala 152:21@2&
_T_716R	

state	

1ICache.scala 154:27J20
_T_718&R$:


ios2_kill	

0ICache.scala 154:44@2&
_T_719R


_T_716


_T_718ICache.scala 154:41Pz6
(:&
:
:
:


iomem0avalid


_T_719ICache.scala 154:18>2$
_T_721R	

refill_addr
6ICache.scala 157:4692
_T_722R


_T_721
6ICache.scala 157:63E2(
_T_726R	

0	

6Parameters.scala 63:32E2(
_T_728R	

6	

6Parameters.scala 63:42C2&
_T_729R


_T_726


_T_728Parameters.scala 63:37E2'
_T_730R	

0


_T_729Parameters.scala 132:31E2'
_T_732R


_T_722	

0Parameters.scala 117:3182
_T_733R


_T_732Parameters.scala 117:49S25
_T_735+R)


_T_733R

	536870912Parameters.scala 117:5282
_T_736R


_T_735Parameters.scala 117:52K2-
_T_738#R!


_T_736R	

0Parameters.scala 117:67D2&
_T_739R


_T_730


_T_738Parameters.scala 132:56E2(
_T_742R	

0	

6Parameters.scala 63:32E2(
_T_744R	

6	

8Parameters.scala 63:42C2&
_T_745R


_T_742


_T_744Parameters.scala 63:37E2'
_T_746R	

0


_T_745Parameters.scala 132:31M2/
_T_748%R#


_T_722

	536870912Parameters.scala 117:3182
_T_749R


_T_748Parameters.scala 117:49S25
_T_751+R)


_T_749R

	536870912Parameters.scala 117:5282
_T_752R


_T_751Parameters.scala 117:52K2-
_T_754#R!


_T_752R	

0Parameters.scala 117:67D2&
_T_755R


_T_746


_T_754Parameters.scala 132:56E2'
_T_757R	

0


_T_739Parameters.scala 134:30D2&
_T_758R


_T_757


_T_755Parameters.scala 134:30

_T_767z*x
opcode

param

size

source

address
 
mask

data
@Edges.scala 342:17&



_T_767Edges.scala 342:17>z%
:



_T_767opcode	

4Edges.scala 343:15=z$
:



_T_767param	

0Edges.scala 344:15<z#
:



_T_767size	

6Edges.scala 345:15>z%
:



_T_767source	

0Edges.scala 346:15>z%
:



_T_767address


_T_722Edges.scala 347:15A2(
_T_779R
	

1	

2OneHot.scala 49:12=2$
_T_780R


_T_779
2
0OneHot.scala 49:37B2(
_T_782R	

6	

3package.scala 41:21>2$
_T_784R


_T_780
2
2package.scala 44:26>2$
_T_785R


_T_722
2
2package.scala 45:26A2'
_T_787R


_T_785	

0package.scala 46:20A2'
_T_788R	

1


_T_787package.scala 49:27@2&
_T_789R


_T_784


_T_788package.scala 50:38@2&
_T_790R


_T_782


_T_789package.scala 50:29A2'
_T_791R	

1


_T_785package.scala 49:27@2&
_T_792R


_T_784


_T_791package.scala 50:38@2&
_T_793R


_T_782


_T_792package.scala 50:29>2$
_T_794R


_T_780
1
1package.scala 44:26>2$
_T_795R


_T_722
1
1package.scala 45:26A2'
_T_797R


_T_795	

0package.scala 46:20@2&
_T_798R


_T_788


_T_797package.scala 49:27@2&
_T_799R


_T_794


_T_798package.scala 50:38@2&
_T_800R


_T_790


_T_799package.scala 50:29@2&
_T_801R


_T_788


_T_795package.scala 49:27@2&
_T_802R


_T_794


_T_801package.scala 50:38@2&
_T_803R


_T_790


_T_802package.scala 50:29@2&
_T_804R


_T_791


_T_797package.scala 49:27@2&
_T_805R


_T_794


_T_804package.scala 50:38@2&
_T_806R


_T_793


_T_805package.scala 50:29@2&
_T_807R


_T_791


_T_795package.scala 49:27@2&
_T_808R


_T_794


_T_807package.scala 50:38@2&
_T_809R


_T_793


_T_808package.scala 50:29>2$
_T_810R


_T_780
0
0package.scala 44:26>2$
_T_811R


_T_722
0
0package.scala 45:26A2'
_T_813R


_T_811	

0package.scala 46:20@2&
_T_814R


_T_798


_T_813package.scala 49:27@2&
_T_815R


_T_810


_T_814package.scala 50:38@2&
_T_816R


_T_800


_T_815package.scala 50:29@2&
_T_817R


_T_798


_T_811package.scala 49:27@2&
_T_818R


_T_810


_T_817package.scala 50:38@2&
_T_819R


_T_800


_T_818package.scala 50:29@2&
_T_820R


_T_801


_T_813package.scala 49:27@2&
_T_821R


_T_810


_T_820package.scala 50:38@2&
_T_822R


_T_803


_T_821package.scala 50:29@2&
_T_823R


_T_801


_T_811package.scala 49:27@2&
_T_824R


_T_810


_T_823package.scala 50:38@2&
_T_825R


_T_803


_T_824package.scala 50:29@2&
_T_826R


_T_804


_T_813package.scala 49:27@2&
_T_827R


_T_810


_T_826package.scala 50:38@2&
_T_828R


_T_806


_T_827package.scala 50:29@2&
_T_829R


_T_804


_T_811package.scala 49:27@2&
_T_830R


_T_810


_T_829package.scala 50:38@2&
_T_831R


_T_806


_T_830package.scala 50:29@2&
_T_832R


_T_807


_T_813package.scala 49:27@2&
_T_833R


_T_810


_T_832package.scala 50:38@2&
_T_834R


_T_809


_T_833package.scala 50:29@2&
_T_835R


_T_807


_T_811package.scala 49:27@2&
_T_836R


_T_810


_T_835package.scala 50:38@2&
_T_837R


_T_809


_T_836package.scala 50:29<2&
_T_838R


_T_819


_T_816Cat.scala 30:58<2&
_T_839R


_T_825


_T_822Cat.scala 30:58<2&
_T_840R


_T_839


_T_838Cat.scala 30:58<2&
_T_841R


_T_831


_T_828Cat.scala 30:58<2&
_T_842R


_T_837


_T_834Cat.scala 30:58<2&
_T_843R


_T_842


_T_841Cat.scala 30:58<2&
_T_844R


_T_843


_T_840Cat.scala 30:58;z"
:



_T_767mask


_T_844Edges.scala 348:15<z#
:



_T_767data	

0Edges.scala 349:15P5
':%
:
:
:


iomem0abits


_T_767ICache.scala 155:17Qz7
(:&
:
:
:


iomem0cvalid	

0ICache.scala 159:18Qz7
(:&
:
:
:


iomem0evalid	

0ICache.scala 160:18D2&
_T_848R	

0	

stateConditional.scala 29:28ย:ฃ



_T_848[:A

	
s1_miss2z
	

state	

1ICache.scala 165:30ICache.scala 165:228z


invalidated	

0ICache.scala 166:19Conditional.scala 29:59D2&
_T_850R	

1	

stateConditional.scala 29:28:๋



_T_850x:^
(:&
:
:
:


iomem0aready2z
	

state	

2ICache.scala 169:37ICache.scala 169:29c:I
:


ios2_kill2z
	

state	

0ICache.scala 170:33ICache.scala 170:25Conditional.scala 29:59D2&
_T_851R	

2	

stateConditional.scala 29:28ฅ:



_T_851x:^
(:&
:
:
:


iomem0dvalid2z
	

state	

3ICache.scala 173:37ICache.scala 173:29Conditional.scala 29:59D2&
_T_852R	

3	

stateConditional.scala 29:28:m



_T_852_:E


refill_done2z
	

state	

0ICache.scala 176:34ICache.scala 176:26Conditional.scala 29:59
ICache