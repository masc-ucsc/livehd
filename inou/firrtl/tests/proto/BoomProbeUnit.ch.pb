
�
��
BoomProbeUnit
clock" 
reset
�	
io�	*�	
�req�*�
ready

valid

�bits�*�
opcode

param

size

source

address
 
mask

data
@
corrupt

�rep�*�
ready

valid

�bits}*{
opcode

param

size

source

address
 
data
@
corrupt

o	meta_readb*`
ready

valid

:bits2*0
idx

way_en

tag

�
meta_write�*�
ready

valid

qbitsi*g
idx

way_en

tag

5data-*+
coh*
state

tag

�wb_req�*�
ready

valid

rbitsj*h
tag

idx

source

param

way_en

	voluntary

way_en

wb_rdy

mshr_rdy

mshr_wb_rdy

$block_state*
state

�lsu_release�*�
ready

valid

�bits}*{
opcode

param

size

source

address
 
data
@
corrupt
�
	

clock�
 �
	

reset�
 �


io�
 L2
state
	

clock"	

reset*	

0�dcache.scala 163:22��
req�*�
opcode

param

size

source

address
 
mask

data
@
corrupt
	

clock"	

0*

req�dcache.scala 165:16J20
req_idx%R#:


reqaddress
11
6�dcache.scala 166:28E2+
req_tag R	:


reqaddress
12�dcache.scala 167:29J0
way_en 	

clock"	

0*


way_en�dcache.scala 169:1992
tag_matchesR"


way_en�dcache.scala 170:28]C
old_coh*
state
	

clock"	

0*
	
old_coh�dcache.scala 171:20;

miss_coh*
state
�Metadata.scala 160:20+�



miss_coh�Metadata.scala 160:20Bz&
:



miss_cohstate	

0�Metadata.scala 161:16U2;
	reply_coh.2,


tag_matches
	
old_coh


miss_coh�dcache.scala 173:22N28
_T2R0:


reqparam:


	reply_cohstate�Cat.scala 29:58<2&
_T_1R	

0	

3�Cat.scala 29:58<2&
_T_2R	

0	

2�Cat.scala 29:58<2&
_T_3R	

0	

1�Cat.scala 29:58<2&
_T_4R	

0	

0�Cat.scala 29:58<2&
_T_5R	

1	

3�Cat.scala 29:58<2&
_T_6R	

1	

2�Cat.scala 29:58<2&
_T_7R	

1	

1�Cat.scala 29:58<2&
_T_8R	

1	

0�Cat.scala 29:58<2&
_T_9R	

2	

3�Cat.scala 29:58=2'
_T_10R	

2	

2�Cat.scala 29:58=2'
_T_11R	

2	

1�Cat.scala 29:58=2'
_T_12R	

2	

0�Cat.scala 29:5872 
_T_13R	

_T_12

_T�Misc.scala 55:20F20
_T_14'2%
	

_T_13	

0	

0�Misc.scala 37:9G20
_T_15'2%
	

_T_13	

5	

0�Misc.scala 37:36G20
_T_16'2%
	

_T_13	

0	

0�Misc.scala 37:6372 
_T_17R	

_T_11

_T�Misc.scala 55:20D2.
_T_18%2#
	

_T_17	

0	

_T_14�Misc.scala 37:9E2.
_T_19%2#
	

_T_17	

2	

_T_15�Misc.scala 37:36E2.
_T_20%2#
	

_T_17	

0	

_T_16�Misc.scala 37:6372 
_T_21R	

_T_10

_T�Misc.scala 55:20D2.
_T_22%2#
	

_T_21	

0	

_T_18�Misc.scala 37:9E2.
_T_23%2#
	

_T_21	

1	

_T_19�Misc.scala 37:36E2.
_T_24%2#
	

_T_21	

0	

_T_20�Misc.scala 37:6362
_T_25R

_T_9

_T�Misc.scala 55:20D2.
_T_26%2#
	

_T_25	

1	

_T_22�Misc.scala 37:9E2.
_T_27%2#
	

_T_25	

1	

_T_23�Misc.scala 37:36E2.
_T_28%2#
	

_T_25	

0	

_T_24�Misc.scala 37:6362
_T_29R

_T_8

_T�Misc.scala 55:20D2.
_T_30%2#
	

_T_29	

0	

_T_26�Misc.scala 37:9E2.
_T_31%2#
	

_T_29	

5	

_T_27�Misc.scala 37:36E2.
_T_32%2#
	

_T_29	

0	

_T_28�Misc.scala 37:6362
_T_33R

_T_7

_T�Misc.scala 55:20D2.
_T_34%2#
	

_T_33	

0	

_T_30�Misc.scala 37:9E2.
_T_35%2#
	

_T_33	

4	

_T_31�Misc.scala 37:36E2.
_T_36%2#
	

_T_33	

1	

_T_32�Misc.scala 37:6362
_T_37R

_T_6

_T�Misc.scala 55:20D2.
_T_38%2#
	

_T_37	

0	

_T_34�Misc.scala 37:9E2.
_T_39%2#
	

_T_37	

0	

_T_35�Misc.scala 37:36E2.
_T_40%2#
	

_T_37	

1	

_T_36�Misc.scala 37:6362
_T_41R

_T_5

_T�Misc.scala 55:20D2.
_T_42%2#
	

_T_41	

1	

_T_38�Misc.scala 37:9E2.
_T_43%2#
	

_T_41	

0	

_T_39�Misc.scala 37:36E2.
_T_44%2#
	

_T_41	

1	

_T_40�Misc.scala 37:6362
_T_45R

_T_4

_T�Misc.scala 55:20D2.
_T_46%2#
	

_T_45	

0	

_T_42�Misc.scala 37:9E2.
_T_47%2#
	

_T_45	

5	

_T_43�Misc.scala 37:36E2.
_T_48%2#
	

_T_45	

0	

_T_44�Misc.scala 37:6362
_T_49R

_T_3

_T�Misc.scala 55:20D2.
_T_50%2#
	

_T_49	

0	

_T_46�Misc.scala 37:9E2.
_T_51%2#
	

_T_49	

4	

_T_47�Misc.scala 37:36E2.
_T_52%2#
	

_T_49	

1	

_T_48�Misc.scala 37:6362
_T_53R

_T_2

_T�Misc.scala 55:20D2.
_T_54%2#
	

_T_53	

0	

_T_50�Misc.scala 37:9E2.
_T_55%2#
	

_T_53	

3	

_T_51�Misc.scala 37:36E2.
_T_56%2#
	

_T_53	

2	

_T_52�Misc.scala 37:6362
_T_57R

_T_1

_T�Misc.scala 55:20G21
is_dirty%2#
	

_T_57	

1	

_T_54�Misc.scala 37:9L25
report_param%2#
	

_T_57	

3	

_T_55�Misc.scala 37:36E2.
_T_58%2#
	

_T_57	

2	

_T_56�Misc.scala 37:63:

new_coh*
state
�Metadata.scala 160:20*�

	
new_coh�Metadata.scala 160:20?z#
:

	
new_cohstate	

_T_58�Metadata.scala 161:16?2%
_T_59R	

state	

0�dcache.scala 176:25Az'
:
:


ioreqready	

_T_59�dcache.scala 176:16?2%
_T_60R	

state	

6�dcache.scala 177:25Az'
:
:


iorepvalid	

_T_60�dcache.scala 177:16�
�
_T_61}*{
opcode

param

size

source

address
 
data
@
corrupt
�Edges.scala 390:17%�
	

_T_61�Edges.scala 390:17=z$
:
	

_T_61opcode	

4�Edges.scala 391:15Az(
:
	

_T_61param

report_param�Edges.scala 392:15Az(
:
	

_T_61size:


reqsize�Edges.scala 393:15Ez,
:
	

_T_61source:


reqsource�Edges.scala 394:15Gz.
:
	

_T_61address:


reqaddress�Edges.scala 395:15;z"
:
	

_T_61data	

0�Edges.scala 396:15>z%
:
	

_T_61corrupt	

0�Edges.scala 397:15Zz@
&:$
:
:


iorepbitscorrupt:
	

_T_61corrupt�dcache.scala 178:15Tz:
#:!
:
:


iorepbitsdata:
	

_T_61data�dcache.scala 178:15Zz@
&:$
:
:


iorepbitsaddress:
	

_T_61address�dcache.scala 178:15Xz>
%:#
:
:


iorepbitssource:
	

_T_61source�dcache.scala 178:15Tz:
#:!
:
:


iorepbitssize:
	

_T_61size�dcache.scala 178:15Vz<
$:"
:
:


iorepbitsparam:
	

_T_61param�dcache.scala 178:15Xz>
%:#
:
:


iorepbitsopcode:
	

_T_61opcode�dcache.scala 178:15P26
_T_62-R+:
:


iorepvalid	

0�dcache.scala 180:10W2>
_T_635R3%:#
:
:


iorepbitsopcode
0
0�Edges.scala 103:36?2%
_T_64R	

_T_63	

0�dcache.scala 180:27=2#
_T_65R	

_T_62	

_T_64�dcache.scala 180:24;2"
_T_66R	

reset
0
0�dcache.scala 180:9<2#
_T_67R	

_T_65	

_T_66�dcache.scala 180:9>2%
_T_68R	

_T_67	

0�dcache.scala 180:9�:�
	

_T_68�R�
�Assertion failed: ProbeUnit should not send ProbeAcks with data, WritebackUnit should handle it
    at dcache.scala:180 assert(!io.rep.valid || !edge.hasData(io.rep.bits),
	

clock"	

1�dcache.scala 180:93B	

clock	

1�dcache.scala 180:9�dcache.scala 180:9?2%
_T_69R	

state	

1�dcache.scala 183:31Gz-
 :
:


io	meta_readvalid	

_T_69�dcache.scala 183:22Qz7
(:&
:
:


io	meta_readbitsidx
	
req_idx�dcache.scala 184:25Qz7
(:&
:
:


io	meta_readbitstag
	
req_tag�dcache.scala 185:25?2%
_T_70R	

state	

9�dcache.scala 187:32Hz.
!:
:


io
meta_writevalid	

_T_70�dcache.scala 187:23Tz:
,:*
 :
:


io
meta_writebitsway_en


way_en�dcache.scala 188:29Rz8
):'
 :
:


io
meta_writebitsidx
	
req_idx�dcache.scala 189:26\zB
3:1
*:(
 :
:


io
meta_writebitsdatatag
	
req_tag�dcache.scala 190:31rzX
>:<
3:1
*:(
 :
:


io
meta_writebitsdatacohstate:

	
new_cohstate�dcache.scala 191:31?2%
_T_71R	

state	

7�dcache.scala 193:28Dz*
:
:


iowb_reqvalid	

_T_71�dcache.scala 193:19Yz?
(:&
:
:


iowb_reqbitssource:


reqsource�dcache.scala 194:25Nz4
%:#
:
:


iowb_reqbitsidx
	
req_idx�dcache.scala 195:22Nz4
%:#
:
:


iowb_reqbitstag
	
req_tag�dcache.scala 196:22Uz;
':%
:
:


iowb_reqbitsparam

report_param�dcache.scala 197:24Pz6
(:&
:
:


iowb_reqbitsway_en


way_en�dcache.scala 198:25Tz:
+:)
:
:


iowb_reqbits	voluntary	

0�dcache.scala 199:28?2%
_T_72R	

state	

6�package.scala 15:47?2%
_T_73R	

state	

7�package.scala 15:47?2%
_T_74R	

state	

8�package.scala 15:47?2%
_T_75R	

state	

9�package.scala 15:47@2&
_T_76R	

state


10�package.scala 15:47=2#
_T_77R	

_T_72	

_T_73�package.scala 64:59=2#
_T_78R	

_T_77	

_T_74�package.scala 64:59=2#
_T_79R	

_T_78	

_T_75�package.scala 64:59=2#
_T_80R	

_T_79	

_T_76�package.scala 64:59?2%
_T_81R	

_T_80	

0�dcache.scala 202:21>z$
:


iomshr_wb_rdy	

_T_81�dcache.scala 202:18?2%
_T_82R	

state	

5�dcache.scala 204:33Iz/
": 
:


iolsu_releasevalid	

_T_82�dcache.scala 204:24�
�
_T_83}*{
opcode

param

size

source

address
 
data
@
corrupt
�Edges.scala 390:17%�
	

_T_83�Edges.scala 390:17=z$
:
	

_T_83opcode	

4�Edges.scala 391:15Az(
:
	

_T_83param

report_param�Edges.scala 392:15Az(
:
	

_T_83size:


reqsize�Edges.scala 393:15Ez,
:
	

_T_83source:


reqsource�Edges.scala 394:15Gz.
:
	

_T_83address:


reqaddress�Edges.scala 395:15;z"
:
	

_T_83data	

0�Edges.scala 396:15>z%
:
	

_T_83corrupt	

0�Edges.scala 397:15bzH
.:,
!:
:


iolsu_releasebitscorrupt:
	

_T_83corrupt�dcache.scala 205:24\zB
+:)
!:
:


iolsu_releasebitsdata:
	

_T_83data�dcache.scala 205:24bzH
.:,
!:
:


iolsu_releasebitsaddress:
	

_T_83address�dcache.scala 205:24`zF
-:+
!:
:


iolsu_releasebitssource:
	

_T_83source�dcache.scala 205:24\zB
+:)
!:
:


iolsu_releasebitssize:
	

_T_83size�dcache.scala 205:24^zD
,:*
!:
:


iolsu_releasebitsparam:
	

_T_83param�dcache.scala 205:24`zF
-:+
!:
:


iolsu_releasebitsopcode:
	

_T_83opcode�dcache.scala 205:24?2%
_T_84R	

state	

0�dcache.scala 208:15�:�
	

_T_84a2E
_T_85<R::
:


ioreqready:
:


ioreqvalid�Decoupled.scala 40:37�:�
	

_T_852z
	

state	

1�dcache.scala 210:13Xz>
:


reqcorrupt&:$
:
:


ioreqbitscorrupt�dcache.scala 211:11Rz8
:


reqdata#:!
:
:


ioreqbitsdata�dcache.scala 211:11Rz8
:


reqmask#:!
:
:


ioreqbitsmask�dcache.scala 211:11Xz>
:


reqaddress&:$
:
:


ioreqbitsaddress�dcache.scala 211:11Vz<
:


reqsource%:#
:
:


ioreqbitssource�dcache.scala 211:11Rz8
:


reqsize#:!
:
:


ioreqbitssize�dcache.scala 211:11Tz:
:


reqparam$:"
:
:


ioreqbitsparam�dcache.scala 211:11Vz<
:


reqopcode%:#
:
:


ioreqbitsopcode�dcache.scala 211:11�dcache.scala 209:26?2%
_T_86R	

state	

1�dcache.scala 213:22�:�
	

_T_86m2Q
_T_87HRF :
:


io	meta_readready :
:


io	meta_readvalid�Decoupled.scala 40:37Y:?
	

_T_872z
	

state	

2�dcache.scala 215:13�dcache.scala 214:32?2%
_T_88R	

state	

2�dcache.scala 217:22�:�
	

_T_882z
	

state	

3�dcache.scala 219:11?2%
_T_89R	

state	

3�dcache.scala 220:22�:�
	

_T_89Vz<
:

	
old_cohstate": 
:


ioblock_statestate�dcache.scala 221:13:z 



way_en:


ioway_en�dcache.scala 222:12Q27
_T_90.R,:


iomshr_rdy:


iowb_rdy�dcache.scala 224:30J20
_T_91'2%
	

_T_90	

4	

1�dcache.scala 224:170z
	

state	

_T_91�dcache.scala 224:11?2%
_T_92R	

state	

4�dcache.scala 225:22�:�
	

_T_92F2,
_T_93#R!

tag_matches


is_dirty�dcache.scala 226:30J20
_T_94'2%
	

_T_93	

7	

5�dcache.scala 226:170z
	

state	

_T_94�dcache.scala 226:11?2%
_T_95R	

state	

5�dcache.scala 227:22�:�
	

_T_95q2U
_T_96LRJ": 
:


iolsu_releaseready": 
:


iolsu_releasevalid�Decoupled.scala 40:37Y:?
	

_T_962z
	

state	

6�dcache.scala 229:13�dcache.scala 228:34?2%
_T_97R	

state	

6�dcache.scala 231:22�	:�	
	

_T_97�:�
:
:


iorepreadyP26
_T_98-2+


tag_matches	

9	

0�dcache.scala 233:190z
	

state	

_T_98�dcache.scala 233:13�dcache.scala 232:25?2%
_T_99R	

state	

7�dcache.scala 235:22�:�
	

_T_99h2L
_T_100BR@:
:


iowb_reqready:
:


iowb_reqvalid�Decoupled.scala 40:37Z:@



_T_1002z
	

state	

8�dcache.scala 237:13�dcache.scala 236:29@2&
_T_101R	

state	

8�dcache.scala 239:22�:�



_T_101m:S
:
:


iowb_reqready2z
	

state	

9�dcache.scala 242:13�dcache.scala 241:28@2&
_T_102R	

state	

9�dcache.scala 244:22�:�



_T_102p2T
_T_103JRH!:
:


io
meta_writeready!:
:


io
meta_writevalid�Decoupled.scala 40:37[:A



_T_1033z
	

state


10�dcache.scala 246:13�dcache.scala 245:33A2'
_T_104R	

state


10�dcache.scala 248:22Z:@



_T_1042z
	

state	

0�dcache.scala 249:11�dcache.scala 248:45�dcache.scala 244:40�dcache.scala 239:44�dcache.scala 235:43�dcache.scala 231:37�dcache.scala 227:41�dcache.scala 225:39�dcache.scala 220:38�dcache.scala 217:39�dcache.scala 213:39�dcache.scala 208:30
BoomProbeUnit