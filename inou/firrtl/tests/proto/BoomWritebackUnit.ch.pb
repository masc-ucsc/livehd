
�o
�o�o
BoomWritebackUnit
clock" 
reset
�
io�*�
�req�*�
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
resp

&idx*
valid


bits 
`data_reqT*R
ready

valid

,bits$*"
way_en

addr

	data_resp
@
	mem_grant

�release�*�
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
 ��
reqj*h
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
	

clock"	

0*

req�dcache.scala 37:16K2
state
	

clock"	

reset*	

0�dcache.scala 39:22W>
r1_data_req_fired
	

clock"	

reset*	

0�dcache.scala 40:34W>
r2_data_req_fired
	

clock"	

reset*	

0�dcache.scala 41:34_F
r1_data_req_cnt
	

clock"	

0*

r1_data_req_cnt�dcache.scala 42:28_F
r2_data_req_cnt
	

clock"	

0*

r2_data_req_cnt�dcache.scala 43:28R9
data_req_cnt
	

clock"	

reset*	

0�dcache.scala 44:29f2J
_TDRB:
:


ioreleaseready:
:


ioreleasevalid�Decoupled.scala 40:37=2"
_T_1RR

4095�package.scala 189:70Z2?
_T_27R5


_T_1':%
:
:


ioreleasebitssize�package.scala 189:77<2!
_T_3R

_T_2
11
0�package.scala 189:8212
_T_4R

_T_3�package.scala 189:4642
_T_5R	

_T_4
3�Edges.scala 221:59Z2A
_T_69R7):'
:
:


ioreleasebitsopcode
0
0�Edges.scala 103:36D2+
_T_7#2!


_T_6

_T_5	

0�Edges.scala 222:14J1
_T_8
		

clock"	

reset*	

0	�Edges.scala 230:27<2#
_T_9R

_T_8	

1�Edges.scala 231:2852
_T_10R

_T_9
1�Edges.scala 231:28=2$
_T_11R

_T_8	

0�Edges.scala 232:25=2$
_T_12R

_T_8	

1�Edges.scala 233:25=2$
_T_13R

_T_7	

0�Edges.scala 233:47@2'
	last_beatR	

_T_12	

_T_13�Edges.scala 233:37F2-
all_beats_doneR

	last_beat

_T�Edges.scala 234:2212
_T_14R	

_T_10�Edges.scala 235:27@2'

beat_countR

_T_7	

_T_14�Edges.scala 235:25�:~


_TD2+
_T_15"2 
	

_T_11

_T_7	

_T_10�Edges.scala 237:21.z


_T_8	

_T_15�Edges.scala 237:15�Edges.scala 236:17Y@
	wb_buffer2


@	

clock"	

0*

	wb_buffer�dcache.scala 46:22K2
acked
	

clock"	

reset*	

0�dcache.scala 47:22>2%
_T_16R	

state	

0�dcache.scala 49:31@z'
:
:


ioidxvalid	

_T_16�dcache.scala 49:22Fz-
:
:


ioidxbits:


reqidx�dcache.scala 50:22Fz-
:
:


ioreleasevalid	

0�dcache.scala 51:22F�,
*:(
:
:


ioreleasebitscorrupt�dcache.scala 52:22C�)
':%
:
:


ioreleasebitsdata�dcache.scala 52:22F�,
*:(
:
:


ioreleasebitsaddress�dcache.scala 52:22E�+
):'
:
:


ioreleasebitssource�dcache.scala 52:22C�)
':%
:
:


ioreleasebitssize�dcache.scala 52:22D�*
(:&
:
:


ioreleasebitsparam�dcache.scala 52:22E�+
):'
:
:


ioreleasebitsopcode�dcache.scala 52:22Bz)
:
:


ioreqready	

0�dcache.scala 53:22Hz/
 :
:


io	meta_readvalid	

0�dcache.scala 54:22D�*
(:&
:
:


io	meta_readbitstag�dcache.scala 55:22G�-
+:)
:
:


io	meta_readbitsway_en�dcache.scala 55:22D�*
(:&
:
:


io	meta_readbitsidx�dcache.scala 55:22Gz.
:
:


iodata_reqvalid	

0�dcache.scala 56:22D�*
(:&
:
:


iodata_reqbitsaddr�dcache.scala 57:22F�,
*:(
:
:


iodata_reqbitsway_en�dcache.scala 57:228z
:


ioresp	

0�dcache.scala 58:22Jz1
": 
:


iolsu_releasevalid	

0�dcache.scala 59:24G21
_T_17(R&:


reqtag:


reqidx�Cat.scala 29:58:2!
	r_addressR	

_T_17
6�dcache.scala 63:4102)
_T_18 R

data_req_cnt
2
0�
 �
�
probeResponse}*{
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
�Edges.scala 405:17-�


probeResponse�Edges.scala 405:17Ez,
:


probeResponseopcode	

5�Edges.scala 406:15Kz2
:


probeResponseparam:


reqparam�Edges.scala 407:15Cz*
:


probeResponsesize	

6�Edges.scala 408:15Ez,
:


probeResponsesource	

2�Edges.scala 409:15Hz/
:


probeResponseaddress

	r_address�Edges.scala 410:15Tz;
:


probeResponsedataJ


	wb_buffer	

_T_18�Edges.scala 411:15Fz-
:


probeResponsecorrupt	

0�Edges.scala 412:1502)
_T_19 R

data_req_cnt
2
0�
 E2'
_T_20R	

0	

0�Parameters.scala 551:31G2)
_T_21 R

	r_address	

0�Parameters.scala 137:3162
_T_22R	

_T_21�Parameters.scala 137:49R24
_T_23+R)	

_T_22R


2147483648!�Parameters.scala 137:5262
_T_24R	

_T_23�Parameters.scala 137:52I2+
_T_25"R 	

_T_24R	

0�Parameters.scala 137:67A2#
_T_26R	

_T_20	

_T_25�Parameters.scala 551:56D2'
_T_27R	

6	

6�Parameters.scala 92:48C2%
_T_28R	

0	

_T_27�Parameters.scala 551:31P22
_T_29)R'

	r_address


2147483648 �Parameters.scala 137:3162
_T_30R	

_T_29�Parameters.scala 137:49R24
_T_31+R)	

_T_30R


2147483648!�Parameters.scala 137:5262
_T_32R	

_T_31�Parameters.scala 137:52I2+
_T_33"R 	

_T_32R	

0�Parameters.scala 137:67A2#
_T_34R	

_T_28	

_T_33�Parameters.scala 551:56C2%
_T_35R	

0	

_T_26�Parameters.scala 553:30A2#
_T_36R	

_T_35	

_T_34�Parameters.scala 553:30�
�
voluntaryRelease}*{
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
�Edges.scala 372:170�


voluntaryRelease�Edges.scala 372:17Hz/
 :


voluntaryReleaseopcode	

7�Edges.scala 373:15Nz5
:


voluntaryReleaseparam:


reqparam�Edges.scala 374:15Fz-
:


voluntaryReleasesize	

6�Edges.scala 375:15Hz/
 :


voluntaryReleasesource	

2�Edges.scala 376:15Kz2
!:


voluntaryReleaseaddress

	r_address�Edges.scala 377:15Wz>
:


voluntaryReleasedataJ


	wb_buffer	

_T_19�Edges.scala 378:15Iz0
!:


voluntaryReleasecorrupt	

0�Edges.scala 379:15>2%
_T_37R	

state	

0�dcache.scala 80:15�2:�2
	

_T_37Bz)
:
:


ioreqready	

1�dcache.scala 81:18a2E
_T_38<R::
:


ioreqready:
:


ioreqvalid�Decoupled.scala 40:37�:�
	

_T_381z
	

state	

1�dcache.scala 83:138z


data_req_cnt	

0�dcache.scala 84:20[zB
:


req	voluntary(:&
:
:


ioreqbits	voluntary�dcache.scala 85:11Uz<
:


reqway_en%:#
:
:


ioreqbitsway_en�dcache.scala 85:11Sz:
:


reqparam$:"
:
:


ioreqbitsparam�dcache.scala 85:11Uz<
:


reqsource%:#
:
:


ioreqbitssource�dcache.scala 85:11Oz6
:


reqidx": 
:
:


ioreqbitsidx�dcache.scala 85:11Oz6
:


reqtag": 
:
:


ioreqbitstag�dcache.scala 85:111z
	

acked	

0�dcache.scala 86:13�dcache.scala 82:26>2%
_T_39R	

state	

1�dcache.scala 88:22�+:�+
	

_T_39E2,
_T_40#R!

data_req_cnt	

8�dcache.scala 89:40Fz-
 :
:


io	meta_readvalid	

_T_40�dcache.scala 89:24Uz<
(:&
:
:


io	meta_readbitsidx:


reqidx�dcache.scala 90:27Uz<
(:&
:
:


io	meta_readbitstag:


reqtag�dcache.scala 91:27E2,
_T_41#R!

data_req_cnt	

8�dcache.scala 93:39Ez,
:
:


iodata_reqvalid	

_T_41�dcache.scala 93:23ZzA
*:(
:
:


iodata_reqbitsway_en:


reqway_en�dcache.scala 94:29B2)
_T_42 R

data_req_cnt
2
0�dcache.scala 96:56@2*
_T_43!R:


reqidx	

_T_42�Cat.scala 29:5862
_T_44R	

_T_43
3�dcache.scala 97:43Nz5
(:&
:
:


iodata_reqbitsaddr	

_T_44�dcache.scala 95:27=z$


r1_data_req_fired	

0�dcache.scala 99:23<z"


r1_data_req_cnt	

0�dcache.scala 100:23Hz.


r2_data_req_fired

r1_data_req_fired�dcache.scala 101:23Dz*


r2_data_req_cnt

r1_data_req_cnt�dcache.scala 102:23k2O
_T_45FRD:
:


iodata_reqready:
:


iodata_reqvalid�Decoupled.scala 40:37m2Q
_T_46HRF :
:


io	meta_readready :
:


io	meta_readvalid�Decoupled.scala 40:37=2#
_T_47R	

_T_45	

_T_46�dcache.scala 103:30�:�
	

_T_47>z$


r1_data_req_fired	

1�dcache.scala 104:25Az'


r1_data_req_cnt

data_req_cnt�dcache.scala 105:25F2,
_T_48#R!

data_req_cnt	

1�dcache.scala 106:3672
_T_49R	

_T_48
1�dcache.scala 106:367z


data_req_cnt	

_T_49�dcache.scala 106:20�dcache.scala 103:54�:�


r2_data_req_fired32,
_T_50#R!

r2_data_req_cnt
2
0�
 Oz5
J


	wb_buffer	

_T_50:


io	data_resp�dcache.scala 109:34I2/
_T_51&R$

r2_data_req_cnt	

7�dcache.scala 110:29�:�
	

_T_519z
:


ioresp	

1�dcache.scala 111:172z
	

state	

2�dcache.scala 112:159z


data_req_cnt	

0�dcache.scala 113:22�dcache.scala 110:53�dcache.scala 108:30?2%
_T_52R	

state	

2�dcache.scala 116:22�:�
	

_T_52Kz1
": 
:


iolsu_releasevalid	

1�dcache.scala 117:26jzP
.:,
!:
:


iolsu_releasebitscorrupt:


probeResponsecorrupt�dcache.scala 118:25dzJ
+:)
!:
:


iolsu_releasebitsdata:


probeResponsedata�dcache.scala 118:25jzP
.:,
!:
:


iolsu_releasebitsaddress:


probeResponseaddress�dcache.scala 118:25hzN
-:+
!:
:


iolsu_releasebitssource:


probeResponsesource�dcache.scala 118:25dzJ
+:)
!:
:


iolsu_releasebitssize:


probeResponsesize�dcache.scala 118:25fzL
,:*
!:
:


iolsu_releasebitsparam:


probeResponseparam�dcache.scala 118:25hzN
-:+
!:
:


iolsu_releasebitsopcode:


probeResponseopcode�dcache.scala 118:25q2U
_T_53LRJ": 
:


iolsu_releaseready": 
:


iolsu_releasevalid�Decoupled.scala 40:37Y:?
	

_T_532z
	

state	

3�dcache.scala 120:12�dcache.scala 119:34?2%
_T_54R	

state	

3�dcache.scala 122:22�:�
	

_T_54F2,
_T_55#R!

data_req_cnt	

8�dcache.scala 123:38Ez+
:
:


ioreleasevalid	

_T_55�dcache.scala 123:22f2L
_T_56C2A
:


req	voluntary

voluntaryRelease

probeResponse�dcache.scala 124:27^zD
*:(
:
:


ioreleasebitscorrupt:
	

_T_56corrupt�dcache.scala 124:21Xz>
':%
:
:


ioreleasebitsdata:
	

_T_56data�dcache.scala 124:21^zD
*:(
:
:


ioreleasebitsaddress:
	

_T_56address�dcache.scala 124:21\zB
):'
:
:


ioreleasebitssource:
	

_T_56source�dcache.scala 124:21Xz>
':%
:
:


ioreleasebitssize:
	

_T_56size�dcache.scala 124:21Zz@
(:&
:
:


ioreleasebitsparam:
	

_T_56param�dcache.scala 124:21\zB
):'
:
:


ioreleasebitsopcode:
	

_T_56opcode�dcache.scala 124:21e:K
:


io	mem_grant2z
	

acked	

1�dcache.scala 127:13�dcache.scala 126:25i2M
_T_57DRB:
:


ioreleaseready:
:


ioreleasevalid�Decoupled.scala 40:37�:�
	

_T_57F2,
_T_58#R!

data_req_cnt	

1�dcache.scala 130:3672
_T_59R	

_T_58
1�dcache.scala 130:367z


data_req_cnt	

_T_59�dcache.scala 130:20�dcache.scala 129:30F2,
_T_60#R!

data_req_cnt	

7�dcache.scala 132:25i2M
_T_61DRB:
:


ioreleaseready:
:


ioreleasevalid�Decoupled.scala 40:37=2#
_T_62R	

_T_60	

_T_61�dcache.scala 132:49�:�
	

_T_62W2=
_T_63422
:


req	voluntary	

4	

0�dcache.scala 133:190z
	

state	

_T_63�dcache.scala 133:13�dcache.scala 132:71?2%
_T_64R	

state	

4�dcache.scala 135:22�:�
	

_T_64e:K
:


io	mem_grant2z
	

acked	

1�dcache.scala 137:13�dcache.scala 136:25Y:?
	

acked2z
	

state	

0�dcache.scala 140:13�dcache.scala 139:18�dcache.scala 135:35�dcache.scala 122:36�dcache.scala 116:41�dcache.scala 88:41�dcache.scala 80:30
BoomWritebackUnit