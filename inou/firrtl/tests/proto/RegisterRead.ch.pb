
��
����
RegisterReadDecode
clock" 
reset
�
io�*�
	iss_valid

�iss_uop�*�
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
	rrd_valid

�rrd_uop�*�
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
�
	

clock�
 �
	

reset�
 �


io�
 nzJ
#:!
:


iorrd_uop
debug_tsrc#:!
:


ioiss_uop
debug_tsrc�func-unit-decode.scala 320:16nzJ
#:!
:


iorrd_uop
debug_fsrc#:!
:


ioiss_uop
debug_fsrc�func-unit-decode.scala 320:16nzJ
#:!
:


iorrd_uop
bp_xcpt_if#:!
:


ioiss_uop
bp_xcpt_if�func-unit-decode.scala 320:16pzL
$:"
:


iorrd_uopbp_debug_if$:"
:


ioiss_uopbp_debug_if�func-unit-decode.scala 320:16nzJ
#:!
:


iorrd_uop
xcpt_ma_if#:!
:


ioiss_uop
xcpt_ma_if�func-unit-decode.scala 320:16nzJ
#:!
:


iorrd_uop
xcpt_ae_if#:!
:


ioiss_uop
xcpt_ae_if�func-unit-decode.scala 320:16nzJ
#:!
:


iorrd_uop
xcpt_pf_if#:!
:


ioiss_uop
xcpt_pf_if�func-unit-decode.scala 320:16lzH
": 
:


iorrd_uop	fp_single": 
:


ioiss_uop	fp_single�func-unit-decode.scala 320:16fzB
:
:


iorrd_uopfp_val:
:


ioiss_uopfp_val�func-unit-decode.scala 320:16hzD
 :
:


iorrd_uopfrs3_en :
:


ioiss_uopfrs3_en�func-unit-decode.scala 320:16nzJ
#:!
:


iorrd_uop
lrs2_rtype#:!
:


ioiss_uop
lrs2_rtype�func-unit-decode.scala 320:16nzJ
#:!
:


iorrd_uop
lrs1_rtype#:!
:


ioiss_uop
lrs1_rtype�func-unit-decode.scala 320:16lzH
": 
:


iorrd_uop	dst_rtype": 
:


ioiss_uop	dst_rtype�func-unit-decode.scala 320:16jzF
!:
:


iorrd_uopldst_val!:
:


ioiss_uopldst_val�func-unit-decode.scala 320:16bz>
:
:


iorrd_uoplrs3:
:


ioiss_uoplrs3�func-unit-decode.scala 320:16bz>
:
:


iorrd_uoplrs2:
:


ioiss_uoplrs2�func-unit-decode.scala 320:16bz>
:
:


iorrd_uoplrs1:
:


ioiss_uoplrs1�func-unit-decode.scala 320:16bz>
:
:


iorrd_uopldst:
:


ioiss_uopldst�func-unit-decode.scala 320:16pzL
$:"
:


iorrd_uopldst_is_rs1$:"
:


ioiss_uopldst_is_rs1�func-unit-decode.scala 320:16xzT
(:&
:


iorrd_uopflush_on_commit(:&
:


ioiss_uopflush_on_commit�func-unit-decode.scala 320:16lzH
": 
:


iorrd_uop	is_unique": 
:


ioiss_uop	is_unique�func-unit-decode.scala 320:16tzP
&:$
:


iorrd_uopis_sys_pc2epc&:$
:


ioiss_uopis_sys_pc2epc�func-unit-decode.scala 320:16jzF
!:
:


iorrd_uopuses_stq!:
:


ioiss_uopuses_stq�func-unit-decode.scala 320:16jzF
!:
:


iorrd_uopuses_ldq!:
:


ioiss_uopuses_ldq�func-unit-decode.scala 320:16fzB
:
:


iorrd_uopis_amo:
:


ioiss_uopis_amo�func-unit-decode.scala 320:16lzH
": 
:


iorrd_uop	is_fencei": 
:


ioiss_uop	is_fencei�func-unit-decode.scala 320:16jzF
!:
:


iorrd_uopis_fence!:
:


ioiss_uopis_fence�func-unit-decode.scala 320:16nzJ
#:!
:


iorrd_uop
mem_signed#:!
:


ioiss_uop
mem_signed�func-unit-decode.scala 320:16jzF
!:
:


iorrd_uopmem_size!:
:


ioiss_uopmem_size�func-unit-decode.scala 320:16hzD
 :
:


iorrd_uopmem_cmd :
:


ioiss_uopmem_cmd�func-unit-decode.scala 320:16nzJ
#:!
:


iorrd_uop
bypassable#:!
:


ioiss_uop
bypassable�func-unit-decode.scala 320:16lzH
": 
:


iorrd_uop	exc_cause": 
:


ioiss_uop	exc_cause�func-unit-decode.scala 320:16lzH
": 
:


iorrd_uop	exception": 
:


ioiss_uop	exception�func-unit-decode.scala 320:16nzJ
#:!
:


iorrd_uop
stale_pdst#:!
:


ioiss_uop
stale_pdst�func-unit-decode.scala 320:16nzJ
#:!
:


iorrd_uop
ppred_busy#:!
:


ioiss_uop
ppred_busy�func-unit-decode.scala 320:16lzH
": 
:


iorrd_uop	prs3_busy": 
:


ioiss_uop	prs3_busy�func-unit-decode.scala 320:16lzH
": 
:


iorrd_uop	prs2_busy": 
:


ioiss_uop	prs2_busy�func-unit-decode.scala 320:16lzH
": 
:


iorrd_uop	prs1_busy": 
:


ioiss_uop	prs1_busy�func-unit-decode.scala 320:16dz@
:
:


iorrd_uopppred:
:


ioiss_uopppred�func-unit-decode.scala 320:16bz>
:
:


iorrd_uopprs3:
:


ioiss_uopprs3�func-unit-decode.scala 320:16bz>
:
:


iorrd_uopprs2:
:


ioiss_uopprs2�func-unit-decode.scala 320:16bz>
:
:


iorrd_uopprs1:
:


ioiss_uopprs1�func-unit-decode.scala 320:16bz>
:
:


iorrd_uoppdst:
:


ioiss_uoppdst�func-unit-decode.scala 320:16hzD
 :
:


iorrd_uoprxq_idx :
:


ioiss_uoprxq_idx�func-unit-decode.scala 320:16hzD
 :
:


iorrd_uopstq_idx :
:


ioiss_uopstq_idx�func-unit-decode.scala 320:16hzD
 :
:


iorrd_uopldq_idx :
:


ioiss_uopldq_idx�func-unit-decode.scala 320:16hzD
 :
:


iorrd_uoprob_idx :
:


ioiss_uoprob_idx�func-unit-decode.scala 320:16jzF
!:
:


iorrd_uopcsr_addr!:
:


ioiss_uopcsr_addr�func-unit-decode.scala 320:16nzJ
#:!
:


iorrd_uop
imm_packed#:!
:


ioiss_uop
imm_packed�func-unit-decode.scala 320:16dz@
:
:


iorrd_uoptaken:
:


ioiss_uoptaken�func-unit-decode.scala 320:16fzB
:
:


iorrd_uoppc_lob:
:


ioiss_uoppc_lob�func-unit-decode.scala 320:16lzH
": 
:


iorrd_uop	edge_inst": 
:


ioiss_uop	edge_inst�func-unit-decode.scala 320:16hzD
 :
:


iorrd_uopftq_idx :
:


ioiss_uopftq_idx�func-unit-decode.scala 320:16fzB
:
:


iorrd_uopbr_tag:
:


ioiss_uopbr_tag�func-unit-decode.scala 320:16hzD
 :
:


iorrd_uopbr_mask :
:


ioiss_uopbr_mask�func-unit-decode.scala 320:16fzB
:
:


iorrd_uopis_sfb:
:


ioiss_uopis_sfb�func-unit-decode.scala 320:16fzB
:
:


iorrd_uopis_jal:
:


ioiss_uopis_jal�func-unit-decode.scala 320:16hzD
 :
:


iorrd_uopis_jalr :
:


ioiss_uopis_jalr�func-unit-decode.scala 320:16dz@
:
:


iorrd_uopis_br:
:


ioiss_uopis_br�func-unit-decode.scala 320:16vzR
':%
:


iorrd_uopiw_p2_poisoned':%
:


ioiss_uopiw_p2_poisoned�func-unit-decode.scala 320:16vzR
':%
:


iorrd_uopiw_p1_poisoned':%
:


ioiss_uopiw_p1_poisoned�func-unit-decode.scala 320:16jzF
!:
:


iorrd_uopiw_state!:
:


ioiss_uopiw_state�func-unit-decode.scala 320:16zzV
):'
:
:


iorrd_uopctrlis_std):'
:
:


ioiss_uopctrlis_std�func-unit-decode.scala 320:16zzV
):'
:
:


iorrd_uopctrlis_sta):'
:
:


ioiss_uopctrlis_sta�func-unit-decode.scala 320:16|zX
*:(
:
:


iorrd_uopctrlis_load*:(
:
:


ioiss_uopctrlis_load�func-unit-decode.scala 320:16|zX
*:(
:
:


iorrd_uopctrlcsr_cmd*:(
:
:


ioiss_uopctrlcsr_cmd�func-unit-decode.scala 320:16zzV
):'
:
:


iorrd_uopctrlfcn_dw):'
:
:


ioiss_uopctrlfcn_dw�func-unit-decode.scala 320:16zzV
):'
:
:


iorrd_uopctrlop_fcn):'
:
:


ioiss_uopctrlop_fcn�func-unit-decode.scala 320:16|zX
*:(
:
:


iorrd_uopctrlimm_sel*:(
:
:


ioiss_uopctrlimm_sel�func-unit-decode.scala 320:16|zX
*:(
:
:


iorrd_uopctrlop2_sel*:(
:
:


ioiss_uopctrlop2_sel�func-unit-decode.scala 320:16|zX
*:(
:
:


iorrd_uopctrlop1_sel*:(
:
:


ioiss_uopctrlop1_sel�func-unit-decode.scala 320:16|zX
*:(
:
:


iorrd_uopctrlbr_type*:(
:
:


ioiss_uopctrlbr_type�func-unit-decode.scala 320:16hzD
 :
:


iorrd_uopfu_code :
:


ioiss_uopfu_code�func-unit-decode.scala 320:16hzD
 :
:


iorrd_uopiq_type :
:


ioiss_uopiq_type�func-unit-decode.scala 320:16jzF
!:
:


iorrd_uopdebug_pc!:
:


ioiss_uopdebug_pc�func-unit-decode.scala 320:16fzB
:
:


iorrd_uopis_rvc:
:


ioiss_uopis_rvc�func-unit-decode.scala 320:16nzJ
#:!
:


iorrd_uop
debug_inst#:!
:


ioiss_uop
debug_inst�func-unit-decode.scala 320:16bz>
:
:


iorrd_uopinst:
:


ioiss_uopinst�func-unit-decode.scala 320:16bz>
:
:


iorrd_uopuopc:
:


ioiss_uopuopc�func-unit-decode.scala 320:16�
�
rrd_cs�*�
br_type

use_alupipe

use_muldivpipe

use_mempipe

op_fcn

fcn_dw

op1_sel

op2_sel

imm_sel

rf_wen

csr_cmd
�func-unit-decode.scala 330:20Q27
_T1R/:
:


iorrd_uopuopc


25�Decode.scala 14:121S29
_T_11R/:
:


iorrd_uopuopc


26�Decode.scala 14:121S29
_T_21R/:
:


iorrd_uopuopc


28�Decode.scala 14:121:2!
_T_3R	

0

_T�Decode.scala 15:3092 
_T_4R

_T_3

_T_1�Decode.scala 15:3092 
_T_5R

_T_4

_T_2�Decode.scala 15:30S2:
_T_62R0:
:


iorrd_uopuopc

125�Decode.scala 14:65>2$
_T_7R

_T_6


24�Decode.scala 14:121S29
_T_81R/:
:


iorrd_uopuopc


29�Decode.scala 14:121<2#
_T_9R	

0

_T_7�Decode.scala 15:30:2!
_T_10R

_T_9

_T_8�Decode.scala 15:30T2:
_T_111R/:
:


iorrd_uopuopc


27�Decode.scala 14:121T2;
_T_122R0:
:


iorrd_uopuopc

126�Decode.scala 14:65@2&
_T_13R	

_T_12


28�Decode.scala 14:121>2%
_T_14R	

0	

_T_11�Decode.scala 15:30<2#
_T_15R	

_T_14	

_T_13�Decode.scala 15:3082"
_T_16R	

_T_10

_T_5�Cat.scala 29:58;2%
_T_17R	

0	

_T_15�Cat.scala 29:5892#
_T_18R	

_T_17	

_T_16�Cat.scala 29:58T2;
_T_192R0:
:


iorrd_uopuopc

125�Decode.scala 14:65A2'
_T_20R	

_T_19

101�Decode.scala 14:121T2;
_T_212R0:
:


iorrd_uopuopc

126�Decode.scala 14:65A2'
_T_22R	

_T_21

102�Decode.scala 14:121U2;
_T_232R0:
:


iorrd_uopuopc

104�Decode.scala 14:121>2%
_T_24R	

0	

_T_20�Decode.scala 15:30<2#
_T_25R	

_T_24	

_T_22�Decode.scala 15:30<2#
_T_26R	

_T_25	

_T_23�Decode.scala 15:3012
_T_27R	

_T_26�Decode.scala 40:35>2%
_T_28R	

0	

_T_20�Decode.scala 15:30<2#
_T_29R	

_T_28	

_T_22�Decode.scala 15:30<2#
_T_30R	

_T_29	

_T_23�Decode.scala 15:30S2:
_T_311R/:
:


iorrd_uopuopc


63�Decode.scala 14:65@2&
_T_32R	

_T_31


11�Decode.scala 14:121S2:
_T_331R/:
:


iorrd_uopuopc


62�Decode.scala 14:65@2&
_T_34R	

_T_33


12�Decode.scala 14:121T2;
_T_352R0:
:


iorrd_uopuopc

126�Decode.scala 14:65@2&
_T_36R	

_T_35


46�Decode.scala 14:121T2;
_T_372R0:
:


iorrd_uopuopc

124�Decode.scala 14:65@2&
_T_38R	

_T_37


48�Decode.scala 14:121S2:
_T_391R/:
:


iorrd_uopuopc


55�Decode.scala 14:65?2%
_T_40R	

_T_39	

6�Decode.scala 14:121S2:
_T_411R/:
:


iorrd_uopuopc


59�Decode.scala 14:65@2&
_T_42R	

_T_41


19�Decode.scala 14:121T2;
_T_432R0:
:


iorrd_uopuopc

111�Decode.scala 14:65?2%
_T_44R	

_T_43	

6�Decode.scala 14:121>2%
_T_45R	

0	

_T_32�Decode.scala 15:30<2#
_T_46R	

_T_45	

_T_34�Decode.scala 15:30<2#
_T_47R	

_T_46	

_T_36�Decode.scala 15:30<2#
_T_48R	

_T_47	

_T_38�Decode.scala 15:30<2#
_T_49R	

_T_48	

_T_40�Decode.scala 15:30<2#
_T_50R	

_T_49	

_T_42�Decode.scala 15:30<2#
_T_51R	

_T_50	

_T_44�Decode.scala 15:30S2:
_T_521R/:
:


iorrd_uopuopc


62�Decode.scala 14:65?2%
_T_53R	

_T_52	

6�Decode.scala 14:121S2:
_T_541R/:
:


iorrd_uopuopc


63�Decode.scala 14:65@2&
_T_55R	

_T_54


10�Decode.scala 14:121S2:
_T_561R/:
:


iorrd_uopuopc


63�Decode.scala 14:65@2&
_T_57R	

_T_56


12�Decode.scala 14:121T2;
_T_582R0:
:


iorrd_uopuopc

121�Decode.scala 14:65@2&
_T_59R	

_T_58


16�Decode.scala 14:121S2:
_T_601R/:
:


iorrd_uopuopc


59�Decode.scala 14:65@2&
_T_61R	

_T_60


25�Decode.scala 14:121T2:
_T_621R/:
:


iorrd_uopuopc


45�Decode.scala 14:121T2;
_T_632R0:
:


iorrd_uopuopc

126�Decode.scala 14:65@2&
_T_64R	

_T_63


48�Decode.scala 14:121S2:
_T_651R/:
:


iorrd_uopuopc


55�Decode.scala 14:65@2&
_T_66R	

_T_65


19�Decode.scala 14:121S2:
_T_671R/:
:


iorrd_uopuopc


55�Decode.scala 14:65@2&
_T_68R	

_T_67


16�Decode.scala 14:121>2%
_T_69R	

0	

_T_53�Decode.scala 15:30<2#
_T_70R	

_T_69	

_T_55�Decode.scala 15:30<2#
_T_71R	

_T_70	

_T_57�Decode.scala 15:30<2#
_T_72R	

_T_71	

_T_59�Decode.scala 15:30<2#
_T_73R	

_T_72	

_T_61�Decode.scala 15:30<2#
_T_74R	

_T_73	

_T_62�Decode.scala 15:30<2#
_T_75R	

_T_74	

_T_64�Decode.scala 15:30<2#
_T_76R	

_T_75	

_T_66�Decode.scala 15:30<2#
_T_77R	

_T_76	

_T_68�Decode.scala 15:30S2:
_T_781R/:
:


iorrd_uopuopc


57�Decode.scala 14:65@2&
_T_79R	

_T_78


17�Decode.scala 14:121S2:
_T_801R/:
:


iorrd_uopuopc


94�Decode.scala 14:65@2&
_T_81R	

_T_80


18�Decode.scala 14:121S2:
_T_821R/:
:


iorrd_uopuopc


54�Decode.scala 14:65@2&
_T_83R	

_T_82


18�Decode.scala 14:121S2:
_T_841R/:
:


iorrd_uopuopc


54�Decode.scala 14:65@2&
_T_85R	

_T_84


20�Decode.scala 14:121S2:
_T_861R/:
:


iorrd_uopuopc


61�Decode.scala 14:65?2%
_T_87R	

_T_86	

8�Decode.scala 14:121S2:
_T_881R/:
:


iorrd_uopuopc


59�Decode.scala 14:65?2%
_T_89R	

_T_88	

9�Decode.scala 14:121>2%
_T_90R	

0	

_T_53�Decode.scala 15:30<2#
_T_91R	

_T_90	

_T_79�Decode.scala 15:30<2#
_T_92R	

_T_91	

_T_81�Decode.scala 15:30<2#
_T_93R	

_T_92	

_T_83�Decode.scala 15:30<2#
_T_94R	

_T_93	

_T_85�Decode.scala 15:30<2#
_T_95R	

_T_94	

_T_87�Decode.scala 15:30<2#
_T_96R	

_T_95	

_T_89�Decode.scala 15:30S2:
_T_971R/:
:


iorrd_uopuopc


47�Decode.scala 14:65?2%
_T_98R	

_T_97	

9�Decode.scala 14:121S2:
_T_991R/:
:


iorrd_uopuopc


47�Decode.scala 14:65A2'
_T_100R	

_T_99


10�Decode.scala 14:121T2;
_T_1011R/:
:


iorrd_uopuopc


47�Decode.scala 14:65B2(
_T_102R


_T_101


12�Decode.scala 14:121T2;
_T_1031R/:
:


iorrd_uopuopc


94�Decode.scala 14:65B2(
_T_104R


_T_103


16�Decode.scala 14:121U2<
_T_1052R0:
:


iorrd_uopuopc

123�Decode.scala 14:65B2(
_T_106R


_T_105


18�Decode.scala 14:121T2;
_T_1071R/:
:


iorrd_uopuopc


60�Decode.scala 14:65B2(
_T_108R


_T_107


24�Decode.scala 14:121T2;
_T_1091R/:
:


iorrd_uopuopc


58�Decode.scala 14:65B2(
_T_110R


_T_109


24�Decode.scala 14:121?2&
_T_111R	

0	

_T_98�Decode.scala 15:30?2&
_T_112R


_T_111


_T_100�Decode.scala 15:30?2&
_T_113R


_T_112


_T_102�Decode.scala 15:30?2&
_T_114R


_T_113


_T_104�Decode.scala 15:30?2&
_T_115R


_T_114


_T_106�Decode.scala 15:30?2&
_T_116R


_T_115


_T_108�Decode.scala 15:30?2&
_T_117R


_T_116


_T_110�Decode.scala 15:30>2%
_T_118R


_T_117	

_T_62�Decode.scala 15:30:2$
_T_119R	

_T_77	

_T_51�Cat.scala 29:58;2%
_T_120R


_T_118	

_T_96�Cat.scala 29:58<2&
_T_121R


_T_120


_T_119�Cat.scala 29:58U2<
_T_1222R0:
:


iorrd_uopuopc

123�Decode.scala 14:65B2(
_T_123R


_T_122


43�Decode.scala 14:121U2<
_T_1242R0:
:


iorrd_uopuopc

124�Decode.scala 14:65B2(
_T_125R


_T_124


44�Decode.scala 14:121@2'
_T_126R	

0


_T_123�Decode.scala 15:30?2&
_T_127R


_T_126


_T_125�Decode.scala 15:30>2%
_T_128R


_T_127	

_T_38�Decode.scala 15:3032
_T_129R


_T_128�Decode.scala 40:35T2;
_T_1301R/:
:


iorrd_uopuopc


27�Decode.scala 14:65A2'
_T_131R


_T_130	

0�Decode.scala 14:121@2'
_T_132R	

0


_T_131�Decode.scala 15:30=2'
_T_133R	

0


_T_132�Cat.scala 29:58T2;
_T_1341R/:
:


iorrd_uopuopc


50�Decode.scala 14:65A2'
_T_135R


_T_134	

0�Decode.scala 14:121T2;
_T_1361R/:
:


iorrd_uopuopc


24�Decode.scala 14:65A2'
_T_137R


_T_136	

0�Decode.scala 14:121T2;
_T_1381R/:
:


iorrd_uopuopc


20�Decode.scala 14:65A2'
_T_139R


_T_138	

0�Decode.scala 14:121T2;
_T_1401R/:
:


iorrd_uopuopc


41�Decode.scala 14:65B2(
_T_141R


_T_140


32�Decode.scala 14:121T2;
_T_1421R/:
:


iorrd_uopuopc


35�Decode.scala 14:65B2(
_T_143R


_T_142


34�Decode.scala 14:121@2'
_T_144R	

0


_T_135�Decode.scala 15:30?2&
_T_145R


_T_144


_T_137�Decode.scala 15:30?2&
_T_146R


_T_145


_T_139�Decode.scala 15:30?2&
_T_147R


_T_146


_T_141�Decode.scala 15:30?2&
_T_148R


_T_147


_T_143�Decode.scala 15:30>2(
_T_149R	

0	

0�Cat.scala 29:58<2&
_T_150R


_T_149


_T_148�Cat.scala 29:58T2;
_T_1511R/:
:


iorrd_uopuopc


43�Decode.scala 14:65A2'
_T_152R


_T_151	

0�Decode.scala 14:121@2'
_T_153R	

0


_T_152�Decode.scala 15:30T2;
_T_1541R/:
:


iorrd_uopuopc


48�Decode.scala 14:65B2(
_T_155R


_T_154


16�Decode.scala 14:121@2'
_T_156R	

0


_T_152�Decode.scala 15:30?2&
_T_157R


_T_156


_T_155�Decode.scala 15:30=2'
_T_158R	

0


_T_157�Cat.scala 29:58<2&
_T_159R


_T_158


_T_153�Cat.scala 29:58T2;
_T_1601R/:
:


iorrd_uopuopc


56�Decode.scala 14:65A2'
_T_161R


_T_160	

8�Decode.scala 14:121T2;
_T_1621R/:
:


iorrd_uopuopc


91�Decode.scala 14:65B2(
_T_163R


_T_162


11�Decode.scala 14:121T2;
_T_1641R/:
:


iorrd_uopuopc


92�Decode.scala 14:65B2(
_T_165R


_T_164


12�Decode.scala 14:121T2;
_T_1661R/:
:


iorrd_uopuopc


92�Decode.scala 14:65B2(
_T_167R


_T_166


16�Decode.scala 14:121U2<
_T_1682R0:
:


iorrd_uopuopc

104�Decode.scala 14:65B2(
_T_169R


_T_168


72�Decode.scala 14:121U2<
_T_1702R0:
:


iorrd_uopuopc

120�Decode.scala 14:65B2(
_T_171R


_T_170


96�Decode.scala 14:121U2<
_T_1722R0:
:


iorrd_uopuopc

108�Decode.scala 14:65A2'
_T_173R


_T_172	

4�Decode.scala 14:121T2;
_T_1741R/:
:


iorrd_uopuopc


54�Decode.scala 14:65A2'
_T_175R


_T_174	

6�Decode.scala 14:121T2;
_T_1761R/:
:


iorrd_uopuopc


58�Decode.scala 14:65B2(
_T_177R


_T_176


16�Decode.scala 14:121T2;
_T_1781R/:
:


iorrd_uopuopc


60�Decode.scala 14:65B2(
_T_179R


_T_178


16�Decode.scala 14:121T2;
_T_1801R/:
:


iorrd_uopuopc


95�Decode.scala 14:65B2(
_T_181R


_T_180


72�Decode.scala 14:121@2'
_T_182R	

0


_T_161�Decode.scala 15:30?2&
_T_183R


_T_182


_T_163�Decode.scala 15:30?2&
_T_184R


_T_183


_T_165�Decode.scala 15:30?2&
_T_185R


_T_184


_T_167�Decode.scala 15:30?2&
_T_186R


_T_185


_T_169�Decode.scala 15:30?2&
_T_187R


_T_186


_T_171�Decode.scala 15:30?2&
_T_188R


_T_187


_T_173�Decode.scala 15:30?2&
_T_189R


_T_188


_T_175�Decode.scala 15:30?2&
_T_190R


_T_189


_T_177�Decode.scala 15:30?2&
_T_191R


_T_190


_T_179�Decode.scala 15:30>2%
_T_192R


_T_191	

_T_79�Decode.scala 15:30?2&
_T_193R


_T_192


_T_181�Decode.scala 15:30>2(
_T_194R	

0	

0�Cat.scala 29:58=2'
_T_195R


_T_194	

0�Cat.scala 29:58Gz$
:



rrd_csbr_type	

_T_18�func-unit-decode.scala 47:42Kz(
:



rrd_csuse_alupipe	

_T_27�func-unit-decode.scala 47:42Nz+
:



rrd_csuse_muldivpipe	

_T_30�func-unit-decode.scala 47:42Mz*
:



rrd_csuse_mempipe	

0�func-unit-decode.scala 47:42Gz$
:



rrd_csop_fcn


_T_121�func-unit-decode.scala 47:42Gz$
:



rrd_csfcn_dw


_T_129�func-unit-decode.scala 47:42Hz%
:



rrd_csop1_sel


_T_133�func-unit-decode.scala 47:42Hz%
:



rrd_csop2_sel


_T_150�func-unit-decode.scala 47:42Hz%
:



rrd_csimm_sel


_T_159�func-unit-decode.scala 47:42Gz$
:



rrd_csrf_wen


_T_193�func-unit-decode.scala 47:42Hz%
:



rrd_cscsr_cmd


_T_195�func-unit-decode.scala 47:42izE
*:(
:
:


iorrd_uopctrlbr_type:



rrd_csbr_type�func-unit-decode.scala 333:27izE
*:(
:
:


iorrd_uopctrlop1_sel:



rrd_csop1_sel�func-unit-decode.scala 334:27izE
*:(
:
:


iorrd_uopctrlop2_sel:



rrd_csop2_sel�func-unit-decode.scala 335:27izE
*:(
:
:


iorrd_uopctrlimm_sel:



rrd_csimm_sel�func-unit-decode.scala 336:27gzC
):'
:
:


iorrd_uopctrlop_fcn:



rrd_csop_fcn�func-unit-decode.scala 337:27T20
_T_196&R$:



rrd_csfcn_dw
0
0�func-unit-decode.scala 338:44[z7
):'
:
:


iorrd_uopctrlfcn_dw


_T_196�func-unit-decode.scala 338:27^2:
_T_1970R.:
:


iorrd_uopuopc	

1�func-unit-decode.scala 339:46\z8
*:(
:
:


iorrd_uopctrlis_load


_T_197�func-unit-decode.scala 339:27^2:
_T_1980R.:
:


iorrd_uopuopc	

2�func-unit-decode.scala 340:46_2;
_T_1991R/:
:


iorrd_uopuopc


67�func-unit-decode.scala 340:76J2&
_T_200R


_T_198


_T_199�func-unit-decode.scala 340:57[z7
):'
:
:


iorrd_uopctrlis_sta


_T_200�func-unit-decode.scala 340:27^2:
_T_2010R.:
:


iorrd_uopuopc	

3�func-unit-decode.scala 341:46e2@
_T_2026R4#:!
:


iorrd_uop
lrs2_rtype	

0� func-unit-decode.scala 341:109i2E
_T_203;R9):'
:
:


iorrd_uopctrlis_sta


_T_202�func-unit-decode.scala 341:84J2&
_T_204R


_T_201


_T_203�func-unit-decode.scala 341:57[z7
):'
:
:


iorrd_uopctrlis_std


_T_204�func-unit-decode.scala 341:27_2;
_T_2051R/:
:


iorrd_uopuopc


67�func-unit-decode.scala 343:25^2:
_T_2060R.:
:


iorrd_uopuopc	

1�func-unit-decode.scala 343:59a2=
_T_2073R1 :
:


iorrd_uopmem_cmd	

6�func-unit-decode.scala 343:91J2&
_T_208R


_T_206


_T_207�func-unit-decode.scala 343:69J2&
_T_209R


_T_205


_T_208�func-unit-decode.scala 343:39�:d



_T_209Vz2
#:!
:


iorrd_uop
imm_packed	

0�func-unit-decode.scala 344:27� func-unit-decode.scala 343:103X24
_T_210*R(:



rrd_cscsr_cmd	

6�func-unit-decode.scala 348:33X24
_T_211*R(:



rrd_cscsr_cmd	

7�func-unit-decode.scala 348:61J2&
_T_212R


_T_210


_T_211�func-unit-decode.scala 348:43^2:
_T_2130R.:
:


iorrd_uopprs1	

0�func-unit-decode.scala 348:82K2'
csr_renR


_T_212


_T_213�func-unit-decode.scala 348:72c2?
_T_214523

	
csr_ren	

2:



rrd_cscsr_cmd�func-unit-decode.scala 349:33\z8
*:(
:
:


iorrd_uopctrlcsr_cmd


_T_214�func-unit-decode.scala 349:27Rz.
:


io	rrd_valid:


io	iss_valid�func-unit-decode.scala 356:16
��
RegisterRead
clock" 
reset
�^
io�^*�^

iss_valids2



�iss_uops�2�
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
=rf_read_ports*2(
$*"
addr

data
A
>prf_read_ports*2(
$*"
addr

data

�bypass�2�
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
A
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

�pred_bypass�2�
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

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

�exe_reqs�2�
�*�
ready

valid

�bits�*�
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
rs1_data
A
rs2_data
A
rs3_data
A
	pred_data

kill

kill

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
 :


rrd_valids2


�register-read.scala 66:30�
�
rrd_uops�2�
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
�register-read.scala 67:302

_T2


�register-read.scala 69:41>z
B


_T
0	

0�register-read.scala 69:41\<
exe_reg_valids2


	

clock"	

reset*

_T�register-read.scala 69:33��
exe_reg_uops�2�
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
	

clock"	

0*

exe_reg_uops�register-read.scala 70:29nN
exe_reg_rs1_data2


A	

clock"	

0*

exe_reg_rs1_data�register-read.scala 71:29nN
exe_reg_rs2_data2


A	

clock"	

0*

exe_reg_rs2_data�register-read.scala 72:29nN
exe_reg_rs3_data2


A	

clock"	

0*

exe_reg_rs3_data�register-read.scala 73:29pP
exe_reg_pred_data2


	

clock"	

0*

exe_reg_pred_data�register-read.scala 74:30H*(
RegisterReadDecodeRegisterReadDecode�register-read.scala 80:335z.
!:


RegisterReadDecodeclock	

clock�
 5z.
!:


RegisterReadDecodereset	

reset�
 pzP
-:+
:


RegisterReadDecodeio	iss_validB
:


io
iss_valids
0�register-read.scala 81:34�zl
;:9
+:)
:


RegisterReadDecodeioiss_uop
debug_tsrc-:+
B
:


ioiss_uops
0
debug_tsrc�register-read.scala 82:34�zl
;:9
+:)
:


RegisterReadDecodeioiss_uop
debug_fsrc-:+
B
:


ioiss_uops
0
debug_fsrc�register-read.scala 82:34�zl
;:9
+:)
:


RegisterReadDecodeioiss_uop
bp_xcpt_if-:+
B
:


ioiss_uops
0
bp_xcpt_if�register-read.scala 82:34�zn
<::
+:)
:


RegisterReadDecodeioiss_uopbp_debug_if.:,
B
:


ioiss_uops
0bp_debug_if�register-read.scala 82:34�zl
;:9
+:)
:


RegisterReadDecodeioiss_uop
xcpt_ma_if-:+
B
:


ioiss_uops
0
xcpt_ma_if�register-read.scala 82:34�zl
;:9
+:)
:


RegisterReadDecodeioiss_uop
xcpt_ae_if-:+
B
:


ioiss_uops
0
xcpt_ae_if�register-read.scala 82:34�zl
;:9
+:)
:


RegisterReadDecodeioiss_uop
xcpt_pf_if-:+
B
:


ioiss_uops
0
xcpt_pf_if�register-read.scala 82:34�zj
::8
+:)
:


RegisterReadDecodeioiss_uop	fp_single,:*
B
:


ioiss_uops
0	fp_single�register-read.scala 82:34�zd
7:5
+:)
:


RegisterReadDecodeioiss_uopfp_val):'
B
:


ioiss_uops
0fp_val�register-read.scala 82:34�zf
8:6
+:)
:


RegisterReadDecodeioiss_uopfrs3_en*:(
B
:


ioiss_uops
0frs3_en�register-read.scala 82:34�zl
;:9
+:)
:


RegisterReadDecodeioiss_uop
lrs2_rtype-:+
B
:


ioiss_uops
0
lrs2_rtype�register-read.scala 82:34�zl
;:9
+:)
:


RegisterReadDecodeioiss_uop
lrs1_rtype-:+
B
:


ioiss_uops
0
lrs1_rtype�register-read.scala 82:34�zj
::8
+:)
:


RegisterReadDecodeioiss_uop	dst_rtype,:*
B
:


ioiss_uops
0	dst_rtype�register-read.scala 82:34�zh
9:7
+:)
:


RegisterReadDecodeioiss_uopldst_val+:)
B
:


ioiss_uops
0ldst_val�register-read.scala 82:34�z`
5:3
+:)
:


RegisterReadDecodeioiss_uoplrs3':%
B
:


ioiss_uops
0lrs3�register-read.scala 82:34�z`
5:3
+:)
:


RegisterReadDecodeioiss_uoplrs2':%
B
:


ioiss_uops
0lrs2�register-read.scala 82:34�z`
5:3
+:)
:


RegisterReadDecodeioiss_uoplrs1':%
B
:


ioiss_uops
0lrs1�register-read.scala 82:34�z`
5:3
+:)
:


RegisterReadDecodeioiss_uopldst':%
B
:


ioiss_uops
0ldst�register-read.scala 82:34�zn
<::
+:)
:


RegisterReadDecodeioiss_uopldst_is_rs1.:,
B
:


ioiss_uops
0ldst_is_rs1�register-read.scala 82:34�zv
@:>
+:)
:


RegisterReadDecodeioiss_uopflush_on_commit2:0
B
:


ioiss_uops
0flush_on_commit�register-read.scala 82:34�zj
::8
+:)
:


RegisterReadDecodeioiss_uop	is_unique,:*
B
:


ioiss_uops
0	is_unique�register-read.scala 82:34�zr
>:<
+:)
:


RegisterReadDecodeioiss_uopis_sys_pc2epc0:.
B
:


ioiss_uops
0is_sys_pc2epc�register-read.scala 82:34�zh
9:7
+:)
:


RegisterReadDecodeioiss_uopuses_stq+:)
B
:


ioiss_uops
0uses_stq�register-read.scala 82:34�zh
9:7
+:)
:


RegisterReadDecodeioiss_uopuses_ldq+:)
B
:


ioiss_uops
0uses_ldq�register-read.scala 82:34�zd
7:5
+:)
:


RegisterReadDecodeioiss_uopis_amo):'
B
:


ioiss_uops
0is_amo�register-read.scala 82:34�zj
::8
+:)
:


RegisterReadDecodeioiss_uop	is_fencei,:*
B
:


ioiss_uops
0	is_fencei�register-read.scala 82:34�zh
9:7
+:)
:


RegisterReadDecodeioiss_uopis_fence+:)
B
:


ioiss_uops
0is_fence�register-read.scala 82:34�zl
;:9
+:)
:


RegisterReadDecodeioiss_uop
mem_signed-:+
B
:


ioiss_uops
0
mem_signed�register-read.scala 82:34�zh
9:7
+:)
:


RegisterReadDecodeioiss_uopmem_size+:)
B
:


ioiss_uops
0mem_size�register-read.scala 82:34�zf
8:6
+:)
:


RegisterReadDecodeioiss_uopmem_cmd*:(
B
:


ioiss_uops
0mem_cmd�register-read.scala 82:34�zl
;:9
+:)
:


RegisterReadDecodeioiss_uop
bypassable-:+
B
:


ioiss_uops
0
bypassable�register-read.scala 82:34�zj
::8
+:)
:


RegisterReadDecodeioiss_uop	exc_cause,:*
B
:


ioiss_uops
0	exc_cause�register-read.scala 82:34�zj
::8
+:)
:


RegisterReadDecodeioiss_uop	exception,:*
B
:


ioiss_uops
0	exception�register-read.scala 82:34�zl
;:9
+:)
:


RegisterReadDecodeioiss_uop
stale_pdst-:+
B
:


ioiss_uops
0
stale_pdst�register-read.scala 82:34�zl
;:9
+:)
:


RegisterReadDecodeioiss_uop
ppred_busy-:+
B
:


ioiss_uops
0
ppred_busy�register-read.scala 82:34�zj
::8
+:)
:


RegisterReadDecodeioiss_uop	prs3_busy,:*
B
:


ioiss_uops
0	prs3_busy�register-read.scala 82:34�zj
::8
+:)
:


RegisterReadDecodeioiss_uop	prs2_busy,:*
B
:


ioiss_uops
0	prs2_busy�register-read.scala 82:34�zj
::8
+:)
:


RegisterReadDecodeioiss_uop	prs1_busy,:*
B
:


ioiss_uops
0	prs1_busy�register-read.scala 82:34�zb
6:4
+:)
:


RegisterReadDecodeioiss_uopppred(:&
B
:


ioiss_uops
0ppred�register-read.scala 82:34�z`
5:3
+:)
:


RegisterReadDecodeioiss_uopprs3':%
B
:


ioiss_uops
0prs3�register-read.scala 82:34�z`
5:3
+:)
:


RegisterReadDecodeioiss_uopprs2':%
B
:


ioiss_uops
0prs2�register-read.scala 82:34�z`
5:3
+:)
:


RegisterReadDecodeioiss_uopprs1':%
B
:


ioiss_uops
0prs1�register-read.scala 82:34�z`
5:3
+:)
:


RegisterReadDecodeioiss_uoppdst':%
B
:


ioiss_uops
0pdst�register-read.scala 82:34�zf
8:6
+:)
:


RegisterReadDecodeioiss_uoprxq_idx*:(
B
:


ioiss_uops
0rxq_idx�register-read.scala 82:34�zf
8:6
+:)
:


RegisterReadDecodeioiss_uopstq_idx*:(
B
:


ioiss_uops
0stq_idx�register-read.scala 82:34�zf
8:6
+:)
:


RegisterReadDecodeioiss_uopldq_idx*:(
B
:


ioiss_uops
0ldq_idx�register-read.scala 82:34�zf
8:6
+:)
:


RegisterReadDecodeioiss_uoprob_idx*:(
B
:


ioiss_uops
0rob_idx�register-read.scala 82:34�zh
9:7
+:)
:


RegisterReadDecodeioiss_uopcsr_addr+:)
B
:


ioiss_uops
0csr_addr�register-read.scala 82:34�zl
;:9
+:)
:


RegisterReadDecodeioiss_uop
imm_packed-:+
B
:


ioiss_uops
0
imm_packed�register-read.scala 82:34�zb
6:4
+:)
:


RegisterReadDecodeioiss_uoptaken(:&
B
:


ioiss_uops
0taken�register-read.scala 82:34�zd
7:5
+:)
:


RegisterReadDecodeioiss_uoppc_lob):'
B
:


ioiss_uops
0pc_lob�register-read.scala 82:34�zj
::8
+:)
:


RegisterReadDecodeioiss_uop	edge_inst,:*
B
:


ioiss_uops
0	edge_inst�register-read.scala 82:34�zf
8:6
+:)
:


RegisterReadDecodeioiss_uopftq_idx*:(
B
:


ioiss_uops
0ftq_idx�register-read.scala 82:34�zd
7:5
+:)
:


RegisterReadDecodeioiss_uopbr_tag):'
B
:


ioiss_uops
0br_tag�register-read.scala 82:34�zf
8:6
+:)
:


RegisterReadDecodeioiss_uopbr_mask*:(
B
:


ioiss_uops
0br_mask�register-read.scala 82:34�zd
7:5
+:)
:


RegisterReadDecodeioiss_uopis_sfb):'
B
:


ioiss_uops
0is_sfb�register-read.scala 82:34�zd
7:5
+:)
:


RegisterReadDecodeioiss_uopis_jal):'
B
:


ioiss_uops
0is_jal�register-read.scala 82:34�zf
8:6
+:)
:


RegisterReadDecodeioiss_uopis_jalr*:(
B
:


ioiss_uops
0is_jalr�register-read.scala 82:34�zb
6:4
+:)
:


RegisterReadDecodeioiss_uopis_br(:&
B
:


ioiss_uops
0is_br�register-read.scala 82:34�zt
?:=
+:)
:


RegisterReadDecodeioiss_uopiw_p2_poisoned1:/
B
:


ioiss_uops
0iw_p2_poisoned�register-read.scala 82:34�zt
?:=
+:)
:


RegisterReadDecodeioiss_uopiw_p1_poisoned1:/
B
:


ioiss_uops
0iw_p1_poisoned�register-read.scala 82:34�zh
9:7
+:)
:


RegisterReadDecodeioiss_uopiw_state+:)
B
:


ioiss_uops
0iw_state�register-read.scala 82:34�zx
A:?
5:3
+:)
:


RegisterReadDecodeioiss_uopctrlis_std3:1
':%
B
:


ioiss_uops
0ctrlis_std�register-read.scala 82:34�zx
A:?
5:3
+:)
:


RegisterReadDecodeioiss_uopctrlis_sta3:1
':%
B
:


ioiss_uops
0ctrlis_sta�register-read.scala 82:34�zz
B:@
5:3
+:)
:


RegisterReadDecodeioiss_uopctrlis_load4:2
':%
B
:


ioiss_uops
0ctrlis_load�register-read.scala 82:34�zz
B:@
5:3
+:)
:


RegisterReadDecodeioiss_uopctrlcsr_cmd4:2
':%
B
:


ioiss_uops
0ctrlcsr_cmd�register-read.scala 82:34�zx
A:?
5:3
+:)
:


RegisterReadDecodeioiss_uopctrlfcn_dw3:1
':%
B
:


ioiss_uops
0ctrlfcn_dw�register-read.scala 82:34�zx
A:?
5:3
+:)
:


RegisterReadDecodeioiss_uopctrlop_fcn3:1
':%
B
:


ioiss_uops
0ctrlop_fcn�register-read.scala 82:34�zz
B:@
5:3
+:)
:


RegisterReadDecodeioiss_uopctrlimm_sel4:2
':%
B
:


ioiss_uops
0ctrlimm_sel�register-read.scala 82:34�zz
B:@
5:3
+:)
:


RegisterReadDecodeioiss_uopctrlop2_sel4:2
':%
B
:


ioiss_uops
0ctrlop2_sel�register-read.scala 82:34�zz
B:@
5:3
+:)
:


RegisterReadDecodeioiss_uopctrlop1_sel4:2
':%
B
:


ioiss_uops
0ctrlop1_sel�register-read.scala 82:34�zz
B:@
5:3
+:)
:


RegisterReadDecodeioiss_uopctrlbr_type4:2
':%
B
:


ioiss_uops
0ctrlbr_type�register-read.scala 82:34�zf
8:6
+:)
:


RegisterReadDecodeioiss_uopfu_code*:(
B
:


ioiss_uops
0fu_code�register-read.scala 82:34�zf
8:6
+:)
:


RegisterReadDecodeioiss_uopiq_type*:(
B
:


ioiss_uops
0iq_type�register-read.scala 82:34�zh
9:7
+:)
:


RegisterReadDecodeioiss_uopdebug_pc+:)
B
:


ioiss_uops
0debug_pc�register-read.scala 82:34�zd
7:5
+:)
:


RegisterReadDecodeioiss_uopis_rvc):'
B
:


ioiss_uops
0is_rvc�register-read.scala 82:34�zl
;:9
+:)
:


RegisterReadDecodeioiss_uop
debug_inst-:+
B
:


ioiss_uops
0
debug_inst�register-read.scala 82:34�z`
5:3
+:)
:


RegisterReadDecodeioiss_uopinst':%
B
:


ioiss_uops
0inst�register-read.scala 82:34�z`
5:3
+:)
:


RegisterReadDecodeioiss_uopuopc':%
B
:


ioiss_uops
0uopc�register-read.scala 82:34�2y
_T_1qRo1:/
:
:


iobrupdateb1mispredict_mask8:6
+:)
:


RegisterReadDecodeiorrd_uopbr_mask�util.scala 118:51;2#
_T_2R

_T_1	

0�util.scala 118:59C2#
_T_3R

_T_2	

0�register-read.scala 85:17e2E
_T_4=R;-:+
:


RegisterReadDecodeio	rrd_valid

_T_3�register-read.scala 84:59P0
_T_5
	

clock"	

0*

_T_5�register-read.scala 84:294z


_T_5

_T_4�register-read.scala 84:29Cz#
B



rrd_valids
0

_T_5�register-read.scala 84:19�
�
_T_6�*�
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
 ^zW
:


_T_6
debug_tsrc;:9
+:)
:


RegisterReadDecodeiorrd_uop
debug_tsrc�
 ^zW
:


_T_6
debug_fsrc;:9
+:)
:


RegisterReadDecodeiorrd_uop
debug_fsrc�
 ^zW
:


_T_6
bp_xcpt_if;:9
+:)
:


RegisterReadDecodeiorrd_uop
bp_xcpt_if�
 `zY
:


_T_6bp_debug_if<::
+:)
:


RegisterReadDecodeiorrd_uopbp_debug_if�
 ^zW
:


_T_6
xcpt_ma_if;:9
+:)
:


RegisterReadDecodeiorrd_uop
xcpt_ma_if�
 ^zW
:


_T_6
xcpt_ae_if;:9
+:)
:


RegisterReadDecodeiorrd_uop
xcpt_ae_if�
 ^zW
:


_T_6
xcpt_pf_if;:9
+:)
:


RegisterReadDecodeiorrd_uop
xcpt_pf_if�
 \zU
:


_T_6	fp_single::8
+:)
:


RegisterReadDecodeiorrd_uop	fp_single�
 VzO
:


_T_6fp_val7:5
+:)
:


RegisterReadDecodeiorrd_uopfp_val�
 XzQ
:


_T_6frs3_en8:6
+:)
:


RegisterReadDecodeiorrd_uopfrs3_en�
 ^zW
:


_T_6
lrs2_rtype;:9
+:)
:


RegisterReadDecodeiorrd_uop
lrs2_rtype�
 ^zW
:


_T_6
lrs1_rtype;:9
+:)
:


RegisterReadDecodeiorrd_uop
lrs1_rtype�
 \zU
:


_T_6	dst_rtype::8
+:)
:


RegisterReadDecodeiorrd_uop	dst_rtype�
 ZzS
:


_T_6ldst_val9:7
+:)
:


RegisterReadDecodeiorrd_uopldst_val�
 RzK
:


_T_6lrs35:3
+:)
:


RegisterReadDecodeiorrd_uoplrs3�
 RzK
:


_T_6lrs25:3
+:)
:


RegisterReadDecodeiorrd_uoplrs2�
 RzK
:


_T_6lrs15:3
+:)
:


RegisterReadDecodeiorrd_uoplrs1�
 RzK
:


_T_6ldst5:3
+:)
:


RegisterReadDecodeiorrd_uopldst�
 `zY
:


_T_6ldst_is_rs1<::
+:)
:


RegisterReadDecodeiorrd_uopldst_is_rs1�
 hza
:


_T_6flush_on_commit@:>
+:)
:


RegisterReadDecodeiorrd_uopflush_on_commit�
 \zU
:


_T_6	is_unique::8
+:)
:


RegisterReadDecodeiorrd_uop	is_unique�
 dz]
:


_T_6is_sys_pc2epc>:<
+:)
:


RegisterReadDecodeiorrd_uopis_sys_pc2epc�
 ZzS
:


_T_6uses_stq9:7
+:)
:


RegisterReadDecodeiorrd_uopuses_stq�
 ZzS
:


_T_6uses_ldq9:7
+:)
:


RegisterReadDecodeiorrd_uopuses_ldq�
 VzO
:


_T_6is_amo7:5
+:)
:


RegisterReadDecodeiorrd_uopis_amo�
 \zU
:


_T_6	is_fencei::8
+:)
:


RegisterReadDecodeiorrd_uop	is_fencei�
 ZzS
:


_T_6is_fence9:7
+:)
:


RegisterReadDecodeiorrd_uopis_fence�
 ^zW
:


_T_6
mem_signed;:9
+:)
:


RegisterReadDecodeiorrd_uop
mem_signed�
 ZzS
:


_T_6mem_size9:7
+:)
:


RegisterReadDecodeiorrd_uopmem_size�
 XzQ
:


_T_6mem_cmd8:6
+:)
:


RegisterReadDecodeiorrd_uopmem_cmd�
 ^zW
:


_T_6
bypassable;:9
+:)
:


RegisterReadDecodeiorrd_uop
bypassable�
 \zU
:


_T_6	exc_cause::8
+:)
:


RegisterReadDecodeiorrd_uop	exc_cause�
 \zU
:


_T_6	exception::8
+:)
:


RegisterReadDecodeiorrd_uop	exception�
 ^zW
:


_T_6
stale_pdst;:9
+:)
:


RegisterReadDecodeiorrd_uop
stale_pdst�
 ^zW
:


_T_6
ppred_busy;:9
+:)
:


RegisterReadDecodeiorrd_uop
ppred_busy�
 \zU
:


_T_6	prs3_busy::8
+:)
:


RegisterReadDecodeiorrd_uop	prs3_busy�
 \zU
:


_T_6	prs2_busy::8
+:)
:


RegisterReadDecodeiorrd_uop	prs2_busy�
 \zU
:


_T_6	prs1_busy::8
+:)
:


RegisterReadDecodeiorrd_uop	prs1_busy�
 TzM
:


_T_6ppred6:4
+:)
:


RegisterReadDecodeiorrd_uopppred�
 RzK
:


_T_6prs35:3
+:)
:


RegisterReadDecodeiorrd_uopprs3�
 RzK
:


_T_6prs25:3
+:)
:


RegisterReadDecodeiorrd_uopprs2�
 RzK
:


_T_6prs15:3
+:)
:


RegisterReadDecodeiorrd_uopprs1�
 RzK
:


_T_6pdst5:3
+:)
:


RegisterReadDecodeiorrd_uoppdst�
 XzQ
:


_T_6rxq_idx8:6
+:)
:


RegisterReadDecodeiorrd_uoprxq_idx�
 XzQ
:


_T_6stq_idx8:6
+:)
:


RegisterReadDecodeiorrd_uopstq_idx�
 XzQ
:


_T_6ldq_idx8:6
+:)
:


RegisterReadDecodeiorrd_uopldq_idx�
 XzQ
:


_T_6rob_idx8:6
+:)
:


RegisterReadDecodeiorrd_uoprob_idx�
 ZzS
:


_T_6csr_addr9:7
+:)
:


RegisterReadDecodeiorrd_uopcsr_addr�
 ^zW
:


_T_6
imm_packed;:9
+:)
:


RegisterReadDecodeiorrd_uop
imm_packed�
 TzM
:


_T_6taken6:4
+:)
:


RegisterReadDecodeiorrd_uoptaken�
 VzO
:


_T_6pc_lob7:5
+:)
:


RegisterReadDecodeiorrd_uoppc_lob�
 \zU
:


_T_6	edge_inst::8
+:)
:


RegisterReadDecodeiorrd_uop	edge_inst�
 XzQ
:


_T_6ftq_idx8:6
+:)
:


RegisterReadDecodeiorrd_uopftq_idx�
 VzO
:


_T_6br_tag7:5
+:)
:


RegisterReadDecodeiorrd_uopbr_tag�
 XzQ
:


_T_6br_mask8:6
+:)
:


RegisterReadDecodeiorrd_uopbr_mask�
 VzO
:


_T_6is_sfb7:5
+:)
:


RegisterReadDecodeiorrd_uopis_sfb�
 VzO
:


_T_6is_jal7:5
+:)
:


RegisterReadDecodeiorrd_uopis_jal�
 XzQ
:


_T_6is_jalr8:6
+:)
:


RegisterReadDecodeiorrd_uopis_jalr�
 TzM
:


_T_6is_br6:4
+:)
:


RegisterReadDecodeiorrd_uopis_br�
 fz_
:


_T_6iw_p2_poisoned?:=
+:)
:


RegisterReadDecodeiorrd_uopiw_p2_poisoned�
 fz_
:


_T_6iw_p1_poisoned?:=
+:)
:


RegisterReadDecodeiorrd_uopiw_p1_poisoned�
 ZzS
:


_T_6iw_state9:7
+:)
:


RegisterReadDecodeiorrd_uopiw_state�
 jzc
:
:


_T_6ctrlis_stdA:?
5:3
+:)
:


RegisterReadDecodeiorrd_uopctrlis_std�
 jzc
:
:


_T_6ctrlis_staA:?
5:3
+:)
:


RegisterReadDecodeiorrd_uopctrlis_sta�
 lze
:
:


_T_6ctrlis_loadB:@
5:3
+:)
:


RegisterReadDecodeiorrd_uopctrlis_load�
 lze
:
:


_T_6ctrlcsr_cmdB:@
5:3
+:)
:


RegisterReadDecodeiorrd_uopctrlcsr_cmd�
 jzc
:
:


_T_6ctrlfcn_dwA:?
5:3
+:)
:


RegisterReadDecodeiorrd_uopctrlfcn_dw�
 jzc
:
:


_T_6ctrlop_fcnA:?
5:3
+:)
:


RegisterReadDecodeiorrd_uopctrlop_fcn�
 lze
:
:


_T_6ctrlimm_selB:@
5:3
+:)
:


RegisterReadDecodeiorrd_uopctrlimm_sel�
 lze
:
:


_T_6ctrlop2_selB:@
5:3
+:)
:


RegisterReadDecodeiorrd_uopctrlop2_sel�
 lze
:
:


_T_6ctrlop1_selB:@
5:3
+:)
:


RegisterReadDecodeiorrd_uopctrlop1_sel�
 lze
:
:


_T_6ctrlbr_typeB:@
5:3
+:)
:


RegisterReadDecodeiorrd_uopctrlbr_type�
 XzQ
:


_T_6fu_code8:6
+:)
:


RegisterReadDecodeiorrd_uopfu_code�
 XzQ
:


_T_6iq_type8:6
+:)
:


RegisterReadDecodeiorrd_uopiq_type�
 ZzS
:


_T_6debug_pc9:7
+:)
:


RegisterReadDecodeiorrd_uopdebug_pc�
 VzO
:


_T_6is_rvc7:5
+:)
:


RegisterReadDecodeiorrd_uopis_rvc�
 ^zW
:


_T_6
debug_inst;:9
+:)
:


RegisterReadDecodeiorrd_uop
debug_inst�
 RzK
:


_T_6inst5:3
+:)
:


RegisterReadDecodeiorrd_uopinst�
 RzK
:


_T_6uopc5:3
+:)
:


RegisterReadDecodeiorrd_uopuopc�
 S2<
_T_74R2.:,
:
:


iobrupdateb1resolve_mask�util.scala 74:37g2P
_T_8HRF8:6
+:)
:


RegisterReadDecodeiorrd_uopbr_mask

_T_7�util.scala 74:358z!
:


_T_6br_mask

_T_8�util.scala 74:20��
_T_9�*�
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
_T_9�register-read.scala 86:29Tz4
:


_T_9
debug_tsrc:


_T_6
debug_tsrc�register-read.scala 86:29Tz4
:


_T_9
debug_fsrc:


_T_6
debug_fsrc�register-read.scala 86:29Tz4
:


_T_9
bp_xcpt_if:


_T_6
bp_xcpt_if�register-read.scala 86:29Vz6
:


_T_9bp_debug_if:


_T_6bp_debug_if�register-read.scala 86:29Tz4
:


_T_9
xcpt_ma_if:


_T_6
xcpt_ma_if�register-read.scala 86:29Tz4
:


_T_9
xcpt_ae_if:


_T_6
xcpt_ae_if�register-read.scala 86:29Tz4
:


_T_9
xcpt_pf_if:


_T_6
xcpt_pf_if�register-read.scala 86:29Rz2
:


_T_9	fp_single:


_T_6	fp_single�register-read.scala 86:29Lz,
:


_T_9fp_val:


_T_6fp_val�register-read.scala 86:29Nz.
:


_T_9frs3_en:


_T_6frs3_en�register-read.scala 86:29Tz4
:


_T_9
lrs2_rtype:


_T_6
lrs2_rtype�register-read.scala 86:29Tz4
:


_T_9
lrs1_rtype:


_T_6
lrs1_rtype�register-read.scala 86:29Rz2
:


_T_9	dst_rtype:


_T_6	dst_rtype�register-read.scala 86:29Pz0
:


_T_9ldst_val:


_T_6ldst_val�register-read.scala 86:29Hz(
:


_T_9lrs3:


_T_6lrs3�register-read.scala 86:29Hz(
:


_T_9lrs2:


_T_6lrs2�register-read.scala 86:29Hz(
:


_T_9lrs1:


_T_6lrs1�register-read.scala 86:29Hz(
:


_T_9ldst:


_T_6ldst�register-read.scala 86:29Vz6
:


_T_9ldst_is_rs1:


_T_6ldst_is_rs1�register-read.scala 86:29^z>
:


_T_9flush_on_commit:


_T_6flush_on_commit�register-read.scala 86:29Rz2
:


_T_9	is_unique:


_T_6	is_unique�register-read.scala 86:29Zz:
:


_T_9is_sys_pc2epc:


_T_6is_sys_pc2epc�register-read.scala 86:29Pz0
:


_T_9uses_stq:


_T_6uses_stq�register-read.scala 86:29Pz0
:


_T_9uses_ldq:


_T_6uses_ldq�register-read.scala 86:29Lz,
:


_T_9is_amo:


_T_6is_amo�register-read.scala 86:29Rz2
:


_T_9	is_fencei:


_T_6	is_fencei�register-read.scala 86:29Pz0
:


_T_9is_fence:


_T_6is_fence�register-read.scala 86:29Tz4
:


_T_9
mem_signed:


_T_6
mem_signed�register-read.scala 86:29Pz0
:


_T_9mem_size:


_T_6mem_size�register-read.scala 86:29Nz.
:


_T_9mem_cmd:


_T_6mem_cmd�register-read.scala 86:29Tz4
:


_T_9
bypassable:


_T_6
bypassable�register-read.scala 86:29Rz2
:


_T_9	exc_cause:


_T_6	exc_cause�register-read.scala 86:29Rz2
:


_T_9	exception:


_T_6	exception�register-read.scala 86:29Tz4
:


_T_9
stale_pdst:


_T_6
stale_pdst�register-read.scala 86:29Tz4
:


_T_9
ppred_busy:


_T_6
ppred_busy�register-read.scala 86:29Rz2
:


_T_9	prs3_busy:


_T_6	prs3_busy�register-read.scala 86:29Rz2
:


_T_9	prs2_busy:


_T_6	prs2_busy�register-read.scala 86:29Rz2
:


_T_9	prs1_busy:


_T_6	prs1_busy�register-read.scala 86:29Jz*
:


_T_9ppred:


_T_6ppred�register-read.scala 86:29Hz(
:


_T_9prs3:


_T_6prs3�register-read.scala 86:29Hz(
:


_T_9prs2:


_T_6prs2�register-read.scala 86:29Hz(
:


_T_9prs1:


_T_6prs1�register-read.scala 86:29Hz(
:


_T_9pdst:


_T_6pdst�register-read.scala 86:29Nz.
:


_T_9rxq_idx:


_T_6rxq_idx�register-read.scala 86:29Nz.
:


_T_9stq_idx:


_T_6stq_idx�register-read.scala 86:29Nz.
:


_T_9ldq_idx:


_T_6ldq_idx�register-read.scala 86:29Nz.
:


_T_9rob_idx:


_T_6rob_idx�register-read.scala 86:29Pz0
:


_T_9csr_addr:


_T_6csr_addr�register-read.scala 86:29Tz4
:


_T_9
imm_packed:


_T_6
imm_packed�register-read.scala 86:29Jz*
:


_T_9taken:


_T_6taken�register-read.scala 86:29Lz,
:


_T_9pc_lob:


_T_6pc_lob�register-read.scala 86:29Rz2
:


_T_9	edge_inst:


_T_6	edge_inst�register-read.scala 86:29Nz.
:


_T_9ftq_idx:


_T_6ftq_idx�register-read.scala 86:29Lz,
:


_T_9br_tag:


_T_6br_tag�register-read.scala 86:29Nz.
:


_T_9br_mask:


_T_6br_mask�register-read.scala 86:29Lz,
:


_T_9is_sfb:


_T_6is_sfb�register-read.scala 86:29Lz,
:


_T_9is_jal:


_T_6is_jal�register-read.scala 86:29Nz.
:


_T_9is_jalr:


_T_6is_jalr�register-read.scala 86:29Jz*
:


_T_9is_br:


_T_6is_br�register-read.scala 86:29\z<
:


_T_9iw_p2_poisoned:


_T_6iw_p2_poisoned�register-read.scala 86:29\z<
:


_T_9iw_p1_poisoned:


_T_6iw_p1_poisoned�register-read.scala 86:29Pz0
:


_T_9iw_state:


_T_6iw_state�register-read.scala 86:29`z@
:
:


_T_9ctrlis_std:
:


_T_6ctrlis_std�register-read.scala 86:29`z@
:
:


_T_9ctrlis_sta:
:


_T_6ctrlis_sta�register-read.scala 86:29bzB
:
:


_T_9ctrlis_load:
:


_T_6ctrlis_load�register-read.scala 86:29bzB
:
:


_T_9ctrlcsr_cmd:
:


_T_6ctrlcsr_cmd�register-read.scala 86:29`z@
:
:


_T_9ctrlfcn_dw:
:


_T_6ctrlfcn_dw�register-read.scala 86:29`z@
:
:


_T_9ctrlop_fcn:
:


_T_6ctrlop_fcn�register-read.scala 86:29bzB
:
:


_T_9ctrlimm_sel:
:


_T_6ctrlimm_sel�register-read.scala 86:29bzB
:
:


_T_9ctrlop2_sel:
:


_T_6ctrlop2_sel�register-read.scala 86:29bzB
:
:


_T_9ctrlop1_sel:
:


_T_6ctrlop1_sel�register-read.scala 86:29bzB
:
:


_T_9ctrlbr_type:
:


_T_6ctrlbr_type�register-read.scala 86:29Nz.
:


_T_9fu_code:


_T_6fu_code�register-read.scala 86:29Nz.
:


_T_9iq_type:


_T_6iq_type�register-read.scala 86:29Pz0
:


_T_9debug_pc:


_T_6debug_pc�register-read.scala 86:29Lz,
:


_T_9is_rvc:


_T_6is_rvc�register-read.scala 86:29Tz4
:


_T_9
debug_inst:


_T_6
debug_inst�register-read.scala 86:29Hz(
:


_T_9inst:


_T_6inst�register-read.scala 86:29Hz(
:


_T_9uopc:


_T_6uopc�register-read.scala 86:29azA
%:#
B



rrd_uops
0
debug_tsrc:


_T_9
debug_tsrc�register-read.scala 86:19azA
%:#
B



rrd_uops
0
debug_fsrc:


_T_9
debug_fsrc�register-read.scala 86:19azA
%:#
B



rrd_uops
0
bp_xcpt_if:


_T_9
bp_xcpt_if�register-read.scala 86:19czC
&:$
B



rrd_uops
0bp_debug_if:


_T_9bp_debug_if�register-read.scala 86:19azA
%:#
B



rrd_uops
0
xcpt_ma_if:


_T_9
xcpt_ma_if�register-read.scala 86:19azA
%:#
B



rrd_uops
0
xcpt_ae_if:


_T_9
xcpt_ae_if�register-read.scala 86:19azA
%:#
B



rrd_uops
0
xcpt_pf_if:


_T_9
xcpt_pf_if�register-read.scala 86:19_z?
$:"
B



rrd_uops
0	fp_single:


_T_9	fp_single�register-read.scala 86:19Yz9
!:
B



rrd_uops
0fp_val:


_T_9fp_val�register-read.scala 86:19[z;
": 
B



rrd_uops
0frs3_en:


_T_9frs3_en�register-read.scala 86:19azA
%:#
B



rrd_uops
0
lrs2_rtype:


_T_9
lrs2_rtype�register-read.scala 86:19azA
%:#
B



rrd_uops
0
lrs1_rtype:


_T_9
lrs1_rtype�register-read.scala 86:19_z?
$:"
B



rrd_uops
0	dst_rtype:


_T_9	dst_rtype�register-read.scala 86:19]z=
#:!
B



rrd_uops
0ldst_val:


_T_9ldst_val�register-read.scala 86:19Uz5
:
B



rrd_uops
0lrs3:


_T_9lrs3�register-read.scala 86:19Uz5
:
B



rrd_uops
0lrs2:


_T_9lrs2�register-read.scala 86:19Uz5
:
B



rrd_uops
0lrs1:


_T_9lrs1�register-read.scala 86:19Uz5
:
B



rrd_uops
0ldst:


_T_9ldst�register-read.scala 86:19czC
&:$
B



rrd_uops
0ldst_is_rs1:


_T_9ldst_is_rs1�register-read.scala 86:19kzK
*:(
B



rrd_uops
0flush_on_commit:


_T_9flush_on_commit�register-read.scala 86:19_z?
$:"
B



rrd_uops
0	is_unique:


_T_9	is_unique�register-read.scala 86:19gzG
(:&
B



rrd_uops
0is_sys_pc2epc:


_T_9is_sys_pc2epc�register-read.scala 86:19]z=
#:!
B



rrd_uops
0uses_stq:


_T_9uses_stq�register-read.scala 86:19]z=
#:!
B



rrd_uops
0uses_ldq:


_T_9uses_ldq�register-read.scala 86:19Yz9
!:
B



rrd_uops
0is_amo:


_T_9is_amo�register-read.scala 86:19_z?
$:"
B



rrd_uops
0	is_fencei:


_T_9	is_fencei�register-read.scala 86:19]z=
#:!
B



rrd_uops
0is_fence:


_T_9is_fence�register-read.scala 86:19azA
%:#
B



rrd_uops
0
mem_signed:


_T_9
mem_signed�register-read.scala 86:19]z=
#:!
B



rrd_uops
0mem_size:


_T_9mem_size�register-read.scala 86:19[z;
": 
B



rrd_uops
0mem_cmd:


_T_9mem_cmd�register-read.scala 86:19azA
%:#
B



rrd_uops
0
bypassable:


_T_9
bypassable�register-read.scala 86:19_z?
$:"
B



rrd_uops
0	exc_cause:


_T_9	exc_cause�register-read.scala 86:19_z?
$:"
B



rrd_uops
0	exception:


_T_9	exception�register-read.scala 86:19azA
%:#
B



rrd_uops
0
stale_pdst:


_T_9
stale_pdst�register-read.scala 86:19azA
%:#
B



rrd_uops
0
ppred_busy:


_T_9
ppred_busy�register-read.scala 86:19_z?
$:"
B



rrd_uops
0	prs3_busy:


_T_9	prs3_busy�register-read.scala 86:19_z?
$:"
B



rrd_uops
0	prs2_busy:


_T_9	prs2_busy�register-read.scala 86:19_z?
$:"
B



rrd_uops
0	prs1_busy:


_T_9	prs1_busy�register-read.scala 86:19Wz7
 :
B



rrd_uops
0ppred:


_T_9ppred�register-read.scala 86:19Uz5
:
B



rrd_uops
0prs3:


_T_9prs3�register-read.scala 86:19Uz5
:
B



rrd_uops
0prs2:


_T_9prs2�register-read.scala 86:19Uz5
:
B



rrd_uops
0prs1:


_T_9prs1�register-read.scala 86:19Uz5
:
B



rrd_uops
0pdst:


_T_9pdst�register-read.scala 86:19[z;
": 
B



rrd_uops
0rxq_idx:


_T_9rxq_idx�register-read.scala 86:19[z;
": 
B



rrd_uops
0stq_idx:


_T_9stq_idx�register-read.scala 86:19[z;
": 
B



rrd_uops
0ldq_idx:


_T_9ldq_idx�register-read.scala 86:19[z;
": 
B



rrd_uops
0rob_idx:


_T_9rob_idx�register-read.scala 86:19]z=
#:!
B



rrd_uops
0csr_addr:


_T_9csr_addr�register-read.scala 86:19azA
%:#
B



rrd_uops
0
imm_packed:


_T_9
imm_packed�register-read.scala 86:19Wz7
 :
B



rrd_uops
0taken:


_T_9taken�register-read.scala 86:19Yz9
!:
B



rrd_uops
0pc_lob:


_T_9pc_lob�register-read.scala 86:19_z?
$:"
B



rrd_uops
0	edge_inst:


_T_9	edge_inst�register-read.scala 86:19[z;
": 
B



rrd_uops
0ftq_idx:


_T_9ftq_idx�register-read.scala 86:19Yz9
!:
B



rrd_uops
0br_tag:


_T_9br_tag�register-read.scala 86:19[z;
": 
B



rrd_uops
0br_mask:


_T_9br_mask�register-read.scala 86:19Yz9
!:
B



rrd_uops
0is_sfb:


_T_9is_sfb�register-read.scala 86:19Yz9
!:
B



rrd_uops
0is_jal:


_T_9is_jal�register-read.scala 86:19[z;
": 
B



rrd_uops
0is_jalr:


_T_9is_jalr�register-read.scala 86:19Wz7
 :
B



rrd_uops
0is_br:


_T_9is_br�register-read.scala 86:19izI
):'
B



rrd_uops
0iw_p2_poisoned:


_T_9iw_p2_poisoned�register-read.scala 86:19izI
):'
B



rrd_uops
0iw_p1_poisoned:


_T_9iw_p1_poisoned�register-read.scala 86:19]z=
#:!
B



rrd_uops
0iw_state:


_T_9iw_state�register-read.scala 86:19mzM
+:)
:
B



rrd_uops
0ctrlis_std:
:


_T_9ctrlis_std�register-read.scala 86:19mzM
+:)
:
B



rrd_uops
0ctrlis_sta:
:


_T_9ctrlis_sta�register-read.scala 86:19ozO
,:*
:
B



rrd_uops
0ctrlis_load:
:


_T_9ctrlis_load�register-read.scala 86:19ozO
,:*
:
B



rrd_uops
0ctrlcsr_cmd:
:


_T_9ctrlcsr_cmd�register-read.scala 86:19mzM
+:)
:
B



rrd_uops
0ctrlfcn_dw:
:


_T_9ctrlfcn_dw�register-read.scala 86:19mzM
+:)
:
B



rrd_uops
0ctrlop_fcn:
:


_T_9ctrlop_fcn�register-read.scala 86:19ozO
,:*
:
B



rrd_uops
0ctrlimm_sel:
:


_T_9ctrlimm_sel�register-read.scala 86:19ozO
,:*
:
B



rrd_uops
0ctrlop2_sel:
:


_T_9ctrlop2_sel�register-read.scala 86:19ozO
,:*
:
B



rrd_uops
0ctrlop1_sel:
:


_T_9ctrlop1_sel�register-read.scala 86:19ozO
,:*
:
B



rrd_uops
0ctrlbr_type:
:


_T_9ctrlbr_type�register-read.scala 86:19[z;
": 
B



rrd_uops
0fu_code:


_T_9fu_code�register-read.scala 86:19[z;
": 
B



rrd_uops
0iq_type:


_T_9iq_type�register-read.scala 86:19]z=
#:!
B



rrd_uops
0debug_pc:


_T_9debug_pc�register-read.scala 86:19Yz9
!:
B



rrd_uops
0is_rvc:


_T_9is_rvc�register-read.scala 86:19azA
%:#
B



rrd_uops
0
debug_inst:


_T_9
debug_inst�register-read.scala 86:19Uz5
:
B



rrd_uops
0inst:


_T_9inst�register-read.scala 86:19Uz5
:
B



rrd_uops
0uopc:


_T_9uopc�register-read.scala 86:19<

rrd_rs1_data2


A�register-read.scala 94:28<

rrd_rs2_data2


A�register-read.scala 95:28<

rrd_rs3_data2


A�register-read.scala 96:28=

rrd_pred_data2


�register-read.scala 97:28<�
B


rrd_rs1_data
0�register-read.scala 98:16<�
B


rrd_rs2_data
0�register-read.scala 99:16=�
B


rrd_rs3_data
0�register-read.scala 100:16>�
B


rrd_pred_data
0�register-read.scala 101:17Q�/
-:+
#B!
:


ioprf_read_ports
0data�register-read.scala 103:21Q�/
-:+
#B!
:


ioprf_read_ports
0addr�register-read.scala 103:21xzW
,:*
"B 
:


iorf_read_ports
0addr':%
B
:


ioiss_uops
0prs1�register-read.scala 118:56xzW
,:*
"B 
:


iorf_read_ports
1addr':%
B
:


ioiss_uops
0prs2�register-read.scala 119:56xzW
,:*
"B 
:


iorf_read_ports
2addr':%
B
:


ioiss_uops
0prs3�register-read.scala 120:56d2C
_T_10:R8':%
B
:


ioiss_uops
0prs1	

0�register-read.scala 124:67S2
_T_11
	

clock"	

0*	

_T_11�register-read.scala 124:577z
	

_T_11	

_T_10�register-read.scala 124:57r2Q
_T_12H2F
	

_T_11	

0,:*
"B 
:


iorf_read_ports
0data�register-read.scala 124:49Gz&
B


rrd_rs1_data
0	

_T_12�register-read.scala 124:43d2C
_T_13:R8':%
B
:


ioiss_uops
0prs2	

0�register-read.scala 125:67S2
_T_14
	

clock"	

0*	

_T_14�register-read.scala 125:577z
	

_T_14	

_T_13�register-read.scala 125:57r2Q
_T_15H2F
	

_T_14	

0,:*
"B 
:


iorf_read_ports
1data�register-read.scala 125:49Gz&
B


rrd_rs2_data
0	

_T_15�register-read.scala 125:43d2C
_T_16:R8':%
B
:


ioiss_uops
0prs3	

0�register-read.scala 126:67S2
_T_17
	

clock"	

0*	

_T_17�register-read.scala 126:577z
	

_T_17	

_T_16�register-read.scala 126:57r2Q
_T_18H2F
	

_T_17	

0,:*
"B 
:


iorf_read_ports
2data�register-read.scala 126:49Gz&
B


rrd_rs3_data
0	

_T_18�register-read.scala 126:43|2d
_T_19[RY1:/
:
:


iobrupdateb1mispredict_mask": 
B



rrd_uops
0br_mask�util.scala 118:51=2%
_T_20R	

_T_19	

0�util.scala 118:59K2*
_T_21!R:


iokill	

_T_20�register-read.scala 130:28]2<
_T_22321
	

_T_21	

0B



rrd_valids
0�register-read.scala 132:29Iz(
B


exe_reg_valids
0	

_T_22�register-read.scala 132:23�
�
_T_23�*�
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
�consts.scala 269:196�
:
	

_T_23
debug_tsrc�consts.scala 270:206�
:
	

_T_23
debug_fsrc�consts.scala 270:206�
:
	

_T_23
bp_xcpt_if�consts.scala 270:207�
:
	

_T_23bp_debug_if�consts.scala 270:206�
:
	

_T_23
xcpt_ma_if�consts.scala 270:206�
:
	

_T_23
xcpt_ae_if�consts.scala 270:206�
:
	

_T_23
xcpt_pf_if�consts.scala 270:205�
:
	

_T_23	fp_single�consts.scala 270:202�
:
	

_T_23fp_val�consts.scala 270:203�
:
	

_T_23frs3_en�consts.scala 270:206�
:
	

_T_23
lrs2_rtype�consts.scala 270:206�
:
	

_T_23
lrs1_rtype�consts.scala 270:205�
:
	

_T_23	dst_rtype�consts.scala 270:204�
:
	

_T_23ldst_val�consts.scala 270:200�
:
	

_T_23lrs3�consts.scala 270:200�
:
	

_T_23lrs2�consts.scala 270:200�
:
	

_T_23lrs1�consts.scala 270:200�
:
	

_T_23ldst�consts.scala 270:207�
:
	

_T_23ldst_is_rs1�consts.scala 270:20;� 
:
	

_T_23flush_on_commit�consts.scala 270:205�
:
	

_T_23	is_unique�consts.scala 270:209�
:
	

_T_23is_sys_pc2epc�consts.scala 270:204�
:
	

_T_23uses_stq�consts.scala 270:204�
:
	

_T_23uses_ldq�consts.scala 270:202�
:
	

_T_23is_amo�consts.scala 270:205�
:
	

_T_23	is_fencei�consts.scala 270:204�
:
	

_T_23is_fence�consts.scala 270:206�
:
	

_T_23
mem_signed�consts.scala 270:204�
:
	

_T_23mem_size�consts.scala 270:203�
:
	

_T_23mem_cmd�consts.scala 270:206�
:
	

_T_23
bypassable�consts.scala 270:205�
:
	

_T_23	exc_cause�consts.scala 270:205�
:
	

_T_23	exception�consts.scala 270:206�
:
	

_T_23
stale_pdst�consts.scala 270:206�
:
	

_T_23
ppred_busy�consts.scala 270:205�
:
	

_T_23	prs3_busy�consts.scala 270:205�
:
	

_T_23	prs2_busy�consts.scala 270:205�
:
	

_T_23	prs1_busy�consts.scala 270:201�
:
	

_T_23ppred�consts.scala 270:200�
:
	

_T_23prs3�consts.scala 270:200�
:
	

_T_23prs2�consts.scala 270:200�
:
	

_T_23prs1�consts.scala 270:200�
:
	

_T_23pdst�consts.scala 270:203�
:
	

_T_23rxq_idx�consts.scala 270:203�
:
	

_T_23stq_idx�consts.scala 270:203�
:
	

_T_23ldq_idx�consts.scala 270:203�
:
	

_T_23rob_idx�consts.scala 270:204�
:
	

_T_23csr_addr�consts.scala 270:206�
:
	

_T_23
imm_packed�consts.scala 270:201�
:
	

_T_23taken�consts.scala 270:202�
:
	

_T_23pc_lob�consts.scala 270:205�
:
	

_T_23	edge_inst�consts.scala 270:203�
:
	

_T_23ftq_idx�consts.scala 270:202�
:
	

_T_23br_tag�consts.scala 270:203�
:
	

_T_23br_mask�consts.scala 270:202�
:
	

_T_23is_sfb�consts.scala 270:202�
:
	

_T_23is_jal�consts.scala 270:203�
:
	

_T_23is_jalr�consts.scala 270:201�
:
	

_T_23is_br�consts.scala 270:20:�
:
	

_T_23iw_p2_poisoned�consts.scala 270:20:�
:
	

_T_23iw_p1_poisoned�consts.scala 270:204�
:
	

_T_23iw_state�consts.scala 270:20<�!
:
:
	

_T_23ctrlis_std�consts.scala 270:20<�!
:
:
	

_T_23ctrlis_sta�consts.scala 270:20=�"
 :
:
	

_T_23ctrlis_load�consts.scala 270:20=�"
 :
:
	

_T_23ctrlcsr_cmd�consts.scala 270:20<�!
:
:
	

_T_23ctrlfcn_dw�consts.scala 270:20<�!
:
:
	

_T_23ctrlop_fcn�consts.scala 270:20=�"
 :
:
	

_T_23ctrlimm_sel�consts.scala 270:20=�"
 :
:
	

_T_23ctrlop2_sel�consts.scala 270:20=�"
 :
:
	

_T_23ctrlop1_sel�consts.scala 270:20=�"
 :
:
	

_T_23ctrlbr_type�consts.scala 270:203�
:
	

_T_23fu_code�consts.scala 270:203�
:
	

_T_23iq_type�consts.scala 270:204�
:
	

_T_23debug_pc�consts.scala 270:202�
:
	

_T_23is_rvc�consts.scala 270:206�
:
	

_T_23
debug_inst�consts.scala 270:200�
:
	

_T_23inst�consts.scala 270:200�
:
	

_T_23uopc�consts.scala 270:20<z"
:
	

_T_23uopc	

0�consts.scala 271:20Bz(
:
	

_T_23
bypassable	

0�consts.scala 272:20>z$
:
	

_T_23fp_val	

0�consts.scala 273:20@z&
:
	

_T_23uses_stq	

0�consts.scala 274:20@z&
:
	

_T_23uses_ldq	

0�consts.scala 275:20<z"
:
	

_T_23pdst	

0�consts.scala 276:20Az'
:
	

_T_23	dst_rtype	

2�consts.scala 277:20�
�
_T_24�*�
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
�consts.scala 279:182�
:
	

_T_24is_std�consts.scala 280:202�
:
	

_T_24is_sta�consts.scala 280:203�
:
	

_T_24is_load�consts.scala 280:203�
:
	

_T_24csr_cmd�consts.scala 280:202�
:
	

_T_24fcn_dw�consts.scala 280:202�
:
	

_T_24op_fcn�consts.scala 280:203�
:
	

_T_24imm_sel�consts.scala 280:203�
:
	

_T_24op2_sel�consts.scala 280:203�
:
	

_T_24op1_sel�consts.scala 280:203�
:
	

_T_24br_type�consts.scala 280:20?z%
:
	

_T_24br_type	

0�consts.scala 281:20?z%
:
	

_T_24csr_cmd	

0�consts.scala 282:20?z%
:
	

_T_24is_load	

0�consts.scala 283:20>z$
:
	

_T_24is_sta	

0�consts.scala 284:20>z$
:
	

_T_24is_std	

0�consts.scala 285:20Rz8
:
:
	

_T_23ctrlis_std:
	

_T_24is_std�consts.scala 287:14Rz8
:
:
	

_T_23ctrlis_sta:
	

_T_24is_sta�consts.scala 287:14Tz:
 :
:
	

_T_23ctrlis_load:
	

_T_24is_load�consts.scala 287:14Tz:
 :
:
	

_T_23ctrlcsr_cmd:
	

_T_24csr_cmd�consts.scala 287:14Rz8
:
:
	

_T_23ctrlfcn_dw:
	

_T_24fcn_dw�consts.scala 287:14Rz8
:
:
	

_T_23ctrlop_fcn:
	

_T_24op_fcn�consts.scala 287:14Tz:
 :
:
	

_T_23ctrlimm_sel:
	

_T_24imm_sel�consts.scala 287:14Tz:
 :
:
	

_T_23ctrlop2_sel:
	

_T_24op2_sel�consts.scala 287:14Tz:
 :
:
	

_T_23ctrlop1_sel:
	

_T_24op1_sel�consts.scala 287:14Tz:
 :
:
	

_T_23ctrlbr_type:
	

_T_24br_type�consts.scala 287:14Y28
_T_25/2-
	

_T_21	

_T_23B



rrd_uops
0�register-read.scala 134:29gzF
):'
B


exe_reg_uops
0
debug_tsrc:
	

_T_25
debug_tsrc�register-read.scala 134:23gzF
):'
B


exe_reg_uops
0
debug_fsrc:
	

_T_25
debug_fsrc�register-read.scala 134:23gzF
):'
B


exe_reg_uops
0
bp_xcpt_if:
	

_T_25
bp_xcpt_if�register-read.scala 134:23izH
*:(
B


exe_reg_uops
0bp_debug_if:
	

_T_25bp_debug_if�register-read.scala 134:23gzF
):'
B


exe_reg_uops
0
xcpt_ma_if:
	

_T_25
xcpt_ma_if�register-read.scala 134:23gzF
):'
B


exe_reg_uops
0
xcpt_ae_if:
	

_T_25
xcpt_ae_if�register-read.scala 134:23gzF
):'
B


exe_reg_uops
0
xcpt_pf_if:
	

_T_25
xcpt_pf_if�register-read.scala 134:23ezD
(:&
B


exe_reg_uops
0	fp_single:
	

_T_25	fp_single�register-read.scala 134:23_z>
%:#
B


exe_reg_uops
0fp_val:
	

_T_25fp_val�register-read.scala 134:23az@
&:$
B


exe_reg_uops
0frs3_en:
	

_T_25frs3_en�register-read.scala 134:23gzF
):'
B


exe_reg_uops
0
lrs2_rtype:
	

_T_25
lrs2_rtype�register-read.scala 134:23gzF
):'
B


exe_reg_uops
0
lrs1_rtype:
	

_T_25
lrs1_rtype�register-read.scala 134:23ezD
(:&
B


exe_reg_uops
0	dst_rtype:
	

_T_25	dst_rtype�register-read.scala 134:23czB
':%
B


exe_reg_uops
0ldst_val:
	

_T_25ldst_val�register-read.scala 134:23[z:
#:!
B


exe_reg_uops
0lrs3:
	

_T_25lrs3�register-read.scala 134:23[z:
#:!
B


exe_reg_uops
0lrs2:
	

_T_25lrs2�register-read.scala 134:23[z:
#:!
B


exe_reg_uops
0lrs1:
	

_T_25lrs1�register-read.scala 134:23[z:
#:!
B


exe_reg_uops
0ldst:
	

_T_25ldst�register-read.scala 134:23izH
*:(
B


exe_reg_uops
0ldst_is_rs1:
	

_T_25ldst_is_rs1�register-read.scala 134:23qzP
.:,
B


exe_reg_uops
0flush_on_commit:
	

_T_25flush_on_commit�register-read.scala 134:23ezD
(:&
B


exe_reg_uops
0	is_unique:
	

_T_25	is_unique�register-read.scala 134:23mzL
,:*
B


exe_reg_uops
0is_sys_pc2epc:
	

_T_25is_sys_pc2epc�register-read.scala 134:23czB
':%
B


exe_reg_uops
0uses_stq:
	

_T_25uses_stq�register-read.scala 134:23czB
':%
B


exe_reg_uops
0uses_ldq:
	

_T_25uses_ldq�register-read.scala 134:23_z>
%:#
B


exe_reg_uops
0is_amo:
	

_T_25is_amo�register-read.scala 134:23ezD
(:&
B


exe_reg_uops
0	is_fencei:
	

_T_25	is_fencei�register-read.scala 134:23czB
':%
B


exe_reg_uops
0is_fence:
	

_T_25is_fence�register-read.scala 134:23gzF
):'
B


exe_reg_uops
0
mem_signed:
	

_T_25
mem_signed�register-read.scala 134:23czB
':%
B


exe_reg_uops
0mem_size:
	

_T_25mem_size�register-read.scala 134:23az@
&:$
B


exe_reg_uops
0mem_cmd:
	

_T_25mem_cmd�register-read.scala 134:23gzF
):'
B


exe_reg_uops
0
bypassable:
	

_T_25
bypassable�register-read.scala 134:23ezD
(:&
B


exe_reg_uops
0	exc_cause:
	

_T_25	exc_cause�register-read.scala 134:23ezD
(:&
B


exe_reg_uops
0	exception:
	

_T_25	exception�register-read.scala 134:23gzF
):'
B


exe_reg_uops
0
stale_pdst:
	

_T_25
stale_pdst�register-read.scala 134:23gzF
):'
B


exe_reg_uops
0
ppred_busy:
	

_T_25
ppred_busy�register-read.scala 134:23ezD
(:&
B


exe_reg_uops
0	prs3_busy:
	

_T_25	prs3_busy�register-read.scala 134:23ezD
(:&
B


exe_reg_uops
0	prs2_busy:
	

_T_25	prs2_busy�register-read.scala 134:23ezD
(:&
B


exe_reg_uops
0	prs1_busy:
	

_T_25	prs1_busy�register-read.scala 134:23]z<
$:"
B


exe_reg_uops
0ppred:
	

_T_25ppred�register-read.scala 134:23[z:
#:!
B


exe_reg_uops
0prs3:
	

_T_25prs3�register-read.scala 134:23[z:
#:!
B


exe_reg_uops
0prs2:
	

_T_25prs2�register-read.scala 134:23[z:
#:!
B


exe_reg_uops
0prs1:
	

_T_25prs1�register-read.scala 134:23[z:
#:!
B


exe_reg_uops
0pdst:
	

_T_25pdst�register-read.scala 134:23az@
&:$
B


exe_reg_uops
0rxq_idx:
	

_T_25rxq_idx�register-read.scala 134:23az@
&:$
B


exe_reg_uops
0stq_idx:
	

_T_25stq_idx�register-read.scala 134:23az@
&:$
B


exe_reg_uops
0ldq_idx:
	

_T_25ldq_idx�register-read.scala 134:23az@
&:$
B


exe_reg_uops
0rob_idx:
	

_T_25rob_idx�register-read.scala 134:23czB
':%
B


exe_reg_uops
0csr_addr:
	

_T_25csr_addr�register-read.scala 134:23gzF
):'
B


exe_reg_uops
0
imm_packed:
	

_T_25
imm_packed�register-read.scala 134:23]z<
$:"
B


exe_reg_uops
0taken:
	

_T_25taken�register-read.scala 134:23_z>
%:#
B


exe_reg_uops
0pc_lob:
	

_T_25pc_lob�register-read.scala 134:23ezD
(:&
B


exe_reg_uops
0	edge_inst:
	

_T_25	edge_inst�register-read.scala 134:23az@
&:$
B


exe_reg_uops
0ftq_idx:
	

_T_25ftq_idx�register-read.scala 134:23_z>
%:#
B


exe_reg_uops
0br_tag:
	

_T_25br_tag�register-read.scala 134:23az@
&:$
B


exe_reg_uops
0br_mask:
	

_T_25br_mask�register-read.scala 134:23_z>
%:#
B


exe_reg_uops
0is_sfb:
	

_T_25is_sfb�register-read.scala 134:23_z>
%:#
B


exe_reg_uops
0is_jal:
	

_T_25is_jal�register-read.scala 134:23az@
&:$
B


exe_reg_uops
0is_jalr:
	

_T_25is_jalr�register-read.scala 134:23]z<
$:"
B


exe_reg_uops
0is_br:
	

_T_25is_br�register-read.scala 134:23ozN
-:+
B


exe_reg_uops
0iw_p2_poisoned:
	

_T_25iw_p2_poisoned�register-read.scala 134:23ozN
-:+
B


exe_reg_uops
0iw_p1_poisoned:
	

_T_25iw_p1_poisoned�register-read.scala 134:23czB
':%
B


exe_reg_uops
0iw_state:
	

_T_25iw_state�register-read.scala 134:23szR
/:-
#:!
B


exe_reg_uops
0ctrlis_std:
:
	

_T_25ctrlis_std�register-read.scala 134:23szR
/:-
#:!
B


exe_reg_uops
0ctrlis_sta:
:
	

_T_25ctrlis_sta�register-read.scala 134:23uzT
0:.
#:!
B


exe_reg_uops
0ctrlis_load :
:
	

_T_25ctrlis_load�register-read.scala 134:23uzT
0:.
#:!
B


exe_reg_uops
0ctrlcsr_cmd :
:
	

_T_25ctrlcsr_cmd�register-read.scala 134:23szR
/:-
#:!
B


exe_reg_uops
0ctrlfcn_dw:
:
	

_T_25ctrlfcn_dw�register-read.scala 134:23szR
/:-
#:!
B


exe_reg_uops
0ctrlop_fcn:
:
	

_T_25ctrlop_fcn�register-read.scala 134:23uzT
0:.
#:!
B


exe_reg_uops
0ctrlimm_sel :
:
	

_T_25ctrlimm_sel�register-read.scala 134:23uzT
0:.
#:!
B


exe_reg_uops
0ctrlop2_sel :
:
	

_T_25ctrlop2_sel�register-read.scala 134:23uzT
0:.
#:!
B


exe_reg_uops
0ctrlop1_sel :
:
	

_T_25ctrlop1_sel�register-read.scala 134:23uzT
0:.
#:!
B


exe_reg_uops
0ctrlbr_type :
:
	

_T_25ctrlbr_type�register-read.scala 134:23az@
&:$
B


exe_reg_uops
0fu_code:
	

_T_25fu_code�register-read.scala 134:23az@
&:$
B


exe_reg_uops
0iq_type:
	

_T_25iq_type�register-read.scala 134:23czB
':%
B


exe_reg_uops
0debug_pc:
	

_T_25debug_pc�register-read.scala 134:23_z>
%:#
B


exe_reg_uops
0is_rvc:
	

_T_25is_rvc�register-read.scala 134:23gzF
):'
B


exe_reg_uops
0
debug_inst:
	

_T_25
debug_inst�register-read.scala 134:23[z:
#:!
B


exe_reg_uops
0inst:
	

_T_25inst�register-read.scala 134:23[z:
#:!
B


exe_reg_uops
0uopc:
	

_T_25uopc�register-read.scala 134:23T2=
_T_264R2.:,
:
:


iobrupdateb1resolve_mask�util.scala 85:27S2<
_T_273R1": 
B



rrd_uops
0br_mask	

_T_26�util.scala 85:25Tz3
&:$
B


exe_reg_uops
0br_mask	

_T_27�register-read.scala 136:29B
!
bypassed_rs1_data2


A�register-read.scala 152:31B
!
bypassed_rs2_data2


A�register-read.scala 153:31C
"
bypassed_pred_data2


�register-read.scala 154:32C�!
B


bypassed_pred_data
0�register-read.scala 155:22V2@
_T_28725
	

0	

0AB


rrd_rs1_data
0�Mux.scala 98:16Lz+
B


bypassed_rs1_data
0	

_T_28�register-read.scala 185:49V2@
_T_29725
	

0	

0AB


rrd_rs2_data
0�Mux.scala 98:16Lz+
B


bypassed_rs2_data
0	

_T_29�register-read.scala 186:49`z?
B


exe_reg_rs1_data
0B


bypassed_rs1_data
0�register-read.scala 198:47`z?
B


exe_reg_rs2_data
0B


bypassed_rs2_data
0�register-read.scala 199:47[z:
B


exe_reg_rs3_data
0B


rrd_rs3_data
0�register-read.scala 200:47hzG
(:&
B
:


ioexe_reqs
0validB


exe_reg_valids
0�register-read.scala 212:29�zm
@:>
0:.
':%
B
:


ioexe_reqs
0bitsuop
debug_tsrc):'
B


exe_reg_uops
0
debug_tsrc�register-read.scala 213:29�zm
@:>
0:.
':%
B
:


ioexe_reqs
0bitsuop
debug_fsrc):'
B


exe_reg_uops
0
debug_fsrc�register-read.scala 213:29�zm
@:>
0:.
':%
B
:


ioexe_reqs
0bitsuop
bp_xcpt_if):'
B


exe_reg_uops
0
bp_xcpt_if�register-read.scala 213:29�zo
A:?
0:.
':%
B
:


ioexe_reqs
0bitsuopbp_debug_if*:(
B


exe_reg_uops
0bp_debug_if�register-read.scala 213:29�zm
@:>
0:.
':%
B
:


ioexe_reqs
0bitsuop
xcpt_ma_if):'
B


exe_reg_uops
0
xcpt_ma_if�register-read.scala 213:29�zm
@:>
0:.
':%
B
:


ioexe_reqs
0bitsuop
xcpt_ae_if):'
B


exe_reg_uops
0
xcpt_ae_if�register-read.scala 213:29�zm
@:>
0:.
':%
B
:


ioexe_reqs
0bitsuop
xcpt_pf_if):'
B


exe_reg_uops
0
xcpt_pf_if�register-read.scala 213:29�zk
?:=
0:.
':%
B
:


ioexe_reqs
0bitsuop	fp_single(:&
B


exe_reg_uops
0	fp_single�register-read.scala 213:29�ze
<::
0:.
':%
B
:


ioexe_reqs
0bitsuopfp_val%:#
B


exe_reg_uops
0fp_val�register-read.scala 213:29�zg
=:;
0:.
':%
B
:


ioexe_reqs
0bitsuopfrs3_en&:$
B


exe_reg_uops
0frs3_en�register-read.scala 213:29�zm
@:>
0:.
':%
B
:


ioexe_reqs
0bitsuop
lrs2_rtype):'
B


exe_reg_uops
0
lrs2_rtype�register-read.scala 213:29�zm
@:>
0:.
':%
B
:


ioexe_reqs
0bitsuop
lrs1_rtype):'
B


exe_reg_uops
0
lrs1_rtype�register-read.scala 213:29�zk
?:=
0:.
':%
B
:


ioexe_reqs
0bitsuop	dst_rtype(:&
B


exe_reg_uops
0	dst_rtype�register-read.scala 213:29�zi
>:<
0:.
':%
B
:


ioexe_reqs
0bitsuopldst_val':%
B


exe_reg_uops
0ldst_val�register-read.scala 213:29�za
::8
0:.
':%
B
:


ioexe_reqs
0bitsuoplrs3#:!
B


exe_reg_uops
0lrs3�register-read.scala 213:29�za
::8
0:.
':%
B
:


ioexe_reqs
0bitsuoplrs2#:!
B


exe_reg_uops
0lrs2�register-read.scala 213:29�za
::8
0:.
':%
B
:


ioexe_reqs
0bitsuoplrs1#:!
B


exe_reg_uops
0lrs1�register-read.scala 213:29�za
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopldst#:!
B


exe_reg_uops
0ldst�register-read.scala 213:29�zo
A:?
0:.
':%
B
:


ioexe_reqs
0bitsuopldst_is_rs1*:(
B


exe_reg_uops
0ldst_is_rs1�register-read.scala 213:29�zw
E:C
0:.
':%
B
:


ioexe_reqs
0bitsuopflush_on_commit.:,
B


exe_reg_uops
0flush_on_commit�register-read.scala 213:29�zk
?:=
0:.
':%
B
:


ioexe_reqs
0bitsuop	is_unique(:&
B


exe_reg_uops
0	is_unique�register-read.scala 213:29�zs
C:A
0:.
':%
B
:


ioexe_reqs
0bitsuopis_sys_pc2epc,:*
B


exe_reg_uops
0is_sys_pc2epc�register-read.scala 213:29�zi
>:<
0:.
':%
B
:


ioexe_reqs
0bitsuopuses_stq':%
B


exe_reg_uops
0uses_stq�register-read.scala 213:29�zi
>:<
0:.
':%
B
:


ioexe_reqs
0bitsuopuses_ldq':%
B


exe_reg_uops
0uses_ldq�register-read.scala 213:29�ze
<::
0:.
':%
B
:


ioexe_reqs
0bitsuopis_amo%:#
B


exe_reg_uops
0is_amo�register-read.scala 213:29�zk
?:=
0:.
':%
B
:


ioexe_reqs
0bitsuop	is_fencei(:&
B


exe_reg_uops
0	is_fencei�register-read.scala 213:29�zi
>:<
0:.
':%
B
:


ioexe_reqs
0bitsuopis_fence':%
B


exe_reg_uops
0is_fence�register-read.scala 213:29�zm
@:>
0:.
':%
B
:


ioexe_reqs
0bitsuop
mem_signed):'
B


exe_reg_uops
0
mem_signed�register-read.scala 213:29�zi
>:<
0:.
':%
B
:


ioexe_reqs
0bitsuopmem_size':%
B


exe_reg_uops
0mem_size�register-read.scala 213:29�zg
=:;
0:.
':%
B
:


ioexe_reqs
0bitsuopmem_cmd&:$
B


exe_reg_uops
0mem_cmd�register-read.scala 213:29�zm
@:>
0:.
':%
B
:


ioexe_reqs
0bitsuop
bypassable):'
B


exe_reg_uops
0
bypassable�register-read.scala 213:29�zk
?:=
0:.
':%
B
:


ioexe_reqs
0bitsuop	exc_cause(:&
B


exe_reg_uops
0	exc_cause�register-read.scala 213:29�zk
?:=
0:.
':%
B
:


ioexe_reqs
0bitsuop	exception(:&
B


exe_reg_uops
0	exception�register-read.scala 213:29�zm
@:>
0:.
':%
B
:


ioexe_reqs
0bitsuop
stale_pdst):'
B


exe_reg_uops
0
stale_pdst�register-read.scala 213:29�zm
@:>
0:.
':%
B
:


ioexe_reqs
0bitsuop
ppred_busy):'
B


exe_reg_uops
0
ppred_busy�register-read.scala 213:29�zk
?:=
0:.
':%
B
:


ioexe_reqs
0bitsuop	prs3_busy(:&
B


exe_reg_uops
0	prs3_busy�register-read.scala 213:29�zk
?:=
0:.
':%
B
:


ioexe_reqs
0bitsuop	prs2_busy(:&
B


exe_reg_uops
0	prs2_busy�register-read.scala 213:29�zk
?:=
0:.
':%
B
:


ioexe_reqs
0bitsuop	prs1_busy(:&
B


exe_reg_uops
0	prs1_busy�register-read.scala 213:29�zc
;:9
0:.
':%
B
:


ioexe_reqs
0bitsuopppred$:"
B


exe_reg_uops
0ppred�register-read.scala 213:29�za
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopprs3#:!
B


exe_reg_uops
0prs3�register-read.scala 213:29�za
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopprs2#:!
B


exe_reg_uops
0prs2�register-read.scala 213:29�za
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopprs1#:!
B


exe_reg_uops
0prs1�register-read.scala 213:29�za
::8
0:.
':%
B
:


ioexe_reqs
0bitsuoppdst#:!
B


exe_reg_uops
0pdst�register-read.scala 213:29�zg
=:;
0:.
':%
B
:


ioexe_reqs
0bitsuoprxq_idx&:$
B


exe_reg_uops
0rxq_idx�register-read.scala 213:29�zg
=:;
0:.
':%
B
:


ioexe_reqs
0bitsuopstq_idx&:$
B


exe_reg_uops
0stq_idx�register-read.scala 213:29�zg
=:;
0:.
':%
B
:


ioexe_reqs
0bitsuopldq_idx&:$
B


exe_reg_uops
0ldq_idx�register-read.scala 213:29�zg
=:;
0:.
':%
B
:


ioexe_reqs
0bitsuoprob_idx&:$
B


exe_reg_uops
0rob_idx�register-read.scala 213:29�zi
>:<
0:.
':%
B
:


ioexe_reqs
0bitsuopcsr_addr':%
B


exe_reg_uops
0csr_addr�register-read.scala 213:29�zm
@:>
0:.
':%
B
:


ioexe_reqs
0bitsuop
imm_packed):'
B


exe_reg_uops
0
imm_packed�register-read.scala 213:29�zc
;:9
0:.
':%
B
:


ioexe_reqs
0bitsuoptaken$:"
B


exe_reg_uops
0taken�register-read.scala 213:29�ze
<::
0:.
':%
B
:


ioexe_reqs
0bitsuoppc_lob%:#
B


exe_reg_uops
0pc_lob�register-read.scala 213:29�zk
?:=
0:.
':%
B
:


ioexe_reqs
0bitsuop	edge_inst(:&
B


exe_reg_uops
0	edge_inst�register-read.scala 213:29�zg
=:;
0:.
':%
B
:


ioexe_reqs
0bitsuopftq_idx&:$
B


exe_reg_uops
0ftq_idx�register-read.scala 213:29�ze
<::
0:.
':%
B
:


ioexe_reqs
0bitsuopbr_tag%:#
B


exe_reg_uops
0br_tag�register-read.scala 213:29�zg
=:;
0:.
':%
B
:


ioexe_reqs
0bitsuopbr_mask&:$
B


exe_reg_uops
0br_mask�register-read.scala 213:29�ze
<::
0:.
':%
B
:


ioexe_reqs
0bitsuopis_sfb%:#
B


exe_reg_uops
0is_sfb�register-read.scala 213:29�ze
<::
0:.
':%
B
:


ioexe_reqs
0bitsuopis_jal%:#
B


exe_reg_uops
0is_jal�register-read.scala 213:29�zg
=:;
0:.
':%
B
:


ioexe_reqs
0bitsuopis_jalr&:$
B


exe_reg_uops
0is_jalr�register-read.scala 213:29�zc
;:9
0:.
':%
B
:


ioexe_reqs
0bitsuopis_br$:"
B


exe_reg_uops
0is_br�register-read.scala 213:29�zu
D:B
0:.
':%
B
:


ioexe_reqs
0bitsuopiw_p2_poisoned-:+
B


exe_reg_uops
0iw_p2_poisoned�register-read.scala 213:29�zu
D:B
0:.
':%
B
:


ioexe_reqs
0bitsuopiw_p1_poisoned-:+
B


exe_reg_uops
0iw_p1_poisoned�register-read.scala 213:29�zi
>:<
0:.
':%
B
:


ioexe_reqs
0bitsuopiw_state':%
B


exe_reg_uops
0iw_state�register-read.scala 213:29�zy
F:D
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopctrlis_std/:-
#:!
B


exe_reg_uops
0ctrlis_std�register-read.scala 213:29�zy
F:D
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopctrlis_sta/:-
#:!
B


exe_reg_uops
0ctrlis_sta�register-read.scala 213:29�z{
G:E
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopctrlis_load0:.
#:!
B


exe_reg_uops
0ctrlis_load�register-read.scala 213:29�z{
G:E
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopctrlcsr_cmd0:.
#:!
B


exe_reg_uops
0ctrlcsr_cmd�register-read.scala 213:29�zy
F:D
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopctrlfcn_dw/:-
#:!
B


exe_reg_uops
0ctrlfcn_dw�register-read.scala 213:29�zy
F:D
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopctrlop_fcn/:-
#:!
B


exe_reg_uops
0ctrlop_fcn�register-read.scala 213:29�z{
G:E
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopctrlimm_sel0:.
#:!
B


exe_reg_uops
0ctrlimm_sel�register-read.scala 213:29�z{
G:E
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopctrlop2_sel0:.
#:!
B


exe_reg_uops
0ctrlop2_sel�register-read.scala 213:29�z{
G:E
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopctrlop1_sel0:.
#:!
B


exe_reg_uops
0ctrlop1_sel�register-read.scala 213:29�z{
G:E
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopctrlbr_type0:.
#:!
B


exe_reg_uops
0ctrlbr_type�register-read.scala 213:29�zg
=:;
0:.
':%
B
:


ioexe_reqs
0bitsuopfu_code&:$
B


exe_reg_uops
0fu_code�register-read.scala 213:29�zg
=:;
0:.
':%
B
:


ioexe_reqs
0bitsuopiq_type&:$
B


exe_reg_uops
0iq_type�register-read.scala 213:29�zi
>:<
0:.
':%
B
:


ioexe_reqs
0bitsuopdebug_pc':%
B


exe_reg_uops
0debug_pc�register-read.scala 213:29�ze
<::
0:.
':%
B
:


ioexe_reqs
0bitsuopis_rvc%:#
B


exe_reg_uops
0is_rvc�register-read.scala 213:29�zm
@:>
0:.
':%
B
:


ioexe_reqs
0bitsuop
debug_inst):'
B


exe_reg_uops
0
debug_inst�register-read.scala 213:29�za
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopinst#:!
B


exe_reg_uops
0inst�register-read.scala 213:29�za
::8
0:.
':%
B
:


ioexe_reqs
0bitsuopuopc#:!
B


exe_reg_uops
0uopc�register-read.scala 213:29wzV
5:3
':%
B
:


ioexe_reqs
0bitsrs1_dataB


exe_reg_rs1_data
0�register-read.scala 214:56wzV
5:3
':%
B
:


ioexe_reqs
0bitsrs2_dataB


exe_reg_rs2_data
0�register-read.scala 215:56wzV
5:3
':%
B
:


ioexe_reqs
0bitsrs3_dataB


exe_reg_rs3_data
0�register-read.scala 216:56
RegisterRead