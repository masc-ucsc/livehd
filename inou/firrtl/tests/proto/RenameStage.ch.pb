
��
�(�(
RenameBusyTable
clock" 
reset
�
io�*�
�ren_uops�2�
�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc

U
busy_respsG2E
A*?
	prs1_busy

	prs2_busy

	prs3_busy

rebusy_reqs2



wb_pdsts2



	wb_valids2



 debug*
	busytable
4�
	

clock�
 �
	

reset�
 �


io�
 Z7

busy_table
4	

clock"	

reset*	

04�rename-busytable.scala 48:27O26
_T0R.
	

1B
:


iowb_pdsts
0�OneHot.scala 58:35P26
_T_1.R,B
:


io	wb_valids
0
0
0�Bitwise.scala 72:15W2=
_T_2523


_T_1

45035996273704954	

04�Bitwise.scala 72:12A2
_T_3R

_T

_T_2�rename-busytable.scala 51:48Q28
_T_40R.
	

1B
:


iowb_pdsts
1�OneHot.scala 58:35P26
_T_5.R,B
:


io	wb_valids
1
0
0�Bitwise.scala 72:15W2=
_T_6523


_T_5

45035996273704954	

04�Bitwise.scala 72:12C2 
_T_7R

_T_4

_T_6�rename-busytable.scala 51:48Q28
_T_80R.
	

1B
:


iowb_pdsts
2�OneHot.scala 58:35P26
_T_9.R,B
:


io	wb_valids
2
0
0�Bitwise.scala 72:15X2>
_T_10523


_T_9

45035996273704954	

04�Bitwise.scala 72:12E2"
_T_11R

_T_8	

_T_10�rename-busytable.scala 51:48D2!
_T_12R

_T_3

_T_7�rename-busytable.scala 51:88F2#
_T_13R	

_T_12	

_T_11�rename-busytable.scala 51:88;2
_T_14R	

_T_13�rename-busytable.scala 50:36S20
busy_table_wbR


busy_table	

_T_14�rename-busytable.scala 50:34\2C
_T_15:R8
	

1':%
B
:


ioren_uops
0pdst�OneHot.scala 58:35S29
_T_160R. B
:


iorebusy_reqs
0
0
0�Bitwise.scala 72:15Y2?
_T_17624
	

_T_16

45035996273704954	

04�Bitwise.scala 72:12F2#
_T_18R	

_T_15	

_T_17�rename-busytable.scala 54:49X25
busy_table_next"R 

busy_table_wb	

_T_18�rename-busytable.scala 53:39Hz%



busy_table

busy_table_next�rename-busytable.scala 56:14i2F
_T_19=R;


busy_table':%
B
:


ioren_uops
0prs1�rename-busytable.scala 67:45E2"
_T_20R	

_T_19
0
0�rename-busytable.scala 67:45J2'
_T_21R	

0	

0�rename-busytable.scala 67:88F2#
_T_22R	

_T_20	

_T_21�rename-busytable.scala 67:67^z;
.:,
B
:


io
busy_resps
0	prs1_busy	

_T_22�rename-busytable.scala 67:32i2F
_T_23=R;


busy_table':%
B
:


ioren_uops
0prs2�rename-busytable.scala 68:45E2"
_T_24R	

_T_23
0
0�rename-busytable.scala 68:45J2'
_T_25R	

0	

0�rename-busytable.scala 68:88F2#
_T_26R	

_T_24	

_T_25�rename-busytable.scala 68:67^z;
.:,
B
:


io
busy_resps
0	prs2_busy	

_T_26�rename-busytable.scala 68:32i2F
_T_27=R;


busy_table':%
B
:


ioren_uops
0prs3�rename-busytable.scala 69:45E2"
_T_28R	

_T_27
0
0�rename-busytable.scala 69:45J2'
_T_29R	

0	

0�rename-busytable.scala 69:88F2#
_T_30R	

_T_28	

_T_29�rename-busytable.scala 69:67^z;
.:,
B
:


io
busy_resps
0	prs3_busy	

_T_30�rename-busytable.scala 69:32`z=
.:,
B
:


io
busy_resps
0	prs3_busy	

0�rename-busytable.scala 70:44Uz2
 :
:


iodebug	busytable


busy_table�rename-busytable.scala 73:22
�ޗ
RenameFreeList
clock" 
reset
�
io�*�
reqs2



8alloc_pregs)2'
#*!
valid

bits

<dealloc_pregs)2'
#*!
valid

bits

:ren_br_tags)2'
#*!
valid

bits

�brupdate�*�
;b15*3
resolve_mask

mispredict_mask

�b2�*�
�uop�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc

valid


mispredict

taken

cfi_type

pc_sel

jalr_target
(
target_offset

OdebugF*D
pipeline_empty

freelist
4
isprlist
4�
	

clock�
 �
	

reset�
 �


io�
 92
_TR	

14�rename-freelist.scala 50:45S1
	free_list
4	

clock"	

reset*

_T�rename-freelist.scala 50:26lJ
br_alloc_lists2


4	

clock"	

0*

br_alloc_lists�rename-freelist.scala 51:27,

sels2


4�util.scala 405:20>2%
_T_1R

	free_list
0
0�OneHot.scala 85:71>2%
_T_2R

	free_list
1
1�OneHot.scala 85:71>2%
_T_3R

	free_list
2
2�OneHot.scala 85:71>2%
_T_4R

	free_list
3
3�OneHot.scala 85:71>2%
_T_5R

	free_list
4
4�OneHot.scala 85:71>2%
_T_6R

	free_list
5
5�OneHot.scala 85:71>2%
_T_7R

	free_list
6
6�OneHot.scala 85:71>2%
_T_8R

	free_list
7
7�OneHot.scala 85:71>2%
_T_9R

	free_list
8
8�OneHot.scala 85:71?2&
_T_10R

	free_list
9
9�OneHot.scala 85:71A2(
_T_11R

	free_list
10
10�OneHot.scala 85:71A2(
_T_12R

	free_list
11
11�OneHot.scala 85:71A2(
_T_13R

	free_list
12
12�OneHot.scala 85:71A2(
_T_14R

	free_list
13
13�OneHot.scala 85:71A2(
_T_15R

	free_list
14
14�OneHot.scala 85:71A2(
_T_16R

	free_list
15
15�OneHot.scala 85:71A2(
_T_17R

	free_list
16
16�OneHot.scala 85:71A2(
_T_18R

	free_list
17
17�OneHot.scala 85:71A2(
_T_19R

	free_list
18
18�OneHot.scala 85:71A2(
_T_20R

	free_list
19
19�OneHot.scala 85:71A2(
_T_21R

	free_list
20
20�OneHot.scala 85:71A2(
_T_22R

	free_list
21
21�OneHot.scala 85:71A2(
_T_23R

	free_list
22
22�OneHot.scala 85:71A2(
_T_24R

	free_list
23
23�OneHot.scala 85:71A2(
_T_25R

	free_list
24
24�OneHot.scala 85:71A2(
_T_26R

	free_list
25
25�OneHot.scala 85:71A2(
_T_27R

	free_list
26
26�OneHot.scala 85:71A2(
_T_28R

	free_list
27
27�OneHot.scala 85:71A2(
_T_29R

	free_list
28
28�OneHot.scala 85:71A2(
_T_30R

	free_list
29
29�OneHot.scala 85:71A2(
_T_31R

	free_list
30
30�OneHot.scala 85:71A2(
_T_32R

	free_list
31
31�OneHot.scala 85:71A2(
_T_33R

	free_list
32
32�OneHot.scala 85:71A2(
_T_34R

	free_list
33
33�OneHot.scala 85:71A2(
_T_35R

	free_list
34
34�OneHot.scala 85:71A2(
_T_36R

	free_list
35
35�OneHot.scala 85:71A2(
_T_37R

	free_list
36
36�OneHot.scala 85:71A2(
_T_38R

	free_list
37
37�OneHot.scala 85:71A2(
_T_39R

	free_list
38
38�OneHot.scala 85:71A2(
_T_40R

	free_list
39
39�OneHot.scala 85:71A2(
_T_41R

	free_list
40
40�OneHot.scala 85:71A2(
_T_42R

	free_list
41
41�OneHot.scala 85:71A2(
_T_43R

	free_list
42
42�OneHot.scala 85:71A2(
_T_44R

	free_list
43
43�OneHot.scala 85:71A2(
_T_45R

	free_list
44
44�OneHot.scala 85:71A2(
_T_46R

	free_list
45
45�OneHot.scala 85:71A2(
_T_47R

	free_list
46
46�OneHot.scala 85:71A2(
_T_48R

	free_list
47
47�OneHot.scala 85:71A2(
_T_49R

	free_list
48
48�OneHot.scala 85:71A2(
_T_50R

	free_list
49
49�OneHot.scala 85:71A2(
_T_51R

	free_list
50
50�OneHot.scala 85:71A2(
_T_52R

	free_list
51
51�OneHot.scala 85:71U2?
_T_53624
	

_T_52

22517998136852484	

04�Mux.scala 47:69S2=
_T_54422
	

_T_51

11258999068426244	

_T_53�Mux.scala 47:69R2<
_T_55321
	

_T_50

5629499534213124	

_T_54�Mux.scala 47:69R2<
_T_56321
	

_T_49

2814749767106564	

_T_55�Mux.scala 47:69R2<
_T_57321
	

_T_48

1407374883553284	

_T_56�Mux.scala 47:69Q2;
_T_58220
	

_T_47

703687441776644	

_T_57�Mux.scala 47:69Q2;
_T_59220
	

_T_46

351843720888324	

_T_58�Mux.scala 47:69Q2;
_T_60220
	

_T_45

175921860444164	

_T_59�Mux.scala 47:69P2:
_T_6112/
	

_T_44

87960930222084	

_T_60�Mux.scala 47:69P2:
_T_6212/
	

_T_43

43980465111044	

_T_61�Mux.scala 47:69P2:
_T_6312/
	

_T_42

21990232555524	

_T_62�Mux.scala 47:69P2:
_T_6412/
	

_T_41

10995116277764	

_T_63�Mux.scala 47:69O29
_T_6502.
	

_T_40

5497558138884	

_T_64�Mux.scala 47:69O29
_T_6602.
	

_T_39

2748779069444	

_T_65�Mux.scala 47:69O29
_T_6702.
	

_T_38

1374389534724	

_T_66�Mux.scala 47:69N28
_T_68/2-
	

_T_37

687194767364	

_T_67�Mux.scala 47:69N28
_T_69/2-
	

_T_36

343597383684	

_T_68�Mux.scala 47:69N28
_T_70/2-
	

_T_35

171798691844	

_T_69�Mux.scala 47:69M27
_T_71.2,
	

_T_34


85899345924	

_T_70�Mux.scala 47:69M27
_T_72.2,
	

_T_33


42949672964	

_T_71�Mux.scala 47:69M27
_T_73.2,
	

_T_32


21474836484	

_T_72�Mux.scala 47:69M27
_T_74.2,
	

_T_31


10737418244	

_T_73�Mux.scala 47:69L26
_T_75-2+
	

_T_30

	5368709124	

_T_74�Mux.scala 47:69L26
_T_76-2+
	

_T_29

	2684354564	

_T_75�Mux.scala 47:69L26
_T_77-2+
	

_T_28

	1342177284	

_T_76�Mux.scala 47:69K25
_T_78,2*
	

_T_27


671088644	

_T_77�Mux.scala 47:69K25
_T_79,2*
	

_T_26


335544324	

_T_78�Mux.scala 47:69K25
_T_80,2*
	

_T_25


167772164	

_T_79�Mux.scala 47:69J24
_T_81+2)
	

_T_24
	
83886084	

_T_80�Mux.scala 47:69J24
_T_82+2)
	

_T_23
	
41943044	

_T_81�Mux.scala 47:69J24
_T_83+2)
	

_T_22
	
20971524	

_T_82�Mux.scala 47:69J24
_T_84+2)
	

_T_21
	
10485764	

_T_83�Mux.scala 47:69I23
_T_85*2(
	

_T_20

5242884	

_T_84�Mux.scala 47:69I23
_T_86*2(
	

_T_19

2621444	

_T_85�Mux.scala 47:69I23
_T_87*2(
	

_T_18

1310724	

_T_86�Mux.scala 47:69H22
_T_88)2'
	

_T_17

655364	

_T_87�Mux.scala 47:69H22
_T_89)2'
	

_T_16

327684	

_T_88�Mux.scala 47:69H22
_T_90)2'
	

_T_15

163844	

_T_89�Mux.scala 47:69G21
_T_91(2&
	

_T_14

81924	

_T_90�Mux.scala 47:69G21
_T_92(2&
	

_T_13

40964	

_T_91�Mux.scala 47:69G21
_T_93(2&
	

_T_12

20484	

_T_92�Mux.scala 47:69G21
_T_94(2&
	

_T_11

10244	

_T_93�Mux.scala 47:69F20
_T_95'2%
	

_T_10

5124	

_T_94�Mux.scala 47:69E2/
_T_96&2$


_T_9

2564	

_T_95�Mux.scala 47:69E2/
_T_97&2$


_T_8

1284	

_T_96�Mux.scala 47:69D2.
_T_98%2#


_T_7


644	

_T_97�Mux.scala 47:69D2.
_T_99%2#


_T_6


324	

_T_98�Mux.scala 47:69E2/
_T_100%2#


_T_5


164	

_T_99�Mux.scala 47:69E2/
_T_101%2#


_T_4	

84


_T_100�Mux.scala 47:69E2/
_T_102%2#


_T_3	

44


_T_101�Mux.scala 47:69E2/
_T_103%2#


_T_2	

24


_T_102�Mux.scala 47:69E2/
_T_104%2#


_T_1	

14


_T_103�Mux.scala 47:697z
B


sels
0


_T_104�util.scala 409:1592!
_T_105RB


sels
0�util.scala 410:21A2)
_T_106R

	free_list


_T_105�util.scala 410:19:

sel_fire2


�rename-freelist.scala 55:23b2I
allocs_0=R;
	

1*:(
 B
:


ioalloc_pregs
0bits�OneHot.scala 58:35M23
_T_107)R'B
:


ioreqs
0
0
0�Bitwise.scala 72:15[2A
_T_108725



_T_107

45035996273704954	

04�Bitwise.scala 72:12J2(
_T_109R


allocs_0


_T_108�rename-freelist.scala 59:88P2.
alloc_masks_0R	

04


_T_109�rename-freelist.scala 59:84I2/
_T_110%R#B



sel_fire
0
0
0�Bitwise.scala 72:15[2A
_T_111725



_T_110

45035996273704954	

04�Bitwise.scala 72:12Q2/
sel_mask#R!B


sels
0


_T_111�rename-freelist.scala 62:60`2F
_T_112<R:,:*
:
:


iobrupdateb2
mispredict
0
0�Bitwise.scala 72:15[2A
_T_113725



_T_112

45035996273704954	

04�Bitwise.scala 72:12�2j
br_deallocs[RYIJG


br_alloc_lists1:/
%:#
:
:


iobrupdateb2uopbr_tag


_T_113�rename-freelist.scala 63:63b2I
_T_114?R=
	

1,:*
"B 
:


iodealloc_pregs
0bits�OneHot.scala 58:35G2%
_T_115R


_T_114
51
0�rename-freelist.scala 64:64a2G
_T_116=R;-:+
"B 
:


iodealloc_pregs
0valid
0
0�Bitwise.scala 72:15[2A
_T_117725



_T_116

45035996273704954	

04�Bitwise.scala 72:12H2&
_T_118R


_T_115


_T_117�rename-freelist.scala 64:79T21
dealloc_mask!R


_T_118

br_deallocs�rename-freelist.scala 64:1108

_T_1192


�rename-freelist.scala 66:25dzB
B



_T_119
0+:)
 B
:


ioren_br_tags
0valid�rename-freelist.scala 66:25`2G
_T_120=R;
	

1*:(
 B
:


ioren_br_tags
0bits�OneHot.scala 58:35F2$
_T_121R


_T_120
0
0�rename-freelist.scala 69:728

_T_1222


�rename-freelist.scala 69:27Cz!
B



_T_122
0


_T_121�rename-freelist.scala 69:27Z28
_T_123.R,B



_T_122
0B



_T_119
0�rename-freelist.scala 69:85<2
_T_124R"


_T_123�rename-freelist.scala 70:29:2$
_T_125R


_T_123
0
0�Mux.scala 29:36A2
_T_126R

br_deallocs�rename-freelist.scala 72:60Y27
_T_127-R+B


br_alloc_lists
0


_T_126�rename-freelist.scala 72:58O2-
_T_128#R!


_T_127

alloc_masks_0�rename-freelist.scala 72:73S21
_T_129'2%



_T_124	

04


_T_128�rename-freelist.scala 71:29Kz)
B


br_alloc_lists
0


_T_129�rename-freelist.scala 71:23`2G
_T_130=R;
	

1*:(
 B
:


ioren_br_tags
0bits�OneHot.scala 58:35F2$
_T_131R


_T_130
1
1�rename-freelist.scala 69:728

_T_1322


�rename-freelist.scala 69:27Cz!
B



_T_132
0


_T_131�rename-freelist.scala 69:27Z28
_T_133.R,B



_T_132
0B



_T_119
0�rename-freelist.scala 69:85<2
_T_134R"


_T_133�rename-freelist.scala 70:29:2$
_T_135R


_T_133
0
0�Mux.scala 29:36A2
_T_136R

br_deallocs�rename-freelist.scala 72:60Y27
_T_137-R+B


br_alloc_lists
1


_T_136�rename-freelist.scala 72:58O2-
_T_138#R!


_T_137

alloc_masks_0�rename-freelist.scala 72:73S21
_T_139'2%



_T_134	

04


_T_138�rename-freelist.scala 71:29Kz)
B


br_alloc_lists
1


_T_139�rename-freelist.scala 71:23`2G
_T_140=R;
	

1*:(
 B
:


ioren_br_tags
0bits�OneHot.scala 58:35F2$
_T_141R


_T_140
2
2�rename-freelist.scala 69:728

_T_1422


�rename-freelist.scala 69:27Cz!
B



_T_142
0


_T_141�rename-freelist.scala 69:27Z28
_T_143.R,B



_T_142
0B



_T_119
0�rename-freelist.scala 69:85<2
_T_144R"


_T_143�rename-freelist.scala 70:29:2$
_T_145R


_T_143
0
0�Mux.scala 29:36A2
_T_146R

br_deallocs�rename-freelist.scala 72:60Y27
_T_147-R+B


br_alloc_lists
2


_T_146�rename-freelist.scala 72:58O2-
_T_148#R!


_T_147

alloc_masks_0�rename-freelist.scala 72:73S21
_T_149'2%



_T_144	

04


_T_148�rename-freelist.scala 71:29Kz)
B


br_alloc_lists
2


_T_149�rename-freelist.scala 71:23`2G
_T_150=R;
	

1*:(
 B
:


ioren_br_tags
0bits�OneHot.scala 58:35F2$
_T_151R


_T_150
3
3�rename-freelist.scala 69:728

_T_1522


�rename-freelist.scala 69:27Cz!
B



_T_152
0


_T_151�rename-freelist.scala 69:27Z28
_T_153.R,B



_T_152
0B



_T_119
0�rename-freelist.scala 69:85<2
_T_154R"


_T_153�rename-freelist.scala 70:29:2$
_T_155R


_T_153
0
0�Mux.scala 29:36A2
_T_156R

br_deallocs�rename-freelist.scala 72:60Y27
_T_157-R+B


br_alloc_lists
3


_T_156�rename-freelist.scala 72:58O2-
_T_158#R!


_T_157

alloc_masks_0�rename-freelist.scala 72:73S21
_T_159'2%



_T_154	

04


_T_158�rename-freelist.scala 71:29Kz)
B


br_alloc_lists
3


_T_159�rename-freelist.scala 71:23`2G
_T_160=R;
	

1*:(
 B
:


ioren_br_tags
0bits�OneHot.scala 58:35F2$
_T_161R


_T_160
4
4�rename-freelist.scala 69:728

_T_1622


�rename-freelist.scala 69:27Cz!
B



_T_162
0


_T_161�rename-freelist.scala 69:27Z28
_T_163.R,B



_T_162
0B



_T_119
0�rename-freelist.scala 69:85<2
_T_164R"


_T_163�rename-freelist.scala 70:29:2$
_T_165R


_T_163
0
0�Mux.scala 29:36A2
_T_166R

br_deallocs�rename-freelist.scala 72:60Y27
_T_167-R+B


br_alloc_lists
4


_T_166�rename-freelist.scala 72:58O2-
_T_168#R!


_T_167

alloc_masks_0�rename-freelist.scala 72:73S21
_T_169'2%



_T_164	

04


_T_168�rename-freelist.scala 71:29Kz)
B


br_alloc_lists
4


_T_169�rename-freelist.scala 71:23`2G
_T_170=R;
	

1*:(
 B
:


ioren_br_tags
0bits�OneHot.scala 58:35F2$
_T_171R


_T_170
5
5�rename-freelist.scala 69:728

_T_1722


�rename-freelist.scala 69:27Cz!
B



_T_172
0


_T_171�rename-freelist.scala 69:27Z28
_T_173.R,B



_T_172
0B



_T_119
0�rename-freelist.scala 69:85<2
_T_174R"


_T_173�rename-freelist.scala 70:29:2$
_T_175R


_T_173
0
0�Mux.scala 29:36A2
_T_176R

br_deallocs�rename-freelist.scala 72:60Y27
_T_177-R+B


br_alloc_lists
5


_T_176�rename-freelist.scala 72:58O2-
_T_178#R!


_T_177

alloc_masks_0�rename-freelist.scala 72:73S21
_T_179'2%



_T_174	

04


_T_178�rename-freelist.scala 71:29Kz)
B


br_alloc_lists
5


_T_179�rename-freelist.scala 71:23`2G
_T_180=R;
	

1*:(
 B
:


ioren_br_tags
0bits�OneHot.scala 58:35F2$
_T_181R


_T_180
6
6�rename-freelist.scala 69:728

_T_1822


�rename-freelist.scala 69:27Cz!
B



_T_182
0


_T_181�rename-freelist.scala 69:27Z28
_T_183.R,B



_T_182
0B



_T_119
0�rename-freelist.scala 69:85<2
_T_184R"


_T_183�rename-freelist.scala 70:29:2$
_T_185R


_T_183
0
0�Mux.scala 29:36A2
_T_186R

br_deallocs�rename-freelist.scala 72:60Y27
_T_187-R+B


br_alloc_lists
6


_T_186�rename-freelist.scala 72:58O2-
_T_188#R!


_T_187

alloc_masks_0�rename-freelist.scala 72:73S21
_T_189'2%



_T_184	

04


_T_188�rename-freelist.scala 71:29Kz)
B


br_alloc_lists
6


_T_189�rename-freelist.scala 71:23`2G
_T_190=R;
	

1*:(
 B
:


ioren_br_tags
0bits�OneHot.scala 58:35F2$
_T_191R


_T_190
7
7�rename-freelist.scala 69:728

_T_1922


�rename-freelist.scala 69:27Cz!
B



_T_192
0


_T_191�rename-freelist.scala 69:27Z28
_T_193.R,B



_T_192
0B



_T_119
0�rename-freelist.scala 69:85<2
_T_194R"


_T_193�rename-freelist.scala 70:29:2$
_T_195R


_T_193
0
0�Mux.scala 29:36A2
_T_196R

br_deallocs�rename-freelist.scala 72:60Y27
_T_197-R+B


br_alloc_lists
7


_T_196�rename-freelist.scala 72:58O2-
_T_198#R!


_T_197

alloc_masks_0�rename-freelist.scala 72:73S21
_T_199'2%



_T_194	

04


_T_198�rename-freelist.scala 71:29Kz)
B


br_alloc_lists
7


_T_199�rename-freelist.scala 71:23>2
_T_200R


sel_mask�rename-freelist.scala 76:29K2)
_T_201R

	free_list


_T_200�rename-freelist.scala 76:27N2,
_T_202"R 


_T_201

dealloc_mask�rename-freelist.scala 76:39=2
_T_203R	

14�rename-freelist.scala 76:57H2&
_T_204R


_T_202


_T_203�rename-freelist.scala 76:55=z


	free_list


_T_204�rename-freelist.scala 76:13C2!
_T_205R"B


sels
0�rename-freelist.scala 80:27U3
_T_206
	

clock"	

reset*	

0�rename-freelist.scala 81:26F2-
_T_207#R!B


sels
0
51
32�OneHot.scala 30:18E2,
_T_208"R B


sels
0
31
0�OneHot.scala 31:1832
_T_209R"


_T_207�OneHot.scala 32:14?2&
_T_210R


_T_207


_T_208�OneHot.scala 32:28?2&
_T_211R


_T_210
31
16�OneHot.scala 30:18>2%
_T_212R


_T_210
15
0�OneHot.scala 31:1832
_T_213R"


_T_211�OneHot.scala 32:14?2&
_T_214R


_T_211


_T_212�OneHot.scala 32:28>2%
_T_215R


_T_214
15
8�OneHot.scala 30:18=2$
_T_216R


_T_214
7
0�OneHot.scala 31:1832
_T_217R"


_T_215�OneHot.scala 32:14?2&
_T_218R


_T_215


_T_216�OneHot.scala 32:28=2$
_T_219R


_T_218
7
4�OneHot.scala 30:18=2$
_T_220R


_T_218
3
0�OneHot.scala 31:1832
_T_221R"


_T_219�OneHot.scala 32:14?2&
_T_222R


_T_219


_T_220�OneHot.scala 32:28=2$
_T_223R


_T_222
3
2�OneHot.scala 30:18=2$
_T_224R


_T_222
1
0�OneHot.scala 31:1832
_T_225R"


_T_223�OneHot.scala 32:14?2&
_T_226R


_T_223


_T_224�OneHot.scala 32:28A2$
_T_227R


_T_226
1
1�CircuitMath.scala 30:8<2&
_T_228R


_T_225


_T_227�Cat.scala 29:58<2&
_T_229R


_T_221


_T_228�Cat.scala 29:58<2&
_T_230R


_T_217


_T_229�Cat.scala 29:58<2&
_T_231R


_T_213


_T_230�Cat.scala 29:58<2&
_T_232R


_T_209


_T_231�Cat.scala 29:58J4
_T_233
	

clock"	

0*


_T_233�Reg.scala 15:16]:G
B



sel_fire
0.z



_T_233


_T_232�Reg.scala 16:23�Reg.scala 16:19X26
_T_234,R*B
:


ioreqs
0	

0�rename-freelist.scala 84:27H2&
_T_235R


_T_206


_T_234�rename-freelist.scala 84:24H2&
_T_236R


_T_235


_T_205�rename-freelist.scala 84:39:z



_T_206


_T_236�rename-freelist.scala 84:13I2'
_T_237R


_T_206	

0�rename-freelist.scala 85:21W25
_T_238+R)


_T_237B
:


ioreqs
0�rename-freelist.scala 85:30H2&
_T_239R


_T_238


_T_205�rename-freelist.scala 85:45Ez#
B



sel_fire
0


_T_239�rename-freelist.scala 85:17Zz8
*:(
 B
:


ioalloc_pregs
0bits


_T_233�rename-freelist.scala 87:29[z9
+:)
 B
:


ioalloc_pregs
0valid


_T_206�rename-freelist.scala 88:29`2G
_T_240=R;
	

1*:(
 B
:


ioalloc_pregs
0bits�OneHot.scala 58:35_2E
_T_241;R9+:)
 B
:


ioalloc_pregs
0valid
0
0�Bitwise.scala 72:15[2A
_T_242725



_T_241

45035996273704954	

04�Bitwise.scala 72:12H2&
_T_243R


_T_240


_T_242�rename-freelist.scala 91:77K2)
_T_244R

	free_list


_T_243�rename-freelist.scala 91:34Oz-
:
:


iodebugfreelist


_T_244�rename-freelist.scala 91:21Pz.
:
:


iodebugisprlist	

0�rename-freelist.scala 92:21c2A
_T_2457R5:
:


iodebugfreelist

dealloc_mask�rename-freelist.scala 94:31<2
_T_246R"


_T_245�rename-freelist.scala 94:47I2'
_T_247R


_T_246	

0�rename-freelist.scala 94:11E2#
_T_248R	

reset
0
0�rename-freelist.scala 94:10H2&
_T_249R


_T_247


_T_248�rename-freelist.scala 94:10I2'
_T_250R


_T_249	

0�rename-freelist.scala 94:10�:�



_T_250�R�
�Assertion failed: [freelist] Returning a free physical register.
    at rename-freelist.scala:94 assert (!(io.debug.freelist & dealloc_mask).orR, "[freelist] Returning a free physical register.")
	

clock"	

1�rename-freelist.scala 94:10<B	

clock	

1�rename-freelist.scala 94:10�rename-freelist.scala 94:10d2B
_T_2518R6%:#
:


iodebugpipeline_empty	

0�rename-freelist.scala 95:11S29
_T_252/R-:
:


iodebugfreelist
0
0�Bitwise.scala 49:65S29
_T_253/R-:
:


iodebugfreelist
1
1�Bitwise.scala 49:65S29
_T_254/R-:
:


iodebugfreelist
2
2�Bitwise.scala 49:65S29
_T_255/R-:
:


iodebugfreelist
3
3�Bitwise.scala 49:65S29
_T_256/R-:
:


iodebugfreelist
4
4�Bitwise.scala 49:65S29
_T_257/R-:
:


iodebugfreelist
5
5�Bitwise.scala 49:65S29
_T_258/R-:
:


iodebugfreelist
6
6�Bitwise.scala 49:65S29
_T_259/R-:
:


iodebugfreelist
7
7�Bitwise.scala 49:65S29
_T_260/R-:
:


iodebugfreelist
8
8�Bitwise.scala 49:65S29
_T_261/R-:
:


iodebugfreelist
9
9�Bitwise.scala 49:65U2;
_T_2621R/:
:


iodebugfreelist
10
10�Bitwise.scala 49:65U2;
_T_2631R/:
:


iodebugfreelist
11
11�Bitwise.scala 49:65U2;
_T_2641R/:
:


iodebugfreelist
12
12�Bitwise.scala 49:65U2;
_T_2651R/:
:


iodebugfreelist
13
13�Bitwise.scala 49:65U2;
_T_2661R/:
:


iodebugfreelist
14
14�Bitwise.scala 49:65U2;
_T_2671R/:
:


iodebugfreelist
15
15�Bitwise.scala 49:65U2;
_T_2681R/:
:


iodebugfreelist
16
16�Bitwise.scala 49:65U2;
_T_2691R/:
:


iodebugfreelist
17
17�Bitwise.scala 49:65U2;
_T_2701R/:
:


iodebugfreelist
18
18�Bitwise.scala 49:65U2;
_T_2711R/:
:


iodebugfreelist
19
19�Bitwise.scala 49:65U2;
_T_2721R/:
:


iodebugfreelist
20
20�Bitwise.scala 49:65U2;
_T_2731R/:
:


iodebugfreelist
21
21�Bitwise.scala 49:65U2;
_T_2741R/:
:


iodebugfreelist
22
22�Bitwise.scala 49:65U2;
_T_2751R/:
:


iodebugfreelist
23
23�Bitwise.scala 49:65U2;
_T_2761R/:
:


iodebugfreelist
24
24�Bitwise.scala 49:65U2;
_T_2771R/:
:


iodebugfreelist
25
25�Bitwise.scala 49:65U2;
_T_2781R/:
:


iodebugfreelist
26
26�Bitwise.scala 49:65U2;
_T_2791R/:
:


iodebugfreelist
27
27�Bitwise.scala 49:65U2;
_T_2801R/:
:


iodebugfreelist
28
28�Bitwise.scala 49:65U2;
_T_2811R/:
:


iodebugfreelist
29
29�Bitwise.scala 49:65U2;
_T_2821R/:
:


iodebugfreelist
30
30�Bitwise.scala 49:65U2;
_T_2831R/:
:


iodebugfreelist
31
31�Bitwise.scala 49:65U2;
_T_2841R/:
:


iodebugfreelist
32
32�Bitwise.scala 49:65U2;
_T_2851R/:
:


iodebugfreelist
33
33�Bitwise.scala 49:65U2;
_T_2861R/:
:


iodebugfreelist
34
34�Bitwise.scala 49:65U2;
_T_2871R/:
:


iodebugfreelist
35
35�Bitwise.scala 49:65U2;
_T_2881R/:
:


iodebugfreelist
36
36�Bitwise.scala 49:65U2;
_T_2891R/:
:


iodebugfreelist
37
37�Bitwise.scala 49:65U2;
_T_2901R/:
:


iodebugfreelist
38
38�Bitwise.scala 49:65U2;
_T_2911R/:
:


iodebugfreelist
39
39�Bitwise.scala 49:65U2;
_T_2921R/:
:


iodebugfreelist
40
40�Bitwise.scala 49:65U2;
_T_2931R/:
:


iodebugfreelist
41
41�Bitwise.scala 49:65U2;
_T_2941R/:
:


iodebugfreelist
42
42�Bitwise.scala 49:65U2;
_T_2951R/:
:


iodebugfreelist
43
43�Bitwise.scala 49:65U2;
_T_2961R/:
:


iodebugfreelist
44
44�Bitwise.scala 49:65U2;
_T_2971R/:
:


iodebugfreelist
45
45�Bitwise.scala 49:65U2;
_T_2981R/:
:


iodebugfreelist
46
46�Bitwise.scala 49:65U2;
_T_2991R/:
:


iodebugfreelist
47
47�Bitwise.scala 49:65U2;
_T_3001R/:
:


iodebugfreelist
48
48�Bitwise.scala 49:65U2;
_T_3011R/:
:


iodebugfreelist
49
49�Bitwise.scala 49:65U2;
_T_3021R/:
:


iodebugfreelist
50
50�Bitwise.scala 49:65U2;
_T_3031R/:
:


iodebugfreelist
51
51�Bitwise.scala 49:65@2&
_T_304R


_T_253


_T_254�Bitwise.scala 47:55>2$
_T_305R


_T_304
1
0�Bitwise.scala 47:55@2&
_T_306R


_T_252


_T_305�Bitwise.scala 47:55>2$
_T_307R


_T_306
1
0�Bitwise.scala 47:55@2&
_T_308R


_T_256


_T_257�Bitwise.scala 47:55>2$
_T_309R


_T_308
1
0�Bitwise.scala 47:55@2&
_T_310R


_T_255


_T_309�Bitwise.scala 47:55>2$
_T_311R


_T_310
1
0�Bitwise.scala 47:55@2&
_T_312R


_T_307


_T_311�Bitwise.scala 47:55>2$
_T_313R


_T_312
2
0�Bitwise.scala 47:55@2&
_T_314R


_T_259


_T_260�Bitwise.scala 47:55>2$
_T_315R


_T_314
1
0�Bitwise.scala 47:55@2&
_T_316R


_T_258


_T_315�Bitwise.scala 47:55>2$
_T_317R


_T_316
1
0�Bitwise.scala 47:55@2&
_T_318R


_T_261


_T_262�Bitwise.scala 47:55>2$
_T_319R


_T_318
1
0�Bitwise.scala 47:55@2&
_T_320R


_T_263


_T_264�Bitwise.scala 47:55>2$
_T_321R


_T_320
1
0�Bitwise.scala 47:55@2&
_T_322R


_T_319


_T_321�Bitwise.scala 47:55>2$
_T_323R


_T_322
2
0�Bitwise.scala 47:55@2&
_T_324R


_T_317


_T_323�Bitwise.scala 47:55>2$
_T_325R


_T_324
2
0�Bitwise.scala 47:55@2&
_T_326R


_T_313


_T_325�Bitwise.scala 47:55>2$
_T_327R


_T_326
3
0�Bitwise.scala 47:55@2&
_T_328R


_T_266


_T_267�Bitwise.scala 47:55>2$
_T_329R


_T_328
1
0�Bitwise.scala 47:55@2&
_T_330R


_T_265


_T_329�Bitwise.scala 47:55>2$
_T_331R


_T_330
1
0�Bitwise.scala 47:55@2&
_T_332R


_T_269


_T_270�Bitwise.scala 47:55>2$
_T_333R


_T_332
1
0�Bitwise.scala 47:55@2&
_T_334R


_T_268


_T_333�Bitwise.scala 47:55>2$
_T_335R


_T_334
1
0�Bitwise.scala 47:55@2&
_T_336R


_T_331


_T_335�Bitwise.scala 47:55>2$
_T_337R


_T_336
2
0�Bitwise.scala 47:55@2&
_T_338R


_T_272


_T_273�Bitwise.scala 47:55>2$
_T_339R


_T_338
1
0�Bitwise.scala 47:55@2&
_T_340R


_T_271


_T_339�Bitwise.scala 47:55>2$
_T_341R


_T_340
1
0�Bitwise.scala 47:55@2&
_T_342R


_T_274


_T_275�Bitwise.scala 47:55>2$
_T_343R


_T_342
1
0�Bitwise.scala 47:55@2&
_T_344R


_T_276


_T_277�Bitwise.scala 47:55>2$
_T_345R


_T_344
1
0�Bitwise.scala 47:55@2&
_T_346R


_T_343


_T_345�Bitwise.scala 47:55>2$
_T_347R


_T_346
2
0�Bitwise.scala 47:55@2&
_T_348R


_T_341


_T_347�Bitwise.scala 47:55>2$
_T_349R


_T_348
2
0�Bitwise.scala 47:55@2&
_T_350R


_T_337


_T_349�Bitwise.scala 47:55>2$
_T_351R


_T_350
3
0�Bitwise.scala 47:55@2&
_T_352R


_T_327


_T_351�Bitwise.scala 47:55>2$
_T_353R


_T_352
4
0�Bitwise.scala 47:55@2&
_T_354R


_T_279


_T_280�Bitwise.scala 47:55>2$
_T_355R


_T_354
1
0�Bitwise.scala 47:55@2&
_T_356R


_T_278


_T_355�Bitwise.scala 47:55>2$
_T_357R


_T_356
1
0�Bitwise.scala 47:55@2&
_T_358R


_T_282


_T_283�Bitwise.scala 47:55>2$
_T_359R


_T_358
1
0�Bitwise.scala 47:55@2&
_T_360R


_T_281


_T_359�Bitwise.scala 47:55>2$
_T_361R


_T_360
1
0�Bitwise.scala 47:55@2&
_T_362R


_T_357


_T_361�Bitwise.scala 47:55>2$
_T_363R


_T_362
2
0�Bitwise.scala 47:55@2&
_T_364R


_T_285


_T_286�Bitwise.scala 47:55>2$
_T_365R


_T_364
1
0�Bitwise.scala 47:55@2&
_T_366R


_T_284


_T_365�Bitwise.scala 47:55>2$
_T_367R


_T_366
1
0�Bitwise.scala 47:55@2&
_T_368R


_T_287


_T_288�Bitwise.scala 47:55>2$
_T_369R


_T_368
1
0�Bitwise.scala 47:55@2&
_T_370R


_T_289


_T_290�Bitwise.scala 47:55>2$
_T_371R


_T_370
1
0�Bitwise.scala 47:55@2&
_T_372R


_T_369


_T_371�Bitwise.scala 47:55>2$
_T_373R


_T_372
2
0�Bitwise.scala 47:55@2&
_T_374R


_T_367


_T_373�Bitwise.scala 47:55>2$
_T_375R


_T_374
2
0�Bitwise.scala 47:55@2&
_T_376R


_T_363


_T_375�Bitwise.scala 47:55>2$
_T_377R


_T_376
3
0�Bitwise.scala 47:55@2&
_T_378R


_T_292


_T_293�Bitwise.scala 47:55>2$
_T_379R


_T_378
1
0�Bitwise.scala 47:55@2&
_T_380R


_T_291


_T_379�Bitwise.scala 47:55>2$
_T_381R


_T_380
1
0�Bitwise.scala 47:55@2&
_T_382R


_T_295


_T_296�Bitwise.scala 47:55>2$
_T_383R


_T_382
1
0�Bitwise.scala 47:55@2&
_T_384R


_T_294


_T_383�Bitwise.scala 47:55>2$
_T_385R


_T_384
1
0�Bitwise.scala 47:55@2&
_T_386R


_T_381


_T_385�Bitwise.scala 47:55>2$
_T_387R


_T_386
2
0�Bitwise.scala 47:55@2&
_T_388R


_T_298


_T_299�Bitwise.scala 47:55>2$
_T_389R


_T_388
1
0�Bitwise.scala 47:55@2&
_T_390R


_T_297


_T_389�Bitwise.scala 47:55>2$
_T_391R


_T_390
1
0�Bitwise.scala 47:55@2&
_T_392R


_T_300


_T_301�Bitwise.scala 47:55>2$
_T_393R


_T_392
1
0�Bitwise.scala 47:55@2&
_T_394R


_T_302


_T_303�Bitwise.scala 47:55>2$
_T_395R


_T_394
1
0�Bitwise.scala 47:55@2&
_T_396R


_T_393


_T_395�Bitwise.scala 47:55>2$
_T_397R


_T_396
2
0�Bitwise.scala 47:55@2&
_T_398R


_T_391


_T_397�Bitwise.scala 47:55>2$
_T_399R


_T_398
2
0�Bitwise.scala 47:55@2&
_T_400R


_T_387


_T_399�Bitwise.scala 47:55>2$
_T_401R


_T_400
3
0�Bitwise.scala 47:55@2&
_T_402R


_T_377


_T_401�Bitwise.scala 47:55>2$
_T_403R


_T_402
4
0�Bitwise.scala 47:55@2&
_T_404R


_T_353


_T_403�Bitwise.scala 47:55>2$
_T_405R


_T_404
5
0�Bitwise.scala 47:55J2(
_T_406R


_T_405


20�rename-freelist.scala 95:67H2&
_T_407R


_T_251


_T_406�rename-freelist.scala 95:36E2#
_T_408R	

reset
0
0�rename-freelist.scala 95:10H2&
_T_409R


_T_407


_T_408�rename-freelist.scala 95:10I2'
_T_410R


_T_409	

0�rename-freelist.scala 95:10�:�



_T_410�R�
�Assertion failed: [freelist] Leaking physical registers.
    at rename-freelist.scala:95 assert (!io.debug.pipeline_empty || PopCount(io.debug.freelist) >= (numPregs - numLregs - 1).U,
	

clock"	

1�rename-freelist.scala 95:10<B	

clock	

1�rename-freelist.scala 95:10�rename-freelist.scala 95:10
����
RenameMapTable
clock" 
reset
�
io�*�
Vmap_reqsH2F
B*@
lrs1

lrs2

lrs3

ldst

[	map_respsN2L
H*F
prs1

prs2

prs3


stale_pdst

I
remap_reqs927
3*1
ldst

pdst

valid

:ren_br_tags)2'
#*!
valid

bits

�brupdate�*�
;b15*3
resolve_mask

mispredict_mask

�b2�*�
�uop�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc

valid


mispredict

taken

cfi_type

pc_sel

jalr_target
(
target_offset

rollback
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
 4

_T2


 �rename-maptable.scala 70:34@z
B


_T
0	

0�rename-maptable.scala 70:34@z
B


_T
1	

0�rename-maptable.scala 70:34@z
B


_T
2	

0�rename-maptable.scala 70:34@z
B


_T
3	

0�rename-maptable.scala 70:34@z
B


_T
4	

0�rename-maptable.scala 70:34@z
B


_T
5	

0�rename-maptable.scala 70:34@z
B


_T
6	

0�rename-maptable.scala 70:34@z
B


_T
7	

0�rename-maptable.scala 70:34@z
B


_T
8	

0�rename-maptable.scala 70:34@z
B


_T
9	

0�rename-maptable.scala 70:34Az
B


_T
10	

0�rename-maptable.scala 70:34Az
B


_T
11	

0�rename-maptable.scala 70:34Az
B


_T
12	

0�rename-maptable.scala 70:34Az
B


_T
13	

0�rename-maptable.scala 70:34Az
B


_T
14	

0�rename-maptable.scala 70:34Az
B


_T
15	

0�rename-maptable.scala 70:34Az
B


_T
16	

0�rename-maptable.scala 70:34Az
B


_T
17	

0�rename-maptable.scala 70:34Az
B


_T
18	

0�rename-maptable.scala 70:34Az
B


_T
19	

0�rename-maptable.scala 70:34Az
B


_T
20	

0�rename-maptable.scala 70:34Az
B


_T
21	

0�rename-maptable.scala 70:34Az
B


_T
22	

0�rename-maptable.scala 70:34Az
B


_T
23	

0�rename-maptable.scala 70:34Az
B


_T
24	

0�rename-maptable.scala 70:34Az
B


_T
25	

0�rename-maptable.scala 70:34Az
B


_T
26	

0�rename-maptable.scala 70:34Az
B


_T
27	

0�rename-maptable.scala 70:34Az
B


_T
28	

0�rename-maptable.scala 70:34Az
B


_T
29	

0�rename-maptable.scala 70:34Az
B


_T
30	

0�rename-maptable.scala 70:34Az
B


_T
31	

0�rename-maptable.scala 70:34Y7
	map_table2


 	

clock"	

reset*

_T�rename-maptable.scala 70:26nL
br_snapshots2
2


 	

clock"	

0*

br_snapshots�rename-maptable.scala 71:25C
!
remap_table2
2


 �rename-maptable.scala 74:25]2D
_T_1<R:
	

1):'
B
:


io
remap_reqs
0ldst�OneHot.scala 58:35\2B
_T_2:R8*:(
B
:


io
remap_reqs
0valid
0
0�Bitwise.scala 72:15Q27
_T_3/2-


_T_2


4294967295 	

0 �Bitwise.scala 72:12N2,
remap_ldsts_oh_0R

_T_1

_T_3�rename-maptable.scala 78:69Rz0
!B
B


remap_table
0
0	

0�rename-maptable.scala 84:27Rz0
!B
B


remap_table
1
0	

0�rename-maptable.scala 84:27N2,
_T_4$R"

remap_ldsts_oh_0
1
1�rename-maptable.scala 87:58y2W
_T_5O2M


_T_4):'
B
:


io
remap_reqs
0pdstB


	map_table
1�rename-maptable.scala 88:70]z;
!B
B


remap_table
0
1B


	map_table
1�rename-maptable.scala 91:27Oz-
!B
B


remap_table
1
1

_T_5�rename-maptable.scala 91:27N2,
_T_6$R"

remap_ldsts_oh_0
2
2�rename-maptable.scala 87:58y2W
_T_7O2M


_T_6):'
B
:


io
remap_reqs
0pdstB


	map_table
2�rename-maptable.scala 88:70]z;
!B
B


remap_table
0
2B


	map_table
2�rename-maptable.scala 91:27Oz-
!B
B


remap_table
1
2

_T_7�rename-maptable.scala 91:27N2,
_T_8$R"

remap_ldsts_oh_0
3
3�rename-maptable.scala 87:58y2W
_T_9O2M


_T_8):'
B
:


io
remap_reqs
0pdstB


	map_table
3�rename-maptable.scala 88:70]z;
!B
B


remap_table
0
3B


	map_table
3�rename-maptable.scala 91:27Oz-
!B
B


remap_table
1
3

_T_9�rename-maptable.scala 91:27O2-
_T_10$R"

remap_ldsts_oh_0
4
4�rename-maptable.scala 87:58{2Y
_T_11P2N
	

_T_10):'
B
:


io
remap_reqs
0pdstB


	map_table
4�rename-maptable.scala 88:70]z;
!B
B


remap_table
0
4B


	map_table
4�rename-maptable.scala 91:27Pz.
!B
B


remap_table
1
4	

_T_11�rename-maptable.scala 91:27O2-
_T_12$R"

remap_ldsts_oh_0
5
5�rename-maptable.scala 87:58{2Y
_T_13P2N
	

_T_12):'
B
:


io
remap_reqs
0pdstB


	map_table
5�rename-maptable.scala 88:70]z;
!B
B


remap_table
0
5B


	map_table
5�rename-maptable.scala 91:27Pz.
!B
B


remap_table
1
5	

_T_13�rename-maptable.scala 91:27O2-
_T_14$R"

remap_ldsts_oh_0
6
6�rename-maptable.scala 87:58{2Y
_T_15P2N
	

_T_14):'
B
:


io
remap_reqs
0pdstB


	map_table
6�rename-maptable.scala 88:70]z;
!B
B


remap_table
0
6B


	map_table
6�rename-maptable.scala 91:27Pz.
!B
B


remap_table
1
6	

_T_15�rename-maptable.scala 91:27O2-
_T_16$R"

remap_ldsts_oh_0
7
7�rename-maptable.scala 87:58{2Y
_T_17P2N
	

_T_16):'
B
:


io
remap_reqs
0pdstB


	map_table
7�rename-maptable.scala 88:70]z;
!B
B


remap_table
0
7B


	map_table
7�rename-maptable.scala 91:27Pz.
!B
B


remap_table
1
7	

_T_17�rename-maptable.scala 91:27O2-
_T_18$R"

remap_ldsts_oh_0
8
8�rename-maptable.scala 87:58{2Y
_T_19P2N
	

_T_18):'
B
:


io
remap_reqs
0pdstB


	map_table
8�rename-maptable.scala 88:70]z;
!B
B


remap_table
0
8B


	map_table
8�rename-maptable.scala 91:27Pz.
!B
B


remap_table
1
8	

_T_19�rename-maptable.scala 91:27O2-
_T_20$R"

remap_ldsts_oh_0
9
9�rename-maptable.scala 87:58{2Y
_T_21P2N
	

_T_20):'
B
:


io
remap_reqs
0pdstB


	map_table
9�rename-maptable.scala 88:70]z;
!B
B


remap_table
0
9B


	map_table
9�rename-maptable.scala 91:27Pz.
!B
B


remap_table
1
9	

_T_21�rename-maptable.scala 91:27Q2/
_T_22&R$

remap_ldsts_oh_0
10
10�rename-maptable.scala 87:58|2Z
_T_23Q2O
	

_T_22):'
B
:


io
remap_reqs
0pdstB


	map_table
10�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
10B


	map_table
10�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
10	

_T_23�rename-maptable.scala 91:27Q2/
_T_24&R$

remap_ldsts_oh_0
11
11�rename-maptable.scala 87:58|2Z
_T_25Q2O
	

_T_24):'
B
:


io
remap_reqs
0pdstB


	map_table
11�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
11B


	map_table
11�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
11	

_T_25�rename-maptable.scala 91:27Q2/
_T_26&R$

remap_ldsts_oh_0
12
12�rename-maptable.scala 87:58|2Z
_T_27Q2O
	

_T_26):'
B
:


io
remap_reqs
0pdstB


	map_table
12�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
12B


	map_table
12�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
12	

_T_27�rename-maptable.scala 91:27Q2/
_T_28&R$

remap_ldsts_oh_0
13
13�rename-maptable.scala 87:58|2Z
_T_29Q2O
	

_T_28):'
B
:


io
remap_reqs
0pdstB


	map_table
13�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
13B


	map_table
13�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
13	

_T_29�rename-maptable.scala 91:27Q2/
_T_30&R$

remap_ldsts_oh_0
14
14�rename-maptable.scala 87:58|2Z
_T_31Q2O
	

_T_30):'
B
:


io
remap_reqs
0pdstB


	map_table
14�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
14B


	map_table
14�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
14	

_T_31�rename-maptable.scala 91:27Q2/
_T_32&R$

remap_ldsts_oh_0
15
15�rename-maptable.scala 87:58|2Z
_T_33Q2O
	

_T_32):'
B
:


io
remap_reqs
0pdstB


	map_table
15�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
15B


	map_table
15�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
15	

_T_33�rename-maptable.scala 91:27Q2/
_T_34&R$

remap_ldsts_oh_0
16
16�rename-maptable.scala 87:58|2Z
_T_35Q2O
	

_T_34):'
B
:


io
remap_reqs
0pdstB


	map_table
16�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
16B


	map_table
16�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
16	

_T_35�rename-maptable.scala 91:27Q2/
_T_36&R$

remap_ldsts_oh_0
17
17�rename-maptable.scala 87:58|2Z
_T_37Q2O
	

_T_36):'
B
:


io
remap_reqs
0pdstB


	map_table
17�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
17B


	map_table
17�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
17	

_T_37�rename-maptable.scala 91:27Q2/
_T_38&R$

remap_ldsts_oh_0
18
18�rename-maptable.scala 87:58|2Z
_T_39Q2O
	

_T_38):'
B
:


io
remap_reqs
0pdstB


	map_table
18�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
18B


	map_table
18�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
18	

_T_39�rename-maptable.scala 91:27Q2/
_T_40&R$

remap_ldsts_oh_0
19
19�rename-maptable.scala 87:58|2Z
_T_41Q2O
	

_T_40):'
B
:


io
remap_reqs
0pdstB


	map_table
19�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
19B


	map_table
19�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
19	

_T_41�rename-maptable.scala 91:27Q2/
_T_42&R$

remap_ldsts_oh_0
20
20�rename-maptable.scala 87:58|2Z
_T_43Q2O
	

_T_42):'
B
:


io
remap_reqs
0pdstB


	map_table
20�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
20B


	map_table
20�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
20	

_T_43�rename-maptable.scala 91:27Q2/
_T_44&R$

remap_ldsts_oh_0
21
21�rename-maptable.scala 87:58|2Z
_T_45Q2O
	

_T_44):'
B
:


io
remap_reqs
0pdstB


	map_table
21�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
21B


	map_table
21�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
21	

_T_45�rename-maptable.scala 91:27Q2/
_T_46&R$

remap_ldsts_oh_0
22
22�rename-maptable.scala 87:58|2Z
_T_47Q2O
	

_T_46):'
B
:


io
remap_reqs
0pdstB


	map_table
22�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
22B


	map_table
22�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
22	

_T_47�rename-maptable.scala 91:27Q2/
_T_48&R$

remap_ldsts_oh_0
23
23�rename-maptable.scala 87:58|2Z
_T_49Q2O
	

_T_48):'
B
:


io
remap_reqs
0pdstB


	map_table
23�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
23B


	map_table
23�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
23	

_T_49�rename-maptable.scala 91:27Q2/
_T_50&R$

remap_ldsts_oh_0
24
24�rename-maptable.scala 87:58|2Z
_T_51Q2O
	

_T_50):'
B
:


io
remap_reqs
0pdstB


	map_table
24�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
24B


	map_table
24�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
24	

_T_51�rename-maptable.scala 91:27Q2/
_T_52&R$

remap_ldsts_oh_0
25
25�rename-maptable.scala 87:58|2Z
_T_53Q2O
	

_T_52):'
B
:


io
remap_reqs
0pdstB


	map_table
25�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
25B


	map_table
25�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
25	

_T_53�rename-maptable.scala 91:27Q2/
_T_54&R$

remap_ldsts_oh_0
26
26�rename-maptable.scala 87:58|2Z
_T_55Q2O
	

_T_54):'
B
:


io
remap_reqs
0pdstB


	map_table
26�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
26B


	map_table
26�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
26	

_T_55�rename-maptable.scala 91:27Q2/
_T_56&R$

remap_ldsts_oh_0
27
27�rename-maptable.scala 87:58|2Z
_T_57Q2O
	

_T_56):'
B
:


io
remap_reqs
0pdstB


	map_table
27�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
27B


	map_table
27�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
27	

_T_57�rename-maptable.scala 91:27Q2/
_T_58&R$

remap_ldsts_oh_0
28
28�rename-maptable.scala 87:58|2Z
_T_59Q2O
	

_T_58):'
B
:


io
remap_reqs
0pdstB


	map_table
28�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
28B


	map_table
28�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
28	

_T_59�rename-maptable.scala 91:27Q2/
_T_60&R$

remap_ldsts_oh_0
29
29�rename-maptable.scala 87:58|2Z
_T_61Q2O
	

_T_60):'
B
:


io
remap_reqs
0pdstB


	map_table
29�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
29B


	map_table
29�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
29	

_T_61�rename-maptable.scala 91:27Q2/
_T_62&R$

remap_ldsts_oh_0
30
30�rename-maptable.scala 87:58|2Z
_T_63Q2O
	

_T_62):'
B
:


io
remap_reqs
0pdstB


	map_table
30�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
30B


	map_table
30�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
30	

_T_63�rename-maptable.scala 91:27Q2/
_T_64&R$

remap_ldsts_oh_0
31
31�rename-maptable.scala 87:58|2Z
_T_65Q2O
	

_T_64):'
B
:


io
remap_reqs
0pdstB


	map_table
31�rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
31B


	map_table
31�rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
31	

_T_65�rename-maptable.scala 91:27�%:�%
+:)
 B
:


ioren_br_tags
0valid�zn
IBG
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
0!B
B


remap_table
1
0�rename-maptable.scala 99:44�zn
IBG
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
1!B
B


remap_table
1
1�rename-maptable.scala 99:44�zn
IBG
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
2!B
B


remap_table
1
2�rename-maptable.scala 99:44�zn
IBG
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
3!B
B


remap_table
1
3�rename-maptable.scala 99:44�zn
IBG
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
4!B
B


remap_table
1
4�rename-maptable.scala 99:44�zn
IBG
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
5!B
B


remap_table
1
5�rename-maptable.scala 99:44�zn
IBG
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
6!B
B


remap_table
1
6�rename-maptable.scala 99:44�zn
IBG
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
7!B
B


remap_table
1
7�rename-maptable.scala 99:44�zn
IBG
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
8!B
B


remap_table
1
8�rename-maptable.scala 99:44�zn
IBG
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
9!B
B


remap_table
1
9�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
10"B 
B


remap_table
1
10�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
11"B 
B


remap_table
1
11�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
12"B 
B


remap_table
1
12�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
13"B 
B


remap_table
1
13�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
14"B 
B


remap_table
1
14�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
15"B 
B


remap_table
1
15�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
16"B 
B


remap_table
1
16�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
17"B 
B


remap_table
1
17�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
18"B 
B


remap_table
1
18�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
19"B 
B


remap_table
1
19�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
20"B 
B


remap_table
1
20�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
21"B 
B


remap_table
1
21�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
22"B 
B


remap_table
1
22�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
23"B 
B


remap_table
1
23�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
24"B 
B


remap_table
1
24�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
25"B 
B


remap_table
1
25�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
26"B 
B


remap_table
1
26�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
27"B 
B


remap_table
1
27�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
28"B 
B


remap_table
1
28�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
29"B 
B


remap_table
1
29�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
30"B 
B


remap_table
1
30�rename-maptable.scala 99:44�zp
JBH
@J>


br_snapshots*:(
 B
:


ioren_br_tags
0bits
31"B 
B


remap_table
1
31�rename-maptable.scala 99:44�rename-maptable.scala 98:36�=:�=
,:*
:
:


iobrupdateb2
mispredict�zj
B


	map_table
0PBN
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
0�rename-maptable.scala 105:15�zj
B


	map_table
1PBN
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
1�rename-maptable.scala 105:15�zj
B


	map_table
2PBN
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
2�rename-maptable.scala 105:15�zj
B


	map_table
3PBN
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
3�rename-maptable.scala 105:15�zj
B


	map_table
4PBN
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
4�rename-maptable.scala 105:15�zj
B


	map_table
5PBN
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
5�rename-maptable.scala 105:15�zj
B


	map_table
6PBN
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
6�rename-maptable.scala 105:15�zj
B


	map_table
7PBN
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
7�rename-maptable.scala 105:15�zj
B


	map_table
8PBN
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
8�rename-maptable.scala 105:15�zj
B


	map_table
9PBN
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
9�rename-maptable.scala 105:15�zl
B


	map_table
10QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
10�rename-maptable.scala 105:15�zl
B


	map_table
11QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
11�rename-maptable.scala 105:15�zl
B


	map_table
12QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
12�rename-maptable.scala 105:15�zl
B


	map_table
13QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
13�rename-maptable.scala 105:15�zl
B


	map_table
14QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
14�rename-maptable.scala 105:15�zl
B


	map_table
15QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
15�rename-maptable.scala 105:15�zl
B


	map_table
16QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
16�rename-maptable.scala 105:15�zl
B


	map_table
17QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
17�rename-maptable.scala 105:15�zl
B


	map_table
18QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
18�rename-maptable.scala 105:15�zl
B


	map_table
19QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
19�rename-maptable.scala 105:15�zl
B


	map_table
20QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
20�rename-maptable.scala 105:15�zl
B


	map_table
21QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
21�rename-maptable.scala 105:15�zl
B


	map_table
22QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
22�rename-maptable.scala 105:15�zl
B


	map_table
23QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
23�rename-maptable.scala 105:15�zl
B


	map_table
24QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
24�rename-maptable.scala 105:15�zl
B


	map_table
25QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
25�rename-maptable.scala 105:15�zl
B


	map_table
26QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
26�rename-maptable.scala 105:15�zl
B


	map_table
27QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
27�rename-maptable.scala 105:15�zl
B


	map_table
28QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
28�rename-maptable.scala 105:15�zl
B


	map_table
29QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
29�rename-maptable.scala 105:15�zl
B


	map_table
30QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
30�rename-maptable.scala 105:15�zl
B


	map_table
31QBO
GJE


br_snapshots1:/
%:#
:
:


iobrupdateb2uopbr_tag
31�rename-maptable.scala 105:15^z;
B


	map_table
0!B
B


remap_table
1
0�rename-maptable.scala 108:15^z;
B


	map_table
1!B
B


remap_table
1
1�rename-maptable.scala 108:15^z;
B


	map_table
2!B
B


remap_table
1
2�rename-maptable.scala 108:15^z;
B


	map_table
3!B
B


remap_table
1
3�rename-maptable.scala 108:15^z;
B


	map_table
4!B
B


remap_table
1
4�rename-maptable.scala 108:15^z;
B


	map_table
5!B
B


remap_table
1
5�rename-maptable.scala 108:15^z;
B


	map_table
6!B
B


remap_table
1
6�rename-maptable.scala 108:15^z;
B


	map_table
7!B
B


remap_table
1
7�rename-maptable.scala 108:15^z;
B


	map_table
8!B
B


remap_table
1
8�rename-maptable.scala 108:15^z;
B


	map_table
9!B
B


remap_table
1
9�rename-maptable.scala 108:15`z=
B


	map_table
10"B 
B


remap_table
1
10�rename-maptable.scala 108:15`z=
B


	map_table
11"B 
B


remap_table
1
11�rename-maptable.scala 108:15`z=
B


	map_table
12"B 
B


remap_table
1
12�rename-maptable.scala 108:15`z=
B


	map_table
13"B 
B


remap_table
1
13�rename-maptable.scala 108:15`z=
B


	map_table
14"B 
B


remap_table
1
14�rename-maptable.scala 108:15`z=
B


	map_table
15"B 
B


remap_table
1
15�rename-maptable.scala 108:15`z=
B


	map_table
16"B 
B


remap_table
1
16�rename-maptable.scala 108:15`z=
B


	map_table
17"B 
B


remap_table
1
17�rename-maptable.scala 108:15`z=
B


	map_table
18"B 
B


remap_table
1
18�rename-maptable.scala 108:15`z=
B


	map_table
19"B 
B


remap_table
1
19�rename-maptable.scala 108:15`z=
B


	map_table
20"B 
B


remap_table
1
20�rename-maptable.scala 108:15`z=
B


	map_table
21"B 
B


remap_table
1
21�rename-maptable.scala 108:15`z=
B


	map_table
22"B 
B


remap_table
1
22�rename-maptable.scala 108:15`z=
B


	map_table
23"B 
B


remap_table
1
23�rename-maptable.scala 108:15`z=
B


	map_table
24"B 
B


remap_table
1
24�rename-maptable.scala 108:15`z=
B


	map_table
25"B 
B


remap_table
1
25�rename-maptable.scala 108:15`z=
B


	map_table
26"B 
B


remap_table
1
26�rename-maptable.scala 108:15`z=
B


	map_table
27"B 
B


remap_table
1
27�rename-maptable.scala 108:15`z=
B


	map_table
28"B 
B


remap_table
1
28�rename-maptable.scala 108:15`z=
B


	map_table
29"B 
B


remap_table
1
29�rename-maptable.scala 108:15`z=
B


	map_table
30"B 
B


remap_table
1
30�rename-maptable.scala 108:15`z=
B


	map_table
31"B 
B


remap_table
1
31�rename-maptable.scala 108:15�rename-maptable.scala 103:36G2@
_T_667R5':%
B
:


iomap_reqs
0lrs1
4
0�
 kzH
(:&
B
:


io	map_resps
0prs1J


	map_table	

_T_66�rename-maptable.scala 113:32G2@
_T_677R5':%
B
:


iomap_reqs
0lrs2
4
0�
 kzH
(:&
B
:


io	map_resps
0prs2J


	map_table	

_T_67�rename-maptable.scala 115:32G2@
_T_687R5':%
B
:


iomap_reqs
0lrs3
4
0�
 kzH
(:&
B
:


io	map_resps
0prs3J


	map_table	

_T_68�rename-maptable.scala 117:32G2@
_T_697R5':%
B
:


iomap_reqs
0ldst
4
0�
 qzN
.:,
B
:


io	map_resps
0
stale_pdstJ


	map_table	

_T_69�rename-maptable.scala 119:32N�*
(:&
B
:


io	map_resps
0prs3�rename-maptable.scala 122:38i2F
_T_70=R;*:(
B
:


io
remap_reqs
0valid	

0�rename-maptable.scala 128:13s2P
_T_71GREB


	map_table
0):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38s2P
_T_72GREB


	map_table
1):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38s2P
_T_73GREB


	map_table
2):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38s2P
_T_74GREB


	map_table
3):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38s2P
_T_75GREB


	map_table
4):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38s2P
_T_76GREB


	map_table
5):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38s2P
_T_77GREB


	map_table
6):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38s2P
_T_78GREB


	map_table
7):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38s2P
_T_79GREB


	map_table
8):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38s2P
_T_80GREB


	map_table
9):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_81HRFB


	map_table
10):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_82HRFB


	map_table
11):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_83HRFB


	map_table
12):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_84HRFB


	map_table
13):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_85HRFB


	map_table
14):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_86HRFB


	map_table
15):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_87HRFB


	map_table
16):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_88HRFB


	map_table
17):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_89HRFB


	map_table
18):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_90HRFB


	map_table
19):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_91HRFB


	map_table
20):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_92HRFB


	map_table
21):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_93HRFB


	map_table
22):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_94HRFB


	map_table
23):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_95HRFB


	map_table
24):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_96HRFB


	map_table
25):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_97HRFB


	map_table
26):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_98HRFB


	map_table
27):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38t2Q
_T_99HRFB


	map_table
28):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38u2R
_T_100HRFB


	map_table
29):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38u2R
_T_101HRFB


	map_table
30):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38u2R
_T_102HRFB


	map_table
31):'
B
:


io
remap_reqs
0pdst�rename-maptable.scala 128:38I2&
_T_103R	

0	

_T_71�rename-maptable.scala 128:38H2%
_T_104R


_T_103	

_T_72�rename-maptable.scala 128:38H2%
_T_105R


_T_104	

_T_73�rename-maptable.scala 128:38H2%
_T_106R


_T_105	

_T_74�rename-maptable.scala 128:38H2%
_T_107R


_T_106	

_T_75�rename-maptable.scala 128:38H2%
_T_108R


_T_107	

_T_76�rename-maptable.scala 128:38H2%
_T_109R


_T_108	

_T_77�rename-maptable.scala 128:38H2%
_T_110R


_T_109	

_T_78�rename-maptable.scala 128:38H2%
_T_111R


_T_110	

_T_79�rename-maptable.scala 128:38H2%
_T_112R


_T_111	

_T_80�rename-maptable.scala 128:38H2%
_T_113R


_T_112	

_T_81�rename-maptable.scala 128:38H2%
_T_114R


_T_113	

_T_82�rename-maptable.scala 128:38H2%
_T_115R


_T_114	

_T_83�rename-maptable.scala 128:38H2%
_T_116R


_T_115	

_T_84�rename-maptable.scala 128:38H2%
_T_117R


_T_116	

_T_85�rename-maptable.scala 128:38H2%
_T_118R


_T_117	

_T_86�rename-maptable.scala 128:38H2%
_T_119R


_T_118	

_T_87�rename-maptable.scala 128:38H2%
_T_120R


_T_119	

_T_88�rename-maptable.scala 128:38H2%
_T_121R


_T_120	

_T_89�rename-maptable.scala 128:38H2%
_T_122R


_T_121	

_T_90�rename-maptable.scala 128:38H2%
_T_123R


_T_122	

_T_91�rename-maptable.scala 128:38H2%
_T_124R


_T_123	

_T_92�rename-maptable.scala 128:38H2%
_T_125R


_T_124	

_T_93�rename-maptable.scala 128:38H2%
_T_126R


_T_125	

_T_94�rename-maptable.scala 128:38H2%
_T_127R


_T_126	

_T_95�rename-maptable.scala 128:38H2%
_T_128R


_T_127	

_T_96�rename-maptable.scala 128:38H2%
_T_129R


_T_128	

_T_97�rename-maptable.scala 128:38H2%
_T_130R


_T_129	

_T_98�rename-maptable.scala 128:38H2%
_T_131R


_T_130	

_T_99�rename-maptable.scala 128:38I2&
_T_132R


_T_131


_T_100�rename-maptable.scala 128:38I2&
_T_133R


_T_132


_T_101�rename-maptable.scala 128:38I2&
_T_134R


_T_133


_T_102�rename-maptable.scala 128:38J2'
_T_135R


_T_134	

0�rename-maptable.scala 128:19H2%
_T_136R	

_T_70


_T_135�rename-maptable.scala 128:16i2F
_T_137<R:):'
B
:


io
remap_reqs
0pdst	

0�rename-maptable.scala 128:47S20
_T_138&R$


_T_137:


iorollback�rename-maptable.scala 128:55I2&
_T_139R


_T_136


_T_138�rename-maptable.scala 128:42F2#
_T_140R	

reset
0
0�rename-maptable.scala 128:12I2&
_T_141R


_T_139


_T_140�rename-maptable.scala 128:12J2'
_T_142R


_T_141	

0�rename-maptable.scala 128:12�:�



_T_142�R�
�Assertion failed: [maptable] Trying to write a duplicate mapping.
    at rename-maptable.scala:128 assert (!r || !map_table.contains(p) || p === 0.U && io.rollback, "[maptable] Trying to write a duplicate mapping.")}
	

clock"	

1�rename-maptable.scala 128:12=B	

clock	

1�rename-maptable.scala 128:12�rename-maptable.scala 128:12
��	��	
RenameStage
clock" 
reset
�P
io�P*�P

ren_stalls2



kill

dec_fire2



�dec_uops�2�
�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc

	ren2_mask2



�	ren2_uops�2�
�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc

�brupdate�*�
;b15*3
resolve_mask

mispredict_mask

�b2�*�
�uop�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc

valid


mispredict

taken

cfi_type

pc_sel

jalr_target
(
target_offset

dis_fire2



	dis_ready

�wakeups�2�
�*�
valid

�bits�*�
�uop�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc

data
@

predicated

�fflags�*�
valid

�bits�*�
�uop�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc

flags


com_valids2



�com_uops�2�
�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc


rbk_valids2



rollback

debug_rob_empty

Hdebug?*=
freelist
4
isprlist
4
	busytable
4�
	

clock�
 �
	

reset�
 �


io�
 8

	ren1_fire2


�rename-stage.scala 97:29�
�
	ren1_uops�2�
�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc
�rename-stage.scala 98:29;

ren2_valids2


�rename-stage.scala 104:29�
�
	ren2_uops�2�
�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc
�rename-stage.scala 105:29?

ren2_alloc_reqs2


�rename-stage.scala 106:29Wz7
B


	ren1_fire
0B
:


iodec_fire
0�rename-stage.scala 113:27wzW
&:$
B


	ren1_uops
0
debug_tsrc-:+
B
:


iodec_uops
0
debug_tsrc�rename-stage.scala 114:27wzW
&:$
B


	ren1_uops
0
debug_fsrc-:+
B
:


iodec_uops
0
debug_fsrc�rename-stage.scala 114:27wzW
&:$
B


	ren1_uops
0
bp_xcpt_if-:+
B
:


iodec_uops
0
bp_xcpt_if�rename-stage.scala 114:27yzY
':%
B


	ren1_uops
0bp_debug_if.:,
B
:


iodec_uops
0bp_debug_if�rename-stage.scala 114:27wzW
&:$
B


	ren1_uops
0
xcpt_ma_if-:+
B
:


iodec_uops
0
xcpt_ma_if�rename-stage.scala 114:27wzW
&:$
B


	ren1_uops
0
xcpt_ae_if-:+
B
:


iodec_uops
0
xcpt_ae_if�rename-stage.scala 114:27wzW
&:$
B


	ren1_uops
0
xcpt_pf_if-:+
B
:


iodec_uops
0
xcpt_pf_if�rename-stage.scala 114:27uzU
%:#
B


	ren1_uops
0	fp_single,:*
B
:


iodec_uops
0	fp_single�rename-stage.scala 114:27ozO
": 
B


	ren1_uops
0fp_val):'
B
:


iodec_uops
0fp_val�rename-stage.scala 114:27qzQ
#:!
B


	ren1_uops
0frs3_en*:(
B
:


iodec_uops
0frs3_en�rename-stage.scala 114:27wzW
&:$
B


	ren1_uops
0
lrs2_rtype-:+
B
:


iodec_uops
0
lrs2_rtype�rename-stage.scala 114:27wzW
&:$
B


	ren1_uops
0
lrs1_rtype-:+
B
:


iodec_uops
0
lrs1_rtype�rename-stage.scala 114:27uzU
%:#
B


	ren1_uops
0	dst_rtype,:*
B
:


iodec_uops
0	dst_rtype�rename-stage.scala 114:27szS
$:"
B


	ren1_uops
0ldst_val+:)
B
:


iodec_uops
0ldst_val�rename-stage.scala 114:27kzK
 :
B


	ren1_uops
0lrs3':%
B
:


iodec_uops
0lrs3�rename-stage.scala 114:27kzK
 :
B


	ren1_uops
0lrs2':%
B
:


iodec_uops
0lrs2�rename-stage.scala 114:27kzK
 :
B


	ren1_uops
0lrs1':%
B
:


iodec_uops
0lrs1�rename-stage.scala 114:27kzK
 :
B


	ren1_uops
0ldst':%
B
:


iodec_uops
0ldst�rename-stage.scala 114:27yzY
':%
B


	ren1_uops
0ldst_is_rs1.:,
B
:


iodec_uops
0ldst_is_rs1�rename-stage.scala 114:27�za
+:)
B


	ren1_uops
0flush_on_commit2:0
B
:


iodec_uops
0flush_on_commit�rename-stage.scala 114:27uzU
%:#
B


	ren1_uops
0	is_unique,:*
B
:


iodec_uops
0	is_unique�rename-stage.scala 114:27}z]
):'
B


	ren1_uops
0is_sys_pc2epc0:.
B
:


iodec_uops
0is_sys_pc2epc�rename-stage.scala 114:27szS
$:"
B


	ren1_uops
0uses_stq+:)
B
:


iodec_uops
0uses_stq�rename-stage.scala 114:27szS
$:"
B


	ren1_uops
0uses_ldq+:)
B
:


iodec_uops
0uses_ldq�rename-stage.scala 114:27ozO
": 
B


	ren1_uops
0is_amo):'
B
:


iodec_uops
0is_amo�rename-stage.scala 114:27uzU
%:#
B


	ren1_uops
0	is_fencei,:*
B
:


iodec_uops
0	is_fencei�rename-stage.scala 114:27szS
$:"
B


	ren1_uops
0is_fence+:)
B
:


iodec_uops
0is_fence�rename-stage.scala 114:27wzW
&:$
B


	ren1_uops
0
mem_signed-:+
B
:


iodec_uops
0
mem_signed�rename-stage.scala 114:27szS
$:"
B


	ren1_uops
0mem_size+:)
B
:


iodec_uops
0mem_size�rename-stage.scala 114:27qzQ
#:!
B


	ren1_uops
0mem_cmd*:(
B
:


iodec_uops
0mem_cmd�rename-stage.scala 114:27wzW
&:$
B


	ren1_uops
0
bypassable-:+
B
:


iodec_uops
0
bypassable�rename-stage.scala 114:27uzU
%:#
B


	ren1_uops
0	exc_cause,:*
B
:


iodec_uops
0	exc_cause�rename-stage.scala 114:27uzU
%:#
B


	ren1_uops
0	exception,:*
B
:


iodec_uops
0	exception�rename-stage.scala 114:27wzW
&:$
B


	ren1_uops
0
stale_pdst-:+
B
:


iodec_uops
0
stale_pdst�rename-stage.scala 114:27wzW
&:$
B


	ren1_uops
0
ppred_busy-:+
B
:


iodec_uops
0
ppred_busy�rename-stage.scala 114:27uzU
%:#
B


	ren1_uops
0	prs3_busy,:*
B
:


iodec_uops
0	prs3_busy�rename-stage.scala 114:27uzU
%:#
B


	ren1_uops
0	prs2_busy,:*
B
:


iodec_uops
0	prs2_busy�rename-stage.scala 114:27uzU
%:#
B


	ren1_uops
0	prs1_busy,:*
B
:


iodec_uops
0	prs1_busy�rename-stage.scala 114:27mzM
!:
B


	ren1_uops
0ppred(:&
B
:


iodec_uops
0ppred�rename-stage.scala 114:27kzK
 :
B


	ren1_uops
0prs3':%
B
:


iodec_uops
0prs3�rename-stage.scala 114:27kzK
 :
B


	ren1_uops
0prs2':%
B
:


iodec_uops
0prs2�rename-stage.scala 114:27kzK
 :
B


	ren1_uops
0prs1':%
B
:


iodec_uops
0prs1�rename-stage.scala 114:27kzK
 :
B


	ren1_uops
0pdst':%
B
:


iodec_uops
0pdst�rename-stage.scala 114:27qzQ
#:!
B


	ren1_uops
0rxq_idx*:(
B
:


iodec_uops
0rxq_idx�rename-stage.scala 114:27qzQ
#:!
B


	ren1_uops
0stq_idx*:(
B
:


iodec_uops
0stq_idx�rename-stage.scala 114:27qzQ
#:!
B


	ren1_uops
0ldq_idx*:(
B
:


iodec_uops
0ldq_idx�rename-stage.scala 114:27qzQ
#:!
B


	ren1_uops
0rob_idx*:(
B
:


iodec_uops
0rob_idx�rename-stage.scala 114:27szS
$:"
B


	ren1_uops
0csr_addr+:)
B
:


iodec_uops
0csr_addr�rename-stage.scala 114:27wzW
&:$
B


	ren1_uops
0
imm_packed-:+
B
:


iodec_uops
0
imm_packed�rename-stage.scala 114:27mzM
!:
B


	ren1_uops
0taken(:&
B
:


iodec_uops
0taken�rename-stage.scala 114:27ozO
": 
B


	ren1_uops
0pc_lob):'
B
:


iodec_uops
0pc_lob�rename-stage.scala 114:27uzU
%:#
B


	ren1_uops
0	edge_inst,:*
B
:


iodec_uops
0	edge_inst�rename-stage.scala 114:27qzQ
#:!
B


	ren1_uops
0ftq_idx*:(
B
:


iodec_uops
0ftq_idx�rename-stage.scala 114:27ozO
": 
B


	ren1_uops
0br_tag):'
B
:


iodec_uops
0br_tag�rename-stage.scala 114:27qzQ
#:!
B


	ren1_uops
0br_mask*:(
B
:


iodec_uops
0br_mask�rename-stage.scala 114:27ozO
": 
B


	ren1_uops
0is_sfb):'
B
:


iodec_uops
0is_sfb�rename-stage.scala 114:27ozO
": 
B


	ren1_uops
0is_jal):'
B
:


iodec_uops
0is_jal�rename-stage.scala 114:27qzQ
#:!
B


	ren1_uops
0is_jalr*:(
B
:


iodec_uops
0is_jalr�rename-stage.scala 114:27mzM
!:
B


	ren1_uops
0is_br(:&
B
:


iodec_uops
0is_br�rename-stage.scala 114:27z_
*:(
B


	ren1_uops
0iw_p2_poisoned1:/
B
:


iodec_uops
0iw_p2_poisoned�rename-stage.scala 114:27z_
*:(
B


	ren1_uops
0iw_p1_poisoned1:/
B
:


iodec_uops
0iw_p1_poisoned�rename-stage.scala 114:27szS
$:"
B


	ren1_uops
0iw_state+:)
B
:


iodec_uops
0iw_state�rename-stage.scala 114:27�zc
,:*
 :
B


	ren1_uops
0ctrlis_std3:1
':%
B
:


iodec_uops
0ctrlis_std�rename-stage.scala 114:27�zc
,:*
 :
B


	ren1_uops
0ctrlis_sta3:1
':%
B
:


iodec_uops
0ctrlis_sta�rename-stage.scala 114:27�ze
-:+
 :
B


	ren1_uops
0ctrlis_load4:2
':%
B
:


iodec_uops
0ctrlis_load�rename-stage.scala 114:27�ze
-:+
 :
B


	ren1_uops
0ctrlcsr_cmd4:2
':%
B
:


iodec_uops
0ctrlcsr_cmd�rename-stage.scala 114:27�zc
,:*
 :
B


	ren1_uops
0ctrlfcn_dw3:1
':%
B
:


iodec_uops
0ctrlfcn_dw�rename-stage.scala 114:27�zc
,:*
 :
B


	ren1_uops
0ctrlop_fcn3:1
':%
B
:


iodec_uops
0ctrlop_fcn�rename-stage.scala 114:27�ze
-:+
 :
B


	ren1_uops
0ctrlimm_sel4:2
':%
B
:


iodec_uops
0ctrlimm_sel�rename-stage.scala 114:27�ze
-:+
 :
B


	ren1_uops
0ctrlop2_sel4:2
':%
B
:


iodec_uops
0ctrlop2_sel�rename-stage.scala 114:27�ze
-:+
 :
B


	ren1_uops
0ctrlop1_sel4:2
':%
B
:


iodec_uops
0ctrlop1_sel�rename-stage.scala 114:27�ze
-:+
 :
B


	ren1_uops
0ctrlbr_type4:2
':%
B
:


iodec_uops
0ctrlbr_type�rename-stage.scala 114:27qzQ
#:!
B


	ren1_uops
0fu_code*:(
B
:


iodec_uops
0fu_code�rename-stage.scala 114:27qzQ
#:!
B


	ren1_uops
0iq_type*:(
B
:


iodec_uops
0iq_type�rename-stage.scala 114:27szS
$:"
B


	ren1_uops
0debug_pc+:)
B
:


iodec_uops
0debug_pc�rename-stage.scala 114:27ozO
": 
B


	ren1_uops
0is_rvc):'
B
:


iodec_uops
0is_rvc�rename-stage.scala 114:27wzW
&:$
B


	ren1_uops
0
debug_inst-:+
B
:


iodec_uops
0
debug_inst�rename-stage.scala 114:27kzK
 :
B


	ren1_uops
0inst':%
B
:


iodec_uops
0inst�rename-stage.scala 114:27kzK
 :
B


	ren1_uops
0uopc':%
B
:


iodec_uops
0uopc�rename-stage.scala 114:27O/
_T
	

clock"	

reset*	

0�rename-stage.scala 118:27��
_T_1�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc
	

clock"	

0*

_T_1�rename-stage.scala 119:23�
�
_T_2�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc
�rename-stage.scala 120:24Tz4
:


_T_2
debug_tsrc:


_T_1
debug_tsrc�rename-stage.scala 122:14Tz4
:


_T_2
debug_fsrc:


_T_1
debug_fsrc�rename-stage.scala 122:14Tz4
:


_T_2
bp_xcpt_if:


_T_1
bp_xcpt_if�rename-stage.scala 122:14Vz6
:


_T_2bp_debug_if:


_T_1bp_debug_if�rename-stage.scala 122:14Tz4
:


_T_2
xcpt_ma_if:


_T_1
xcpt_ma_if�rename-stage.scala 122:14Tz4
:


_T_2
xcpt_ae_if:


_T_1
xcpt_ae_if�rename-stage.scala 122:14Tz4
:


_T_2
xcpt_pf_if:


_T_1
xcpt_pf_if�rename-stage.scala 122:14Rz2
:


_T_2	fp_single:


_T_1	fp_single�rename-stage.scala 122:14Lz,
:


_T_2fp_val:


_T_1fp_val�rename-stage.scala 122:14Nz.
:


_T_2frs3_en:


_T_1frs3_en�rename-stage.scala 122:14Tz4
:


_T_2
lrs2_rtype:


_T_1
lrs2_rtype�rename-stage.scala 122:14Tz4
:


_T_2
lrs1_rtype:


_T_1
lrs1_rtype�rename-stage.scala 122:14Rz2
:


_T_2	dst_rtype:


_T_1	dst_rtype�rename-stage.scala 122:14Pz0
:


_T_2ldst_val:


_T_1ldst_val�rename-stage.scala 122:14Hz(
:


_T_2lrs3:


_T_1lrs3�rename-stage.scala 122:14Hz(
:


_T_2lrs2:


_T_1lrs2�rename-stage.scala 122:14Hz(
:


_T_2lrs1:


_T_1lrs1�rename-stage.scala 122:14Hz(
:


_T_2ldst:


_T_1ldst�rename-stage.scala 122:14Vz6
:


_T_2ldst_is_rs1:


_T_1ldst_is_rs1�rename-stage.scala 122:14^z>
:


_T_2flush_on_commit:


_T_1flush_on_commit�rename-stage.scala 122:14Rz2
:


_T_2	is_unique:


_T_1	is_unique�rename-stage.scala 122:14Zz:
:


_T_2is_sys_pc2epc:


_T_1is_sys_pc2epc�rename-stage.scala 122:14Pz0
:


_T_2uses_stq:


_T_1uses_stq�rename-stage.scala 122:14Pz0
:


_T_2uses_ldq:


_T_1uses_ldq�rename-stage.scala 122:14Lz,
:


_T_2is_amo:


_T_1is_amo�rename-stage.scala 122:14Rz2
:


_T_2	is_fencei:


_T_1	is_fencei�rename-stage.scala 122:14Pz0
:


_T_2is_fence:


_T_1is_fence�rename-stage.scala 122:14Tz4
:


_T_2
mem_signed:


_T_1
mem_signed�rename-stage.scala 122:14Pz0
:


_T_2mem_size:


_T_1mem_size�rename-stage.scala 122:14Nz.
:


_T_2mem_cmd:


_T_1mem_cmd�rename-stage.scala 122:14Tz4
:


_T_2
bypassable:


_T_1
bypassable�rename-stage.scala 122:14Rz2
:


_T_2	exc_cause:


_T_1	exc_cause�rename-stage.scala 122:14Rz2
:


_T_2	exception:


_T_1	exception�rename-stage.scala 122:14Tz4
:


_T_2
stale_pdst:


_T_1
stale_pdst�rename-stage.scala 122:14Tz4
:


_T_2
ppred_busy:


_T_1
ppred_busy�rename-stage.scala 122:14Rz2
:


_T_2	prs3_busy:


_T_1	prs3_busy�rename-stage.scala 122:14Rz2
:


_T_2	prs2_busy:


_T_1	prs2_busy�rename-stage.scala 122:14Rz2
:


_T_2	prs1_busy:


_T_1	prs1_busy�rename-stage.scala 122:14Jz*
:


_T_2ppred:


_T_1ppred�rename-stage.scala 122:14Hz(
:


_T_2prs3:


_T_1prs3�rename-stage.scala 122:14Hz(
:


_T_2prs2:


_T_1prs2�rename-stage.scala 122:14Hz(
:


_T_2prs1:


_T_1prs1�rename-stage.scala 122:14Hz(
:


_T_2pdst:


_T_1pdst�rename-stage.scala 122:14Nz.
:


_T_2rxq_idx:


_T_1rxq_idx�rename-stage.scala 122:14Nz.
:


_T_2stq_idx:


_T_1stq_idx�rename-stage.scala 122:14Nz.
:


_T_2ldq_idx:


_T_1ldq_idx�rename-stage.scala 122:14Nz.
:


_T_2rob_idx:


_T_1rob_idx�rename-stage.scala 122:14Pz0
:


_T_2csr_addr:


_T_1csr_addr�rename-stage.scala 122:14Tz4
:


_T_2
imm_packed:


_T_1
imm_packed�rename-stage.scala 122:14Jz*
:


_T_2taken:


_T_1taken�rename-stage.scala 122:14Lz,
:


_T_2pc_lob:


_T_1pc_lob�rename-stage.scala 122:14Rz2
:


_T_2	edge_inst:


_T_1	edge_inst�rename-stage.scala 122:14Nz.
:


_T_2ftq_idx:


_T_1ftq_idx�rename-stage.scala 122:14Lz,
:


_T_2br_tag:


_T_1br_tag�rename-stage.scala 122:14Nz.
:


_T_2br_mask:


_T_1br_mask�rename-stage.scala 122:14Lz,
:


_T_2is_sfb:


_T_1is_sfb�rename-stage.scala 122:14Lz,
:


_T_2is_jal:


_T_1is_jal�rename-stage.scala 122:14Nz.
:


_T_2is_jalr:


_T_1is_jalr�rename-stage.scala 122:14Jz*
:


_T_2is_br:


_T_1is_br�rename-stage.scala 122:14\z<
:


_T_2iw_p2_poisoned:


_T_1iw_p2_poisoned�rename-stage.scala 122:14\z<
:


_T_2iw_p1_poisoned:


_T_1iw_p1_poisoned�rename-stage.scala 122:14Pz0
:


_T_2iw_state:


_T_1iw_state�rename-stage.scala 122:14`z@
:
:


_T_2ctrlis_std:
:


_T_1ctrlis_std�rename-stage.scala 122:14`z@
:
:


_T_2ctrlis_sta:
:


_T_1ctrlis_sta�rename-stage.scala 122:14bzB
:
:


_T_2ctrlis_load:
:


_T_1ctrlis_load�rename-stage.scala 122:14bzB
:
:


_T_2ctrlcsr_cmd:
:


_T_1ctrlcsr_cmd�rename-stage.scala 122:14`z@
:
:


_T_2ctrlfcn_dw:
:


_T_1ctrlfcn_dw�rename-stage.scala 122:14`z@
:
:


_T_2ctrlop_fcn:
:


_T_1ctrlop_fcn�rename-stage.scala 122:14bzB
:
:


_T_2ctrlimm_sel:
:


_T_1ctrlimm_sel�rename-stage.scala 122:14bzB
:
:


_T_2ctrlop2_sel:
:


_T_1ctrlop2_sel�rename-stage.scala 122:14bzB
:
:


_T_2ctrlop1_sel:
:


_T_1ctrlop1_sel�rename-stage.scala 122:14bzB
:
:


_T_2ctrlbr_type:
:


_T_1ctrlbr_type�rename-stage.scala 122:14Nz.
:


_T_2fu_code:


_T_1fu_code�rename-stage.scala 122:14Nz.
:


_T_2iq_type:


_T_1iq_type�rename-stage.scala 122:14Pz0
:


_T_2debug_pc:


_T_1debug_pc�rename-stage.scala 122:14Lz,
:


_T_2is_rvc:


_T_1is_rvc�rename-stage.scala 122:14Tz4
:


_T_2
debug_inst:


_T_1
debug_inst�rename-stage.scala 122:14Hz(
:


_T_2inst:


_T_1inst�rename-stage.scala 122:14Hz(
:


_T_2uopc:


_T_1uopc�rename-stage.scala 122:14�s:�s
:


iokill5z


_T	

0�rename-stage.scala 125:15�r:�r
:


io	dis_ready@z 


_TB


	ren1_fire
0�rename-stage.scala 127:15bzB
:


_T_2
debug_tsrc&:$
B


	ren1_uops
0
debug_tsrc�rename-stage.scala 128:16bzB
:


_T_2
debug_fsrc&:$
B


	ren1_uops
0
debug_fsrc�rename-stage.scala 128:16bzB
:


_T_2
bp_xcpt_if&:$
B


	ren1_uops
0
bp_xcpt_if�rename-stage.scala 128:16dzD
:


_T_2bp_debug_if':%
B


	ren1_uops
0bp_debug_if�rename-stage.scala 128:16bzB
:


_T_2
xcpt_ma_if&:$
B


	ren1_uops
0
xcpt_ma_if�rename-stage.scala 128:16bzB
:


_T_2
xcpt_ae_if&:$
B


	ren1_uops
0
xcpt_ae_if�rename-stage.scala 128:16bzB
:


_T_2
xcpt_pf_if&:$
B


	ren1_uops
0
xcpt_pf_if�rename-stage.scala 128:16`z@
:


_T_2	fp_single%:#
B


	ren1_uops
0	fp_single�rename-stage.scala 128:16Zz:
:


_T_2fp_val": 
B


	ren1_uops
0fp_val�rename-stage.scala 128:16\z<
:


_T_2frs3_en#:!
B


	ren1_uops
0frs3_en�rename-stage.scala 128:16bzB
:


_T_2
lrs2_rtype&:$
B


	ren1_uops
0
lrs2_rtype�rename-stage.scala 128:16bzB
:


_T_2
lrs1_rtype&:$
B


	ren1_uops
0
lrs1_rtype�rename-stage.scala 128:16`z@
:


_T_2	dst_rtype%:#
B


	ren1_uops
0	dst_rtype�rename-stage.scala 128:16^z>
:


_T_2ldst_val$:"
B


	ren1_uops
0ldst_val�rename-stage.scala 128:16Vz6
:


_T_2lrs3 :
B


	ren1_uops
0lrs3�rename-stage.scala 128:16Vz6
:


_T_2lrs2 :
B


	ren1_uops
0lrs2�rename-stage.scala 128:16Vz6
:


_T_2lrs1 :
B


	ren1_uops
0lrs1�rename-stage.scala 128:16Vz6
:


_T_2ldst :
B


	ren1_uops
0ldst�rename-stage.scala 128:16dzD
:


_T_2ldst_is_rs1':%
B


	ren1_uops
0ldst_is_rs1�rename-stage.scala 128:16lzL
:


_T_2flush_on_commit+:)
B


	ren1_uops
0flush_on_commit�rename-stage.scala 128:16`z@
:


_T_2	is_unique%:#
B


	ren1_uops
0	is_unique�rename-stage.scala 128:16hzH
:


_T_2is_sys_pc2epc):'
B


	ren1_uops
0is_sys_pc2epc�rename-stage.scala 128:16^z>
:


_T_2uses_stq$:"
B


	ren1_uops
0uses_stq�rename-stage.scala 128:16^z>
:


_T_2uses_ldq$:"
B


	ren1_uops
0uses_ldq�rename-stage.scala 128:16Zz:
:


_T_2is_amo": 
B


	ren1_uops
0is_amo�rename-stage.scala 128:16`z@
:


_T_2	is_fencei%:#
B


	ren1_uops
0	is_fencei�rename-stage.scala 128:16^z>
:


_T_2is_fence$:"
B


	ren1_uops
0is_fence�rename-stage.scala 128:16bzB
:


_T_2
mem_signed&:$
B


	ren1_uops
0
mem_signed�rename-stage.scala 128:16^z>
:


_T_2mem_size$:"
B


	ren1_uops
0mem_size�rename-stage.scala 128:16\z<
:


_T_2mem_cmd#:!
B


	ren1_uops
0mem_cmd�rename-stage.scala 128:16bzB
:


_T_2
bypassable&:$
B


	ren1_uops
0
bypassable�rename-stage.scala 128:16`z@
:


_T_2	exc_cause%:#
B


	ren1_uops
0	exc_cause�rename-stage.scala 128:16`z@
:


_T_2	exception%:#
B


	ren1_uops
0	exception�rename-stage.scala 128:16bzB
:


_T_2
stale_pdst&:$
B


	ren1_uops
0
stale_pdst�rename-stage.scala 128:16bzB
:


_T_2
ppred_busy&:$
B


	ren1_uops
0
ppred_busy�rename-stage.scala 128:16`z@
:


_T_2	prs3_busy%:#
B


	ren1_uops
0	prs3_busy�rename-stage.scala 128:16`z@
:


_T_2	prs2_busy%:#
B


	ren1_uops
0	prs2_busy�rename-stage.scala 128:16`z@
:


_T_2	prs1_busy%:#
B


	ren1_uops
0	prs1_busy�rename-stage.scala 128:16Xz8
:


_T_2ppred!:
B


	ren1_uops
0ppred�rename-stage.scala 128:16Vz6
:


_T_2prs3 :
B


	ren1_uops
0prs3�rename-stage.scala 128:16Vz6
:


_T_2prs2 :
B


	ren1_uops
0prs2�rename-stage.scala 128:16Vz6
:


_T_2prs1 :
B


	ren1_uops
0prs1�rename-stage.scala 128:16Vz6
:


_T_2pdst :
B


	ren1_uops
0pdst�rename-stage.scala 128:16\z<
:


_T_2rxq_idx#:!
B


	ren1_uops
0rxq_idx�rename-stage.scala 128:16\z<
:


_T_2stq_idx#:!
B


	ren1_uops
0stq_idx�rename-stage.scala 128:16\z<
:


_T_2ldq_idx#:!
B


	ren1_uops
0ldq_idx�rename-stage.scala 128:16\z<
:


_T_2rob_idx#:!
B


	ren1_uops
0rob_idx�rename-stage.scala 128:16^z>
:


_T_2csr_addr$:"
B


	ren1_uops
0csr_addr�rename-stage.scala 128:16bzB
:


_T_2
imm_packed&:$
B


	ren1_uops
0
imm_packed�rename-stage.scala 128:16Xz8
:


_T_2taken!:
B


	ren1_uops
0taken�rename-stage.scala 128:16Zz:
:


_T_2pc_lob": 
B


	ren1_uops
0pc_lob�rename-stage.scala 128:16`z@
:


_T_2	edge_inst%:#
B


	ren1_uops
0	edge_inst�rename-stage.scala 128:16\z<
:


_T_2ftq_idx#:!
B


	ren1_uops
0ftq_idx�rename-stage.scala 128:16Zz:
:


_T_2br_tag": 
B


	ren1_uops
0br_tag�rename-stage.scala 128:16\z<
:


_T_2br_mask#:!
B


	ren1_uops
0br_mask�rename-stage.scala 128:16Zz:
:


_T_2is_sfb": 
B


	ren1_uops
0is_sfb�rename-stage.scala 128:16Zz:
:


_T_2is_jal": 
B


	ren1_uops
0is_jal�rename-stage.scala 128:16\z<
:


_T_2is_jalr#:!
B


	ren1_uops
0is_jalr�rename-stage.scala 128:16Xz8
:


_T_2is_br!:
B


	ren1_uops
0is_br�rename-stage.scala 128:16jzJ
:


_T_2iw_p2_poisoned*:(
B


	ren1_uops
0iw_p2_poisoned�rename-stage.scala 128:16jzJ
:


_T_2iw_p1_poisoned*:(
B


	ren1_uops
0iw_p1_poisoned�rename-stage.scala 128:16^z>
:


_T_2iw_state$:"
B


	ren1_uops
0iw_state�rename-stage.scala 128:16nzN
:
:


_T_2ctrlis_std,:*
 :
B


	ren1_uops
0ctrlis_std�rename-stage.scala 128:16nzN
:
:


_T_2ctrlis_sta,:*
 :
B


	ren1_uops
0ctrlis_sta�rename-stage.scala 128:16pzP
:
:


_T_2ctrlis_load-:+
 :
B


	ren1_uops
0ctrlis_load�rename-stage.scala 128:16pzP
:
:


_T_2ctrlcsr_cmd-:+
 :
B


	ren1_uops
0ctrlcsr_cmd�rename-stage.scala 128:16nzN
:
:


_T_2ctrlfcn_dw,:*
 :
B


	ren1_uops
0ctrlfcn_dw�rename-stage.scala 128:16nzN
:
:


_T_2ctrlop_fcn,:*
 :
B


	ren1_uops
0ctrlop_fcn�rename-stage.scala 128:16pzP
:
:


_T_2ctrlimm_sel-:+
 :
B


	ren1_uops
0ctrlimm_sel�rename-stage.scala 128:16pzP
:
:


_T_2ctrlop2_sel-:+
 :
B


	ren1_uops
0ctrlop2_sel�rename-stage.scala 128:16pzP
:
:


_T_2ctrlop1_sel-:+
 :
B


	ren1_uops
0ctrlop1_sel�rename-stage.scala 128:16pzP
:
:


_T_2ctrlbr_type-:+
 :
B


	ren1_uops
0ctrlbr_type�rename-stage.scala 128:16\z<
:


_T_2fu_code#:!
B


	ren1_uops
0fu_code�rename-stage.scala 128:16\z<
:


_T_2iq_type#:!
B


	ren1_uops
0iq_type�rename-stage.scala 128:16^z>
:


_T_2debug_pc$:"
B


	ren1_uops
0debug_pc�rename-stage.scala 128:16Zz:
:


_T_2is_rvc": 
B


	ren1_uops
0is_rvc�rename-stage.scala 128:16bzB
:


_T_2
debug_inst&:$
B


	ren1_uops
0
debug_inst�rename-stage.scala 128:16Vz6
:


_T_2inst :
B


	ren1_uops
0inst�rename-stage.scala 128:16Vz6
:


_T_2uopc :
B


	ren1_uops
0uopc�rename-stage.scala 128:16X28
_T_30R.B
:


iodis_fire
0	

0�rename-stage.scala 130:29>2
_T_4R

_T

_T_3�rename-stage.scala 130:262z


_T

_T_4�rename-stage.scala 130:15Tz4
:


_T_2
debug_tsrc:


_T_1
debug_tsrc�rename-stage.scala 131:16Tz4
:


_T_2
debug_fsrc:


_T_1
debug_fsrc�rename-stage.scala 131:16Tz4
:


_T_2
bp_xcpt_if:


_T_1
bp_xcpt_if�rename-stage.scala 131:16Vz6
:


_T_2bp_debug_if:


_T_1bp_debug_if�rename-stage.scala 131:16Tz4
:


_T_2
xcpt_ma_if:


_T_1
xcpt_ma_if�rename-stage.scala 131:16Tz4
:


_T_2
xcpt_ae_if:


_T_1
xcpt_ae_if�rename-stage.scala 131:16Tz4
:


_T_2
xcpt_pf_if:


_T_1
xcpt_pf_if�rename-stage.scala 131:16Rz2
:


_T_2	fp_single:


_T_1	fp_single�rename-stage.scala 131:16Lz,
:


_T_2fp_val:


_T_1fp_val�rename-stage.scala 131:16Nz.
:


_T_2frs3_en:


_T_1frs3_en�rename-stage.scala 131:16Tz4
:


_T_2
lrs2_rtype:


_T_1
lrs2_rtype�rename-stage.scala 131:16Tz4
:


_T_2
lrs1_rtype:


_T_1
lrs1_rtype�rename-stage.scala 131:16Rz2
:


_T_2	dst_rtype:


_T_1	dst_rtype�rename-stage.scala 131:16Pz0
:


_T_2ldst_val:


_T_1ldst_val�rename-stage.scala 131:16Hz(
:


_T_2lrs3:


_T_1lrs3�rename-stage.scala 131:16Hz(
:


_T_2lrs2:


_T_1lrs2�rename-stage.scala 131:16Hz(
:


_T_2lrs1:


_T_1lrs1�rename-stage.scala 131:16Hz(
:


_T_2ldst:


_T_1ldst�rename-stage.scala 131:16Vz6
:


_T_2ldst_is_rs1:


_T_1ldst_is_rs1�rename-stage.scala 131:16^z>
:


_T_2flush_on_commit:


_T_1flush_on_commit�rename-stage.scala 131:16Rz2
:


_T_2	is_unique:


_T_1	is_unique�rename-stage.scala 131:16Zz:
:


_T_2is_sys_pc2epc:


_T_1is_sys_pc2epc�rename-stage.scala 131:16Pz0
:


_T_2uses_stq:


_T_1uses_stq�rename-stage.scala 131:16Pz0
:


_T_2uses_ldq:


_T_1uses_ldq�rename-stage.scala 131:16Lz,
:


_T_2is_amo:


_T_1is_amo�rename-stage.scala 131:16Rz2
:


_T_2	is_fencei:


_T_1	is_fencei�rename-stage.scala 131:16Pz0
:


_T_2is_fence:


_T_1is_fence�rename-stage.scala 131:16Tz4
:


_T_2
mem_signed:


_T_1
mem_signed�rename-stage.scala 131:16Pz0
:


_T_2mem_size:


_T_1mem_size�rename-stage.scala 131:16Nz.
:


_T_2mem_cmd:


_T_1mem_cmd�rename-stage.scala 131:16Tz4
:


_T_2
bypassable:


_T_1
bypassable�rename-stage.scala 131:16Rz2
:


_T_2	exc_cause:


_T_1	exc_cause�rename-stage.scala 131:16Rz2
:


_T_2	exception:


_T_1	exception�rename-stage.scala 131:16Tz4
:


_T_2
stale_pdst:


_T_1
stale_pdst�rename-stage.scala 131:16Tz4
:


_T_2
ppred_busy:


_T_1
ppred_busy�rename-stage.scala 131:16Rz2
:


_T_2	prs3_busy:


_T_1	prs3_busy�rename-stage.scala 131:16Rz2
:


_T_2	prs2_busy:


_T_1	prs2_busy�rename-stage.scala 131:16Rz2
:


_T_2	prs1_busy:


_T_1	prs1_busy�rename-stage.scala 131:16Jz*
:


_T_2ppred:


_T_1ppred�rename-stage.scala 131:16Hz(
:


_T_2prs3:


_T_1prs3�rename-stage.scala 131:16Hz(
:


_T_2prs2:


_T_1prs2�rename-stage.scala 131:16Hz(
:


_T_2prs1:


_T_1prs1�rename-stage.scala 131:16Hz(
:


_T_2pdst:


_T_1pdst�rename-stage.scala 131:16Nz.
:


_T_2rxq_idx:


_T_1rxq_idx�rename-stage.scala 131:16Nz.
:


_T_2stq_idx:


_T_1stq_idx�rename-stage.scala 131:16Nz.
:


_T_2ldq_idx:


_T_1ldq_idx�rename-stage.scala 131:16Nz.
:


_T_2rob_idx:


_T_1rob_idx�rename-stage.scala 131:16Pz0
:


_T_2csr_addr:


_T_1csr_addr�rename-stage.scala 131:16Tz4
:


_T_2
imm_packed:


_T_1
imm_packed�rename-stage.scala 131:16Jz*
:


_T_2taken:


_T_1taken�rename-stage.scala 131:16Lz,
:


_T_2pc_lob:


_T_1pc_lob�rename-stage.scala 131:16Rz2
:


_T_2	edge_inst:


_T_1	edge_inst�rename-stage.scala 131:16Nz.
:


_T_2ftq_idx:


_T_1ftq_idx�rename-stage.scala 131:16Lz,
:


_T_2br_tag:


_T_1br_tag�rename-stage.scala 131:16Nz.
:


_T_2br_mask:


_T_1br_mask�rename-stage.scala 131:16Lz,
:


_T_2is_sfb:


_T_1is_sfb�rename-stage.scala 131:16Lz,
:


_T_2is_jal:


_T_1is_jal�rename-stage.scala 131:16Nz.
:


_T_2is_jalr:


_T_1is_jalr�rename-stage.scala 131:16Jz*
:


_T_2is_br:


_T_1is_br�rename-stage.scala 131:16\z<
:


_T_2iw_p2_poisoned:


_T_1iw_p2_poisoned�rename-stage.scala 131:16\z<
:


_T_2iw_p1_poisoned:


_T_1iw_p1_poisoned�rename-stage.scala 131:16Pz0
:


_T_2iw_state:


_T_1iw_state�rename-stage.scala 131:16`z@
:
:


_T_2ctrlis_std:
:


_T_1ctrlis_std�rename-stage.scala 131:16`z@
:
:


_T_2ctrlis_sta:
:


_T_1ctrlis_sta�rename-stage.scala 131:16bzB
:
:


_T_2ctrlis_load:
:


_T_1ctrlis_load�rename-stage.scala 131:16bzB
:
:


_T_2ctrlcsr_cmd:
:


_T_1ctrlcsr_cmd�rename-stage.scala 131:16`z@
:
:


_T_2ctrlfcn_dw:
:


_T_1ctrlfcn_dw�rename-stage.scala 131:16`z@
:
:


_T_2ctrlop_fcn:
:


_T_1ctrlop_fcn�rename-stage.scala 131:16bzB
:
:


_T_2ctrlimm_sel:
:


_T_1ctrlimm_sel�rename-stage.scala 131:16bzB
:
:


_T_2ctrlop2_sel:
:


_T_1ctrlop2_sel�rename-stage.scala 131:16bzB
:
:


_T_2ctrlop1_sel:
:


_T_1ctrlop1_sel�rename-stage.scala 131:16bzB
:
:


_T_2ctrlbr_type:
:


_T_1ctrlbr_type�rename-stage.scala 131:16Nz.
:


_T_2fu_code:


_T_1fu_code�rename-stage.scala 131:16Nz.
:


_T_2iq_type:


_T_1iq_type�rename-stage.scala 131:16Pz0
:


_T_2debug_pc:


_T_1debug_pc�rename-stage.scala 131:16Lz,
:


_T_2is_rvc:


_T_1is_rvc�rename-stage.scala 131:16Tz4
:


_T_2
debug_inst:


_T_1
debug_inst�rename-stage.scala 131:16Hz(
:


_T_2inst:


_T_1inst�rename-stage.scala 131:16Hz(
:


_T_2uopc:


_T_1uopc�rename-stage.scala 131:16�rename-stage.scala 126:30�rename-stage.scala 124:20�
�
_T_5�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc
�rename-stage.scala 171:28Tz4
:


_T_5
debug_tsrc:


_T_2
debug_tsrc�rename-stage.scala 172:18Tz4
:


_T_5
debug_fsrc:


_T_2
debug_fsrc�rename-stage.scala 172:18Tz4
:


_T_5
bp_xcpt_if:


_T_2
bp_xcpt_if�rename-stage.scala 172:18Vz6
:


_T_5bp_debug_if:


_T_2bp_debug_if�rename-stage.scala 172:18Tz4
:


_T_5
xcpt_ma_if:


_T_2
xcpt_ma_if�rename-stage.scala 172:18Tz4
:


_T_5
xcpt_ae_if:


_T_2
xcpt_ae_if�rename-stage.scala 172:18Tz4
:


_T_5
xcpt_pf_if:


_T_2
xcpt_pf_if�rename-stage.scala 172:18Rz2
:


_T_5	fp_single:


_T_2	fp_single�rename-stage.scala 172:18Lz,
:


_T_5fp_val:


_T_2fp_val�rename-stage.scala 172:18Nz.
:


_T_5frs3_en:


_T_2frs3_en�rename-stage.scala 172:18Tz4
:


_T_5
lrs2_rtype:


_T_2
lrs2_rtype�rename-stage.scala 172:18Tz4
:


_T_5
lrs1_rtype:


_T_2
lrs1_rtype�rename-stage.scala 172:18Rz2
:


_T_5	dst_rtype:


_T_2	dst_rtype�rename-stage.scala 172:18Pz0
:


_T_5ldst_val:


_T_2ldst_val�rename-stage.scala 172:18Hz(
:


_T_5lrs3:


_T_2lrs3�rename-stage.scala 172:18Hz(
:


_T_5lrs2:


_T_2lrs2�rename-stage.scala 172:18Hz(
:


_T_5lrs1:


_T_2lrs1�rename-stage.scala 172:18Hz(
:


_T_5ldst:


_T_2ldst�rename-stage.scala 172:18Vz6
:


_T_5ldst_is_rs1:


_T_2ldst_is_rs1�rename-stage.scala 172:18^z>
:


_T_5flush_on_commit:


_T_2flush_on_commit�rename-stage.scala 172:18Rz2
:


_T_5	is_unique:


_T_2	is_unique�rename-stage.scala 172:18Zz:
:


_T_5is_sys_pc2epc:


_T_2is_sys_pc2epc�rename-stage.scala 172:18Pz0
:


_T_5uses_stq:


_T_2uses_stq�rename-stage.scala 172:18Pz0
:


_T_5uses_ldq:


_T_2uses_ldq�rename-stage.scala 172:18Lz,
:


_T_5is_amo:


_T_2is_amo�rename-stage.scala 172:18Rz2
:


_T_5	is_fencei:


_T_2	is_fencei�rename-stage.scala 172:18Pz0
:


_T_5is_fence:


_T_2is_fence�rename-stage.scala 172:18Tz4
:


_T_5
mem_signed:


_T_2
mem_signed�rename-stage.scala 172:18Pz0
:


_T_5mem_size:


_T_2mem_size�rename-stage.scala 172:18Nz.
:


_T_5mem_cmd:


_T_2mem_cmd�rename-stage.scala 172:18Tz4
:


_T_5
bypassable:


_T_2
bypassable�rename-stage.scala 172:18Rz2
:


_T_5	exc_cause:


_T_2	exc_cause�rename-stage.scala 172:18Rz2
:


_T_5	exception:


_T_2	exception�rename-stage.scala 172:18Tz4
:


_T_5
stale_pdst:


_T_2
stale_pdst�rename-stage.scala 172:18Tz4
:


_T_5
ppred_busy:


_T_2
ppred_busy�rename-stage.scala 172:18Rz2
:


_T_5	prs3_busy:


_T_2	prs3_busy�rename-stage.scala 172:18Rz2
:


_T_5	prs2_busy:


_T_2	prs2_busy�rename-stage.scala 172:18Rz2
:


_T_5	prs1_busy:


_T_2	prs1_busy�rename-stage.scala 172:18Jz*
:


_T_5ppred:


_T_2ppred�rename-stage.scala 172:18Hz(
:


_T_5prs3:


_T_2prs3�rename-stage.scala 172:18Hz(
:


_T_5prs2:


_T_2prs2�rename-stage.scala 172:18Hz(
:


_T_5prs1:


_T_2prs1�rename-stage.scala 172:18Hz(
:


_T_5pdst:


_T_2pdst�rename-stage.scala 172:18Nz.
:


_T_5rxq_idx:


_T_2rxq_idx�rename-stage.scala 172:18Nz.
:


_T_5stq_idx:


_T_2stq_idx�rename-stage.scala 172:18Nz.
:


_T_5ldq_idx:


_T_2ldq_idx�rename-stage.scala 172:18Nz.
:


_T_5rob_idx:


_T_2rob_idx�rename-stage.scala 172:18Pz0
:


_T_5csr_addr:


_T_2csr_addr�rename-stage.scala 172:18Tz4
:


_T_5
imm_packed:


_T_2
imm_packed�rename-stage.scala 172:18Jz*
:


_T_5taken:


_T_2taken�rename-stage.scala 172:18Lz,
:


_T_5pc_lob:


_T_2pc_lob�rename-stage.scala 172:18Rz2
:


_T_5	edge_inst:


_T_2	edge_inst�rename-stage.scala 172:18Nz.
:


_T_5ftq_idx:


_T_2ftq_idx�rename-stage.scala 172:18Lz,
:


_T_5br_tag:


_T_2br_tag�rename-stage.scala 172:18Nz.
:


_T_5br_mask:


_T_2br_mask�rename-stage.scala 172:18Lz,
:


_T_5is_sfb:


_T_2is_sfb�rename-stage.scala 172:18Lz,
:


_T_5is_jal:


_T_2is_jal�rename-stage.scala 172:18Nz.
:


_T_5is_jalr:


_T_2is_jalr�rename-stage.scala 172:18Jz*
:


_T_5is_br:


_T_2is_br�rename-stage.scala 172:18\z<
:


_T_5iw_p2_poisoned:


_T_2iw_p2_poisoned�rename-stage.scala 172:18\z<
:


_T_5iw_p1_poisoned:


_T_2iw_p1_poisoned�rename-stage.scala 172:18Pz0
:


_T_5iw_state:


_T_2iw_state�rename-stage.scala 172:18`z@
:
:


_T_5ctrlis_std:
:


_T_2ctrlis_std�rename-stage.scala 172:18`z@
:
:


_T_5ctrlis_sta:
:


_T_2ctrlis_sta�rename-stage.scala 172:18bzB
:
:


_T_5ctrlis_load:
:


_T_2ctrlis_load�rename-stage.scala 172:18bzB
:
:


_T_5ctrlcsr_cmd:
:


_T_2ctrlcsr_cmd�rename-stage.scala 172:18`z@
:
:


_T_5ctrlfcn_dw:
:


_T_2ctrlfcn_dw�rename-stage.scala 172:18`z@
:
:


_T_5ctrlop_fcn:
:


_T_2ctrlop_fcn�rename-stage.scala 172:18bzB
:
:


_T_5ctrlimm_sel:
:


_T_2ctrlimm_sel�rename-stage.scala 172:18bzB
:
:


_T_5ctrlop2_sel:
:


_T_2ctrlop2_sel�rename-stage.scala 172:18bzB
:
:


_T_5ctrlop1_sel:
:


_T_2ctrlop1_sel�rename-stage.scala 172:18bzB
:
:


_T_5ctrlbr_type:
:


_T_2ctrlbr_type�rename-stage.scala 172:18Nz.
:


_T_5fu_code:


_T_2fu_code�rename-stage.scala 172:18Nz.
:


_T_5iq_type:


_T_2iq_type�rename-stage.scala 172:18Pz0
:


_T_5debug_pc:


_T_2debug_pc�rename-stage.scala 172:18Lz,
:


_T_5is_rvc:


_T_2is_rvc�rename-stage.scala 172:18Tz4
:


_T_5
debug_inst:


_T_2
debug_inst�rename-stage.scala 172:18Hz(
:


_T_5inst:


_T_2inst�rename-stage.scala 172:18Hz(
:


_T_5uopc:


_T_2uopc�rename-stage.scala 172:18b2B
_T_6:R8 :
B


	ren2_uops
0ldst:


_T_2lrs1�rename-stage.scala 174:87T24
_T_7,R*B


ren2_alloc_reqs
0

_T_6�rename-stage.scala 174:77b2B
_T_8:R8 :
B


	ren2_uops
0ldst:


_T_2lrs2�rename-stage.scala 175:87T24
_T_9,R*B


ren2_alloc_reqs
0

_T_8�rename-stage.scala 175:77c2C
_T_10:R8 :
B


	ren2_uops
0ldst:


_T_2lrs3�rename-stage.scala 176:87V26
_T_11-R+B


ren2_alloc_reqs
0	

_T_10�rename-stage.scala 176:77c2C
_T_12:R8 :
B


	ren2_uops
0ldst:


_T_2ldst�rename-stage.scala 177:87V26
_T_13-R+B


ren2_alloc_reqs
0	

_T_12�rename-stage.scala 177:77E2/
_T_14&2$


_T_7	

1	

0�Mux.scala 47:69;2"
_T_15R	

_T_14
0
0�OneHot.scala 83:30E2/
_T_16&2$


_T_9	

1	

0�Mux.scala 47:69;2"
_T_17R	

_T_16
0
0�OneHot.scala 83:30F20
_T_18'2%
	

_T_11	

1	

0�Mux.scala 47:69;2"
_T_19R	

_T_18
0
0�OneHot.scala 83:30F20
_T_20'2%
	

_T_13	

1	

0�Mux.scala 47:69;2"
_T_21R	

_T_20
0
0�OneHot.scala 83:30�:b


_T_7Vz6
:


_T_5prs1 :
B


	ren2_uops
0pdst�rename-stage.scala 191:52�rename-stage.scala 191:26�:b


_T_9Vz6
:


_T_5prs2 :
B


	ren2_uops
0pdst�rename-stage.scala 192:52�rename-stage.scala 192:26�:c
	

_T_11Vz6
:


_T_5prs3 :
B


	ren2_uops
0pdst�rename-stage.scala 193:52�rename-stage.scala 193:26�:i
	

_T_13\z<
:


_T_5
stale_pdst :
B


	ren2_uops
0pdst�rename-stage.scala 194:52�rename-stage.scala 194:26P20
_T_22'R%:


_T_2	prs1_busy

_T_7�rename-stage.scala 196:45Dz$
:


_T_5	prs1_busy	

_T_22�rename-stage.scala 196:28P20
_T_23'R%:


_T_2	prs2_busy

_T_9�rename-stage.scala 197:45Dz$
:


_T_5	prs2_busy	

_T_23�rename-stage.scala 197:28Q21
_T_24(R&:


_T_2	prs3_busy	

_T_11�rename-stage.scala 198:45Dz$
:


_T_5	prs3_busy	

_T_24�rename-stage.scala 198:285�
:


_T_5prs3�rename-stage.scala 201:30Fz&
:


_T_5	prs3_busy	

0�rename-stage.scala 202:30�
�
_T_25�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc
�
 <z5
:
	

_T_25
debug_tsrc:


_T_5
debug_tsrc�
 <z5
:
	

_T_25
debug_fsrc:


_T_5
debug_fsrc�
 <z5
:
	

_T_25
bp_xcpt_if:


_T_5
bp_xcpt_if�
 >z7
:
	

_T_25bp_debug_if:


_T_5bp_debug_if�
 <z5
:
	

_T_25
xcpt_ma_if:


_T_5
xcpt_ma_if�
 <z5
:
	

_T_25
xcpt_ae_if:


_T_5
xcpt_ae_if�
 <z5
:
	

_T_25
xcpt_pf_if:


_T_5
xcpt_pf_if�
 :z3
:
	

_T_25	fp_single:


_T_5	fp_single�
 4z-
:
	

_T_25fp_val:


_T_5fp_val�
 6z/
:
	

_T_25frs3_en:


_T_5frs3_en�
 <z5
:
	

_T_25
lrs2_rtype:


_T_5
lrs2_rtype�
 <z5
:
	

_T_25
lrs1_rtype:


_T_5
lrs1_rtype�
 :z3
:
	

_T_25	dst_rtype:


_T_5	dst_rtype�
 8z1
:
	

_T_25ldst_val:


_T_5ldst_val�
 0z)
:
	

_T_25lrs3:


_T_5lrs3�
 0z)
:
	

_T_25lrs2:


_T_5lrs2�
 0z)
:
	

_T_25lrs1:


_T_5lrs1�
 0z)
:
	

_T_25ldst:


_T_5ldst�
 >z7
:
	

_T_25ldst_is_rs1:


_T_5ldst_is_rs1�
 Fz?
:
	

_T_25flush_on_commit:


_T_5flush_on_commit�
 :z3
:
	

_T_25	is_unique:


_T_5	is_unique�
 Bz;
:
	

_T_25is_sys_pc2epc:


_T_5is_sys_pc2epc�
 8z1
:
	

_T_25uses_stq:


_T_5uses_stq�
 8z1
:
	

_T_25uses_ldq:


_T_5uses_ldq�
 4z-
:
	

_T_25is_amo:


_T_5is_amo�
 :z3
:
	

_T_25	is_fencei:


_T_5	is_fencei�
 8z1
:
	

_T_25is_fence:


_T_5is_fence�
 <z5
:
	

_T_25
mem_signed:


_T_5
mem_signed�
 8z1
:
	

_T_25mem_size:


_T_5mem_size�
 6z/
:
	

_T_25mem_cmd:


_T_5mem_cmd�
 <z5
:
	

_T_25
bypassable:


_T_5
bypassable�
 :z3
:
	

_T_25	exc_cause:


_T_5	exc_cause�
 :z3
:
	

_T_25	exception:


_T_5	exception�
 <z5
:
	

_T_25
stale_pdst:


_T_5
stale_pdst�
 <z5
:
	

_T_25
ppred_busy:


_T_5
ppred_busy�
 :z3
:
	

_T_25	prs3_busy:


_T_5	prs3_busy�
 :z3
:
	

_T_25	prs2_busy:


_T_5	prs2_busy�
 :z3
:
	

_T_25	prs1_busy:


_T_5	prs1_busy�
 2z+
:
	

_T_25ppred:


_T_5ppred�
 0z)
:
	

_T_25prs3:


_T_5prs3�
 0z)
:
	

_T_25prs2:


_T_5prs2�
 0z)
:
	

_T_25prs1:


_T_5prs1�
 0z)
:
	

_T_25pdst:


_T_5pdst�
 6z/
:
	

_T_25rxq_idx:


_T_5rxq_idx�
 6z/
:
	

_T_25stq_idx:


_T_5stq_idx�
 6z/
:
	

_T_25ldq_idx:


_T_5ldq_idx�
 6z/
:
	

_T_25rob_idx:


_T_5rob_idx�
 8z1
:
	

_T_25csr_addr:


_T_5csr_addr�
 <z5
:
	

_T_25
imm_packed:


_T_5
imm_packed�
 2z+
:
	

_T_25taken:


_T_5taken�
 4z-
:
	

_T_25pc_lob:


_T_5pc_lob�
 :z3
:
	

_T_25	edge_inst:


_T_5	edge_inst�
 6z/
:
	

_T_25ftq_idx:


_T_5ftq_idx�
 4z-
:
	

_T_25br_tag:


_T_5br_tag�
 6z/
:
	

_T_25br_mask:


_T_5br_mask�
 4z-
:
	

_T_25is_sfb:


_T_5is_sfb�
 4z-
:
	

_T_25is_jal:


_T_5is_jal�
 6z/
:
	

_T_25is_jalr:


_T_5is_jalr�
 2z+
:
	

_T_25is_br:


_T_5is_br�
 Dz=
:
	

_T_25iw_p2_poisoned:


_T_5iw_p2_poisoned�
 Dz=
:
	

_T_25iw_p1_poisoned:


_T_5iw_p1_poisoned�
 8z1
:
	

_T_25iw_state:


_T_5iw_state�
 HzA
:
:
	

_T_25ctrlis_std:
:


_T_5ctrlis_std�
 HzA
:
:
	

_T_25ctrlis_sta:
:


_T_5ctrlis_sta�
 JzC
 :
:
	

_T_25ctrlis_load:
:


_T_5ctrlis_load�
 JzC
 :
:
	

_T_25ctrlcsr_cmd:
:


_T_5ctrlcsr_cmd�
 HzA
:
:
	

_T_25ctrlfcn_dw:
:


_T_5ctrlfcn_dw�
 HzA
:
:
	

_T_25ctrlop_fcn:
:


_T_5ctrlop_fcn�
 JzC
 :
:
	

_T_25ctrlimm_sel:
:


_T_5ctrlimm_sel�
 JzC
 :
:
	

_T_25ctrlop2_sel:
:


_T_5ctrlop2_sel�
 JzC
 :
:
	

_T_25ctrlop1_sel:
:


_T_5ctrlop1_sel�
 JzC
 :
:
	

_T_25ctrlbr_type:
:


_T_5ctrlbr_type�
 6z/
:
	

_T_25fu_code:


_T_5fu_code�
 6z/
:
	

_T_25iq_type:


_T_5iq_type�
 8z1
:
	

_T_25debug_pc:


_T_5debug_pc�
 4z-
:
	

_T_25is_rvc:


_T_5is_rvc�
 <z5
:
	

_T_25
debug_inst:


_T_5
debug_inst�
 0z)
:
	

_T_25inst:


_T_5inst�
 0z)
:
	

_T_25uopc:


_T_5uopc�
 T2=
_T_264R2.:,
:
:


iobrupdateb1resolve_mask�util.scala 74:37F2/
_T_27&R$:


_T_5br_mask	

_T_26�util.scala 74:35:z#
:
	

_T_25br_mask	

_T_27�util.scala 74:20Uz5
:


_T_1
debug_tsrc:
	

_T_25
debug_tsrc�rename-stage.scala 134:11Uz5
:


_T_1
debug_fsrc:
	

_T_25
debug_fsrc�rename-stage.scala 134:11Uz5
:


_T_1
bp_xcpt_if:
	

_T_25
bp_xcpt_if�rename-stage.scala 134:11Wz7
:


_T_1bp_debug_if:
	

_T_25bp_debug_if�rename-stage.scala 134:11Uz5
:


_T_1
xcpt_ma_if:
	

_T_25
xcpt_ma_if�rename-stage.scala 134:11Uz5
:


_T_1
xcpt_ae_if:
	

_T_25
xcpt_ae_if�rename-stage.scala 134:11Uz5
:


_T_1
xcpt_pf_if:
	

_T_25
xcpt_pf_if�rename-stage.scala 134:11Sz3
:


_T_1	fp_single:
	

_T_25	fp_single�rename-stage.scala 134:11Mz-
:


_T_1fp_val:
	

_T_25fp_val�rename-stage.scala 134:11Oz/
:


_T_1frs3_en:
	

_T_25frs3_en�rename-stage.scala 134:11Uz5
:


_T_1
lrs2_rtype:
	

_T_25
lrs2_rtype�rename-stage.scala 134:11Uz5
:


_T_1
lrs1_rtype:
	

_T_25
lrs1_rtype�rename-stage.scala 134:11Sz3
:


_T_1	dst_rtype:
	

_T_25	dst_rtype�rename-stage.scala 134:11Qz1
:


_T_1ldst_val:
	

_T_25ldst_val�rename-stage.scala 134:11Iz)
:


_T_1lrs3:
	

_T_25lrs3�rename-stage.scala 134:11Iz)
:


_T_1lrs2:
	

_T_25lrs2�rename-stage.scala 134:11Iz)
:


_T_1lrs1:
	

_T_25lrs1�rename-stage.scala 134:11Iz)
:


_T_1ldst:
	

_T_25ldst�rename-stage.scala 134:11Wz7
:


_T_1ldst_is_rs1:
	

_T_25ldst_is_rs1�rename-stage.scala 134:11_z?
:


_T_1flush_on_commit:
	

_T_25flush_on_commit�rename-stage.scala 134:11Sz3
:


_T_1	is_unique:
	

_T_25	is_unique�rename-stage.scala 134:11[z;
:


_T_1is_sys_pc2epc:
	

_T_25is_sys_pc2epc�rename-stage.scala 134:11Qz1
:


_T_1uses_stq:
	

_T_25uses_stq�rename-stage.scala 134:11Qz1
:


_T_1uses_ldq:
	

_T_25uses_ldq�rename-stage.scala 134:11Mz-
:


_T_1is_amo:
	

_T_25is_amo�rename-stage.scala 134:11Sz3
:


_T_1	is_fencei:
	

_T_25	is_fencei�rename-stage.scala 134:11Qz1
:


_T_1is_fence:
	

_T_25is_fence�rename-stage.scala 134:11Uz5
:


_T_1
mem_signed:
	

_T_25
mem_signed�rename-stage.scala 134:11Qz1
:


_T_1mem_size:
	

_T_25mem_size�rename-stage.scala 134:11Oz/
:


_T_1mem_cmd:
	

_T_25mem_cmd�rename-stage.scala 134:11Uz5
:


_T_1
bypassable:
	

_T_25
bypassable�rename-stage.scala 134:11Sz3
:


_T_1	exc_cause:
	

_T_25	exc_cause�rename-stage.scala 134:11Sz3
:


_T_1	exception:
	

_T_25	exception�rename-stage.scala 134:11Uz5
:


_T_1
stale_pdst:
	

_T_25
stale_pdst�rename-stage.scala 134:11Uz5
:


_T_1
ppred_busy:
	

_T_25
ppred_busy�rename-stage.scala 134:11Sz3
:


_T_1	prs3_busy:
	

_T_25	prs3_busy�rename-stage.scala 134:11Sz3
:


_T_1	prs2_busy:
	

_T_25	prs2_busy�rename-stage.scala 134:11Sz3
:


_T_1	prs1_busy:
	

_T_25	prs1_busy�rename-stage.scala 134:11Kz+
:


_T_1ppred:
	

_T_25ppred�rename-stage.scala 134:11Iz)
:


_T_1prs3:
	

_T_25prs3�rename-stage.scala 134:11Iz)
:


_T_1prs2:
	

_T_25prs2�rename-stage.scala 134:11Iz)
:


_T_1prs1:
	

_T_25prs1�rename-stage.scala 134:11Iz)
:


_T_1pdst:
	

_T_25pdst�rename-stage.scala 134:11Oz/
:


_T_1rxq_idx:
	

_T_25rxq_idx�rename-stage.scala 134:11Oz/
:


_T_1stq_idx:
	

_T_25stq_idx�rename-stage.scala 134:11Oz/
:


_T_1ldq_idx:
	

_T_25ldq_idx�rename-stage.scala 134:11Oz/
:


_T_1rob_idx:
	

_T_25rob_idx�rename-stage.scala 134:11Qz1
:


_T_1csr_addr:
	

_T_25csr_addr�rename-stage.scala 134:11Uz5
:


_T_1
imm_packed:
	

_T_25
imm_packed�rename-stage.scala 134:11Kz+
:


_T_1taken:
	

_T_25taken�rename-stage.scala 134:11Mz-
:


_T_1pc_lob:
	

_T_25pc_lob�rename-stage.scala 134:11Sz3
:


_T_1	edge_inst:
	

_T_25	edge_inst�rename-stage.scala 134:11Oz/
:


_T_1ftq_idx:
	

_T_25ftq_idx�rename-stage.scala 134:11Mz-
:


_T_1br_tag:
	

_T_25br_tag�rename-stage.scala 134:11Oz/
:


_T_1br_mask:
	

_T_25br_mask�rename-stage.scala 134:11Mz-
:


_T_1is_sfb:
	

_T_25is_sfb�rename-stage.scala 134:11Mz-
:


_T_1is_jal:
	

_T_25is_jal�rename-stage.scala 134:11Oz/
:


_T_1is_jalr:
	

_T_25is_jalr�rename-stage.scala 134:11Kz+
:


_T_1is_br:
	

_T_25is_br�rename-stage.scala 134:11]z=
:


_T_1iw_p2_poisoned:
	

_T_25iw_p2_poisoned�rename-stage.scala 134:11]z=
:


_T_1iw_p1_poisoned:
	

_T_25iw_p1_poisoned�rename-stage.scala 134:11Qz1
:


_T_1iw_state:
	

_T_25iw_state�rename-stage.scala 134:11azA
:
:


_T_1ctrlis_std:
:
	

_T_25ctrlis_std�rename-stage.scala 134:11azA
:
:


_T_1ctrlis_sta:
:
	

_T_25ctrlis_sta�rename-stage.scala 134:11czC
:
:


_T_1ctrlis_load :
:
	

_T_25ctrlis_load�rename-stage.scala 134:11czC
:
:


_T_1ctrlcsr_cmd :
:
	

_T_25ctrlcsr_cmd�rename-stage.scala 134:11azA
:
:


_T_1ctrlfcn_dw:
:
	

_T_25ctrlfcn_dw�rename-stage.scala 134:11azA
:
:


_T_1ctrlop_fcn:
:
	

_T_25ctrlop_fcn�rename-stage.scala 134:11czC
:
:


_T_1ctrlimm_sel :
:
	

_T_25ctrlimm_sel�rename-stage.scala 134:11czC
:
:


_T_1ctrlop2_sel :
:
	

_T_25ctrlop2_sel�rename-stage.scala 134:11czC
:
:


_T_1ctrlop1_sel :
:
	

_T_25ctrlop1_sel�rename-stage.scala 134:11czC
:
:


_T_1ctrlbr_type :
:
	

_T_25ctrlbr_type�rename-stage.scala 134:11Oz/
:


_T_1fu_code:
	

_T_25fu_code�rename-stage.scala 134:11Oz/
:


_T_1iq_type:
	

_T_25iq_type�rename-stage.scala 134:11Qz1
:


_T_1debug_pc:
	

_T_25debug_pc�rename-stage.scala 134:11Mz-
:


_T_1is_rvc:
	

_T_25is_rvc�rename-stage.scala 134:11Uz5
:


_T_1
debug_inst:
	

_T_25
debug_inst�rename-stage.scala 134:11Iz)
:


_T_1inst:
	

_T_25inst�rename-stage.scala 134:11Iz)
:


_T_1uopc:
	

_T_25uopc�rename-stage.scala 134:11Bz"
B


ren2_valids
0

_T�rename-stage.scala 136:20bzB
&:$
B


	ren2_uops
0
debug_tsrc:


_T_1
debug_tsrc�rename-stage.scala 137:20bzB
&:$
B


	ren2_uops
0
debug_fsrc:


_T_1
debug_fsrc�rename-stage.scala 137:20bzB
&:$
B


	ren2_uops
0
bp_xcpt_if:


_T_1
bp_xcpt_if�rename-stage.scala 137:20dzD
':%
B


	ren2_uops
0bp_debug_if:


_T_1bp_debug_if�rename-stage.scala 137:20bzB
&:$
B


	ren2_uops
0
xcpt_ma_if:


_T_1
xcpt_ma_if�rename-stage.scala 137:20bzB
&:$
B


	ren2_uops
0
xcpt_ae_if:


_T_1
xcpt_ae_if�rename-stage.scala 137:20bzB
&:$
B


	ren2_uops
0
xcpt_pf_if:


_T_1
xcpt_pf_if�rename-stage.scala 137:20`z@
%:#
B


	ren2_uops
0	fp_single:


_T_1	fp_single�rename-stage.scala 137:20Zz:
": 
B


	ren2_uops
0fp_val:


_T_1fp_val�rename-stage.scala 137:20\z<
#:!
B


	ren2_uops
0frs3_en:


_T_1frs3_en�rename-stage.scala 137:20bzB
&:$
B


	ren2_uops
0
lrs2_rtype:


_T_1
lrs2_rtype�rename-stage.scala 137:20bzB
&:$
B


	ren2_uops
0
lrs1_rtype:


_T_1
lrs1_rtype�rename-stage.scala 137:20`z@
%:#
B


	ren2_uops
0	dst_rtype:


_T_1	dst_rtype�rename-stage.scala 137:20^z>
$:"
B


	ren2_uops
0ldst_val:


_T_1ldst_val�rename-stage.scala 137:20Vz6
 :
B


	ren2_uops
0lrs3:


_T_1lrs3�rename-stage.scala 137:20Vz6
 :
B


	ren2_uops
0lrs2:


_T_1lrs2�rename-stage.scala 137:20Vz6
 :
B


	ren2_uops
0lrs1:


_T_1lrs1�rename-stage.scala 137:20Vz6
 :
B


	ren2_uops
0ldst:


_T_1ldst�rename-stage.scala 137:20dzD
':%
B


	ren2_uops
0ldst_is_rs1:


_T_1ldst_is_rs1�rename-stage.scala 137:20lzL
+:)
B


	ren2_uops
0flush_on_commit:


_T_1flush_on_commit�rename-stage.scala 137:20`z@
%:#
B


	ren2_uops
0	is_unique:


_T_1	is_unique�rename-stage.scala 137:20hzH
):'
B


	ren2_uops
0is_sys_pc2epc:


_T_1is_sys_pc2epc�rename-stage.scala 137:20^z>
$:"
B


	ren2_uops
0uses_stq:


_T_1uses_stq�rename-stage.scala 137:20^z>
$:"
B


	ren2_uops
0uses_ldq:


_T_1uses_ldq�rename-stage.scala 137:20Zz:
": 
B


	ren2_uops
0is_amo:


_T_1is_amo�rename-stage.scala 137:20`z@
%:#
B


	ren2_uops
0	is_fencei:


_T_1	is_fencei�rename-stage.scala 137:20^z>
$:"
B


	ren2_uops
0is_fence:


_T_1is_fence�rename-stage.scala 137:20bzB
&:$
B


	ren2_uops
0
mem_signed:


_T_1
mem_signed�rename-stage.scala 137:20^z>
$:"
B


	ren2_uops
0mem_size:


_T_1mem_size�rename-stage.scala 137:20\z<
#:!
B


	ren2_uops
0mem_cmd:


_T_1mem_cmd�rename-stage.scala 137:20bzB
&:$
B


	ren2_uops
0
bypassable:


_T_1
bypassable�rename-stage.scala 137:20`z@
%:#
B


	ren2_uops
0	exc_cause:


_T_1	exc_cause�rename-stage.scala 137:20`z@
%:#
B


	ren2_uops
0	exception:


_T_1	exception�rename-stage.scala 137:20bzB
&:$
B


	ren2_uops
0
stale_pdst:


_T_1
stale_pdst�rename-stage.scala 137:20bzB
&:$
B


	ren2_uops
0
ppred_busy:


_T_1
ppred_busy�rename-stage.scala 137:20`z@
%:#
B


	ren2_uops
0	prs3_busy:


_T_1	prs3_busy�rename-stage.scala 137:20`z@
%:#
B


	ren2_uops
0	prs2_busy:


_T_1	prs2_busy�rename-stage.scala 137:20`z@
%:#
B


	ren2_uops
0	prs1_busy:


_T_1	prs1_busy�rename-stage.scala 137:20Xz8
!:
B


	ren2_uops
0ppred:


_T_1ppred�rename-stage.scala 137:20Vz6
 :
B


	ren2_uops
0prs3:


_T_1prs3�rename-stage.scala 137:20Vz6
 :
B


	ren2_uops
0prs2:


_T_1prs2�rename-stage.scala 137:20Vz6
 :
B


	ren2_uops
0prs1:


_T_1prs1�rename-stage.scala 137:20Vz6
 :
B


	ren2_uops
0pdst:


_T_1pdst�rename-stage.scala 137:20\z<
#:!
B


	ren2_uops
0rxq_idx:


_T_1rxq_idx�rename-stage.scala 137:20\z<
#:!
B


	ren2_uops
0stq_idx:


_T_1stq_idx�rename-stage.scala 137:20\z<
#:!
B


	ren2_uops
0ldq_idx:


_T_1ldq_idx�rename-stage.scala 137:20\z<
#:!
B


	ren2_uops
0rob_idx:


_T_1rob_idx�rename-stage.scala 137:20^z>
$:"
B


	ren2_uops
0csr_addr:


_T_1csr_addr�rename-stage.scala 137:20bzB
&:$
B


	ren2_uops
0
imm_packed:


_T_1
imm_packed�rename-stage.scala 137:20Xz8
!:
B


	ren2_uops
0taken:


_T_1taken�rename-stage.scala 137:20Zz:
": 
B


	ren2_uops
0pc_lob:


_T_1pc_lob�rename-stage.scala 137:20`z@
%:#
B


	ren2_uops
0	edge_inst:


_T_1	edge_inst�rename-stage.scala 137:20\z<
#:!
B


	ren2_uops
0ftq_idx:


_T_1ftq_idx�rename-stage.scala 137:20Zz:
": 
B


	ren2_uops
0br_tag:


_T_1br_tag�rename-stage.scala 137:20\z<
#:!
B


	ren2_uops
0br_mask:


_T_1br_mask�rename-stage.scala 137:20Zz:
": 
B


	ren2_uops
0is_sfb:


_T_1is_sfb�rename-stage.scala 137:20Zz:
": 
B


	ren2_uops
0is_jal:


_T_1is_jal�rename-stage.scala 137:20\z<
#:!
B


	ren2_uops
0is_jalr:


_T_1is_jalr�rename-stage.scala 137:20Xz8
!:
B


	ren2_uops
0is_br:


_T_1is_br�rename-stage.scala 137:20jzJ
*:(
B


	ren2_uops
0iw_p2_poisoned:


_T_1iw_p2_poisoned�rename-stage.scala 137:20jzJ
*:(
B


	ren2_uops
0iw_p1_poisoned:


_T_1iw_p1_poisoned�rename-stage.scala 137:20^z>
$:"
B


	ren2_uops
0iw_state:


_T_1iw_state�rename-stage.scala 137:20nzN
,:*
 :
B


	ren2_uops
0ctrlis_std:
:


_T_1ctrlis_std�rename-stage.scala 137:20nzN
,:*
 :
B


	ren2_uops
0ctrlis_sta:
:


_T_1ctrlis_sta�rename-stage.scala 137:20pzP
-:+
 :
B


	ren2_uops
0ctrlis_load:
:


_T_1ctrlis_load�rename-stage.scala 137:20pzP
-:+
 :
B


	ren2_uops
0ctrlcsr_cmd:
:


_T_1ctrlcsr_cmd�rename-stage.scala 137:20nzN
,:*
 :
B


	ren2_uops
0ctrlfcn_dw:
:


_T_1ctrlfcn_dw�rename-stage.scala 137:20nzN
,:*
 :
B


	ren2_uops
0ctrlop_fcn:
:


_T_1ctrlop_fcn�rename-stage.scala 137:20pzP
-:+
 :
B


	ren2_uops
0ctrlimm_sel:
:


_T_1ctrlimm_sel�rename-stage.scala 137:20pzP
-:+
 :
B


	ren2_uops
0ctrlop2_sel:
:


_T_1ctrlop2_sel�rename-stage.scala 137:20pzP
-:+
 :
B


	ren2_uops
0ctrlop1_sel:
:


_T_1ctrlop1_sel�rename-stage.scala 137:20pzP
-:+
 :
B


	ren2_uops
0ctrlbr_type:
:


_T_1ctrlbr_type�rename-stage.scala 137:20\z<
#:!
B


	ren2_uops
0fu_code:


_T_1fu_code�rename-stage.scala 137:20\z<
#:!
B


	ren2_uops
0iq_type:


_T_1iq_type�rename-stage.scala 137:20^z>
$:"
B


	ren2_uops
0debug_pc:


_T_1debug_pc�rename-stage.scala 137:20Zz:
": 
B


	ren2_uops
0is_rvc:


_T_1is_rvc�rename-stage.scala 137:20bzB
&:$
B


	ren2_uops
0
debug_inst:


_T_1
debug_inst�rename-stage.scala 137:20Vz6
 :
B


	ren2_uops
0inst:


_T_1inst�rename-stage.scala 137:20Vz6
 :
B


	ren2_uops
0uopc:


_T_1uopc�rename-stage.scala 137:20Zz:
B
:


io	ren2_mask
0B


ren2_valids
0�rename-stage.scala 143:16:*
maptableRenameMapTable�rename-stage.scala 211:24+z$
:



maptableclock	

clock�
 +z$
:



maptablereset	

reset�
 :*
freelistRenameFreeList�rename-stage.scala 217:24+z$
:



freelistclock	

clock�
 +z$
:



freelistreset	

reset�
 <*
	busytableRenameBusyTable�rename-stage.scala 221:25,z%
:


	busytableclock	

clock�
 ,z%
:


	busytablereset	

reset�
 Y
9
ren2_br_tags)2'
#*!
valid

bits
�rename-stage.scala 230:29:


com_valids2


�rename-stage.scala 233:29:


rbk_valids2


�rename-stage.scala 234:29a2A
_T_288R6%:#
B


	ren2_uops
0	dst_rtype	

0�rename-stage.scala 237:78^2>
_T_295R3$:"
B


	ren2_uops
0ldst_val	

_T_28�rename-stage.scala 237:52W27
_T_30.R,	

_T_29B
:


iodis_fire
0�rename-stage.scala 237:88Iz)
B


ren2_alloc_reqs
0	

_T_30�rename-stage.scala 237:27Z2>
_T_315R3": 
B


	ren2_uops
0is_sfb	

0�micro-op.scala 146:36W2;
_T_322R0!:
B


	ren2_uops
0is_br	

_T_31�micro-op.scala 146:33Y2=
_T_334R2	

_T_32#:!
B


	ren2_uops
0is_jalr�micro-op.scala 146:45W27
_T_34.R,B
:


iodis_fire
0	

_T_33�rename-stage.scala 238:43Qz1
$:"
B


ren2_br_tags
0valid	

_T_34�rename-stage.scala 238:27h2H
_T_35?R=,:*
B
:


iocom_uops
0	dst_rtype	

0�rename-stage.scala 240:82e2E
_T_36<R:+:)
B
:


iocom_uops
0ldst_val	

_T_35�rename-stage.scala 240:54Y29
_T_370R.	

_T_36B
:


io
com_valids
0�rename-stage.scala 240:92Dz$
B



com_valids
0	

_T_37�rename-stage.scala 240:27h2H
_T_38?R=,:*
B
:


iocom_uops
0	dst_rtype	

0�rename-stage.scala 241:82e2E
_T_39<R:+:)
B
:


iocom_uops
0ldst_val	

_T_38�rename-stage.scala 241:54Y29
_T_400R.	

_T_39B
:


io
rbk_valids
0�rename-stage.scala 241:92Dz$
B



rbk_valids
0	

_T_40�rename-stage.scala 241:27izI
#:!
B


ren2_br_tags
0bits": 
B


	ren2_uops
0br_tag�rename-stage.scala 242:27t
T
map_reqsH2F
B*@
lrs1

lrs2

lrs3

ldst
�rename-stage.scala 249:24g
G

remap_reqs927
3*1
ldst

pdst

valid
�rename-stage.scala 250:24czC
:
B



map_reqs
0lrs1 :
B


	ren1_uops
0lrs1�rename-stage.scala 254:22czC
:
B



map_reqs
0lrs2 :
B


	ren1_uops
0lrs2�rename-stage.scala 255:22czC
:
B



map_reqs
0lrs3 :
B


	ren1_uops
0lrs3�rename-stage.scala 256:22czC
:
B



map_reqs
0ldst :
B


	ren1_uops
0ldst�rename-stage.scala 257:22�2l
_T_41c2a
:


iorollback':%
B
:


iocom_uops
0ldst :
B


	ren2_uops
0ldst�rename-stage.scala 259:30Nz.
!:
B



remap_reqs
0ldst	

_T_41�rename-stage.scala 259:24�2r
_T_42i2g
:


iorollback-:+
B
:


iocom_uops
0
stale_pdst :
B


	ren2_uops
0pdst�rename-stage.scala 260:30Nz.
!:
B



remap_reqs
0pdst	

_T_42�rename-stage.scala 260:24d2D
_T_43;R9B


ren2_alloc_reqs
0B



rbk_valids
0�rename-stage.scala 263:38Oz/
": 
B



remap_reqs
0valid	

_T_43�rename-stage.scala 263:33xzX
5:3
+B)
": 
:



maptableiomap_reqs
0ldst:
B



map_reqs
0ldst�rename-stage.scala 266:27xzX
5:3
+B)
": 
:



maptableiomap_reqs
0lrs3:
B



map_reqs
0lrs3�rename-stage.scala 266:27xzX
5:3
+B)
": 
:



maptableiomap_reqs
0lrs2:
B



map_reqs
0lrs2�rename-stage.scala 266:27xzX
5:3
+B)
": 
:



maptableiomap_reqs
0lrs1:
B



map_reqs
0lrs1�rename-stage.scala 266:27~z^
8:6
-B+
$:"
:



maptableio
remap_reqs
0valid": 
B



remap_reqs
0valid�rename-stage.scala 267:27|z\
7:5
-B+
$:"
:



maptableio
remap_reqs
0pdst!:
B



remap_reqs
0pdst�rename-stage.scala 267:27|z\
7:5
-B+
$:"
:



maptableio
remap_reqs
0ldst!:
B



remap_reqs
0ldst�rename-stage.scala 267:27z_
8:6
.B,
%:#
:



maptableioren_br_tags
0bits#:!
B


ren2_br_tags
0bits�rename-stage.scala 268:27�za
9:7
.B,
%:#
:



maptableioren_br_tags
0valid$:"
B


ren2_br_tags
0valid�rename-stage.scala 268:27�zp
=:;
*:(
": 
:



maptableiobrupdateb2target_offset/:-
:
:


iobrupdateb2target_offset�rename-stage.scala 269:29�zl
;:9
*:(
": 
:



maptableiobrupdateb2jalr_target-:+
:
:


iobrupdateb2jalr_target�rename-stage.scala 269:29�zb
6:4
*:(
": 
:



maptableiobrupdateb2pc_sel(:&
:
:


iobrupdateb2pc_sel�rename-stage.scala 269:29�zf
8:6
*:(
": 
:



maptableiobrupdateb2cfi_type*:(
:
:


iobrupdateb2cfi_type�rename-stage.scala 269:29�z`
5:3
*:(
": 
:



maptableiobrupdateb2taken':%
:
:


iobrupdateb2taken�rename-stage.scala 269:29�zj
::8
*:(
": 
:



maptableiobrupdateb2
mispredict,:*
:
:


iobrupdateb2
mispredict�rename-stage.scala 269:29�z`
5:3
*:(
": 
:



maptableiobrupdateb2valid':%
:
:


iobrupdateb2valid�rename-stage.scala 269:29�z|
C:A
3:1
*:(
": 
:



maptableiobrupdateb2uop
debug_tsrc5:3
%:#
:
:


iobrupdateb2uop
debug_tsrc�rename-stage.scala 269:29�z|
C:A
3:1
*:(
": 
:



maptableiobrupdateb2uop
debug_fsrc5:3
%:#
:
:


iobrupdateb2uop
debug_fsrc�rename-stage.scala 269:29�z|
C:A
3:1
*:(
": 
:



maptableiobrupdateb2uop
bp_xcpt_if5:3
%:#
:
:


iobrupdateb2uop
bp_xcpt_if�rename-stage.scala 269:29�z~
D:B
3:1
*:(
": 
:



maptableiobrupdateb2uopbp_debug_if6:4
%:#
:
:


iobrupdateb2uopbp_debug_if�rename-stage.scala 269:29�z|
C:A
3:1
*:(
": 
:



maptableiobrupdateb2uop
xcpt_ma_if5:3
%:#
:
:


iobrupdateb2uop
xcpt_ma_if�rename-stage.scala 269:29�z|
C:A
3:1
*:(
": 
:



maptableiobrupdateb2uop
xcpt_ae_if5:3
%:#
:
:


iobrupdateb2uop
xcpt_ae_if�rename-stage.scala 269:29�z|
C:A
3:1
*:(
": 
:



maptableiobrupdateb2uop
xcpt_pf_if5:3
%:#
:
:


iobrupdateb2uop
xcpt_pf_if�rename-stage.scala 269:29�zz
B:@
3:1
*:(
": 
:



maptableiobrupdateb2uop	fp_single4:2
%:#
:
:


iobrupdateb2uop	fp_single�rename-stage.scala 269:29�zt
?:=
3:1
*:(
": 
:



maptableiobrupdateb2uopfp_val1:/
%:#
:
:


iobrupdateb2uopfp_val�rename-stage.scala 269:29�zv
@:>
3:1
*:(
": 
:



maptableiobrupdateb2uopfrs3_en2:0
%:#
:
:


iobrupdateb2uopfrs3_en�rename-stage.scala 269:29�z|
C:A
3:1
*:(
": 
:



maptableiobrupdateb2uop
lrs2_rtype5:3
%:#
:
:


iobrupdateb2uop
lrs2_rtype�rename-stage.scala 269:29�z|
C:A
3:1
*:(
": 
:



maptableiobrupdateb2uop
lrs1_rtype5:3
%:#
:
:


iobrupdateb2uop
lrs1_rtype�rename-stage.scala 269:29�zz
B:@
3:1
*:(
": 
:



maptableiobrupdateb2uop	dst_rtype4:2
%:#
:
:


iobrupdateb2uop	dst_rtype�rename-stage.scala 269:29�zx
A:?
3:1
*:(
": 
:



maptableiobrupdateb2uopldst_val3:1
%:#
:
:


iobrupdateb2uopldst_val�rename-stage.scala 269:29�zp
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uoplrs3/:-
%:#
:
:


iobrupdateb2uoplrs3�rename-stage.scala 269:29�zp
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uoplrs2/:-
%:#
:
:


iobrupdateb2uoplrs2�rename-stage.scala 269:29�zp
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uoplrs1/:-
%:#
:
:


iobrupdateb2uoplrs1�rename-stage.scala 269:29�zp
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopldst/:-
%:#
:
:


iobrupdateb2uopldst�rename-stage.scala 269:29�z~
D:B
3:1
*:(
": 
:



maptableiobrupdateb2uopldst_is_rs16:4
%:#
:
:


iobrupdateb2uopldst_is_rs1�rename-stage.scala 269:29�z�
H:F
3:1
*:(
": 
:



maptableiobrupdateb2uopflush_on_commit::8
%:#
:
:


iobrupdateb2uopflush_on_commit�rename-stage.scala 269:29�zz
B:@
3:1
*:(
": 
:



maptableiobrupdateb2uop	is_unique4:2
%:#
:
:


iobrupdateb2uop	is_unique�rename-stage.scala 269:29�z�
F:D
3:1
*:(
": 
:



maptableiobrupdateb2uopis_sys_pc2epc8:6
%:#
:
:


iobrupdateb2uopis_sys_pc2epc�rename-stage.scala 269:29�zx
A:?
3:1
*:(
": 
:



maptableiobrupdateb2uopuses_stq3:1
%:#
:
:


iobrupdateb2uopuses_stq�rename-stage.scala 269:29�zx
A:?
3:1
*:(
": 
:



maptableiobrupdateb2uopuses_ldq3:1
%:#
:
:


iobrupdateb2uopuses_ldq�rename-stage.scala 269:29�zt
?:=
3:1
*:(
": 
:



maptableiobrupdateb2uopis_amo1:/
%:#
:
:


iobrupdateb2uopis_amo�rename-stage.scala 269:29�zz
B:@
3:1
*:(
": 
:



maptableiobrupdateb2uop	is_fencei4:2
%:#
:
:


iobrupdateb2uop	is_fencei�rename-stage.scala 269:29�zx
A:?
3:1
*:(
": 
:



maptableiobrupdateb2uopis_fence3:1
%:#
:
:


iobrupdateb2uopis_fence�rename-stage.scala 269:29�z|
C:A
3:1
*:(
": 
:



maptableiobrupdateb2uop
mem_signed5:3
%:#
:
:


iobrupdateb2uop
mem_signed�rename-stage.scala 269:29�zx
A:?
3:1
*:(
": 
:



maptableiobrupdateb2uopmem_size3:1
%:#
:
:


iobrupdateb2uopmem_size�rename-stage.scala 269:29�zv
@:>
3:1
*:(
": 
:



maptableiobrupdateb2uopmem_cmd2:0
%:#
:
:


iobrupdateb2uopmem_cmd�rename-stage.scala 269:29�z|
C:A
3:1
*:(
": 
:



maptableiobrupdateb2uop
bypassable5:3
%:#
:
:


iobrupdateb2uop
bypassable�rename-stage.scala 269:29�zz
B:@
3:1
*:(
": 
:



maptableiobrupdateb2uop	exc_cause4:2
%:#
:
:


iobrupdateb2uop	exc_cause�rename-stage.scala 269:29�zz
B:@
3:1
*:(
": 
:



maptableiobrupdateb2uop	exception4:2
%:#
:
:


iobrupdateb2uop	exception�rename-stage.scala 269:29�z|
C:A
3:1
*:(
": 
:



maptableiobrupdateb2uop
stale_pdst5:3
%:#
:
:


iobrupdateb2uop
stale_pdst�rename-stage.scala 269:29�z|
C:A
3:1
*:(
": 
:



maptableiobrupdateb2uop
ppred_busy5:3
%:#
:
:


iobrupdateb2uop
ppred_busy�rename-stage.scala 269:29�zz
B:@
3:1
*:(
": 
:



maptableiobrupdateb2uop	prs3_busy4:2
%:#
:
:


iobrupdateb2uop	prs3_busy�rename-stage.scala 269:29�zz
B:@
3:1
*:(
": 
:



maptableiobrupdateb2uop	prs2_busy4:2
%:#
:
:


iobrupdateb2uop	prs2_busy�rename-stage.scala 269:29�zz
B:@
3:1
*:(
": 
:



maptableiobrupdateb2uop	prs1_busy4:2
%:#
:
:


iobrupdateb2uop	prs1_busy�rename-stage.scala 269:29�zr
>:<
3:1
*:(
": 
:



maptableiobrupdateb2uopppred0:.
%:#
:
:


iobrupdateb2uopppred�rename-stage.scala 269:29�zp
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopprs3/:-
%:#
:
:


iobrupdateb2uopprs3�rename-stage.scala 269:29�zp
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopprs2/:-
%:#
:
:


iobrupdateb2uopprs2�rename-stage.scala 269:29�zp
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopprs1/:-
%:#
:
:


iobrupdateb2uopprs1�rename-stage.scala 269:29�zp
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uoppdst/:-
%:#
:
:


iobrupdateb2uoppdst�rename-stage.scala 269:29�zv
@:>
3:1
*:(
": 
:



maptableiobrupdateb2uoprxq_idx2:0
%:#
:
:


iobrupdateb2uoprxq_idx�rename-stage.scala 269:29�zv
@:>
3:1
*:(
": 
:



maptableiobrupdateb2uopstq_idx2:0
%:#
:
:


iobrupdateb2uopstq_idx�rename-stage.scala 269:29�zv
@:>
3:1
*:(
": 
:



maptableiobrupdateb2uopldq_idx2:0
%:#
:
:


iobrupdateb2uopldq_idx�rename-stage.scala 269:29�zv
@:>
3:1
*:(
": 
:



maptableiobrupdateb2uoprob_idx2:0
%:#
:
:


iobrupdateb2uoprob_idx�rename-stage.scala 269:29�zx
A:?
3:1
*:(
": 
:



maptableiobrupdateb2uopcsr_addr3:1
%:#
:
:


iobrupdateb2uopcsr_addr�rename-stage.scala 269:29�z|
C:A
3:1
*:(
": 
:



maptableiobrupdateb2uop
imm_packed5:3
%:#
:
:


iobrupdateb2uop
imm_packed�rename-stage.scala 269:29�zr
>:<
3:1
*:(
": 
:



maptableiobrupdateb2uoptaken0:.
%:#
:
:


iobrupdateb2uoptaken�rename-stage.scala 269:29�zt
?:=
3:1
*:(
": 
:



maptableiobrupdateb2uoppc_lob1:/
%:#
:
:


iobrupdateb2uoppc_lob�rename-stage.scala 269:29�zz
B:@
3:1
*:(
": 
:



maptableiobrupdateb2uop	edge_inst4:2
%:#
:
:


iobrupdateb2uop	edge_inst�rename-stage.scala 269:29�zv
@:>
3:1
*:(
": 
:



maptableiobrupdateb2uopftq_idx2:0
%:#
:
:


iobrupdateb2uopftq_idx�rename-stage.scala 269:29�zt
?:=
3:1
*:(
": 
:



maptableiobrupdateb2uopbr_tag1:/
%:#
:
:


iobrupdateb2uopbr_tag�rename-stage.scala 269:29�zv
@:>
3:1
*:(
": 
:



maptableiobrupdateb2uopbr_mask2:0
%:#
:
:


iobrupdateb2uopbr_mask�rename-stage.scala 269:29�zt
?:=
3:1
*:(
": 
:



maptableiobrupdateb2uopis_sfb1:/
%:#
:
:


iobrupdateb2uopis_sfb�rename-stage.scala 269:29�zt
?:=
3:1
*:(
": 
:



maptableiobrupdateb2uopis_jal1:/
%:#
:
:


iobrupdateb2uopis_jal�rename-stage.scala 269:29�zv
@:>
3:1
*:(
": 
:



maptableiobrupdateb2uopis_jalr2:0
%:#
:
:


iobrupdateb2uopis_jalr�rename-stage.scala 269:29�zr
>:<
3:1
*:(
": 
:



maptableiobrupdateb2uopis_br0:.
%:#
:
:


iobrupdateb2uopis_br�rename-stage.scala 269:29�z�
G:E
3:1
*:(
": 
:



maptableiobrupdateb2uopiw_p2_poisoned9:7
%:#
:
:


iobrupdateb2uopiw_p2_poisoned�rename-stage.scala 269:29�z�
G:E
3:1
*:(
": 
:



maptableiobrupdateb2uopiw_p1_poisoned9:7
%:#
:
:


iobrupdateb2uopiw_p1_poisoned�rename-stage.scala 269:29�zx
A:?
3:1
*:(
": 
:



maptableiobrupdateb2uopiw_state3:1
%:#
:
:


iobrupdateb2uopiw_state�rename-stage.scala 269:29�z�
I:G
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopctrlis_std;:9
/:-
%:#
:
:


iobrupdateb2uopctrlis_std�rename-stage.scala 269:29�z�
I:G
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopctrlis_sta;:9
/:-
%:#
:
:


iobrupdateb2uopctrlis_sta�rename-stage.scala 269:29�z�
J:H
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopctrlis_load<::
/:-
%:#
:
:


iobrupdateb2uopctrlis_load�rename-stage.scala 269:29�z�
J:H
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopctrlcsr_cmd<::
/:-
%:#
:
:


iobrupdateb2uopctrlcsr_cmd�rename-stage.scala 269:29�z�
I:G
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopctrlfcn_dw;:9
/:-
%:#
:
:


iobrupdateb2uopctrlfcn_dw�rename-stage.scala 269:29�z�
I:G
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopctrlop_fcn;:9
/:-
%:#
:
:


iobrupdateb2uopctrlop_fcn�rename-stage.scala 269:29�z�
J:H
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopctrlimm_sel<::
/:-
%:#
:
:


iobrupdateb2uopctrlimm_sel�rename-stage.scala 269:29�z�
J:H
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopctrlop2_sel<::
/:-
%:#
:
:


iobrupdateb2uopctrlop2_sel�rename-stage.scala 269:29�z�
J:H
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopctrlop1_sel<::
/:-
%:#
:
:


iobrupdateb2uopctrlop1_sel�rename-stage.scala 269:29�z�
J:H
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopctrlbr_type<::
/:-
%:#
:
:


iobrupdateb2uopctrlbr_type�rename-stage.scala 269:29�zv
@:>
3:1
*:(
": 
:



maptableiobrupdateb2uopfu_code2:0
%:#
:
:


iobrupdateb2uopfu_code�rename-stage.scala 269:29�zv
@:>
3:1
*:(
": 
:



maptableiobrupdateb2uopiq_type2:0
%:#
:
:


iobrupdateb2uopiq_type�rename-stage.scala 269:29�zx
A:?
3:1
*:(
": 
:



maptableiobrupdateb2uopdebug_pc3:1
%:#
:
:


iobrupdateb2uopdebug_pc�rename-stage.scala 269:29�zt
?:=
3:1
*:(
": 
:



maptableiobrupdateb2uopis_rvc1:/
%:#
:
:


iobrupdateb2uopis_rvc�rename-stage.scala 269:29�z|
C:A
3:1
*:(
": 
:



maptableiobrupdateb2uop
debug_inst5:3
%:#
:
:


iobrupdateb2uop
debug_inst�rename-stage.scala 269:29�zp
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopinst/:-
%:#
:
:


iobrupdateb2uopinst�rename-stage.scala 269:29�zp
=:;
3:1
*:(
": 
:



maptableiobrupdateb2uopuopc/:-
%:#
:
:


iobrupdateb2uopuopc�rename-stage.scala 269:29�zt
?:=
*:(
": 
:



maptableiobrupdateb1mispredict_mask1:/
:
:


iobrupdateb1mispredict_mask�rename-stage.scala 269:29�zn
<::
*:(
": 
:



maptableiobrupdateb1resolve_mask.:,
:
:


iobrupdateb1resolve_mask�rename-stage.scala 269:29Zz:
": 
:



maptableiorollback:


iorollback�rename-stage.scala 270:27zzZ
 :
B


	ren1_uops
0prs16:4
,B*
#:!
:



maptableio	map_resps
0prs1�rename-stage.scala 276:20zzZ
 :
B


	ren1_uops
0prs26:4
,B*
#:!
:



maptableio	map_resps
0prs2�rename-stage.scala 277:20zzZ
 :
B


	ren1_uops
0prs36:4
,B*
#:!
:



maptableio	map_resps
0prs3�rename-stage.scala 278:20�zf
&:$
B


	ren1_uops
0
stale_pdst<::
,B*
#:!
:



maptableio	map_resps
0
stale_pdst�rename-stage.scala 279:20gzG
'B%
:
:



freelistioreqs
0B


ren2_alloc_reqs
0�rename-stage.scala 288:20_2?
_T_446R4B



com_valids
0B



rbk_valids
0�rename-stage.scala 290:37hzH
;:9
0B.
':%
:



freelistiodealloc_pregs
0valid	

_T_44�rename-stage.scala 290:32�2y
_T_45p2n
:


iorollback':%
B
:


iocom_uops
0pdst-:+
B
:


iocom_uops
0
stale_pdst�rename-stage.scala 292:33gzG
::8
0B.
':%
:



freelistiodealloc_pregs
0bits	

_T_45�rename-stage.scala 292:27z_
8:6
.B,
%:#
:



freelistioren_br_tags
0bits#:!
B


ren2_br_tags
0bits�rename-stage.scala 293:27�za
9:7
.B,
%:#
:



freelistioren_br_tags
0valid$:"
B


ren2_br_tags
0valid�rename-stage.scala 293:27�zp
=:;
*:(
": 
:



freelistiobrupdateb2target_offset/:-
:
:


iobrupdateb2target_offset�rename-stage.scala 294:24�zl
;:9
*:(
": 
:



freelistiobrupdateb2jalr_target-:+
:
:


iobrupdateb2jalr_target�rename-stage.scala 294:24�zb
6:4
*:(
": 
:



freelistiobrupdateb2pc_sel(:&
:
:


iobrupdateb2pc_sel�rename-stage.scala 294:24�zf
8:6
*:(
": 
:



freelistiobrupdateb2cfi_type*:(
:
:


iobrupdateb2cfi_type�rename-stage.scala 294:24�z`
5:3
*:(
": 
:



freelistiobrupdateb2taken':%
:
:


iobrupdateb2taken�rename-stage.scala 294:24�zj
::8
*:(
": 
:



freelistiobrupdateb2
mispredict,:*
:
:


iobrupdateb2
mispredict�rename-stage.scala 294:24�z`
5:3
*:(
": 
:



freelistiobrupdateb2valid':%
:
:


iobrupdateb2valid�rename-stage.scala 294:24�z|
C:A
3:1
*:(
": 
:



freelistiobrupdateb2uop
debug_tsrc5:3
%:#
:
:


iobrupdateb2uop
debug_tsrc�rename-stage.scala 294:24�z|
C:A
3:1
*:(
": 
:



freelistiobrupdateb2uop
debug_fsrc5:3
%:#
:
:


iobrupdateb2uop
debug_fsrc�rename-stage.scala 294:24�z|
C:A
3:1
*:(
": 
:



freelistiobrupdateb2uop
bp_xcpt_if5:3
%:#
:
:


iobrupdateb2uop
bp_xcpt_if�rename-stage.scala 294:24�z~
D:B
3:1
*:(
": 
:



freelistiobrupdateb2uopbp_debug_if6:4
%:#
:
:


iobrupdateb2uopbp_debug_if�rename-stage.scala 294:24�z|
C:A
3:1
*:(
": 
:



freelistiobrupdateb2uop
xcpt_ma_if5:3
%:#
:
:


iobrupdateb2uop
xcpt_ma_if�rename-stage.scala 294:24�z|
C:A
3:1
*:(
": 
:



freelistiobrupdateb2uop
xcpt_ae_if5:3
%:#
:
:


iobrupdateb2uop
xcpt_ae_if�rename-stage.scala 294:24�z|
C:A
3:1
*:(
": 
:



freelistiobrupdateb2uop
xcpt_pf_if5:3
%:#
:
:


iobrupdateb2uop
xcpt_pf_if�rename-stage.scala 294:24�zz
B:@
3:1
*:(
": 
:



freelistiobrupdateb2uop	fp_single4:2
%:#
:
:


iobrupdateb2uop	fp_single�rename-stage.scala 294:24�zt
?:=
3:1
*:(
": 
:



freelistiobrupdateb2uopfp_val1:/
%:#
:
:


iobrupdateb2uopfp_val�rename-stage.scala 294:24�zv
@:>
3:1
*:(
": 
:



freelistiobrupdateb2uopfrs3_en2:0
%:#
:
:


iobrupdateb2uopfrs3_en�rename-stage.scala 294:24�z|
C:A
3:1
*:(
": 
:



freelistiobrupdateb2uop
lrs2_rtype5:3
%:#
:
:


iobrupdateb2uop
lrs2_rtype�rename-stage.scala 294:24�z|
C:A
3:1
*:(
": 
:



freelistiobrupdateb2uop
lrs1_rtype5:3
%:#
:
:


iobrupdateb2uop
lrs1_rtype�rename-stage.scala 294:24�zz
B:@
3:1
*:(
": 
:



freelistiobrupdateb2uop	dst_rtype4:2
%:#
:
:


iobrupdateb2uop	dst_rtype�rename-stage.scala 294:24�zx
A:?
3:1
*:(
": 
:



freelistiobrupdateb2uopldst_val3:1
%:#
:
:


iobrupdateb2uopldst_val�rename-stage.scala 294:24�zp
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uoplrs3/:-
%:#
:
:


iobrupdateb2uoplrs3�rename-stage.scala 294:24�zp
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uoplrs2/:-
%:#
:
:


iobrupdateb2uoplrs2�rename-stage.scala 294:24�zp
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uoplrs1/:-
%:#
:
:


iobrupdateb2uoplrs1�rename-stage.scala 294:24�zp
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopldst/:-
%:#
:
:


iobrupdateb2uopldst�rename-stage.scala 294:24�z~
D:B
3:1
*:(
": 
:



freelistiobrupdateb2uopldst_is_rs16:4
%:#
:
:


iobrupdateb2uopldst_is_rs1�rename-stage.scala 294:24�z�
H:F
3:1
*:(
": 
:



freelistiobrupdateb2uopflush_on_commit::8
%:#
:
:


iobrupdateb2uopflush_on_commit�rename-stage.scala 294:24�zz
B:@
3:1
*:(
": 
:



freelistiobrupdateb2uop	is_unique4:2
%:#
:
:


iobrupdateb2uop	is_unique�rename-stage.scala 294:24�z�
F:D
3:1
*:(
": 
:



freelistiobrupdateb2uopis_sys_pc2epc8:6
%:#
:
:


iobrupdateb2uopis_sys_pc2epc�rename-stage.scala 294:24�zx
A:?
3:1
*:(
": 
:



freelistiobrupdateb2uopuses_stq3:1
%:#
:
:


iobrupdateb2uopuses_stq�rename-stage.scala 294:24�zx
A:?
3:1
*:(
": 
:



freelistiobrupdateb2uopuses_ldq3:1
%:#
:
:


iobrupdateb2uopuses_ldq�rename-stage.scala 294:24�zt
?:=
3:1
*:(
": 
:



freelistiobrupdateb2uopis_amo1:/
%:#
:
:


iobrupdateb2uopis_amo�rename-stage.scala 294:24�zz
B:@
3:1
*:(
": 
:



freelistiobrupdateb2uop	is_fencei4:2
%:#
:
:


iobrupdateb2uop	is_fencei�rename-stage.scala 294:24�zx
A:?
3:1
*:(
": 
:



freelistiobrupdateb2uopis_fence3:1
%:#
:
:


iobrupdateb2uopis_fence�rename-stage.scala 294:24�z|
C:A
3:1
*:(
": 
:



freelistiobrupdateb2uop
mem_signed5:3
%:#
:
:


iobrupdateb2uop
mem_signed�rename-stage.scala 294:24�zx
A:?
3:1
*:(
": 
:



freelistiobrupdateb2uopmem_size3:1
%:#
:
:


iobrupdateb2uopmem_size�rename-stage.scala 294:24�zv
@:>
3:1
*:(
": 
:



freelistiobrupdateb2uopmem_cmd2:0
%:#
:
:


iobrupdateb2uopmem_cmd�rename-stage.scala 294:24�z|
C:A
3:1
*:(
": 
:



freelistiobrupdateb2uop
bypassable5:3
%:#
:
:


iobrupdateb2uop
bypassable�rename-stage.scala 294:24�zz
B:@
3:1
*:(
": 
:



freelistiobrupdateb2uop	exc_cause4:2
%:#
:
:


iobrupdateb2uop	exc_cause�rename-stage.scala 294:24�zz
B:@
3:1
*:(
": 
:



freelistiobrupdateb2uop	exception4:2
%:#
:
:


iobrupdateb2uop	exception�rename-stage.scala 294:24�z|
C:A
3:1
*:(
": 
:



freelistiobrupdateb2uop
stale_pdst5:3
%:#
:
:


iobrupdateb2uop
stale_pdst�rename-stage.scala 294:24�z|
C:A
3:1
*:(
": 
:



freelistiobrupdateb2uop
ppred_busy5:3
%:#
:
:


iobrupdateb2uop
ppred_busy�rename-stage.scala 294:24�zz
B:@
3:1
*:(
": 
:



freelistiobrupdateb2uop	prs3_busy4:2
%:#
:
:


iobrupdateb2uop	prs3_busy�rename-stage.scala 294:24�zz
B:@
3:1
*:(
": 
:



freelistiobrupdateb2uop	prs2_busy4:2
%:#
:
:


iobrupdateb2uop	prs2_busy�rename-stage.scala 294:24�zz
B:@
3:1
*:(
": 
:



freelistiobrupdateb2uop	prs1_busy4:2
%:#
:
:


iobrupdateb2uop	prs1_busy�rename-stage.scala 294:24�zr
>:<
3:1
*:(
": 
:



freelistiobrupdateb2uopppred0:.
%:#
:
:


iobrupdateb2uopppred�rename-stage.scala 294:24�zp
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopprs3/:-
%:#
:
:


iobrupdateb2uopprs3�rename-stage.scala 294:24�zp
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopprs2/:-
%:#
:
:


iobrupdateb2uopprs2�rename-stage.scala 294:24�zp
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopprs1/:-
%:#
:
:


iobrupdateb2uopprs1�rename-stage.scala 294:24�zp
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uoppdst/:-
%:#
:
:


iobrupdateb2uoppdst�rename-stage.scala 294:24�zv
@:>
3:1
*:(
": 
:



freelistiobrupdateb2uoprxq_idx2:0
%:#
:
:


iobrupdateb2uoprxq_idx�rename-stage.scala 294:24�zv
@:>
3:1
*:(
": 
:



freelistiobrupdateb2uopstq_idx2:0
%:#
:
:


iobrupdateb2uopstq_idx�rename-stage.scala 294:24�zv
@:>
3:1
*:(
": 
:



freelistiobrupdateb2uopldq_idx2:0
%:#
:
:


iobrupdateb2uopldq_idx�rename-stage.scala 294:24�zv
@:>
3:1
*:(
": 
:



freelistiobrupdateb2uoprob_idx2:0
%:#
:
:


iobrupdateb2uoprob_idx�rename-stage.scala 294:24�zx
A:?
3:1
*:(
": 
:



freelistiobrupdateb2uopcsr_addr3:1
%:#
:
:


iobrupdateb2uopcsr_addr�rename-stage.scala 294:24�z|
C:A
3:1
*:(
": 
:



freelistiobrupdateb2uop
imm_packed5:3
%:#
:
:


iobrupdateb2uop
imm_packed�rename-stage.scala 294:24�zr
>:<
3:1
*:(
": 
:



freelistiobrupdateb2uoptaken0:.
%:#
:
:


iobrupdateb2uoptaken�rename-stage.scala 294:24�zt
?:=
3:1
*:(
": 
:



freelistiobrupdateb2uoppc_lob1:/
%:#
:
:


iobrupdateb2uoppc_lob�rename-stage.scala 294:24�zz
B:@
3:1
*:(
": 
:



freelistiobrupdateb2uop	edge_inst4:2
%:#
:
:


iobrupdateb2uop	edge_inst�rename-stage.scala 294:24�zv
@:>
3:1
*:(
": 
:



freelistiobrupdateb2uopftq_idx2:0
%:#
:
:


iobrupdateb2uopftq_idx�rename-stage.scala 294:24�zt
?:=
3:1
*:(
": 
:



freelistiobrupdateb2uopbr_tag1:/
%:#
:
:


iobrupdateb2uopbr_tag�rename-stage.scala 294:24�zv
@:>
3:1
*:(
": 
:



freelistiobrupdateb2uopbr_mask2:0
%:#
:
:


iobrupdateb2uopbr_mask�rename-stage.scala 294:24�zt
?:=
3:1
*:(
": 
:



freelistiobrupdateb2uopis_sfb1:/
%:#
:
:


iobrupdateb2uopis_sfb�rename-stage.scala 294:24�zt
?:=
3:1
*:(
": 
:



freelistiobrupdateb2uopis_jal1:/
%:#
:
:


iobrupdateb2uopis_jal�rename-stage.scala 294:24�zv
@:>
3:1
*:(
": 
:



freelistiobrupdateb2uopis_jalr2:0
%:#
:
:


iobrupdateb2uopis_jalr�rename-stage.scala 294:24�zr
>:<
3:1
*:(
": 
:



freelistiobrupdateb2uopis_br0:.
%:#
:
:


iobrupdateb2uopis_br�rename-stage.scala 294:24�z�
G:E
3:1
*:(
": 
:



freelistiobrupdateb2uopiw_p2_poisoned9:7
%:#
:
:


iobrupdateb2uopiw_p2_poisoned�rename-stage.scala 294:24�z�
G:E
3:1
*:(
": 
:



freelistiobrupdateb2uopiw_p1_poisoned9:7
%:#
:
:


iobrupdateb2uopiw_p1_poisoned�rename-stage.scala 294:24�zx
A:?
3:1
*:(
": 
:



freelistiobrupdateb2uopiw_state3:1
%:#
:
:


iobrupdateb2uopiw_state�rename-stage.scala 294:24�z�
I:G
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopctrlis_std;:9
/:-
%:#
:
:


iobrupdateb2uopctrlis_std�rename-stage.scala 294:24�z�
I:G
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopctrlis_sta;:9
/:-
%:#
:
:


iobrupdateb2uopctrlis_sta�rename-stage.scala 294:24�z�
J:H
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopctrlis_load<::
/:-
%:#
:
:


iobrupdateb2uopctrlis_load�rename-stage.scala 294:24�z�
J:H
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopctrlcsr_cmd<::
/:-
%:#
:
:


iobrupdateb2uopctrlcsr_cmd�rename-stage.scala 294:24�z�
I:G
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopctrlfcn_dw;:9
/:-
%:#
:
:


iobrupdateb2uopctrlfcn_dw�rename-stage.scala 294:24�z�
I:G
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopctrlop_fcn;:9
/:-
%:#
:
:


iobrupdateb2uopctrlop_fcn�rename-stage.scala 294:24�z�
J:H
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopctrlimm_sel<::
/:-
%:#
:
:


iobrupdateb2uopctrlimm_sel�rename-stage.scala 294:24�z�
J:H
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopctrlop2_sel<::
/:-
%:#
:
:


iobrupdateb2uopctrlop2_sel�rename-stage.scala 294:24�z�
J:H
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopctrlop1_sel<::
/:-
%:#
:
:


iobrupdateb2uopctrlop1_sel�rename-stage.scala 294:24�z�
J:H
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopctrlbr_type<::
/:-
%:#
:
:


iobrupdateb2uopctrlbr_type�rename-stage.scala 294:24�zv
@:>
3:1
*:(
": 
:



freelistiobrupdateb2uopfu_code2:0
%:#
:
:


iobrupdateb2uopfu_code�rename-stage.scala 294:24�zv
@:>
3:1
*:(
": 
:



freelistiobrupdateb2uopiq_type2:0
%:#
:
:


iobrupdateb2uopiq_type�rename-stage.scala 294:24�zx
A:?
3:1
*:(
": 
:



freelistiobrupdateb2uopdebug_pc3:1
%:#
:
:


iobrupdateb2uopdebug_pc�rename-stage.scala 294:24�zt
?:=
3:1
*:(
": 
:



freelistiobrupdateb2uopis_rvc1:/
%:#
:
:


iobrupdateb2uopis_rvc�rename-stage.scala 294:24�z|
C:A
3:1
*:(
": 
:



freelistiobrupdateb2uop
debug_inst5:3
%:#
:
:


iobrupdateb2uop
debug_inst�rename-stage.scala 294:24�zp
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopinst/:-
%:#
:
:


iobrupdateb2uopinst�rename-stage.scala 294:24�zp
=:;
3:1
*:(
": 
:



freelistiobrupdateb2uopuopc/:-
%:#
:
:


iobrupdateb2uopuopc�rename-stage.scala 294:24�zt
?:=
*:(
": 
:



freelistiobrupdateb1mispredict_mask1:/
:
:


iobrupdateb1mispredict_mask�rename-stage.scala 294:24�zn
<::
*:(
": 
:



freelistiobrupdateb1resolve_mask.:,
:
:


iobrupdateb1resolve_mask�rename-stage.scala 294:24rzR
3:1
:
:



freelistiodebugpipeline_empty:


iodebug_rob_empty�rename-stage.scala 295:36X28
_T_46/R-B


ren2_alloc_reqs
0	

0�rename-stage.scala 297:74t2T
_T_47KRI8:6
.B,
%:#
:



freelistioalloc_pregs
0bits	

0�rename-stage.scala 297:87C2#
_T_48R	

_T_46	

_T_47�rename-stage.scala 297:77B2"
_T_49R	

reset
0
0�rename-stage.scala 297:10C2#
_T_50R	

_T_48	

_T_49�rename-stage.scala 297:10E2%
_T_51R	

_T_50	

0�rename-stage.scala 297:10�:�
	

_T_51�R�
�Assertion failed: [rename-stage] A uop is trying to allocate the zero physical register.
    at rename-stage.scala:297 assert (ren2_alloc_reqs zip freelist.io.alloc_pregs map {case (r,p) => !r || p.bits =/= 0.U} reduce (_&&_),
	

clock"	

1�rename-stage.scala 297:10:B	

clock	

1�rename-stage.scala 297:10�rename-stage.scala 297:10\2<
_T_523R1 :
B


	ren2_uops
0ldst	

0�rename-stage.scala 303:30E2%
_T_53R	

_T_52	

0�rename-stage.scala 303:38}2]
_T_54T2R
	

_T_538:6
.B,
%:#
:



freelistioalloc_pregs
0bits	

0�rename-stage.scala 303:20Mz-
 :
B


	ren2_uops
0pdst	

_T_54�rename-stage.scala 303:14�zf
<::
,B*
#:!
:


	busytableioren_uops
0
debug_tsrc&:$
B


	ren2_uops
0
debug_tsrc�rename-stage.scala 309:25�zf
<::
,B*
#:!
:


	busytableioren_uops
0
debug_fsrc&:$
B


	ren2_uops
0
debug_fsrc�rename-stage.scala 309:25�zf
<::
,B*
#:!
:


	busytableioren_uops
0
bp_xcpt_if&:$
B


	ren2_uops
0
bp_xcpt_if�rename-stage.scala 309:25�zh
=:;
,B*
#:!
:


	busytableioren_uops
0bp_debug_if':%
B


	ren2_uops
0bp_debug_if�rename-stage.scala 309:25�zf
<::
,B*
#:!
:


	busytableioren_uops
0
xcpt_ma_if&:$
B


	ren2_uops
0
xcpt_ma_if�rename-stage.scala 309:25�zf
<::
,B*
#:!
:


	busytableioren_uops
0
xcpt_ae_if&:$
B


	ren2_uops
0
xcpt_ae_if�rename-stage.scala 309:25�zf
<::
,B*
#:!
:


	busytableioren_uops
0
xcpt_pf_if&:$
B


	ren2_uops
0
xcpt_pf_if�rename-stage.scala 309:25�zd
;:9
,B*
#:!
:


	busytableioren_uops
0	fp_single%:#
B


	ren2_uops
0	fp_single�rename-stage.scala 309:25~z^
8:6
,B*
#:!
:


	busytableioren_uops
0fp_val": 
B


	ren2_uops
0fp_val�rename-stage.scala 309:25�z`
9:7
,B*
#:!
:


	busytableioren_uops
0frs3_en#:!
B


	ren2_uops
0frs3_en�rename-stage.scala 309:25�zf
<::
,B*
#:!
:


	busytableioren_uops
0
lrs2_rtype&:$
B


	ren2_uops
0
lrs2_rtype�rename-stage.scala 309:25�zf
<::
,B*
#:!
:


	busytableioren_uops
0
lrs1_rtype&:$
B


	ren2_uops
0
lrs1_rtype�rename-stage.scala 309:25�zd
;:9
,B*
#:!
:


	busytableioren_uops
0	dst_rtype%:#
B


	ren2_uops
0	dst_rtype�rename-stage.scala 309:25�zb
::8
,B*
#:!
:


	busytableioren_uops
0ldst_val$:"
B


	ren2_uops
0ldst_val�rename-stage.scala 309:25zzZ
6:4
,B*
#:!
:


	busytableioren_uops
0lrs3 :
B


	ren2_uops
0lrs3�rename-stage.scala 309:25zzZ
6:4
,B*
#:!
:


	busytableioren_uops
0lrs2 :
B


	ren2_uops
0lrs2�rename-stage.scala 309:25zzZ
6:4
,B*
#:!
:


	busytableioren_uops
0lrs1 :
B


	ren2_uops
0lrs1�rename-stage.scala 309:25zzZ
6:4
,B*
#:!
:


	busytableioren_uops
0ldst :
B


	ren2_uops
0ldst�rename-stage.scala 309:25�zh
=:;
,B*
#:!
:


	busytableioren_uops
0ldst_is_rs1':%
B


	ren2_uops
0ldst_is_rs1�rename-stage.scala 309:25�zp
A:?
,B*
#:!
:


	busytableioren_uops
0flush_on_commit+:)
B


	ren2_uops
0flush_on_commit�rename-stage.scala 309:25�zd
;:9
,B*
#:!
:


	busytableioren_uops
0	is_unique%:#
B


	ren2_uops
0	is_unique�rename-stage.scala 309:25�zl
?:=
,B*
#:!
:


	busytableioren_uops
0is_sys_pc2epc):'
B


	ren2_uops
0is_sys_pc2epc�rename-stage.scala 309:25�zb
::8
,B*
#:!
:


	busytableioren_uops
0uses_stq$:"
B


	ren2_uops
0uses_stq�rename-stage.scala 309:25�zb
::8
,B*
#:!
:


	busytableioren_uops
0uses_ldq$:"
B


	ren2_uops
0uses_ldq�rename-stage.scala 309:25~z^
8:6
,B*
#:!
:


	busytableioren_uops
0is_amo": 
B


	ren2_uops
0is_amo�rename-stage.scala 309:25�zd
;:9
,B*
#:!
:


	busytableioren_uops
0	is_fencei%:#
B


	ren2_uops
0	is_fencei�rename-stage.scala 309:25�zb
::8
,B*
#:!
:


	busytableioren_uops
0is_fence$:"
B


	ren2_uops
0is_fence�rename-stage.scala 309:25�zf
<::
,B*
#:!
:


	busytableioren_uops
0
mem_signed&:$
B


	ren2_uops
0
mem_signed�rename-stage.scala 309:25�zb
::8
,B*
#:!
:


	busytableioren_uops
0mem_size$:"
B


	ren2_uops
0mem_size�rename-stage.scala 309:25�z`
9:7
,B*
#:!
:


	busytableioren_uops
0mem_cmd#:!
B


	ren2_uops
0mem_cmd�rename-stage.scala 309:25�zf
<::
,B*
#:!
:


	busytableioren_uops
0
bypassable&:$
B


	ren2_uops
0
bypassable�rename-stage.scala 309:25�zd
;:9
,B*
#:!
:


	busytableioren_uops
0	exc_cause%:#
B


	ren2_uops
0	exc_cause�rename-stage.scala 309:25�zd
;:9
,B*
#:!
:


	busytableioren_uops
0	exception%:#
B


	ren2_uops
0	exception�rename-stage.scala 309:25�zf
<::
,B*
#:!
:


	busytableioren_uops
0
stale_pdst&:$
B


	ren2_uops
0
stale_pdst�rename-stage.scala 309:25�zf
<::
,B*
#:!
:


	busytableioren_uops
0
ppred_busy&:$
B


	ren2_uops
0
ppred_busy�rename-stage.scala 309:25�zd
;:9
,B*
#:!
:


	busytableioren_uops
0	prs3_busy%:#
B


	ren2_uops
0	prs3_busy�rename-stage.scala 309:25�zd
;:9
,B*
#:!
:


	busytableioren_uops
0	prs2_busy%:#
B


	ren2_uops
0	prs2_busy�rename-stage.scala 309:25�zd
;:9
,B*
#:!
:


	busytableioren_uops
0	prs1_busy%:#
B


	ren2_uops
0	prs1_busy�rename-stage.scala 309:25|z\
7:5
,B*
#:!
:


	busytableioren_uops
0ppred!:
B


	ren2_uops
0ppred�rename-stage.scala 309:25zzZ
6:4
,B*
#:!
:


	busytableioren_uops
0prs3 :
B


	ren2_uops
0prs3�rename-stage.scala 309:25zzZ
6:4
,B*
#:!
:


	busytableioren_uops
0prs2 :
B


	ren2_uops
0prs2�rename-stage.scala 309:25zzZ
6:4
,B*
#:!
:


	busytableioren_uops
0prs1 :
B


	ren2_uops
0prs1�rename-stage.scala 309:25zzZ
6:4
,B*
#:!
:


	busytableioren_uops
0pdst :
B


	ren2_uops
0pdst�rename-stage.scala 309:25�z`
9:7
,B*
#:!
:


	busytableioren_uops
0rxq_idx#:!
B


	ren2_uops
0rxq_idx�rename-stage.scala 309:25�z`
9:7
,B*
#:!
:


	busytableioren_uops
0stq_idx#:!
B


	ren2_uops
0stq_idx�rename-stage.scala 309:25�z`
9:7
,B*
#:!
:


	busytableioren_uops
0ldq_idx#:!
B


	ren2_uops
0ldq_idx�rename-stage.scala 309:25�z`
9:7
,B*
#:!
:


	busytableioren_uops
0rob_idx#:!
B


	ren2_uops
0rob_idx�rename-stage.scala 309:25�zb
::8
,B*
#:!
:


	busytableioren_uops
0csr_addr$:"
B


	ren2_uops
0csr_addr�rename-stage.scala 309:25�zf
<::
,B*
#:!
:


	busytableioren_uops
0
imm_packed&:$
B


	ren2_uops
0
imm_packed�rename-stage.scala 309:25|z\
7:5
,B*
#:!
:


	busytableioren_uops
0taken!:
B


	ren2_uops
0taken�rename-stage.scala 309:25~z^
8:6
,B*
#:!
:


	busytableioren_uops
0pc_lob": 
B


	ren2_uops
0pc_lob�rename-stage.scala 309:25�zd
;:9
,B*
#:!
:


	busytableioren_uops
0	edge_inst%:#
B


	ren2_uops
0	edge_inst�rename-stage.scala 309:25�z`
9:7
,B*
#:!
:


	busytableioren_uops
0ftq_idx#:!
B


	ren2_uops
0ftq_idx�rename-stage.scala 309:25~z^
8:6
,B*
#:!
:


	busytableioren_uops
0br_tag": 
B


	ren2_uops
0br_tag�rename-stage.scala 309:25�z`
9:7
,B*
#:!
:


	busytableioren_uops
0br_mask#:!
B


	ren2_uops
0br_mask�rename-stage.scala 309:25~z^
8:6
,B*
#:!
:


	busytableioren_uops
0is_sfb": 
B


	ren2_uops
0is_sfb�rename-stage.scala 309:25~z^
8:6
,B*
#:!
:


	busytableioren_uops
0is_jal": 
B


	ren2_uops
0is_jal�rename-stage.scala 309:25�z`
9:7
,B*
#:!
:


	busytableioren_uops
0is_jalr#:!
B


	ren2_uops
0is_jalr�rename-stage.scala 309:25|z\
7:5
,B*
#:!
:


	busytableioren_uops
0is_br!:
B


	ren2_uops
0is_br�rename-stage.scala 309:25�zn
@:>
,B*
#:!
:


	busytableioren_uops
0iw_p2_poisoned*:(
B


	ren2_uops
0iw_p2_poisoned�rename-stage.scala 309:25�zn
@:>
,B*
#:!
:


	busytableioren_uops
0iw_p1_poisoned*:(
B


	ren2_uops
0iw_p1_poisoned�rename-stage.scala 309:25�zb
::8
,B*
#:!
:


	busytableioren_uops
0iw_state$:"
B


	ren2_uops
0iw_state�rename-stage.scala 309:25�zr
B:@
6:4
,B*
#:!
:


	busytableioren_uops
0ctrlis_std,:*
 :
B


	ren2_uops
0ctrlis_std�rename-stage.scala 309:25�zr
B:@
6:4
,B*
#:!
:


	busytableioren_uops
0ctrlis_sta,:*
 :
B


	ren2_uops
0ctrlis_sta�rename-stage.scala 309:25�zt
C:A
6:4
,B*
#:!
:


	busytableioren_uops
0ctrlis_load-:+
 :
B


	ren2_uops
0ctrlis_load�rename-stage.scala 309:25�zt
C:A
6:4
,B*
#:!
:


	busytableioren_uops
0ctrlcsr_cmd-:+
 :
B


	ren2_uops
0ctrlcsr_cmd�rename-stage.scala 309:25�zr
B:@
6:4
,B*
#:!
:


	busytableioren_uops
0ctrlfcn_dw,:*
 :
B


	ren2_uops
0ctrlfcn_dw�rename-stage.scala 309:25�zr
B:@
6:4
,B*
#:!
:


	busytableioren_uops
0ctrlop_fcn,:*
 :
B


	ren2_uops
0ctrlop_fcn�rename-stage.scala 309:25�zt
C:A
6:4
,B*
#:!
:


	busytableioren_uops
0ctrlimm_sel-:+
 :
B


	ren2_uops
0ctrlimm_sel�rename-stage.scala 309:25�zt
C:A
6:4
,B*
#:!
:


	busytableioren_uops
0ctrlop2_sel-:+
 :
B


	ren2_uops
0ctrlop2_sel�rename-stage.scala 309:25�zt
C:A
6:4
,B*
#:!
:


	busytableioren_uops
0ctrlop1_sel-:+
 :
B


	ren2_uops
0ctrlop1_sel�rename-stage.scala 309:25�zt
C:A
6:4
,B*
#:!
:


	busytableioren_uops
0ctrlbr_type-:+
 :
B


	ren2_uops
0ctrlbr_type�rename-stage.scala 309:25�z`
9:7
,B*
#:!
:


	busytableioren_uops
0fu_code#:!
B


	ren2_uops
0fu_code�rename-stage.scala 309:25�z`
9:7
,B*
#:!
:


	busytableioren_uops
0iq_type#:!
B


	ren2_uops
0iq_type�rename-stage.scala 309:25�zb
::8
,B*
#:!
:


	busytableioren_uops
0debug_pc$:"
B


	ren2_uops
0debug_pc�rename-stage.scala 309:25~z^
8:6
,B*
#:!
:


	busytableioren_uops
0is_rvc": 
B


	ren2_uops
0is_rvc�rename-stage.scala 309:25�zf
<::
,B*
#:!
:


	busytableioren_uops
0
debug_inst&:$
B


	ren2_uops
0
debug_inst�rename-stage.scala 309:25zzZ
6:4
,B*
#:!
:


	busytableioren_uops
0inst :
B


	ren2_uops
0inst�rename-stage.scala 309:25zzZ
6:4
,B*
#:!
:


	busytableioren_uops
0uopc :
B


	ren2_uops
0uopc�rename-stage.scala 309:25ozO
/B-
&:$
:


	busytableiorebusy_reqs
0B


ren2_alloc_reqs
0�rename-stage.scala 310:28xzX
-B+
$:"
:


	busytableio	wb_valids
0':%
B
:


iowakeups
0valid�rename-stage.scala 311:26xzX
-B+
$:"
:


	busytableio	wb_valids
1':%
B
:


iowakeups
1valid�rename-stage.scala 311:26xzX
-B+
$:"
:


	busytableio	wb_valids
2':%
B
:


iowakeups
2valid�rename-stage.scala 311:26�zi
,B*
#:!
:


	busytableiowb_pdsts
09:7
/:-
&:$
B
:


iowakeups
0bitsuoppdst�rename-stage.scala 312:25�zi
,B*
#:!
:


	busytableiowb_pdsts
19:7
/:-
&:$
B
:


iowakeups
1bitsuoppdst�rename-stage.scala 312:25�zi
,B*
#:!
:


	busytableiowb_pdsts
29:7
/:-
&:$
B
:


iowakeups
2bitsuoppdst�rename-stage.scala 312:25z2Z
_T_55QRO>:<
/:-
&:$
B
:


iowakeups
0bitsuop	dst_rtype	

0�rename-stage.scala 314:65a2A
_T_568R6':%
B
:


iowakeups
0valid	

_T_55�rename-stage.scala 314:41z2Z
_T_57QRO>:<
/:-
&:$
B
:


iowakeups
1bitsuop	dst_rtype	

0�rename-stage.scala 314:65a2A
_T_588R6':%
B
:


iowakeups
1valid	

_T_57�rename-stage.scala 314:41z2Z
_T_59QRO>:<
/:-
&:$
B
:


iowakeups
2bitsuop	dst_rtype	

0�rename-stage.scala 314:65a2A
_T_608R6':%
B
:


iowakeups
2valid	

_T_59�rename-stage.scala 314:41C2#
_T_61R	

_T_56	

_T_58�rename-stage.scala 314:84C2#
_T_62R	

_T_61	

_T_60�rename-stage.scala 314:84E2%
_T_63R	

_T_62	

0�rename-stage.scala 314:11B2"
_T_64R	

reset
0
0�rename-stage.scala 314:10C2#
_T_65R	

_T_63	

_T_64�rename-stage.scala 314:10E2%
_T_66R	

_T_65	

0�rename-stage.scala 314:10�:�
	

_T_66�R�
�Assertion failed: [rename] Wakeup has wrong rtype.
    at rename-stage.scala:314 assert (!(io.wakeups.map(x => x.valid && x.bits.uop.dst_rtype =/= rtype).reduce(_||_)),
	

clock"	

1�rename-stage.scala 314:10:B	

clock	

1�rename-stage.scala 314:10�rename-stage.scala 314:10b2B
_T_679R7&:$
B


	ren2_uops
0
lrs1_rtype	

0�rename-stage.scala 320:37w2W
_T_68NRL	

_T_67=:;
.B,
%:#
:


	busytableio
busy_resps
0	prs1_busy�rename-stage.scala 320:47Rz2
%:#
B


	ren2_uops
0	prs1_busy	

_T_68�rename-stage.scala 320:19b2B
_T_699R7&:$
B


	ren2_uops
0
lrs2_rtype	

0�rename-stage.scala 321:37w2W
_T_70NRL	

_T_69=:;
.B,
%:#
:


	busytableio
busy_resps
0	prs2_busy�rename-stage.scala 321:47Rz2
%:#
B


	ren2_uops
0	prs2_busy	

_T_70�rename-stage.scala 321:19�2q
_T_71hRf#:!
B


	ren2_uops
0frs3_en=:;
.B,
%:#
:


	busytableio
busy_resps
0	prs3_busy�rename-stage.scala 322:34Rz2
%:#
B


	ren2_uops
0	prs3_busy	

_T_71�rename-stage.scala 322:19�2f
_T_72]R[B


ren2_valids
0=:;
.B,
%:#
:


	busytableio
busy_resps
0	prs1_busy�rename-stage.scala 325:21G2'
_T_73R	

0	

0�rename-stage.scala 325:48C2#
_T_74R	

_T_72	

_T_73�rename-stage.scala 325:39\2<
_T_753R1 :
B


	ren2_uops
0lrs1	

0�rename-stage.scala 325:71C2#
_T_76R	

_T_74	

_T_75�rename-stage.scala 325:59E2%
_T_77R	

_T_76	

0�rename-stage.scala 325:13B2"
_T_78R	

reset
0
0�rename-stage.scala 325:12C2#
_T_79R	

_T_77	

_T_78�rename-stage.scala 325:12E2%
_T_80R	

_T_79	

0�rename-stage.scala 325:12�:�
	

_T_80�R�
�Assertion failed: [rename] x0 is busy??
    at rename-stage.scala:325 assert (!(valid && busy.prs1_busy && rtype === RT_FIX && uop.lrs1 === 0.U), "[rename] x0 is busy??")
	

clock"	

1�rename-stage.scala 325:12:B	

clock	

1�rename-stage.scala 325:12�rename-stage.scala 325:12�2f
_T_81]R[B


ren2_valids
0=:;
.B,
%:#
:


	busytableio
busy_resps
0	prs2_busy�rename-stage.scala 326:21G2'
_T_82R	

0	

0�rename-stage.scala 326:48C2#
_T_83R	

_T_81	

_T_82�rename-stage.scala 326:39\2<
_T_843R1 :
B


	ren2_uops
0lrs2	

0�rename-stage.scala 326:71C2#
_T_85R	

_T_83	

_T_84�rename-stage.scala 326:59E2%
_T_86R	

_T_85	

0�rename-stage.scala 326:13B2"
_T_87R	

reset
0
0�rename-stage.scala 326:12C2#
_T_88R	

_T_86	

_T_87�rename-stage.scala 326:12E2%
_T_89R	

_T_88	

0�rename-stage.scala 326:12�:�
	

_T_89�R�
�Assertion failed: [rename] x0 is busy??
    at rename-stage.scala:326 assert (!(valid && busy.prs2_busy && rtype === RT_FIX && uop.lrs2 === 0.U), "[rename] x0 is busy??")
	

clock"	

1�rename-stage.scala 326:12:B	

clock	

1�rename-stage.scala 326:12�rename-stage.scala 326:12a2A
_T_908R6%:#
B


	ren2_uops
0	dst_rtype	

0�rename-stage.scala 336:49u2U
_T_91LRJ9:7
.B,
%:#
:



freelistioalloc_pregs
0valid	

0�rename-stage.scala 336:63C2#
_T_92R	

_T_90	

_T_91�rename-stage.scala 336:60Lz,
B
:


io
ren_stalls
0	

_T_92�rename-stage.scala 336:22�
�
_T_93�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc
�rename-stage.scala 338:28czC
:
	

_T_93
debug_tsrc&:$
B


	ren2_uops
0
debug_tsrc�rename-stage.scala 340:29czC
:
	

_T_93
debug_fsrc&:$
B


	ren2_uops
0
debug_fsrc�rename-stage.scala 340:29czC
:
	

_T_93
bp_xcpt_if&:$
B


	ren2_uops
0
bp_xcpt_if�rename-stage.scala 340:29ezE
:
	

_T_93bp_debug_if':%
B


	ren2_uops
0bp_debug_if�rename-stage.scala 340:29czC
:
	

_T_93
xcpt_ma_if&:$
B


	ren2_uops
0
xcpt_ma_if�rename-stage.scala 340:29czC
:
	

_T_93
xcpt_ae_if&:$
B


	ren2_uops
0
xcpt_ae_if�rename-stage.scala 340:29czC
:
	

_T_93
xcpt_pf_if&:$
B


	ren2_uops
0
xcpt_pf_if�rename-stage.scala 340:29azA
:
	

_T_93	fp_single%:#
B


	ren2_uops
0	fp_single�rename-stage.scala 340:29[z;
:
	

_T_93fp_val": 
B


	ren2_uops
0fp_val�rename-stage.scala 340:29]z=
:
	

_T_93frs3_en#:!
B


	ren2_uops
0frs3_en�rename-stage.scala 340:29czC
:
	

_T_93
lrs2_rtype&:$
B


	ren2_uops
0
lrs2_rtype�rename-stage.scala 340:29czC
:
	

_T_93
lrs1_rtype&:$
B


	ren2_uops
0
lrs1_rtype�rename-stage.scala 340:29azA
:
	

_T_93	dst_rtype%:#
B


	ren2_uops
0	dst_rtype�rename-stage.scala 340:29_z?
:
	

_T_93ldst_val$:"
B


	ren2_uops
0ldst_val�rename-stage.scala 340:29Wz7
:
	

_T_93lrs3 :
B


	ren2_uops
0lrs3�rename-stage.scala 340:29Wz7
:
	

_T_93lrs2 :
B


	ren2_uops
0lrs2�rename-stage.scala 340:29Wz7
:
	

_T_93lrs1 :
B


	ren2_uops
0lrs1�rename-stage.scala 340:29Wz7
:
	

_T_93ldst :
B


	ren2_uops
0ldst�rename-stage.scala 340:29ezE
:
	

_T_93ldst_is_rs1':%
B


	ren2_uops
0ldst_is_rs1�rename-stage.scala 340:29mzM
:
	

_T_93flush_on_commit+:)
B


	ren2_uops
0flush_on_commit�rename-stage.scala 340:29azA
:
	

_T_93	is_unique%:#
B


	ren2_uops
0	is_unique�rename-stage.scala 340:29izI
:
	

_T_93is_sys_pc2epc):'
B


	ren2_uops
0is_sys_pc2epc�rename-stage.scala 340:29_z?
:
	

_T_93uses_stq$:"
B


	ren2_uops
0uses_stq�rename-stage.scala 340:29_z?
:
	

_T_93uses_ldq$:"
B


	ren2_uops
0uses_ldq�rename-stage.scala 340:29[z;
:
	

_T_93is_amo": 
B


	ren2_uops
0is_amo�rename-stage.scala 340:29azA
:
	

_T_93	is_fencei%:#
B


	ren2_uops
0	is_fencei�rename-stage.scala 340:29_z?
:
	

_T_93is_fence$:"
B


	ren2_uops
0is_fence�rename-stage.scala 340:29czC
:
	

_T_93
mem_signed&:$
B


	ren2_uops
0
mem_signed�rename-stage.scala 340:29_z?
:
	

_T_93mem_size$:"
B


	ren2_uops
0mem_size�rename-stage.scala 340:29]z=
:
	

_T_93mem_cmd#:!
B


	ren2_uops
0mem_cmd�rename-stage.scala 340:29czC
:
	

_T_93
bypassable&:$
B


	ren2_uops
0
bypassable�rename-stage.scala 340:29azA
:
	

_T_93	exc_cause%:#
B


	ren2_uops
0	exc_cause�rename-stage.scala 340:29azA
:
	

_T_93	exception%:#
B


	ren2_uops
0	exception�rename-stage.scala 340:29czC
:
	

_T_93
stale_pdst&:$
B


	ren2_uops
0
stale_pdst�rename-stage.scala 340:29czC
:
	

_T_93
ppred_busy&:$
B


	ren2_uops
0
ppred_busy�rename-stage.scala 340:29azA
:
	

_T_93	prs3_busy%:#
B


	ren2_uops
0	prs3_busy�rename-stage.scala 340:29azA
:
	

_T_93	prs2_busy%:#
B


	ren2_uops
0	prs2_busy�rename-stage.scala 340:29azA
:
	

_T_93	prs1_busy%:#
B


	ren2_uops
0	prs1_busy�rename-stage.scala 340:29Yz9
:
	

_T_93ppred!:
B


	ren2_uops
0ppred�rename-stage.scala 340:29Wz7
:
	

_T_93prs3 :
B


	ren2_uops
0prs3�rename-stage.scala 340:29Wz7
:
	

_T_93prs2 :
B


	ren2_uops
0prs2�rename-stage.scala 340:29Wz7
:
	

_T_93prs1 :
B


	ren2_uops
0prs1�rename-stage.scala 340:29Wz7
:
	

_T_93pdst :
B


	ren2_uops
0pdst�rename-stage.scala 340:29]z=
:
	

_T_93rxq_idx#:!
B


	ren2_uops
0rxq_idx�rename-stage.scala 340:29]z=
:
	

_T_93stq_idx#:!
B


	ren2_uops
0stq_idx�rename-stage.scala 340:29]z=
:
	

_T_93ldq_idx#:!
B


	ren2_uops
0ldq_idx�rename-stage.scala 340:29]z=
:
	

_T_93rob_idx#:!
B


	ren2_uops
0rob_idx�rename-stage.scala 340:29_z?
:
	

_T_93csr_addr$:"
B


	ren2_uops
0csr_addr�rename-stage.scala 340:29czC
:
	

_T_93
imm_packed&:$
B


	ren2_uops
0
imm_packed�rename-stage.scala 340:29Yz9
:
	

_T_93taken!:
B


	ren2_uops
0taken�rename-stage.scala 340:29[z;
:
	

_T_93pc_lob": 
B


	ren2_uops
0pc_lob�rename-stage.scala 340:29azA
:
	

_T_93	edge_inst%:#
B


	ren2_uops
0	edge_inst�rename-stage.scala 340:29]z=
:
	

_T_93ftq_idx#:!
B


	ren2_uops
0ftq_idx�rename-stage.scala 340:29[z;
:
	

_T_93br_tag": 
B


	ren2_uops
0br_tag�rename-stage.scala 340:29]z=
:
	

_T_93br_mask#:!
B


	ren2_uops
0br_mask�rename-stage.scala 340:29[z;
:
	

_T_93is_sfb": 
B


	ren2_uops
0is_sfb�rename-stage.scala 340:29[z;
:
	

_T_93is_jal": 
B


	ren2_uops
0is_jal�rename-stage.scala 340:29]z=
:
	

_T_93is_jalr#:!
B


	ren2_uops
0is_jalr�rename-stage.scala 340:29Yz9
:
	

_T_93is_br!:
B


	ren2_uops
0is_br�rename-stage.scala 340:29kzK
:
	

_T_93iw_p2_poisoned*:(
B


	ren2_uops
0iw_p2_poisoned�rename-stage.scala 340:29kzK
:
	

_T_93iw_p1_poisoned*:(
B


	ren2_uops
0iw_p1_poisoned�rename-stage.scala 340:29_z?
:
	

_T_93iw_state$:"
B


	ren2_uops
0iw_state�rename-stage.scala 340:29ozO
:
:
	

_T_93ctrlis_std,:*
 :
B


	ren2_uops
0ctrlis_std�rename-stage.scala 340:29ozO
:
:
	

_T_93ctrlis_sta,:*
 :
B


	ren2_uops
0ctrlis_sta�rename-stage.scala 340:29qzQ
 :
:
	

_T_93ctrlis_load-:+
 :
B


	ren2_uops
0ctrlis_load�rename-stage.scala 340:29qzQ
 :
:
	

_T_93ctrlcsr_cmd-:+
 :
B


	ren2_uops
0ctrlcsr_cmd�rename-stage.scala 340:29ozO
:
:
	

_T_93ctrlfcn_dw,:*
 :
B


	ren2_uops
0ctrlfcn_dw�rename-stage.scala 340:29ozO
:
:
	

_T_93ctrlop_fcn,:*
 :
B


	ren2_uops
0ctrlop_fcn�rename-stage.scala 340:29qzQ
 :
:
	

_T_93ctrlimm_sel-:+
 :
B


	ren2_uops
0ctrlimm_sel�rename-stage.scala 340:29qzQ
 :
:
	

_T_93ctrlop2_sel-:+
 :
B


	ren2_uops
0ctrlop2_sel�rename-stage.scala 340:29qzQ
 :
:
	

_T_93ctrlop1_sel-:+
 :
B


	ren2_uops
0ctrlop1_sel�rename-stage.scala 340:29qzQ
 :
:
	

_T_93ctrlbr_type-:+
 :
B


	ren2_uops
0ctrlbr_type�rename-stage.scala 340:29]z=
:
	

_T_93fu_code#:!
B


	ren2_uops
0fu_code�rename-stage.scala 340:29]z=
:
	

_T_93iq_type#:!
B


	ren2_uops
0iq_type�rename-stage.scala 340:29_z?
:
	

_T_93debug_pc$:"
B


	ren2_uops
0debug_pc�rename-stage.scala 340:29[z;
:
	

_T_93is_rvc": 
B


	ren2_uops
0is_rvc�rename-stage.scala 340:29czC
:
	

_T_93
debug_inst&:$
B


	ren2_uops
0
debug_inst�rename-stage.scala 340:29Wz7
:
	

_T_93inst :
B


	ren2_uops
0inst�rename-stage.scala 340:29Wz7
:
	

_T_93uopc :
B


	ren2_uops
0uopc�rename-stage.scala 340:29�
�
_T_94�*�
uopc

inst
 

debug_inst
 
is_rvc

debug_pc
(
iq_type

fu_code


�ctrl�*�
br_type

op1_sel

op2_sel

imm_sel

op_fcn

fcn_dw

csr_cmd

is_load

is_sta

is_std

iw_state

iw_p1_poisoned

iw_p2_poisoned

is_br

is_jalr

is_jal

is_sfb

br_mask

br_tag

ftq_idx

	edge_inst

pc_lob

taken


imm_packed

csr_addr

rob_idx

ldq_idx

stq_idx

rxq_idx

pdst

prs1

prs2

prs3

ppred

	prs1_busy

	prs2_busy

	prs3_busy


ppred_busy


stale_pdst

	exception

	exc_cause
@

bypassable

mem_cmd

mem_size


mem_signed

is_fence

	is_fencei

is_amo

uses_ldq

uses_stq

is_sys_pc2epc

	is_unique

flush_on_commit

ldst_is_rs1

ldst

lrs1

lrs2

lrs3

ldst_val

	dst_rtype


lrs1_rtype


lrs2_rtype

frs3_en

fp_val

	fp_single


xcpt_pf_if


xcpt_ae_if


xcpt_ma_if

bp_debug_if


bp_xcpt_if


debug_fsrc


debug_tsrc
�
 =z6
:
	

_T_94
debug_tsrc:
	

_T_93
debug_tsrc�
 =z6
:
	

_T_94
debug_fsrc:
	

_T_93
debug_fsrc�
 =z6
:
	

_T_94
bp_xcpt_if:
	

_T_93
bp_xcpt_if�
 ?z8
:
	

_T_94bp_debug_if:
	

_T_93bp_debug_if�
 =z6
:
	

_T_94
xcpt_ma_if:
	

_T_93
xcpt_ma_if�
 =z6
:
	

_T_94
xcpt_ae_if:
	

_T_93
xcpt_ae_if�
 =z6
:
	

_T_94
xcpt_pf_if:
	

_T_93
xcpt_pf_if�
 ;z4
:
	

_T_94	fp_single:
	

_T_93	fp_single�
 5z.
:
	

_T_94fp_val:
	

_T_93fp_val�
 7z0
:
	

_T_94frs3_en:
	

_T_93frs3_en�
 =z6
:
	

_T_94
lrs2_rtype:
	

_T_93
lrs2_rtype�
 =z6
:
	

_T_94
lrs1_rtype:
	

_T_93
lrs1_rtype�
 ;z4
:
	

_T_94	dst_rtype:
	

_T_93	dst_rtype�
 9z2
:
	

_T_94ldst_val:
	

_T_93ldst_val�
 1z*
:
	

_T_94lrs3:
	

_T_93lrs3�
 1z*
:
	

_T_94lrs2:
	

_T_93lrs2�
 1z*
:
	

_T_94lrs1:
	

_T_93lrs1�
 1z*
:
	

_T_94ldst:
	

_T_93ldst�
 ?z8
:
	

_T_94ldst_is_rs1:
	

_T_93ldst_is_rs1�
 Gz@
:
	

_T_94flush_on_commit:
	

_T_93flush_on_commit�
 ;z4
:
	

_T_94	is_unique:
	

_T_93	is_unique�
 Cz<
:
	

_T_94is_sys_pc2epc:
	

_T_93is_sys_pc2epc�
 9z2
:
	

_T_94uses_stq:
	

_T_93uses_stq�
 9z2
:
	

_T_94uses_ldq:
	

_T_93uses_ldq�
 5z.
:
	

_T_94is_amo:
	

_T_93is_amo�
 ;z4
:
	

_T_94	is_fencei:
	

_T_93	is_fencei�
 9z2
:
	

_T_94is_fence:
	

_T_93is_fence�
 =z6
:
	

_T_94
mem_signed:
	

_T_93
mem_signed�
 9z2
:
	

_T_94mem_size:
	

_T_93mem_size�
 7z0
:
	

_T_94mem_cmd:
	

_T_93mem_cmd�
 =z6
:
	

_T_94
bypassable:
	

_T_93
bypassable�
 ;z4
:
	

_T_94	exc_cause:
	

_T_93	exc_cause�
 ;z4
:
	

_T_94	exception:
	

_T_93	exception�
 =z6
:
	

_T_94
stale_pdst:
	

_T_93
stale_pdst�
 =z6
:
	

_T_94
ppred_busy:
	

_T_93
ppred_busy�
 ;z4
:
	

_T_94	prs3_busy:
	

_T_93	prs3_busy�
 ;z4
:
	

_T_94	prs2_busy:
	

_T_93	prs2_busy�
 ;z4
:
	

_T_94	prs1_busy:
	

_T_93	prs1_busy�
 3z,
:
	

_T_94ppred:
	

_T_93ppred�
 1z*
:
	

_T_94prs3:
	

_T_93prs3�
 1z*
:
	

_T_94prs2:
	

_T_93prs2�
 1z*
:
	

_T_94prs1:
	

_T_93prs1�
 1z*
:
	

_T_94pdst:
	

_T_93pdst�
 7z0
:
	

_T_94rxq_idx:
	

_T_93rxq_idx�
 7z0
:
	

_T_94stq_idx:
	

_T_93stq_idx�
 7z0
:
	

_T_94ldq_idx:
	

_T_93ldq_idx�
 7z0
:
	

_T_94rob_idx:
	

_T_93rob_idx�
 9z2
:
	

_T_94csr_addr:
	

_T_93csr_addr�
 =z6
:
	

_T_94
imm_packed:
	

_T_93
imm_packed�
 3z,
:
	

_T_94taken:
	

_T_93taken�
 5z.
:
	

_T_94pc_lob:
	

_T_93pc_lob�
 ;z4
:
	

_T_94	edge_inst:
	

_T_93	edge_inst�
 7z0
:
	

_T_94ftq_idx:
	

_T_93ftq_idx�
 5z.
:
	

_T_94br_tag:
	

_T_93br_tag�
 7z0
:
	

_T_94br_mask:
	

_T_93br_mask�
 5z.
:
	

_T_94is_sfb:
	

_T_93is_sfb�
 5z.
:
	

_T_94is_jal:
	

_T_93is_jal�
 7z0
:
	

_T_94is_jalr:
	

_T_93is_jalr�
 3z,
:
	

_T_94is_br:
	

_T_93is_br�
 Ez>
:
	

_T_94iw_p2_poisoned:
	

_T_93iw_p2_poisoned�
 Ez>
:
	

_T_94iw_p1_poisoned:
	

_T_93iw_p1_poisoned�
 9z2
:
	

_T_94iw_state:
	

_T_93iw_state�
 IzB
:
:
	

_T_94ctrlis_std:
:
	

_T_93ctrlis_std�
 IzB
:
:
	

_T_94ctrlis_sta:
:
	

_T_93ctrlis_sta�
 KzD
 :
:
	

_T_94ctrlis_load :
:
	

_T_93ctrlis_load�
 KzD
 :
:
	

_T_94ctrlcsr_cmd :
:
	

_T_93ctrlcsr_cmd�
 IzB
:
:
	

_T_94ctrlfcn_dw:
:
	

_T_93ctrlfcn_dw�
 IzB
:
:
	

_T_94ctrlop_fcn:
:
	

_T_93ctrlop_fcn�
 KzD
 :
:
	

_T_94ctrlimm_sel :
:
	

_T_93ctrlimm_sel�
 KzD
 :
:
	

_T_94ctrlop2_sel :
:
	

_T_93ctrlop2_sel�
 KzD
 :
:
	

_T_94ctrlop1_sel :
:
	

_T_93ctrlop1_sel�
 KzD
 :
:
	

_T_94ctrlbr_type :
:
	

_T_93ctrlbr_type�
 7z0
:
	

_T_94fu_code:
	

_T_93fu_code�
 7z0
:
	

_T_94iq_type:
	

_T_93iq_type�
 9z2
:
	

_T_94debug_pc:
	

_T_93debug_pc�
 5z.
:
	

_T_94is_rvc:
	

_T_93is_rvc�
 =z6
:
	

_T_94
debug_inst:
	

_T_93
debug_inst�
 1z*
:
	

_T_94inst:
	

_T_93inst�
 1z*
:
	

_T_94uopc:
	

_T_93uopc�
 T2=
_T_954R2.:,
:
:


iobrupdateb1resolve_mask�util.scala 74:37G20
_T_96'R%:
	

_T_93br_mask	

_T_95�util.scala 74:35:z#
:
	

_T_94br_mask	

_T_96�util.scala 74:20kzK
.:,
B
:


io	ren2_uops
0
debug_tsrc:
	

_T_94
debug_tsrc�rename-stage.scala 342:21kzK
.:,
B
:


io	ren2_uops
0
debug_fsrc:
	

_T_94
debug_fsrc�rename-stage.scala 342:21kzK
.:,
B
:


io	ren2_uops
0
bp_xcpt_if:
	

_T_94
bp_xcpt_if�rename-stage.scala 342:21mzM
/:-
B
:


io	ren2_uops
0bp_debug_if:
	

_T_94bp_debug_if�rename-stage.scala 342:21kzK
.:,
B
:


io	ren2_uops
0
xcpt_ma_if:
	

_T_94
xcpt_ma_if�rename-stage.scala 342:21kzK
.:,
B
:


io	ren2_uops
0
xcpt_ae_if:
	

_T_94
xcpt_ae_if�rename-stage.scala 342:21kzK
.:,
B
:


io	ren2_uops
0
xcpt_pf_if:
	

_T_94
xcpt_pf_if�rename-stage.scala 342:21izI
-:+
B
:


io	ren2_uops
0	fp_single:
	

_T_94	fp_single�rename-stage.scala 342:21czC
*:(
B
:


io	ren2_uops
0fp_val:
	

_T_94fp_val�rename-stage.scala 342:21ezE
+:)
B
:


io	ren2_uops
0frs3_en:
	

_T_94frs3_en�rename-stage.scala 342:21kzK
.:,
B
:


io	ren2_uops
0
lrs2_rtype:
	

_T_94
lrs2_rtype�rename-stage.scala 342:21kzK
.:,
B
:


io	ren2_uops
0
lrs1_rtype:
	

_T_94
lrs1_rtype�rename-stage.scala 342:21izI
-:+
B
:


io	ren2_uops
0	dst_rtype:
	

_T_94	dst_rtype�rename-stage.scala 342:21gzG
,:*
B
:


io	ren2_uops
0ldst_val:
	

_T_94ldst_val�rename-stage.scala 342:21_z?
(:&
B
:


io	ren2_uops
0lrs3:
	

_T_94lrs3�rename-stage.scala 342:21_z?
(:&
B
:


io	ren2_uops
0lrs2:
	

_T_94lrs2�rename-stage.scala 342:21_z?
(:&
B
:


io	ren2_uops
0lrs1:
	

_T_94lrs1�rename-stage.scala 342:21_z?
(:&
B
:


io	ren2_uops
0ldst:
	

_T_94ldst�rename-stage.scala 342:21mzM
/:-
B
:


io	ren2_uops
0ldst_is_rs1:
	

_T_94ldst_is_rs1�rename-stage.scala 342:21uzU
3:1
B
:


io	ren2_uops
0flush_on_commit:
	

_T_94flush_on_commit�rename-stage.scala 342:21izI
-:+
B
:


io	ren2_uops
0	is_unique:
	

_T_94	is_unique�rename-stage.scala 342:21qzQ
1:/
B
:


io	ren2_uops
0is_sys_pc2epc:
	

_T_94is_sys_pc2epc�rename-stage.scala 342:21gzG
,:*
B
:


io	ren2_uops
0uses_stq:
	

_T_94uses_stq�rename-stage.scala 342:21gzG
,:*
B
:


io	ren2_uops
0uses_ldq:
	

_T_94uses_ldq�rename-stage.scala 342:21czC
*:(
B
:


io	ren2_uops
0is_amo:
	

_T_94is_amo�rename-stage.scala 342:21izI
-:+
B
:


io	ren2_uops
0	is_fencei:
	

_T_94	is_fencei�rename-stage.scala 342:21gzG
,:*
B
:


io	ren2_uops
0is_fence:
	

_T_94is_fence�rename-stage.scala 342:21kzK
.:,
B
:


io	ren2_uops
0
mem_signed:
	

_T_94
mem_signed�rename-stage.scala 342:21gzG
,:*
B
:


io	ren2_uops
0mem_size:
	

_T_94mem_size�rename-stage.scala 342:21ezE
+:)
B
:


io	ren2_uops
0mem_cmd:
	

_T_94mem_cmd�rename-stage.scala 342:21kzK
.:,
B
:


io	ren2_uops
0
bypassable:
	

_T_94
bypassable�rename-stage.scala 342:21izI
-:+
B
:


io	ren2_uops
0	exc_cause:
	

_T_94	exc_cause�rename-stage.scala 342:21izI
-:+
B
:


io	ren2_uops
0	exception:
	

_T_94	exception�rename-stage.scala 342:21kzK
.:,
B
:


io	ren2_uops
0
stale_pdst:
	

_T_94
stale_pdst�rename-stage.scala 342:21kzK
.:,
B
:


io	ren2_uops
0
ppred_busy:
	

_T_94
ppred_busy�rename-stage.scala 342:21izI
-:+
B
:


io	ren2_uops
0	prs3_busy:
	

_T_94	prs3_busy�rename-stage.scala 342:21izI
-:+
B
:


io	ren2_uops
0	prs2_busy:
	

_T_94	prs2_busy�rename-stage.scala 342:21izI
-:+
B
:


io	ren2_uops
0	prs1_busy:
	

_T_94	prs1_busy�rename-stage.scala 342:21azA
):'
B
:


io	ren2_uops
0ppred:
	

_T_94ppred�rename-stage.scala 342:21_z?
(:&
B
:


io	ren2_uops
0prs3:
	

_T_94prs3�rename-stage.scala 342:21_z?
(:&
B
:


io	ren2_uops
0prs2:
	

_T_94prs2�rename-stage.scala 342:21_z?
(:&
B
:


io	ren2_uops
0prs1:
	

_T_94prs1�rename-stage.scala 342:21_z?
(:&
B
:


io	ren2_uops
0pdst:
	

_T_94pdst�rename-stage.scala 342:21ezE
+:)
B
:


io	ren2_uops
0rxq_idx:
	

_T_94rxq_idx�rename-stage.scala 342:21ezE
+:)
B
:


io	ren2_uops
0stq_idx:
	

_T_94stq_idx�rename-stage.scala 342:21ezE
+:)
B
:


io	ren2_uops
0ldq_idx:
	

_T_94ldq_idx�rename-stage.scala 342:21ezE
+:)
B
:


io	ren2_uops
0rob_idx:
	

_T_94rob_idx�rename-stage.scala 342:21gzG
,:*
B
:


io	ren2_uops
0csr_addr:
	

_T_94csr_addr�rename-stage.scala 342:21kzK
.:,
B
:


io	ren2_uops
0
imm_packed:
	

_T_94
imm_packed�rename-stage.scala 342:21azA
):'
B
:


io	ren2_uops
0taken:
	

_T_94taken�rename-stage.scala 342:21czC
*:(
B
:


io	ren2_uops
0pc_lob:
	

_T_94pc_lob�rename-stage.scala 342:21izI
-:+
B
:


io	ren2_uops
0	edge_inst:
	

_T_94	edge_inst�rename-stage.scala 342:21ezE
+:)
B
:


io	ren2_uops
0ftq_idx:
	

_T_94ftq_idx�rename-stage.scala 342:21czC
*:(
B
:


io	ren2_uops
0br_tag:
	

_T_94br_tag�rename-stage.scala 342:21ezE
+:)
B
:


io	ren2_uops
0br_mask:
	

_T_94br_mask�rename-stage.scala 342:21czC
*:(
B
:


io	ren2_uops
0is_sfb:
	

_T_94is_sfb�rename-stage.scala 342:21czC
*:(
B
:


io	ren2_uops
0is_jal:
	

_T_94is_jal�rename-stage.scala 342:21ezE
+:)
B
:


io	ren2_uops
0is_jalr:
	

_T_94is_jalr�rename-stage.scala 342:21azA
):'
B
:


io	ren2_uops
0is_br:
	

_T_94is_br�rename-stage.scala 342:21szS
2:0
B
:


io	ren2_uops
0iw_p2_poisoned:
	

_T_94iw_p2_poisoned�rename-stage.scala 342:21szS
2:0
B
:


io	ren2_uops
0iw_p1_poisoned:
	

_T_94iw_p1_poisoned�rename-stage.scala 342:21gzG
,:*
B
:


io	ren2_uops
0iw_state:
	

_T_94iw_state�rename-stage.scala 342:21wzW
4:2
(:&
B
:


io	ren2_uops
0ctrlis_std:
:
	

_T_94ctrlis_std�rename-stage.scala 342:21wzW
4:2
(:&
B
:


io	ren2_uops
0ctrlis_sta:
:
	

_T_94ctrlis_sta�rename-stage.scala 342:21yzY
5:3
(:&
B
:


io	ren2_uops
0ctrlis_load :
:
	

_T_94ctrlis_load�rename-stage.scala 342:21yzY
5:3
(:&
B
:


io	ren2_uops
0ctrlcsr_cmd :
:
	

_T_94ctrlcsr_cmd�rename-stage.scala 342:21wzW
4:2
(:&
B
:


io	ren2_uops
0ctrlfcn_dw:
:
	

_T_94ctrlfcn_dw�rename-stage.scala 342:21wzW
4:2
(:&
B
:


io	ren2_uops
0ctrlop_fcn:
:
	

_T_94ctrlop_fcn�rename-stage.scala 342:21yzY
5:3
(:&
B
:


io	ren2_uops
0ctrlimm_sel :
:
	

_T_94ctrlimm_sel�rename-stage.scala 342:21yzY
5:3
(:&
B
:


io	ren2_uops
0ctrlop2_sel :
:
	

_T_94ctrlop2_sel�rename-stage.scala 342:21yzY
5:3
(:&
B
:


io	ren2_uops
0ctrlop1_sel :
:
	

_T_94ctrlop1_sel�rename-stage.scala 342:21yzY
5:3
(:&
B
:


io	ren2_uops
0ctrlbr_type :
:
	

_T_94ctrlbr_type�rename-stage.scala 342:21ezE
+:)
B
:


io	ren2_uops
0fu_code:
	

_T_94fu_code�rename-stage.scala 342:21ezE
+:)
B
:


io	ren2_uops
0iq_type:
	

_T_94iq_type�rename-stage.scala 342:21gzG
,:*
B
:


io	ren2_uops
0debug_pc:
	

_T_94debug_pc�rename-stage.scala 342:21czC
*:(
B
:


io	ren2_uops
0is_rvc:
	

_T_94is_rvc�rename-stage.scala 342:21kzK
.:,
B
:


io	ren2_uops
0
debug_inst:
	

_T_94
debug_inst�rename-stage.scala 342:21_z?
(:&
B
:


io	ren2_uops
0inst:
	

_T_94inst�rename-stage.scala 342:21_z?
(:&
B
:


io	ren2_uops
0uopc:
	

_T_94uopc�rename-stage.scala 342:21pzP
:
:


iodebugfreelist-:+
:
:



freelistiodebugfreelist�rename-stage.scala 348:22pzP
:
:


iodebugisprlist-:+
:
:



freelistiodebugisprlist�rename-stage.scala 349:22szS
 :
:


iodebug	busytable/:-
 :
:


	busytableiodebug	busytable�rename-stage.scala 350:22
RenameStage