
ψ
βή
RenameFreeList
clock" 
reset
Λ
ioΒ*Ώ
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
brupdate*
;b15*3
resolve_mask

mispredict_mask

Νb2Ζ*Γ
²uopͺ*§
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

Ζctrl½*Ί
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
4
	

clock
 
	

reset
 


io
 92
_TR	

14rename-freelist.scala 50:45S1
	free_list
4	

clock"	

reset*

_Trename-freelist.scala 50:26lJ
br_alloc_lists2


4	

clock"	

0*

br_alloc_listsrename-freelist.scala 51:27,

sels2


4util.scala 405:20>2%
_T_1R

	free_list
0
0OneHot.scala 85:71>2%
_T_2R

	free_list
1
1OneHot.scala 85:71>2%
_T_3R

	free_list
2
2OneHot.scala 85:71>2%
_T_4R

	free_list
3
3OneHot.scala 85:71>2%
_T_5R

	free_list
4
4OneHot.scala 85:71>2%
_T_6R

	free_list
5
5OneHot.scala 85:71>2%
_T_7R

	free_list
6
6OneHot.scala 85:71>2%
_T_8R

	free_list
7
7OneHot.scala 85:71>2%
_T_9R

	free_list
8
8OneHot.scala 85:71?2&
_T_10R

	free_list
9
9OneHot.scala 85:71A2(
_T_11R

	free_list
10
10OneHot.scala 85:71A2(
_T_12R

	free_list
11
11OneHot.scala 85:71A2(
_T_13R

	free_list
12
12OneHot.scala 85:71A2(
_T_14R

	free_list
13
13OneHot.scala 85:71A2(
_T_15R

	free_list
14
14OneHot.scala 85:71A2(
_T_16R

	free_list
15
15OneHot.scala 85:71A2(
_T_17R

	free_list
16
16OneHot.scala 85:71A2(
_T_18R

	free_list
17
17OneHot.scala 85:71A2(
_T_19R

	free_list
18
18OneHot.scala 85:71A2(
_T_20R

	free_list
19
19OneHot.scala 85:71A2(
_T_21R

	free_list
20
20OneHot.scala 85:71A2(
_T_22R

	free_list
21
21OneHot.scala 85:71A2(
_T_23R

	free_list
22
22OneHot.scala 85:71A2(
_T_24R

	free_list
23
23OneHot.scala 85:71A2(
_T_25R

	free_list
24
24OneHot.scala 85:71A2(
_T_26R

	free_list
25
25OneHot.scala 85:71A2(
_T_27R

	free_list
26
26OneHot.scala 85:71A2(
_T_28R

	free_list
27
27OneHot.scala 85:71A2(
_T_29R

	free_list
28
28OneHot.scala 85:71A2(
_T_30R

	free_list
29
29OneHot.scala 85:71A2(
_T_31R

	free_list
30
30OneHot.scala 85:71A2(
_T_32R

	free_list
31
31OneHot.scala 85:71A2(
_T_33R

	free_list
32
32OneHot.scala 85:71A2(
_T_34R

	free_list
33
33OneHot.scala 85:71A2(
_T_35R

	free_list
34
34OneHot.scala 85:71A2(
_T_36R

	free_list
35
35OneHot.scala 85:71A2(
_T_37R

	free_list
36
36OneHot.scala 85:71A2(
_T_38R

	free_list
37
37OneHot.scala 85:71A2(
_T_39R

	free_list
38
38OneHot.scala 85:71A2(
_T_40R

	free_list
39
39OneHot.scala 85:71A2(
_T_41R

	free_list
40
40OneHot.scala 85:71A2(
_T_42R

	free_list
41
41OneHot.scala 85:71A2(
_T_43R

	free_list
42
42OneHot.scala 85:71A2(
_T_44R

	free_list
43
43OneHot.scala 85:71A2(
_T_45R

	free_list
44
44OneHot.scala 85:71A2(
_T_46R

	free_list
45
45OneHot.scala 85:71A2(
_T_47R

	free_list
46
46OneHot.scala 85:71A2(
_T_48R

	free_list
47
47OneHot.scala 85:71A2(
_T_49R

	free_list
48
48OneHot.scala 85:71A2(
_T_50R

	free_list
49
49OneHot.scala 85:71A2(
_T_51R

	free_list
50
50OneHot.scala 85:71A2(
_T_52R

	free_list
51
51OneHot.scala 85:71U2?
_T_53624
	

_T_52

22517998136852484	

04Mux.scala 47:69S2=
_T_54422
	

_T_51

11258999068426244	

_T_53Mux.scala 47:69R2<
_T_55321
	

_T_50

5629499534213124	

_T_54Mux.scala 47:69R2<
_T_56321
	

_T_49

2814749767106564	

_T_55Mux.scala 47:69R2<
_T_57321
	

_T_48

1407374883553284	

_T_56Mux.scala 47:69Q2;
_T_58220
	

_T_47

703687441776644	

_T_57Mux.scala 47:69Q2;
_T_59220
	

_T_46

351843720888324	

_T_58Mux.scala 47:69Q2;
_T_60220
	

_T_45

175921860444164	

_T_59Mux.scala 47:69P2:
_T_6112/
	

_T_44

87960930222084	

_T_60Mux.scala 47:69P2:
_T_6212/
	

_T_43

43980465111044	

_T_61Mux.scala 47:69P2:
_T_6312/
	

_T_42

21990232555524	

_T_62Mux.scala 47:69P2:
_T_6412/
	

_T_41

10995116277764	

_T_63Mux.scala 47:69O29
_T_6502.
	

_T_40

5497558138884	

_T_64Mux.scala 47:69O29
_T_6602.
	

_T_39

2748779069444	

_T_65Mux.scala 47:69O29
_T_6702.
	

_T_38

1374389534724	

_T_66Mux.scala 47:69N28
_T_68/2-
	

_T_37

687194767364	

_T_67Mux.scala 47:69N28
_T_69/2-
	

_T_36

343597383684	

_T_68Mux.scala 47:69N28
_T_70/2-
	

_T_35

171798691844	

_T_69Mux.scala 47:69M27
_T_71.2,
	

_T_34


85899345924	

_T_70Mux.scala 47:69M27
_T_72.2,
	

_T_33


42949672964	

_T_71Mux.scala 47:69M27
_T_73.2,
	

_T_32


21474836484	

_T_72Mux.scala 47:69M27
_T_74.2,
	

_T_31


10737418244	

_T_73Mux.scala 47:69L26
_T_75-2+
	

_T_30

	5368709124	

_T_74Mux.scala 47:69L26
_T_76-2+
	

_T_29

	2684354564	

_T_75Mux.scala 47:69L26
_T_77-2+
	

_T_28

	1342177284	

_T_76Mux.scala 47:69K25
_T_78,2*
	

_T_27


671088644	

_T_77Mux.scala 47:69K25
_T_79,2*
	

_T_26


335544324	

_T_78Mux.scala 47:69K25
_T_80,2*
	

_T_25


167772164	

_T_79Mux.scala 47:69J24
_T_81+2)
	

_T_24
	
83886084	

_T_80Mux.scala 47:69J24
_T_82+2)
	

_T_23
	
41943044	

_T_81Mux.scala 47:69J24
_T_83+2)
	

_T_22
	
20971524	

_T_82Mux.scala 47:69J24
_T_84+2)
	

_T_21
	
10485764	

_T_83Mux.scala 47:69I23
_T_85*2(
	

_T_20

5242884	

_T_84Mux.scala 47:69I23
_T_86*2(
	

_T_19

2621444	

_T_85Mux.scala 47:69I23
_T_87*2(
	

_T_18

1310724	

_T_86Mux.scala 47:69H22
_T_88)2'
	

_T_17

655364	

_T_87Mux.scala 47:69H22
_T_89)2'
	

_T_16

327684	

_T_88Mux.scala 47:69H22
_T_90)2'
	

_T_15

163844	

_T_89Mux.scala 47:69G21
_T_91(2&
	

_T_14

81924	

_T_90Mux.scala 47:69G21
_T_92(2&
	

_T_13

40964	

_T_91Mux.scala 47:69G21
_T_93(2&
	

_T_12

20484	

_T_92Mux.scala 47:69G21
_T_94(2&
	

_T_11

10244	

_T_93Mux.scala 47:69F20
_T_95'2%
	

_T_10

5124	

_T_94Mux.scala 47:69E2/
_T_96&2$


_T_9

2564	

_T_95Mux.scala 47:69E2/
_T_97&2$


_T_8

1284	

_T_96Mux.scala 47:69D2.
_T_98%2#


_T_7


644	

_T_97Mux.scala 47:69D2.
_T_99%2#


_T_6


324	

_T_98Mux.scala 47:69E2/
_T_100%2#


_T_5


164	

_T_99Mux.scala 47:69E2/
_T_101%2#


_T_4	

84


_T_100Mux.scala 47:69E2/
_T_102%2#


_T_3	

44


_T_101Mux.scala 47:69E2/
_T_103%2#


_T_2	

24


_T_102Mux.scala 47:69E2/
_T_104%2#


_T_1	

14


_T_103Mux.scala 47:697z
B


sels
0


_T_104util.scala 409:1592!
_T_105RB


sels
0util.scala 410:21A2)
_T_106R

	free_list


_T_105util.scala 410:19:

sel_fire2


rename-freelist.scala 55:23b2I
allocs_0=R;
	

1*:(
 B
:


ioalloc_pregs
0bitsOneHot.scala 58:35M23
_T_107)R'B
:


ioreqs
0
0
0Bitwise.scala 72:15[2A
_T_108725



_T_107

45035996273704954	

04Bitwise.scala 72:12J2(
_T_109R


allocs_0


_T_108rename-freelist.scala 59:88P2.
alloc_masks_0R	

04


_T_109rename-freelist.scala 59:84I2/
_T_110%R#B



sel_fire
0
0
0Bitwise.scala 72:15[2A
_T_111725



_T_110

45035996273704954	

04Bitwise.scala 72:12Q2/
sel_mask#R!B


sels
0


_T_111rename-freelist.scala 62:60`2F
_T_112<R:,:*
:
:


iobrupdateb2
mispredict
0
0Bitwise.scala 72:15[2A
_T_113725



_T_112

45035996273704954	

04Bitwise.scala 72:122j
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
_T_113rename-freelist.scala 63:63b2I
_T_114?R=
	

1,:*
"B 
:


iodealloc_pregs
0bitsOneHot.scala 58:35G2%
_T_115R


_T_114
51
0rename-freelist.scala 64:64a2G
_T_116=R;-:+
"B 
:


iodealloc_pregs
0valid
0
0Bitwise.scala 72:15[2A
_T_117725



_T_116

45035996273704954	

04Bitwise.scala 72:12H2&
_T_118R


_T_115


_T_117rename-freelist.scala 64:79T21
dealloc_mask!R


_T_118

br_deallocsrename-freelist.scala 64:1108

_T_1192


rename-freelist.scala 66:25dzB
B



_T_119
0+:)
 B
:


ioren_br_tags
0validrename-freelist.scala 66:25`2G
_T_120=R;
	

1*:(
 B
:


ioren_br_tags
0bitsOneHot.scala 58:35F2$
_T_121R


_T_120
0
0rename-freelist.scala 69:728

_T_1222


rename-freelist.scala 69:27Cz!
B



_T_122
0


_T_121rename-freelist.scala 69:27Z28
_T_123.R,B



_T_122
0B



_T_119
0rename-freelist.scala 69:85<2
_T_124R"


_T_123rename-freelist.scala 70:29:2$
_T_125R


_T_123
0
0Mux.scala 29:36A2
_T_126R

br_deallocsrename-freelist.scala 72:60Y27
_T_127-R+B


br_alloc_lists
0


_T_126rename-freelist.scala 72:58O2-
_T_128#R!


_T_127

alloc_masks_0rename-freelist.scala 72:73S21
_T_129'2%



_T_124	

04


_T_128rename-freelist.scala 71:29Kz)
B


br_alloc_lists
0


_T_129rename-freelist.scala 71:23`2G
_T_130=R;
	

1*:(
 B
:


ioren_br_tags
0bitsOneHot.scala 58:35F2$
_T_131R


_T_130
1
1rename-freelist.scala 69:728

_T_1322


rename-freelist.scala 69:27Cz!
B



_T_132
0


_T_131rename-freelist.scala 69:27Z28
_T_133.R,B



_T_132
0B



_T_119
0rename-freelist.scala 69:85<2
_T_134R"


_T_133rename-freelist.scala 70:29:2$
_T_135R


_T_133
0
0Mux.scala 29:36A2
_T_136R

br_deallocsrename-freelist.scala 72:60Y27
_T_137-R+B


br_alloc_lists
1


_T_136rename-freelist.scala 72:58O2-
_T_138#R!


_T_137

alloc_masks_0rename-freelist.scala 72:73S21
_T_139'2%



_T_134	

04


_T_138rename-freelist.scala 71:29Kz)
B


br_alloc_lists
1


_T_139rename-freelist.scala 71:23`2G
_T_140=R;
	

1*:(
 B
:


ioren_br_tags
0bitsOneHot.scala 58:35F2$
_T_141R


_T_140
2
2rename-freelist.scala 69:728

_T_1422


rename-freelist.scala 69:27Cz!
B



_T_142
0


_T_141rename-freelist.scala 69:27Z28
_T_143.R,B



_T_142
0B



_T_119
0rename-freelist.scala 69:85<2
_T_144R"


_T_143rename-freelist.scala 70:29:2$
_T_145R


_T_143
0
0Mux.scala 29:36A2
_T_146R

br_deallocsrename-freelist.scala 72:60Y27
_T_147-R+B


br_alloc_lists
2


_T_146rename-freelist.scala 72:58O2-
_T_148#R!


_T_147

alloc_masks_0rename-freelist.scala 72:73S21
_T_149'2%



_T_144	

04


_T_148rename-freelist.scala 71:29Kz)
B


br_alloc_lists
2


_T_149rename-freelist.scala 71:23`2G
_T_150=R;
	

1*:(
 B
:


ioren_br_tags
0bitsOneHot.scala 58:35F2$
_T_151R


_T_150
3
3rename-freelist.scala 69:728

_T_1522


rename-freelist.scala 69:27Cz!
B



_T_152
0


_T_151rename-freelist.scala 69:27Z28
_T_153.R,B



_T_152
0B



_T_119
0rename-freelist.scala 69:85<2
_T_154R"


_T_153rename-freelist.scala 70:29:2$
_T_155R


_T_153
0
0Mux.scala 29:36A2
_T_156R

br_deallocsrename-freelist.scala 72:60Y27
_T_157-R+B


br_alloc_lists
3


_T_156rename-freelist.scala 72:58O2-
_T_158#R!


_T_157

alloc_masks_0rename-freelist.scala 72:73S21
_T_159'2%



_T_154	

04


_T_158rename-freelist.scala 71:29Kz)
B


br_alloc_lists
3


_T_159rename-freelist.scala 71:23`2G
_T_160=R;
	

1*:(
 B
:


ioren_br_tags
0bitsOneHot.scala 58:35F2$
_T_161R


_T_160
4
4rename-freelist.scala 69:728

_T_1622


rename-freelist.scala 69:27Cz!
B



_T_162
0


_T_161rename-freelist.scala 69:27Z28
_T_163.R,B



_T_162
0B



_T_119
0rename-freelist.scala 69:85<2
_T_164R"


_T_163rename-freelist.scala 70:29:2$
_T_165R


_T_163
0
0Mux.scala 29:36A2
_T_166R

br_deallocsrename-freelist.scala 72:60Y27
_T_167-R+B


br_alloc_lists
4


_T_166rename-freelist.scala 72:58O2-
_T_168#R!


_T_167

alloc_masks_0rename-freelist.scala 72:73S21
_T_169'2%



_T_164	

04


_T_168rename-freelist.scala 71:29Kz)
B


br_alloc_lists
4


_T_169rename-freelist.scala 71:23`2G
_T_170=R;
	

1*:(
 B
:


ioren_br_tags
0bitsOneHot.scala 58:35F2$
_T_171R


_T_170
5
5rename-freelist.scala 69:728

_T_1722


rename-freelist.scala 69:27Cz!
B



_T_172
0


_T_171rename-freelist.scala 69:27Z28
_T_173.R,B



_T_172
0B



_T_119
0rename-freelist.scala 69:85<2
_T_174R"


_T_173rename-freelist.scala 70:29:2$
_T_175R


_T_173
0
0Mux.scala 29:36A2
_T_176R

br_deallocsrename-freelist.scala 72:60Y27
_T_177-R+B


br_alloc_lists
5


_T_176rename-freelist.scala 72:58O2-
_T_178#R!


_T_177

alloc_masks_0rename-freelist.scala 72:73S21
_T_179'2%



_T_174	

04


_T_178rename-freelist.scala 71:29Kz)
B


br_alloc_lists
5


_T_179rename-freelist.scala 71:23`2G
_T_180=R;
	

1*:(
 B
:


ioren_br_tags
0bitsOneHot.scala 58:35F2$
_T_181R


_T_180
6
6rename-freelist.scala 69:728

_T_1822


rename-freelist.scala 69:27Cz!
B



_T_182
0


_T_181rename-freelist.scala 69:27Z28
_T_183.R,B



_T_182
0B



_T_119
0rename-freelist.scala 69:85<2
_T_184R"


_T_183rename-freelist.scala 70:29:2$
_T_185R


_T_183
0
0Mux.scala 29:36A2
_T_186R

br_deallocsrename-freelist.scala 72:60Y27
_T_187-R+B


br_alloc_lists
6


_T_186rename-freelist.scala 72:58O2-
_T_188#R!


_T_187

alloc_masks_0rename-freelist.scala 72:73S21
_T_189'2%



_T_184	

04


_T_188rename-freelist.scala 71:29Kz)
B


br_alloc_lists
6


_T_189rename-freelist.scala 71:23`2G
_T_190=R;
	

1*:(
 B
:


ioren_br_tags
0bitsOneHot.scala 58:35F2$
_T_191R


_T_190
7
7rename-freelist.scala 69:728

_T_1922


rename-freelist.scala 69:27Cz!
B



_T_192
0


_T_191rename-freelist.scala 69:27Z28
_T_193.R,B



_T_192
0B



_T_119
0rename-freelist.scala 69:85<2
_T_194R"


_T_193rename-freelist.scala 70:29:2$
_T_195R


_T_193
0
0Mux.scala 29:36A2
_T_196R

br_deallocsrename-freelist.scala 72:60Y27
_T_197-R+B


br_alloc_lists
7


_T_196rename-freelist.scala 72:58O2-
_T_198#R!


_T_197

alloc_masks_0rename-freelist.scala 72:73S21
_T_199'2%



_T_194	

04


_T_198rename-freelist.scala 71:29Kz)
B


br_alloc_lists
7


_T_199rename-freelist.scala 71:23>2
_T_200R


sel_maskrename-freelist.scala 76:29K2)
_T_201R

	free_list


_T_200rename-freelist.scala 76:27N2,
_T_202"R 


_T_201

dealloc_maskrename-freelist.scala 76:39=2
_T_203R	

14rename-freelist.scala 76:57H2&
_T_204R


_T_202


_T_203rename-freelist.scala 76:55=z


	free_list


_T_204rename-freelist.scala 76:13C2!
_T_205R"B


sels
0rename-freelist.scala 80:27U3
_T_206
	

clock"	

reset*	

0rename-freelist.scala 81:26F2-
_T_207#R!B


sels
0
51
32OneHot.scala 30:18E2,
_T_208"R B


sels
0
31
0OneHot.scala 31:1832
_T_209R"


_T_207OneHot.scala 32:14?2&
_T_210R


_T_207


_T_208OneHot.scala 32:28?2&
_T_211R


_T_210
31
16OneHot.scala 30:18>2%
_T_212R


_T_210
15
0OneHot.scala 31:1832
_T_213R"


_T_211OneHot.scala 32:14?2&
_T_214R


_T_211


_T_212OneHot.scala 32:28>2%
_T_215R


_T_214
15
8OneHot.scala 30:18=2$
_T_216R


_T_214
7
0OneHot.scala 31:1832
_T_217R"


_T_215OneHot.scala 32:14?2&
_T_218R


_T_215


_T_216OneHot.scala 32:28=2$
_T_219R


_T_218
7
4OneHot.scala 30:18=2$
_T_220R


_T_218
3
0OneHot.scala 31:1832
_T_221R"


_T_219OneHot.scala 32:14?2&
_T_222R


_T_219


_T_220OneHot.scala 32:28=2$
_T_223R


_T_222
3
2OneHot.scala 30:18=2$
_T_224R


_T_222
1
0OneHot.scala 31:1832
_T_225R"


_T_223OneHot.scala 32:14?2&
_T_226R


_T_223


_T_224OneHot.scala 32:28A2$
_T_227R


_T_226
1
1CircuitMath.scala 30:8<2&
_T_228R


_T_225


_T_227Cat.scala 29:58<2&
_T_229R


_T_221


_T_228Cat.scala 29:58<2&
_T_230R


_T_217


_T_229Cat.scala 29:58<2&
_T_231R


_T_213


_T_230Cat.scala 29:58<2&
_T_232R


_T_209


_T_231Cat.scala 29:58J4
_T_233
	

clock"	

0*


_T_233Reg.scala 15:16]:G
B



sel_fire
0.z



_T_233


_T_232Reg.scala 16:23Reg.scala 16:19X26
_T_234,R*B
:


ioreqs
0	

0rename-freelist.scala 84:27H2&
_T_235R


_T_206


_T_234rename-freelist.scala 84:24H2&
_T_236R


_T_235


_T_205rename-freelist.scala 84:39:z



_T_206


_T_236rename-freelist.scala 84:13I2'
_T_237R


_T_206	

0rename-freelist.scala 85:21W25
_T_238+R)


_T_237B
:


ioreqs
0rename-freelist.scala 85:30H2&
_T_239R


_T_238


_T_205rename-freelist.scala 85:45Ez#
B



sel_fire
0


_T_239rename-freelist.scala 85:17Zz8
*:(
 B
:


ioalloc_pregs
0bits


_T_233rename-freelist.scala 87:29[z9
+:)
 B
:


ioalloc_pregs
0valid


_T_206rename-freelist.scala 88:29`2G
_T_240=R;
	

1*:(
 B
:


ioalloc_pregs
0bitsOneHot.scala 58:35_2E
_T_241;R9+:)
 B
:


ioalloc_pregs
0valid
0
0Bitwise.scala 72:15[2A
_T_242725



_T_241

45035996273704954	

04Bitwise.scala 72:12H2&
_T_243R


_T_240


_T_242rename-freelist.scala 91:77K2)
_T_244R

	free_list


_T_243rename-freelist.scala 91:34Oz-
:
:


iodebugfreelist


_T_244rename-freelist.scala 91:21Pz.
:
:


iodebugisprlist	

0rename-freelist.scala 92:21c2A
_T_2457R5:
:


iodebugfreelist

dealloc_maskrename-freelist.scala 94:31<2
_T_246R"


_T_245rename-freelist.scala 94:47I2'
_T_247R


_T_246	

0rename-freelist.scala 94:11E2#
_T_248R	

reset
0
0rename-freelist.scala 94:10H2&
_T_249R


_T_247


_T_248rename-freelist.scala 94:10I2'
_T_250R


_T_249	

0rename-freelist.scala 94:10ς:Ο



_T_250Rί
ΔAssertion failed: [freelist] Returning a free physical register.
    at rename-freelist.scala:94 assert (!(io.debug.freelist & dealloc_mask).orR, "[freelist] Returning a free physical register.")
	

clock"	

1rename-freelist.scala 94:10<B	

clock	

1rename-freelist.scala 94:10rename-freelist.scala 94:10d2B
_T_2518R6%:#
:


iodebugpipeline_empty	

0rename-freelist.scala 95:11S29
_T_252/R-:
:


iodebugfreelist
0
0Bitwise.scala 49:65S29
_T_253/R-:
:


iodebugfreelist
1
1Bitwise.scala 49:65S29
_T_254/R-:
:


iodebugfreelist
2
2Bitwise.scala 49:65S29
_T_255/R-:
:


iodebugfreelist
3
3Bitwise.scala 49:65S29
_T_256/R-:
:


iodebugfreelist
4
4Bitwise.scala 49:65S29
_T_257/R-:
:


iodebugfreelist
5
5Bitwise.scala 49:65S29
_T_258/R-:
:


iodebugfreelist
6
6Bitwise.scala 49:65S29
_T_259/R-:
:


iodebugfreelist
7
7Bitwise.scala 49:65S29
_T_260/R-:
:


iodebugfreelist
8
8Bitwise.scala 49:65S29
_T_261/R-:
:


iodebugfreelist
9
9Bitwise.scala 49:65U2;
_T_2621R/:
:


iodebugfreelist
10
10Bitwise.scala 49:65U2;
_T_2631R/:
:


iodebugfreelist
11
11Bitwise.scala 49:65U2;
_T_2641R/:
:


iodebugfreelist
12
12Bitwise.scala 49:65U2;
_T_2651R/:
:


iodebugfreelist
13
13Bitwise.scala 49:65U2;
_T_2661R/:
:


iodebugfreelist
14
14Bitwise.scala 49:65U2;
_T_2671R/:
:


iodebugfreelist
15
15Bitwise.scala 49:65U2;
_T_2681R/:
:


iodebugfreelist
16
16Bitwise.scala 49:65U2;
_T_2691R/:
:


iodebugfreelist
17
17Bitwise.scala 49:65U2;
_T_2701R/:
:


iodebugfreelist
18
18Bitwise.scala 49:65U2;
_T_2711R/:
:


iodebugfreelist
19
19Bitwise.scala 49:65U2;
_T_2721R/:
:


iodebugfreelist
20
20Bitwise.scala 49:65U2;
_T_2731R/:
:


iodebugfreelist
21
21Bitwise.scala 49:65U2;
_T_2741R/:
:


iodebugfreelist
22
22Bitwise.scala 49:65U2;
_T_2751R/:
:


iodebugfreelist
23
23Bitwise.scala 49:65U2;
_T_2761R/:
:


iodebugfreelist
24
24Bitwise.scala 49:65U2;
_T_2771R/:
:


iodebugfreelist
25
25Bitwise.scala 49:65U2;
_T_2781R/:
:


iodebugfreelist
26
26Bitwise.scala 49:65U2;
_T_2791R/:
:


iodebugfreelist
27
27Bitwise.scala 49:65U2;
_T_2801R/:
:


iodebugfreelist
28
28Bitwise.scala 49:65U2;
_T_2811R/:
:


iodebugfreelist
29
29Bitwise.scala 49:65U2;
_T_2821R/:
:


iodebugfreelist
30
30Bitwise.scala 49:65U2;
_T_2831R/:
:


iodebugfreelist
31
31Bitwise.scala 49:65U2;
_T_2841R/:
:


iodebugfreelist
32
32Bitwise.scala 49:65U2;
_T_2851R/:
:


iodebugfreelist
33
33Bitwise.scala 49:65U2;
_T_2861R/:
:


iodebugfreelist
34
34Bitwise.scala 49:65U2;
_T_2871R/:
:


iodebugfreelist
35
35Bitwise.scala 49:65U2;
_T_2881R/:
:


iodebugfreelist
36
36Bitwise.scala 49:65U2;
_T_2891R/:
:


iodebugfreelist
37
37Bitwise.scala 49:65U2;
_T_2901R/:
:


iodebugfreelist
38
38Bitwise.scala 49:65U2;
_T_2911R/:
:


iodebugfreelist
39
39Bitwise.scala 49:65U2;
_T_2921R/:
:


iodebugfreelist
40
40Bitwise.scala 49:65U2;
_T_2931R/:
:


iodebugfreelist
41
41Bitwise.scala 49:65U2;
_T_2941R/:
:


iodebugfreelist
42
42Bitwise.scala 49:65U2;
_T_2951R/:
:


iodebugfreelist
43
43Bitwise.scala 49:65U2;
_T_2961R/:
:


iodebugfreelist
44
44Bitwise.scala 49:65U2;
_T_2971R/:
:


iodebugfreelist
45
45Bitwise.scala 49:65U2;
_T_2981R/:
:


iodebugfreelist
46
46Bitwise.scala 49:65U2;
_T_2991R/:
:


iodebugfreelist
47
47Bitwise.scala 49:65U2;
_T_3001R/:
:


iodebugfreelist
48
48Bitwise.scala 49:65U2;
_T_3011R/:
:


iodebugfreelist
49
49Bitwise.scala 49:65U2;
_T_3021R/:
:


iodebugfreelist
50
50Bitwise.scala 49:65U2;
_T_3031R/:
:


iodebugfreelist
51
51Bitwise.scala 49:65@2&
_T_304R


_T_253


_T_254Bitwise.scala 47:55>2$
_T_305R


_T_304
1
0Bitwise.scala 47:55@2&
_T_306R


_T_252


_T_305Bitwise.scala 47:55>2$
_T_307R


_T_306
1
0Bitwise.scala 47:55@2&
_T_308R


_T_256


_T_257Bitwise.scala 47:55>2$
_T_309R


_T_308
1
0Bitwise.scala 47:55@2&
_T_310R


_T_255


_T_309Bitwise.scala 47:55>2$
_T_311R


_T_310
1
0Bitwise.scala 47:55@2&
_T_312R


_T_307


_T_311Bitwise.scala 47:55>2$
_T_313R


_T_312
2
0Bitwise.scala 47:55@2&
_T_314R


_T_259


_T_260Bitwise.scala 47:55>2$
_T_315R


_T_314
1
0Bitwise.scala 47:55@2&
_T_316R


_T_258


_T_315Bitwise.scala 47:55>2$
_T_317R


_T_316
1
0Bitwise.scala 47:55@2&
_T_318R


_T_261


_T_262Bitwise.scala 47:55>2$
_T_319R


_T_318
1
0Bitwise.scala 47:55@2&
_T_320R


_T_263


_T_264Bitwise.scala 47:55>2$
_T_321R


_T_320
1
0Bitwise.scala 47:55@2&
_T_322R


_T_319


_T_321Bitwise.scala 47:55>2$
_T_323R


_T_322
2
0Bitwise.scala 47:55@2&
_T_324R


_T_317


_T_323Bitwise.scala 47:55>2$
_T_325R


_T_324
2
0Bitwise.scala 47:55@2&
_T_326R


_T_313


_T_325Bitwise.scala 47:55>2$
_T_327R


_T_326
3
0Bitwise.scala 47:55@2&
_T_328R


_T_266


_T_267Bitwise.scala 47:55>2$
_T_329R


_T_328
1
0Bitwise.scala 47:55@2&
_T_330R


_T_265


_T_329Bitwise.scala 47:55>2$
_T_331R


_T_330
1
0Bitwise.scala 47:55@2&
_T_332R


_T_269


_T_270Bitwise.scala 47:55>2$
_T_333R


_T_332
1
0Bitwise.scala 47:55@2&
_T_334R


_T_268


_T_333Bitwise.scala 47:55>2$
_T_335R


_T_334
1
0Bitwise.scala 47:55@2&
_T_336R


_T_331


_T_335Bitwise.scala 47:55>2$
_T_337R


_T_336
2
0Bitwise.scala 47:55@2&
_T_338R


_T_272


_T_273Bitwise.scala 47:55>2$
_T_339R


_T_338
1
0Bitwise.scala 47:55@2&
_T_340R


_T_271


_T_339Bitwise.scala 47:55>2$
_T_341R


_T_340
1
0Bitwise.scala 47:55@2&
_T_342R


_T_274


_T_275Bitwise.scala 47:55>2$
_T_343R


_T_342
1
0Bitwise.scala 47:55@2&
_T_344R


_T_276


_T_277Bitwise.scala 47:55>2$
_T_345R


_T_344
1
0Bitwise.scala 47:55@2&
_T_346R


_T_343


_T_345Bitwise.scala 47:55>2$
_T_347R


_T_346
2
0Bitwise.scala 47:55@2&
_T_348R


_T_341


_T_347Bitwise.scala 47:55>2$
_T_349R


_T_348
2
0Bitwise.scala 47:55@2&
_T_350R


_T_337


_T_349Bitwise.scala 47:55>2$
_T_351R


_T_350
3
0Bitwise.scala 47:55@2&
_T_352R


_T_327


_T_351Bitwise.scala 47:55>2$
_T_353R


_T_352
4
0Bitwise.scala 47:55@2&
_T_354R


_T_279


_T_280Bitwise.scala 47:55>2$
_T_355R


_T_354
1
0Bitwise.scala 47:55@2&
_T_356R


_T_278


_T_355Bitwise.scala 47:55>2$
_T_357R


_T_356
1
0Bitwise.scala 47:55@2&
_T_358R


_T_282


_T_283Bitwise.scala 47:55>2$
_T_359R


_T_358
1
0Bitwise.scala 47:55@2&
_T_360R


_T_281


_T_359Bitwise.scala 47:55>2$
_T_361R


_T_360
1
0Bitwise.scala 47:55@2&
_T_362R


_T_357


_T_361Bitwise.scala 47:55>2$
_T_363R


_T_362
2
0Bitwise.scala 47:55@2&
_T_364R


_T_285


_T_286Bitwise.scala 47:55>2$
_T_365R


_T_364
1
0Bitwise.scala 47:55@2&
_T_366R


_T_284


_T_365Bitwise.scala 47:55>2$
_T_367R


_T_366
1
0Bitwise.scala 47:55@2&
_T_368R


_T_287


_T_288Bitwise.scala 47:55>2$
_T_369R


_T_368
1
0Bitwise.scala 47:55@2&
_T_370R


_T_289


_T_290Bitwise.scala 47:55>2$
_T_371R


_T_370
1
0Bitwise.scala 47:55@2&
_T_372R


_T_369


_T_371Bitwise.scala 47:55>2$
_T_373R


_T_372
2
0Bitwise.scala 47:55@2&
_T_374R


_T_367


_T_373Bitwise.scala 47:55>2$
_T_375R


_T_374
2
0Bitwise.scala 47:55@2&
_T_376R


_T_363


_T_375Bitwise.scala 47:55>2$
_T_377R


_T_376
3
0Bitwise.scala 47:55@2&
_T_378R


_T_292


_T_293Bitwise.scala 47:55>2$
_T_379R


_T_378
1
0Bitwise.scala 47:55@2&
_T_380R


_T_291


_T_379Bitwise.scala 47:55>2$
_T_381R


_T_380
1
0Bitwise.scala 47:55@2&
_T_382R


_T_295


_T_296Bitwise.scala 47:55>2$
_T_383R


_T_382
1
0Bitwise.scala 47:55@2&
_T_384R


_T_294


_T_383Bitwise.scala 47:55>2$
_T_385R


_T_384
1
0Bitwise.scala 47:55@2&
_T_386R


_T_381


_T_385Bitwise.scala 47:55>2$
_T_387R


_T_386
2
0Bitwise.scala 47:55@2&
_T_388R


_T_298


_T_299Bitwise.scala 47:55>2$
_T_389R


_T_388
1
0Bitwise.scala 47:55@2&
_T_390R


_T_297


_T_389Bitwise.scala 47:55>2$
_T_391R


_T_390
1
0Bitwise.scala 47:55@2&
_T_392R


_T_300


_T_301Bitwise.scala 47:55>2$
_T_393R


_T_392
1
0Bitwise.scala 47:55@2&
_T_394R


_T_302


_T_303Bitwise.scala 47:55>2$
_T_395R


_T_394
1
0Bitwise.scala 47:55@2&
_T_396R


_T_393


_T_395Bitwise.scala 47:55>2$
_T_397R


_T_396
2
0Bitwise.scala 47:55@2&
_T_398R


_T_391


_T_397Bitwise.scala 47:55>2$
_T_399R


_T_398
2
0Bitwise.scala 47:55@2&
_T_400R


_T_387


_T_399Bitwise.scala 47:55>2$
_T_401R


_T_400
3
0Bitwise.scala 47:55@2&
_T_402R


_T_377


_T_401Bitwise.scala 47:55>2$
_T_403R


_T_402
4
0Bitwise.scala 47:55@2&
_T_404R


_T_353


_T_403Bitwise.scala 47:55>2$
_T_405R


_T_404
5
0Bitwise.scala 47:55J2(
_T_406R


_T_405


20rename-freelist.scala 95:67H2&
_T_407R


_T_251


_T_406rename-freelist.scala 95:36E2#
_T_408R	

reset
0
0rename-freelist.scala 95:10H2&
_T_409R


_T_407


_T_408rename-freelist.scala 95:10I2'
_T_410R


_T_409	

0rename-freelist.scala 95:10η:Δ



_T_410χRΤ
ΉAssertion failed: [freelist] Leaking physical registers.
    at rename-freelist.scala:95 assert (!io.debug.pipeline_empty || PopCount(io.debug.freelist) >= (numPregs - numLregs - 1).U,
	

clock"	

1rename-freelist.scala 95:10<B	

clock	

1rename-freelist.scala 95:10rename-freelist.scala 95:10
RenameFreeList