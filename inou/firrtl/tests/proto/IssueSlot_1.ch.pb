
��
����
IssueSlot_1
clock" 
reset
�7
io�7*�7
valid

will_be_valid

request


request_hp

grant
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

kill

clear

ldspec_miss

[wakeup_portsI2G
C*A
valid

.bits&*$
pdst

poisoned

9pred_wakeup_port#*!
valid

bits

=spec_ld_wakeup)2'
#*!
valid

bits

�in_uop�*�
valid

�bits�*�
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
�out_uop�*�
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
WdebugN*L
p1

p2

p3

ppred

state
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
 -


next_state �issue-slot.scala 81:29,

	next_uopc �issue-slot.scala 82:292

next_lrs1_rtype �issue-slot.scala 83:292

next_lrs2_rtype �issue-slot.scala 84:29O2
state
	

clock"	

reset*	

0�issue-slot.scala 86:22L/
p1
	

clock"	

reset*	

0�issue-slot.scala 87:22L/
p2
	

clock"	

reset*	

0�issue-slot.scala 88:22L/
p3
	

clock"	

reset*	

0�issue-slot.scala 89:22O2
ppred
	

clock"	

reset*	

0�issue-slot.scala 90:22U8
p1_poisoned
	

clock"	

reset*	

0�issue-slot.scala 95:28U8
p2_poisoned
	

clock"	

reset*	

0�issue-slot.scala 96:28;z


p1_poisoned	

0�issue-slot.scala 97:15;z


p2_poisoned	

0�issue-slot.scala 98:15�2x
next_p1_poisonedd2b
:
:


ioin_uopvalid0:.
:
:


ioin_uopbitsiw_p1_poisoned

p1_poisoned�issue-slot.scala 99:29�2x
next_p2_poisonedd2b
:
:


ioin_uopvalid0:.
:
:


ioin_uopbitsiw_p2_poisoned

p2_poisoned�issue-slot.scala 100:29�
�
_T�*�
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
�consts.scala 269:193�
:


_T
debug_tsrc�consts.scala 270:203�
:


_T
debug_fsrc�consts.scala 270:203�
:


_T
bp_xcpt_if�consts.scala 270:204�
:


_Tbp_debug_if�consts.scala 270:203�
:


_T
xcpt_ma_if�consts.scala 270:203�
:


_T
xcpt_ae_if�consts.scala 270:203�
:


_T
xcpt_pf_if�consts.scala 270:202�
:


_T	fp_single�consts.scala 270:20/�
:


_Tfp_val�consts.scala 270:200�
:


_Tfrs3_en�consts.scala 270:203�
:


_T
lrs2_rtype�consts.scala 270:203�
:


_T
lrs1_rtype�consts.scala 270:202�
:


_T	dst_rtype�consts.scala 270:201�
:


_Tldst_val�consts.scala 270:20-�
:


_Tlrs3�consts.scala 270:20-�
:


_Tlrs2�consts.scala 270:20-�
:


_Tlrs1�consts.scala 270:20-�
:


_Tldst�consts.scala 270:204�
:


_Tldst_is_rs1�consts.scala 270:208�
:


_Tflush_on_commit�consts.scala 270:202�
:


_T	is_unique�consts.scala 270:206�
:


_Tis_sys_pc2epc�consts.scala 270:201�
:


_Tuses_stq�consts.scala 270:201�
:


_Tuses_ldq�consts.scala 270:20/�
:


_Tis_amo�consts.scala 270:202�
:


_T	is_fencei�consts.scala 270:201�
:


_Tis_fence�consts.scala 270:203�
:


_T
mem_signed�consts.scala 270:201�
:


_Tmem_size�consts.scala 270:200�
:


_Tmem_cmd�consts.scala 270:203�
:


_T
bypassable�consts.scala 270:202�
:


_T	exc_cause�consts.scala 270:202�
:


_T	exception�consts.scala 270:203�
:


_T
stale_pdst�consts.scala 270:203�
:


_T
ppred_busy�consts.scala 270:202�
:


_T	prs3_busy�consts.scala 270:202�
:


_T	prs2_busy�consts.scala 270:202�
:


_T	prs1_busy�consts.scala 270:20.�
:


_Tppred�consts.scala 270:20-�
:


_Tprs3�consts.scala 270:20-�
:


_Tprs2�consts.scala 270:20-�
:


_Tprs1�consts.scala 270:20-�
:


_Tpdst�consts.scala 270:200�
:


_Trxq_idx�consts.scala 270:200�
:


_Tstq_idx�consts.scala 270:200�
:


_Tldq_idx�consts.scala 270:200�
:


_Trob_idx�consts.scala 270:201�
:


_Tcsr_addr�consts.scala 270:203�
:


_T
imm_packed�consts.scala 270:20.�
:


_Ttaken�consts.scala 270:20/�
:


_Tpc_lob�consts.scala 270:202�
:


_T	edge_inst�consts.scala 270:200�
:


_Tftq_idx�consts.scala 270:20/�
:


_Tbr_tag�consts.scala 270:200�
:


_Tbr_mask�consts.scala 270:20/�
:


_Tis_sfb�consts.scala 270:20/�
:


_Tis_jal�consts.scala 270:200�
:


_Tis_jalr�consts.scala 270:20.�
:


_Tis_br�consts.scala 270:207�
:


_Tiw_p2_poisoned�consts.scala 270:207�
:


_Tiw_p1_poisoned�consts.scala 270:201�
:


_Tiw_state�consts.scala 270:209�
:
:


_Tctrlis_std�consts.scala 270:209�
:
:


_Tctrlis_sta�consts.scala 270:20:�
:
:


_Tctrlis_load�consts.scala 270:20:�
:
:


_Tctrlcsr_cmd�consts.scala 270:209�
:
:


_Tctrlfcn_dw�consts.scala 270:209�
:
:


_Tctrlop_fcn�consts.scala 270:20:�
:
:


_Tctrlimm_sel�consts.scala 270:20:�
:
:


_Tctrlop2_sel�consts.scala 270:20:�
:
:


_Tctrlop1_sel�consts.scala 270:20:�
:
:


_Tctrlbr_type�consts.scala 270:200�
:


_Tfu_code�consts.scala 270:200�
:


_Tiq_type�consts.scala 270:201�
:


_Tdebug_pc�consts.scala 270:20/�
:


_Tis_rvc�consts.scala 270:203�
:


_T
debug_inst�consts.scala 270:20-�
:


_Tinst�consts.scala 270:20-�
:


_Tuopc�consts.scala 270:209z
:


_Tuopc	

0�consts.scala 271:20?z%
:


_T
bypassable	

0�consts.scala 272:20;z!
:


_Tfp_val	

0�consts.scala 273:20=z#
:


_Tuses_stq	

0�consts.scala 274:20=z#
:


_Tuses_ldq	

0�consts.scala 275:209z
:


_Tpdst	

0�consts.scala 276:20>z$
:


_T	dst_rtype	

2�consts.scala 277:20�
�
_T_1�*�
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
�consts.scala 279:181�
:


_T_1is_std�consts.scala 280:201�
:


_T_1is_sta�consts.scala 280:202�
:


_T_1is_load�consts.scala 280:202�
:


_T_1csr_cmd�consts.scala 280:201�
:


_T_1fcn_dw�consts.scala 280:201�
:


_T_1op_fcn�consts.scala 280:202�
:


_T_1imm_sel�consts.scala 280:202�
:


_T_1op2_sel�consts.scala 280:202�
:


_T_1op1_sel�consts.scala 280:202�
:


_T_1br_type�consts.scala 280:20>z$
:


_T_1br_type	

0�consts.scala 281:20>z$
:


_T_1csr_cmd	

0�consts.scala 282:20>z$
:


_T_1is_load	

0�consts.scala 283:20=z#
:


_T_1is_sta	

0�consts.scala 284:20=z#
:


_T_1is_std	

0�consts.scala 285:20Nz4
:
:


_Tctrlis_std:


_T_1is_std�consts.scala 287:14Nz4
:
:


_Tctrlis_sta:


_T_1is_sta�consts.scala 287:14Pz6
:
:


_Tctrlis_load:


_T_1is_load�consts.scala 287:14Pz6
:
:


_Tctrlcsr_cmd:


_T_1csr_cmd�consts.scala 287:14Nz4
:
:


_Tctrlfcn_dw:


_T_1fcn_dw�consts.scala 287:14Nz4
:
:


_Tctrlop_fcn:


_T_1op_fcn�consts.scala 287:14Pz6
:
:


_Tctrlimm_sel:


_T_1imm_sel�consts.scala 287:14Pz6
:
:


_Tctrlop2_sel:


_T_1op2_sel�consts.scala 287:14Pz6
:
:


_Tctrlop1_sel:


_T_1op1_sel�consts.scala 287:14Pz6
:
:


_Tctrlbr_type:


_T_1br_type�consts.scala 287:14��
slot_uop�*�
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
clock"	

reset*

_T�issue-slot.scala 102:25w2Y
next_uopM2K
:
:


ioin_uopvalid:
:


ioin_uopbits


slot_uop�issue-slot.scala 103:21�:�
:


iokill6z
	

state	

0�issue-slot.scala 111:11�:�
:
:


ioin_uopvalidUz7
	

state*:(
:
:


ioin_uopbitsiw_state�issue-slot.scala 113:11�:�
:


ioclear6z
	

state	

0�issue-slot.scala 115:119z
	

state


next_state�issue-slot.scala 117:11�issue-slot.scala 114:26�issue-slot.scala 112:33�issue-slot.scala 110:189z



next_state	

state�issue-slot.scala 126:14Ez'


	next_uopc:



slot_uopuopc�issue-slot.scala 127:13Qz3


next_lrs1_rtype:



slot_uop
lrs1_rtype�issue-slot.scala 128:19Qz3


next_lrs2_rtype:



slot_uop
lrs2_rtype�issue-slot.scala 129:19�:�
:


iokill;z



next_state	

0�issue-slot.scala 132:16B2$
_T_2R	

state	

1�issue-slot.scala 133:36G2)
_T_3!R:


iogrant

_T_2�issue-slot.scala 133:26B2$
_T_4R	

state	

2�issue-slot.scala 134:25G2)
_T_5!R:


iogrant

_T_4�issue-slot.scala 134:15<2
_T_6R

_T_5

p1�issue-slot.scala 134:40<2
_T_7R

_T_6

p2�issue-slot.scala 134:46?2!
_T_8R

_T_7	

ppred�issue-slot.scala 134:52>2 
_T_9R

_T_3

_T_8�issue-slot.scala 133:52�
:�



_T_9M2/
_T_10&R$

p1_poisoned

p2_poisoned�issue-slot.scala 136:44O21
_T_11(R&:


ioldspec_miss	

_T_10�issue-slot.scala 136:28C2%
_T_12R	

_T_11	

0�issue-slot.scala 136:11f:H
	

_T_12;z



next_state	

0�issue-slot.scala 137:18�issue-slot.scala 136:62C2%
_T_13R	

state	

2�issue-slot.scala 139:35I2+
_T_14"R :


iogrant	

_T_13�issue-slot.scala 139:25�:�
	

_T_14M2/
_T_15&R$

p1_poisoned

p2_poisoned�issue-slot.scala 140:44O21
_T_16(R&:


ioldspec_miss	

_T_15�issue-slot.scala 140:28C2%
_T_17R	

_T_16	

0�issue-slot.scala 140:11�:�
	

_T_17;z



next_state	

1�issue-slot.scala 141:18�:�


p1Cz%
:



slot_uopuopc	

3�issue-slot.scala 143:23:z


	next_uopc	

3�issue-slot.scala 144:19Iz+
:



slot_uop
lrs1_rtype	

2�issue-slot.scala 145:29@z"


next_lrs1_rtype	

2�issue-slot.scala 146:25Iz+
:



slot_uop
lrs2_rtype	

2�issue-slot.scala 148:29@z"


next_lrs2_rtype	

2�issue-slot.scala 149:25�issue-slot.scala 142:17�issue-slot.scala 140:62�issue-slot.scala 139:51�issue-slot.scala 134:63�issue-slot.scala 131:18�G:�G
:
:


ioin_uopvalidjzL
:



slot_uop
debug_tsrc,:*
:
:


ioin_uopbits
debug_tsrc�issue-slot.scala 155:14jzL
:



slot_uop
debug_fsrc,:*
:
:


ioin_uopbits
debug_fsrc�issue-slot.scala 155:14jzL
:



slot_uop
bp_xcpt_if,:*
:
:


ioin_uopbits
bp_xcpt_if�issue-slot.scala 155:14lzN
:



slot_uopbp_debug_if-:+
:
:


ioin_uopbitsbp_debug_if�issue-slot.scala 155:14jzL
:



slot_uop
xcpt_ma_if,:*
:
:


ioin_uopbits
xcpt_ma_if�issue-slot.scala 155:14jzL
:



slot_uop
xcpt_ae_if,:*
:
:


ioin_uopbits
xcpt_ae_if�issue-slot.scala 155:14jzL
:



slot_uop
xcpt_pf_if,:*
:
:


ioin_uopbits
xcpt_pf_if�issue-slot.scala 155:14hzJ
:



slot_uop	fp_single+:)
:
:


ioin_uopbits	fp_single�issue-slot.scala 155:14bzD
:



slot_uopfp_val(:&
:
:


ioin_uopbitsfp_val�issue-slot.scala 155:14dzF
:



slot_uopfrs3_en):'
:
:


ioin_uopbitsfrs3_en�issue-slot.scala 155:14jzL
:



slot_uop
lrs2_rtype,:*
:
:


ioin_uopbits
lrs2_rtype�issue-slot.scala 155:14jzL
:



slot_uop
lrs1_rtype,:*
:
:


ioin_uopbits
lrs1_rtype�issue-slot.scala 155:14hzJ
:



slot_uop	dst_rtype+:)
:
:


ioin_uopbits	dst_rtype�issue-slot.scala 155:14fzH
:



slot_uopldst_val*:(
:
:


ioin_uopbitsldst_val�issue-slot.scala 155:14^z@
:



slot_uoplrs3&:$
:
:


ioin_uopbitslrs3�issue-slot.scala 155:14^z@
:



slot_uoplrs2&:$
:
:


ioin_uopbitslrs2�issue-slot.scala 155:14^z@
:



slot_uoplrs1&:$
:
:


ioin_uopbitslrs1�issue-slot.scala 155:14^z@
:



slot_uopldst&:$
:
:


ioin_uopbitsldst�issue-slot.scala 155:14lzN
:



slot_uopldst_is_rs1-:+
:
:


ioin_uopbitsldst_is_rs1�issue-slot.scala 155:14tzV
!:



slot_uopflush_on_commit1:/
:
:


ioin_uopbitsflush_on_commit�issue-slot.scala 155:14hzJ
:



slot_uop	is_unique+:)
:
:


ioin_uopbits	is_unique�issue-slot.scala 155:14pzR
:



slot_uopis_sys_pc2epc/:-
:
:


ioin_uopbitsis_sys_pc2epc�issue-slot.scala 155:14fzH
:



slot_uopuses_stq*:(
:
:


ioin_uopbitsuses_stq�issue-slot.scala 155:14fzH
:



slot_uopuses_ldq*:(
:
:


ioin_uopbitsuses_ldq�issue-slot.scala 155:14bzD
:



slot_uopis_amo(:&
:
:


ioin_uopbitsis_amo�issue-slot.scala 155:14hzJ
:



slot_uop	is_fencei+:)
:
:


ioin_uopbits	is_fencei�issue-slot.scala 155:14fzH
:



slot_uopis_fence*:(
:
:


ioin_uopbitsis_fence�issue-slot.scala 155:14jzL
:



slot_uop
mem_signed,:*
:
:


ioin_uopbits
mem_signed�issue-slot.scala 155:14fzH
:



slot_uopmem_size*:(
:
:


ioin_uopbitsmem_size�issue-slot.scala 155:14dzF
:



slot_uopmem_cmd):'
:
:


ioin_uopbitsmem_cmd�issue-slot.scala 155:14jzL
:



slot_uop
bypassable,:*
:
:


ioin_uopbits
bypassable�issue-slot.scala 155:14hzJ
:



slot_uop	exc_cause+:)
:
:


ioin_uopbits	exc_cause�issue-slot.scala 155:14hzJ
:



slot_uop	exception+:)
:
:


ioin_uopbits	exception�issue-slot.scala 155:14jzL
:



slot_uop
stale_pdst,:*
:
:


ioin_uopbits
stale_pdst�issue-slot.scala 155:14jzL
:



slot_uop
ppred_busy,:*
:
:


ioin_uopbits
ppred_busy�issue-slot.scala 155:14hzJ
:



slot_uop	prs3_busy+:)
:
:


ioin_uopbits	prs3_busy�issue-slot.scala 155:14hzJ
:



slot_uop	prs2_busy+:)
:
:


ioin_uopbits	prs2_busy�issue-slot.scala 155:14hzJ
:



slot_uop	prs1_busy+:)
:
:


ioin_uopbits	prs1_busy�issue-slot.scala 155:14`zB
:



slot_uopppred':%
:
:


ioin_uopbitsppred�issue-slot.scala 155:14^z@
:



slot_uopprs3&:$
:
:


ioin_uopbitsprs3�issue-slot.scala 155:14^z@
:



slot_uopprs2&:$
:
:


ioin_uopbitsprs2�issue-slot.scala 155:14^z@
:



slot_uopprs1&:$
:
:


ioin_uopbitsprs1�issue-slot.scala 155:14^z@
:



slot_uoppdst&:$
:
:


ioin_uopbitspdst�issue-slot.scala 155:14dzF
:



slot_uoprxq_idx):'
:
:


ioin_uopbitsrxq_idx�issue-slot.scala 155:14dzF
:



slot_uopstq_idx):'
:
:


ioin_uopbitsstq_idx�issue-slot.scala 155:14dzF
:



slot_uopldq_idx):'
:
:


ioin_uopbitsldq_idx�issue-slot.scala 155:14dzF
:



slot_uoprob_idx):'
:
:


ioin_uopbitsrob_idx�issue-slot.scala 155:14fzH
:



slot_uopcsr_addr*:(
:
:


ioin_uopbitscsr_addr�issue-slot.scala 155:14jzL
:



slot_uop
imm_packed,:*
:
:


ioin_uopbits
imm_packed�issue-slot.scala 155:14`zB
:



slot_uoptaken':%
:
:


ioin_uopbitstaken�issue-slot.scala 155:14bzD
:



slot_uoppc_lob(:&
:
:


ioin_uopbitspc_lob�issue-slot.scala 155:14hzJ
:



slot_uop	edge_inst+:)
:
:


ioin_uopbits	edge_inst�issue-slot.scala 155:14dzF
:



slot_uopftq_idx):'
:
:


ioin_uopbitsftq_idx�issue-slot.scala 155:14bzD
:



slot_uopbr_tag(:&
:
:


ioin_uopbitsbr_tag�issue-slot.scala 155:14dzF
:



slot_uopbr_mask):'
:
:


ioin_uopbitsbr_mask�issue-slot.scala 155:14bzD
:



slot_uopis_sfb(:&
:
:


ioin_uopbitsis_sfb�issue-slot.scala 155:14bzD
:



slot_uopis_jal(:&
:
:


ioin_uopbitsis_jal�issue-slot.scala 155:14dzF
:



slot_uopis_jalr):'
:
:


ioin_uopbitsis_jalr�issue-slot.scala 155:14`zB
:



slot_uopis_br':%
:
:


ioin_uopbitsis_br�issue-slot.scala 155:14rzT
 :



slot_uopiw_p2_poisoned0:.
:
:


ioin_uopbitsiw_p2_poisoned�issue-slot.scala 155:14rzT
 :



slot_uopiw_p1_poisoned0:.
:
:


ioin_uopbitsiw_p1_poisoned�issue-slot.scala 155:14fzH
:



slot_uopiw_state*:(
:
:


ioin_uopbitsiw_state�issue-slot.scala 155:14vzX
": 
:



slot_uopctrlis_std2:0
&:$
:
:


ioin_uopbitsctrlis_std�issue-slot.scala 155:14vzX
": 
:



slot_uopctrlis_sta2:0
&:$
:
:


ioin_uopbitsctrlis_sta�issue-slot.scala 155:14xzZ
#:!
:



slot_uopctrlis_load3:1
&:$
:
:


ioin_uopbitsctrlis_load�issue-slot.scala 155:14xzZ
#:!
:



slot_uopctrlcsr_cmd3:1
&:$
:
:


ioin_uopbitsctrlcsr_cmd�issue-slot.scala 155:14vzX
": 
:



slot_uopctrlfcn_dw2:0
&:$
:
:


ioin_uopbitsctrlfcn_dw�issue-slot.scala 155:14vzX
": 
:



slot_uopctrlop_fcn2:0
&:$
:
:


ioin_uopbitsctrlop_fcn�issue-slot.scala 155:14xzZ
#:!
:



slot_uopctrlimm_sel3:1
&:$
:
:


ioin_uopbitsctrlimm_sel�issue-slot.scala 155:14xzZ
#:!
:



slot_uopctrlop2_sel3:1
&:$
:
:


ioin_uopbitsctrlop2_sel�issue-slot.scala 155:14xzZ
#:!
:



slot_uopctrlop1_sel3:1
&:$
:
:


ioin_uopbitsctrlop1_sel�issue-slot.scala 155:14xzZ
#:!
:



slot_uopctrlbr_type3:1
&:$
:
:


ioin_uopbitsctrlbr_type�issue-slot.scala 155:14dzF
:



slot_uopfu_code):'
:
:


ioin_uopbitsfu_code�issue-slot.scala 155:14dzF
:



slot_uopiq_type):'
:
:


ioin_uopbitsiq_type�issue-slot.scala 155:14fzH
:



slot_uopdebug_pc*:(
:
:


ioin_uopbitsdebug_pc�issue-slot.scala 155:14bzD
:



slot_uopis_rvc(:&
:
:


ioin_uopbitsis_rvc�issue-slot.scala 155:14jzL
:



slot_uop
debug_inst,:*
:
:


ioin_uopbits
debug_inst�issue-slot.scala 155:14^z@
:



slot_uopinst&:$
:
:


ioin_uopbitsinst�issue-slot.scala 155:14^z@
:



slot_uopuopc&:$
:
:


ioin_uopbitsuopc�issue-slot.scala 155:14B2%
_T_18R	

state	

0�issue-slot.scala 78:26I2+
_T_19"R 	

_T_18:


ioclear�issue-slot.scala 156:24H2*
_T_20!R	

_T_19:


iokill�issue-slot.scala 156:36@2"
_T_21R	

reset
0
0�issue-slot.scala 156:12A2#
_T_22R	

_T_20	

_T_21�issue-slot.scala 156:12C2%
_T_23R	

_T_22	

0�issue-slot.scala 156:12�:�
	

_T_23�R�
�Assertion failed: trying to overwrite a valid issue slot.
    at issue-slot.scala:156 assert (is_invalid || io.clear || io.kill, "trying to overwrite a valid issue slot.")
	

clock"	

1�issue-slot.scala 156:128B	

clock	

1�issue-slot.scala 156:12�issue-slot.scala 156:12�issue-slot.scala 154:26

next_p1
�
 z

	
next_p1

p1�
 

next_p2
�
 z

	
next_p2

p2�
 

next_p3
�
 z

	
next_p3

p3�
 


next_ppred
�
 "z



next_ppred	

ppred�
 �:�
:
:


ioin_uopvalide2G
_T_24>R<+:)
:
:


ioin_uopbits	prs1_busy	

0�issue-slot.scala 169:110z


p1	

_T_24�issue-slot.scala 169:8e2G
_T_25>R<+:)
:
:


ioin_uopbits	prs2_busy	

0�issue-slot.scala 170:110z


p2	

_T_25�issue-slot.scala 170:8e2G
_T_26>R<+:)
:
:


ioin_uopbits	prs3_busy	

0�issue-slot.scala 171:110z


p3	

_T_26�issue-slot.scala 171:8f2H
_T_27?R=,:*
:
:


ioin_uopbits
ppred_busy	

0�issue-slot.scala 172:144z
	

ppred	

_T_27�issue-slot.scala 172:11�issue-slot.scala 168:26Z2<
_T_283R1:


ioldspec_miss

next_p1_poisoned�issue-slot.scala 175:24�:�
	

_T_28P22
_T_29)R':



next_uopprs1	

0�issue-slot.scala 176:26@2"
_T_30R	

reset
0
0�issue-slot.scala 176:11A2#
_T_31R	

_T_29	

_T_30�issue-slot.scala 176:11C2%
_T_32R	

_T_31	

0�issue-slot.scala 176:11�:�
	

_T_32�R�
�Assertion failed: Poison bit can't be set for prs1=x0!
    at issue-slot.scala:176 assert(next_uop.prs1 =/= 0.U, "Poison bit can't be set for prs1=x0!")
	

clock"	

1�issue-slot.scala 176:118B	

clock	

1�issue-slot.scala 176:11�issue-slot.scala 176:112z


p1	

0�issue-slot.scala 177:8�issue-slot.scala 175:45Z2<
_T_333R1:


ioldspec_miss

next_p2_poisoned�issue-slot.scala 179:24�:�
	

_T_33P22
_T_34)R':



next_uopprs2	

0�issue-slot.scala 180:26@2"
_T_35R	

reset
0
0�issue-slot.scala 180:11A2#
_T_36R	

_T_34	

_T_35�issue-slot.scala 180:11C2%
_T_37R	

_T_36	

0�issue-slot.scala 180:11�:�
	

_T_37�R�
�Assertion failed: Poison bit can't be set for prs2=x0!
    at issue-slot.scala:180 assert(next_uop.prs2 =/= 0.U, "Poison bit can't be set for prs2=x0!")
	

clock"	

1�issue-slot.scala 180:118B	

clock	

1�issue-slot.scala 180:11�issue-slot.scala 180:112z


p2	

0�issue-slot.scala 181:8�issue-slot.scala 179:45z2\
_T_38SRQ5:3
+:)
!B
:


iowakeup_ports
0bitspdst:



next_uopprs1�issue-slot.scala 186:40d2F
_T_39=R;,:*
!B
:


iowakeup_ports
0valid	

_T_38�issue-slot.scala 185:36^:@
	

_T_393z


p1	

1�issue-slot.scala 187:10�issue-slot.scala 186:60z2\
_T_40SRQ5:3
+:)
!B
:


iowakeup_ports
0bitspdst:



next_uopprs2�issue-slot.scala 190:40d2F
_T_41=R;,:*
!B
:


iowakeup_ports
0valid	

_T_40�issue-slot.scala 189:36^:@
	

_T_413z


p2	

1�issue-slot.scala 191:10�issue-slot.scala 190:60z2\
_T_42SRQ5:3
+:)
!B
:


iowakeup_ports
0bitspdst:



next_uopprs3�issue-slot.scala 194:40d2F
_T_43=R;,:*
!B
:


iowakeup_ports
0valid	

_T_42�issue-slot.scala 193:36^:@
	

_T_433z


p3	

1�issue-slot.scala 195:10�issue-slot.scala 194:60z2\
_T_44SRQ5:3
+:)
!B
:


iowakeup_ports
1bitspdst:



next_uopprs1�issue-slot.scala 186:40d2F
_T_45=R;,:*
!B
:


iowakeup_ports
1valid	

_T_44�issue-slot.scala 185:36^:@
	

_T_453z


p1	

1�issue-slot.scala 187:10�issue-slot.scala 186:60z2\
_T_46SRQ5:3
+:)
!B
:


iowakeup_ports
1bitspdst:



next_uopprs2�issue-slot.scala 190:40d2F
_T_47=R;,:*
!B
:


iowakeup_ports
1valid	

_T_46�issue-slot.scala 189:36^:@
	

_T_473z


p2	

1�issue-slot.scala 191:10�issue-slot.scala 190:60z2\
_T_48SRQ5:3
+:)
!B
:


iowakeup_ports
1bitspdst:



next_uopprs3�issue-slot.scala 194:40d2F
_T_49=R;,:*
!B
:


iowakeup_ports
1valid	

_T_48�issue-slot.scala 193:36^:@
	

_T_493z


p3	

1�issue-slot.scala 195:10�issue-slot.scala 194:60l2N
_T_50ERC&:$
:


iopred_wakeup_portbits:



next_uopppred�issue-slot.scala 198:63_2A
_T_518R6':%
:


iopred_wakeup_portvalid	

_T_50�issue-slot.scala 198:35a:C
	

_T_516z
	

ppred	

1�issue-slot.scala 199:11�issue-slot.scala 198:83g2I
_T_52@R>-:+
#B!
:


iospec_ld_wakeup
0bits	

0�issue-slot.scala 203:71f2H
_T_53?R=.:,
#B!
:


iospec_ld_wakeup
0valid	

_T_52�issue-slot.scala 203:42C2%
_T_54R	

_T_53	

0�issue-slot.scala 203:13@2"
_T_55R	

reset
0
0�issue-slot.scala 203:12A2#
_T_56R	

_T_54	

_T_55�issue-slot.scala 203:12C2%
_T_57R	

_T_56	

0�issue-slot.scala 203:12�:�
	

_T_57�R�
�Assertion failed: Loads to x0 should never speculatively wakeup other instructions
    at issue-slot.scala:203 assert (!(io.spec_ld_wakeup(w).valid && io.spec_ld_wakeup(w).bits === 0.U),
	

clock"	

1�issue-slot.scala 203:128B	

clock	

1�issue-slot.scala 203:12�issue-slot.scala 203:12r2T
_T_58KRI-:+
#B!
:


iospec_ld_wakeup
0bits:



next_uopprs1�issue-slot.scala 210:33f2H
_T_59?R=.:,
#B!
:


iospec_ld_wakeup
0valid	

_T_58�issue-slot.scala 209:38V28
_T_60/R-:



next_uop
lrs1_rtype	

0�issue-slot.scala 211:27A2#
_T_61R	

_T_59	

_T_60�issue-slot.scala 210:51�:�
	

_T_613z


p1	

1�issue-slot.scala 212:10<z


p1_poisoned	

1�issue-slot.scala 213:19N20
_T_62'R%

next_p1_poisoned	

0�issue-slot.scala 214:15@2"
_T_63R	

reset
0
0�issue-slot.scala 214:14A2#
_T_64R	

_T_62	

_T_63�issue-slot.scala 214:14C2%
_T_65R	

_T_64	

0�issue-slot.scala 214:14�:�
	

_T_65�Rb
HAssertion failed
    at issue-slot.scala:214 assert (!next_p1_poisoned)
	

clock"	

1�issue-slot.scala 214:148B	

clock	

1�issue-slot.scala 214:14�issue-slot.scala 214:14�issue-slot.scala 211:39r2T
_T_66KRI-:+
#B!
:


iospec_ld_wakeup
0bits:



next_uopprs2�issue-slot.scala 217:33f2H
_T_67?R=.:,
#B!
:


iospec_ld_wakeup
0valid	

_T_66�issue-slot.scala 216:38V28
_T_68/R-:



next_uop
lrs2_rtype	

0�issue-slot.scala 218:27A2#
_T_69R	

_T_67	

_T_68�issue-slot.scala 217:51�:�
	

_T_693z


p2	

1�issue-slot.scala 219:10<z


p2_poisoned	

1�issue-slot.scala 220:19N20
_T_70'R%

next_p2_poisoned	

0�issue-slot.scala 221:15@2"
_T_71R	

reset
0
0�issue-slot.scala 221:14A2#
_T_72R	

_T_70	

_T_71�issue-slot.scala 221:14C2%
_T_73R	

_T_72	

0�issue-slot.scala 221:14�:�
	

_T_73�Rb
HAssertion failed
    at issue-slot.scala:221 assert (!next_p2_poisoned)
	

clock"	

1�issue-slot.scala 221:148B	

clock	

1�issue-slot.scala 221:14�issue-slot.scala 221:14�issue-slot.scala 218:39T2=
_T_744R2.:,
:
:


iobrupdateb1resolve_mask�util.scala 85:27Q2:
next_br_mask*R(:



slot_uopbr_mask	

_T_74�util.scala 85:25s2[
_T_75RRP1:/
:
:


iobrupdateb1mispredict_mask:



slot_uopbr_mask�util.scala 118:51=2%
_T_76R	

_T_75	

0�util.scala 118:59f:H
	

_T_76;z



next_state	

0�issue-slot.scala 232:16�issue-slot.scala 231:50V29
_T_770R.:
:


ioin_uopvalid	

0�issue-slot.scala 235:9v:X
	

_T_77Kz-
:



slot_uopbr_mask

next_br_mask�issue-slot.scala 236:22�issue-slot.scala 235:27B2%
_T_78R	

state	

0�issue-slot.scala 79:24>2 
_T_79R	

_T_78

p1�issue-slot.scala 241:26>2 
_T_80R	

_T_79

p2�issue-slot.scala 241:32>2 
_T_81R	

_T_80

p3�issue-slot.scala 241:38A2#
_T_82R	

_T_81	

ppred�issue-slot.scala 241:44J2,
_T_83#R!:


iokill	

0�issue-slot.scala 241:56A2#
_T_84R	

_T_82	

_T_83�issue-slot.scala 241:53>z 
:


iorequest	

_T_84�issue-slot.scala 241:14^2@
_T_857R5:



slot_uopis_br:



slot_uopis_jal�issue-slot.scala 242:38Y2;
high_priority*R(	

_T_85:



slot_uopis_jalr�issue-slot.scala 242:57S25
_T_86,R*:


iorequest

high_priority�issue-slot.scala 243:31Az#
:


io
request_hp	

_T_86�issue-slot.scala 243:17C2%
_T_87R	

state	

1�issue-slot.scala 245:15�:�
	

_T_87;2
_T_88R

p1

p2�issue-slot.scala 246:22>2 
_T_89R	

_T_88

p3�issue-slot.scala 246:28A2#
_T_90R	

_T_89	

ppred�issue-slot.scala 246:34J2,
_T_91#R!:


iokill	

0�issue-slot.scala 246:46A2#
_T_92R	

_T_90	

_T_91�issue-slot.scala 246:43>z 
:


iorequest	

_T_92�issue-slot.scala 246:16C2%
_T_93R	

state	

2�issue-slot.scala 247:22�:�
	

_T_93;2
_T_94R

p1

p2�issue-slot.scala 248:23A2#
_T_95R	

_T_94	

ppred�issue-slot.scala 248:30J2,
_T_96#R!:


iokill	

0�issue-slot.scala 248:42A2#
_T_97R	

_T_95	

_T_96�issue-slot.scala 248:39>z 
:


iorequest	

_T_97�issue-slot.scala 248:16@z"
:


iorequest	

0�issue-slot.scala 250:16�issue-slot.scala 247:37�issue-slot.scala 245:30B2%
_T_98R	

state	

0�issue-slot.scala 79:24<z
:


iovalid	

_T_98�issue-slot.scala 254:12]z?
:
:


iouop
debug_tsrc:



slot_uop
debug_tsrc�issue-slot.scala 255:10]z?
:
:


iouop
debug_fsrc:



slot_uop
debug_fsrc�issue-slot.scala 255:10]z?
:
:


iouop
bp_xcpt_if:



slot_uop
bp_xcpt_if�issue-slot.scala 255:10_zA
 :
:


iouopbp_debug_if:



slot_uopbp_debug_if�issue-slot.scala 255:10]z?
:
:


iouop
xcpt_ma_if:



slot_uop
xcpt_ma_if�issue-slot.scala 255:10]z?
:
:


iouop
xcpt_ae_if:



slot_uop
xcpt_ae_if�issue-slot.scala 255:10]z?
:
:


iouop
xcpt_pf_if:



slot_uop
xcpt_pf_if�issue-slot.scala 255:10[z=
:
:


iouop	fp_single:



slot_uop	fp_single�issue-slot.scala 255:10Uz7
:
:


iouopfp_val:



slot_uopfp_val�issue-slot.scala 255:10Wz9
:
:


iouopfrs3_en:



slot_uopfrs3_en�issue-slot.scala 255:10]z?
:
:


iouop
lrs2_rtype:



slot_uop
lrs2_rtype�issue-slot.scala 255:10]z?
:
:


iouop
lrs1_rtype:



slot_uop
lrs1_rtype�issue-slot.scala 255:10[z=
:
:


iouop	dst_rtype:



slot_uop	dst_rtype�issue-slot.scala 255:10Yz;
:
:


iouopldst_val:



slot_uopldst_val�issue-slot.scala 255:10Qz3
:
:


iouoplrs3:



slot_uoplrs3�issue-slot.scala 255:10Qz3
:
:


iouoplrs2:



slot_uoplrs2�issue-slot.scala 255:10Qz3
:
:


iouoplrs1:



slot_uoplrs1�issue-slot.scala 255:10Qz3
:
:


iouopldst:



slot_uopldst�issue-slot.scala 255:10_zA
 :
:


iouopldst_is_rs1:



slot_uopldst_is_rs1�issue-slot.scala 255:10gzI
$:"
:


iouopflush_on_commit!:



slot_uopflush_on_commit�issue-slot.scala 255:10[z=
:
:


iouop	is_unique:



slot_uop	is_unique�issue-slot.scala 255:10czE
": 
:


iouopis_sys_pc2epc:



slot_uopis_sys_pc2epc�issue-slot.scala 255:10Yz;
:
:


iouopuses_stq:



slot_uopuses_stq�issue-slot.scala 255:10Yz;
:
:


iouopuses_ldq:



slot_uopuses_ldq�issue-slot.scala 255:10Uz7
:
:


iouopis_amo:



slot_uopis_amo�issue-slot.scala 255:10[z=
:
:


iouop	is_fencei:



slot_uop	is_fencei�issue-slot.scala 255:10Yz;
:
:


iouopis_fence:



slot_uopis_fence�issue-slot.scala 255:10]z?
:
:


iouop
mem_signed:



slot_uop
mem_signed�issue-slot.scala 255:10Yz;
:
:


iouopmem_size:



slot_uopmem_size�issue-slot.scala 255:10Wz9
:
:


iouopmem_cmd:



slot_uopmem_cmd�issue-slot.scala 255:10]z?
:
:


iouop
bypassable:



slot_uop
bypassable�issue-slot.scala 255:10[z=
:
:


iouop	exc_cause:



slot_uop	exc_cause�issue-slot.scala 255:10[z=
:
:


iouop	exception:



slot_uop	exception�issue-slot.scala 255:10]z?
:
:


iouop
stale_pdst:



slot_uop
stale_pdst�issue-slot.scala 255:10]z?
:
:


iouop
ppred_busy:



slot_uop
ppred_busy�issue-slot.scala 255:10[z=
:
:


iouop	prs3_busy:



slot_uop	prs3_busy�issue-slot.scala 255:10[z=
:
:


iouop	prs2_busy:



slot_uop	prs2_busy�issue-slot.scala 255:10[z=
:
:


iouop	prs1_busy:



slot_uop	prs1_busy�issue-slot.scala 255:10Sz5
:
:


iouopppred:



slot_uopppred�issue-slot.scala 255:10Qz3
:
:


iouopprs3:



slot_uopprs3�issue-slot.scala 255:10Qz3
:
:


iouopprs2:



slot_uopprs2�issue-slot.scala 255:10Qz3
:
:


iouopprs1:



slot_uopprs1�issue-slot.scala 255:10Qz3
:
:


iouoppdst:



slot_uoppdst�issue-slot.scala 255:10Wz9
:
:


iouoprxq_idx:



slot_uoprxq_idx�issue-slot.scala 255:10Wz9
:
:


iouopstq_idx:



slot_uopstq_idx�issue-slot.scala 255:10Wz9
:
:


iouopldq_idx:



slot_uopldq_idx�issue-slot.scala 255:10Wz9
:
:


iouoprob_idx:



slot_uoprob_idx�issue-slot.scala 255:10Yz;
:
:


iouopcsr_addr:



slot_uopcsr_addr�issue-slot.scala 255:10]z?
:
:


iouop
imm_packed:



slot_uop
imm_packed�issue-slot.scala 255:10Sz5
:
:


iouoptaken:



slot_uoptaken�issue-slot.scala 255:10Uz7
:
:


iouoppc_lob:



slot_uoppc_lob�issue-slot.scala 255:10[z=
:
:


iouop	edge_inst:



slot_uop	edge_inst�issue-slot.scala 255:10Wz9
:
:


iouopftq_idx:



slot_uopftq_idx�issue-slot.scala 255:10Uz7
:
:


iouopbr_tag:



slot_uopbr_tag�issue-slot.scala 255:10Wz9
:
:


iouopbr_mask:



slot_uopbr_mask�issue-slot.scala 255:10Uz7
:
:


iouopis_sfb:



slot_uopis_sfb�issue-slot.scala 255:10Uz7
:
:


iouopis_jal:



slot_uopis_jal�issue-slot.scala 255:10Wz9
:
:


iouopis_jalr:



slot_uopis_jalr�issue-slot.scala 255:10Sz5
:
:


iouopis_br:



slot_uopis_br�issue-slot.scala 255:10ezG
#:!
:


iouopiw_p2_poisoned :



slot_uopiw_p2_poisoned�issue-slot.scala 255:10ezG
#:!
:


iouopiw_p1_poisoned :



slot_uopiw_p1_poisoned�issue-slot.scala 255:10Yz;
:
:


iouopiw_state:



slot_uopiw_state�issue-slot.scala 255:10izK
%:#
:
:


iouopctrlis_std": 
:



slot_uopctrlis_std�issue-slot.scala 255:10izK
%:#
:
:


iouopctrlis_sta": 
:



slot_uopctrlis_sta�issue-slot.scala 255:10kzM
&:$
:
:


iouopctrlis_load#:!
:



slot_uopctrlis_load�issue-slot.scala 255:10kzM
&:$
:
:


iouopctrlcsr_cmd#:!
:



slot_uopctrlcsr_cmd�issue-slot.scala 255:10izK
%:#
:
:


iouopctrlfcn_dw": 
:



slot_uopctrlfcn_dw�issue-slot.scala 255:10izK
%:#
:
:


iouopctrlop_fcn": 
:



slot_uopctrlop_fcn�issue-slot.scala 255:10kzM
&:$
:
:


iouopctrlimm_sel#:!
:



slot_uopctrlimm_sel�issue-slot.scala 255:10kzM
&:$
:
:


iouopctrlop2_sel#:!
:



slot_uopctrlop2_sel�issue-slot.scala 255:10kzM
&:$
:
:


iouopctrlop1_sel#:!
:



slot_uopctrlop1_sel�issue-slot.scala 255:10kzM
&:$
:
:


iouopctrlbr_type#:!
:



slot_uopctrlbr_type�issue-slot.scala 255:10Wz9
:
:


iouopfu_code:



slot_uopfu_code�issue-slot.scala 255:10Wz9
:
:


iouopiq_type:



slot_uopiq_type�issue-slot.scala 255:10Yz;
:
:


iouopdebug_pc:



slot_uopdebug_pc�issue-slot.scala 255:10Uz7
:
:


iouopis_rvc:



slot_uopis_rvc�issue-slot.scala 255:10]z?
:
:


iouop
debug_inst:



slot_uop
debug_inst�issue-slot.scala 255:10Qz3
:
:


iouopinst:



slot_uopinst�issue-slot.scala 255:10Qz3
:
:


iouopuopc:



slot_uopuopc�issue-slot.scala 255:10Tz6
#:!
:


iouopiw_p1_poisoned

p1_poisoned�issue-slot.scala 256:25Tz6
#:!
:


iouopiw_p2_poisoned

p2_poisoned�issue-slot.scala 257:25C2%
_T_99R	

state	

1�issue-slot.scala 260:40D2&
_T_100R	

state	

2�issue-slot.scala 260:65@2"
_T_101R


_T_100

p1�issue-slot.scala 260:80@2"
_T_102R


_T_101

p2�issue-slot.scala 260:86C2%
_T_103R


_T_102	

ppred�issue-slot.scala 260:92C2%
_T_104R	

_T_99


_T_103�issue-slot.scala 260:55O21

may_vacate#R!:


iogrant


_T_104�issue-slot.scala 260:29N20
_T_105&R$

p1_poisoned

p2_poisoned�issue-slot.scala 261:53W29
squash_grant)R':


ioldspec_miss


_T_105�issue-slot.scala 261:37C2&
_T_106R	

state	

0�issue-slot.scala 79:24K2-
_T_107#R!

squash_grant	

0�issue-slot.scala 262:51H2*
_T_108 R


may_vacate


_T_107�issue-slot.scala 262:48E2'
_T_109R


_T_108	

0�issue-slot.scala 262:35D2&
_T_110R


_T_106


_T_109�issue-slot.scala 262:32Ez'
:


iowill_be_valid


_T_110�issue-slot.scala 262:20azC
#:!
:


ioout_uop
debug_tsrc:



slot_uop
debug_tsrc�issue-slot.scala 264:25azC
#:!
:


ioout_uop
debug_fsrc:



slot_uop
debug_fsrc�issue-slot.scala 264:25azC
#:!
:


ioout_uop
bp_xcpt_if:



slot_uop
bp_xcpt_if�issue-slot.scala 264:25czE
$:"
:


ioout_uopbp_debug_if:



slot_uopbp_debug_if�issue-slot.scala 264:25azC
#:!
:


ioout_uop
xcpt_ma_if:



slot_uop
xcpt_ma_if�issue-slot.scala 264:25azC
#:!
:


ioout_uop
xcpt_ae_if:



slot_uop
xcpt_ae_if�issue-slot.scala 264:25azC
#:!
:


ioout_uop
xcpt_pf_if:



slot_uop
xcpt_pf_if�issue-slot.scala 264:25_zA
": 
:


ioout_uop	fp_single:



slot_uop	fp_single�issue-slot.scala 264:25Yz;
:
:


ioout_uopfp_val:



slot_uopfp_val�issue-slot.scala 264:25[z=
 :
:


ioout_uopfrs3_en:



slot_uopfrs3_en�issue-slot.scala 264:25azC
#:!
:


ioout_uop
lrs2_rtype:



slot_uop
lrs2_rtype�issue-slot.scala 264:25azC
#:!
:


ioout_uop
lrs1_rtype:



slot_uop
lrs1_rtype�issue-slot.scala 264:25_zA
": 
:


ioout_uop	dst_rtype:



slot_uop	dst_rtype�issue-slot.scala 264:25]z?
!:
:


ioout_uopldst_val:



slot_uopldst_val�issue-slot.scala 264:25Uz7
:
:


ioout_uoplrs3:



slot_uoplrs3�issue-slot.scala 264:25Uz7
:
:


ioout_uoplrs2:



slot_uoplrs2�issue-slot.scala 264:25Uz7
:
:


ioout_uoplrs1:



slot_uoplrs1�issue-slot.scala 264:25Uz7
:
:


ioout_uopldst:



slot_uopldst�issue-slot.scala 264:25czE
$:"
:


ioout_uopldst_is_rs1:



slot_uopldst_is_rs1�issue-slot.scala 264:25kzM
(:&
:


ioout_uopflush_on_commit!:



slot_uopflush_on_commit�issue-slot.scala 264:25_zA
": 
:


ioout_uop	is_unique:



slot_uop	is_unique�issue-slot.scala 264:25gzI
&:$
:


ioout_uopis_sys_pc2epc:



slot_uopis_sys_pc2epc�issue-slot.scala 264:25]z?
!:
:


ioout_uopuses_stq:



slot_uopuses_stq�issue-slot.scala 264:25]z?
!:
:


ioout_uopuses_ldq:



slot_uopuses_ldq�issue-slot.scala 264:25Yz;
:
:


ioout_uopis_amo:



slot_uopis_amo�issue-slot.scala 264:25_zA
": 
:


ioout_uop	is_fencei:



slot_uop	is_fencei�issue-slot.scala 264:25]z?
!:
:


ioout_uopis_fence:



slot_uopis_fence�issue-slot.scala 264:25azC
#:!
:


ioout_uop
mem_signed:



slot_uop
mem_signed�issue-slot.scala 264:25]z?
!:
:


ioout_uopmem_size:



slot_uopmem_size�issue-slot.scala 264:25[z=
 :
:


ioout_uopmem_cmd:



slot_uopmem_cmd�issue-slot.scala 264:25azC
#:!
:


ioout_uop
bypassable:



slot_uop
bypassable�issue-slot.scala 264:25_zA
": 
:


ioout_uop	exc_cause:



slot_uop	exc_cause�issue-slot.scala 264:25_zA
": 
:


ioout_uop	exception:



slot_uop	exception�issue-slot.scala 264:25azC
#:!
:


ioout_uop
stale_pdst:



slot_uop
stale_pdst�issue-slot.scala 264:25azC
#:!
:


ioout_uop
ppred_busy:



slot_uop
ppred_busy�issue-slot.scala 264:25_zA
": 
:


ioout_uop	prs3_busy:



slot_uop	prs3_busy�issue-slot.scala 264:25_zA
": 
:


ioout_uop	prs2_busy:



slot_uop	prs2_busy�issue-slot.scala 264:25_zA
": 
:


ioout_uop	prs1_busy:



slot_uop	prs1_busy�issue-slot.scala 264:25Wz9
:
:


ioout_uopppred:



slot_uopppred�issue-slot.scala 264:25Uz7
:
:


ioout_uopprs3:



slot_uopprs3�issue-slot.scala 264:25Uz7
:
:


ioout_uopprs2:



slot_uopprs2�issue-slot.scala 264:25Uz7
:
:


ioout_uopprs1:



slot_uopprs1�issue-slot.scala 264:25Uz7
:
:


ioout_uoppdst:



slot_uoppdst�issue-slot.scala 264:25[z=
 :
:


ioout_uoprxq_idx:



slot_uoprxq_idx�issue-slot.scala 264:25[z=
 :
:


ioout_uopstq_idx:



slot_uopstq_idx�issue-slot.scala 264:25[z=
 :
:


ioout_uopldq_idx:



slot_uopldq_idx�issue-slot.scala 264:25[z=
 :
:


ioout_uoprob_idx:



slot_uoprob_idx�issue-slot.scala 264:25]z?
!:
:


ioout_uopcsr_addr:



slot_uopcsr_addr�issue-slot.scala 264:25azC
#:!
:


ioout_uop
imm_packed:



slot_uop
imm_packed�issue-slot.scala 264:25Wz9
:
:


ioout_uoptaken:



slot_uoptaken�issue-slot.scala 264:25Yz;
:
:


ioout_uoppc_lob:



slot_uoppc_lob�issue-slot.scala 264:25_zA
": 
:


ioout_uop	edge_inst:



slot_uop	edge_inst�issue-slot.scala 264:25[z=
 :
:


ioout_uopftq_idx:



slot_uopftq_idx�issue-slot.scala 264:25Yz;
:
:


ioout_uopbr_tag:



slot_uopbr_tag�issue-slot.scala 264:25[z=
 :
:


ioout_uopbr_mask:



slot_uopbr_mask�issue-slot.scala 264:25Yz;
:
:


ioout_uopis_sfb:



slot_uopis_sfb�issue-slot.scala 264:25Yz;
:
:


ioout_uopis_jal:



slot_uopis_jal�issue-slot.scala 264:25[z=
 :
:


ioout_uopis_jalr:



slot_uopis_jalr�issue-slot.scala 264:25Wz9
:
:


ioout_uopis_br:



slot_uopis_br�issue-slot.scala 264:25izK
':%
:


ioout_uopiw_p2_poisoned :



slot_uopiw_p2_poisoned�issue-slot.scala 264:25izK
':%
:


ioout_uopiw_p1_poisoned :



slot_uopiw_p1_poisoned�issue-slot.scala 264:25]z?
!:
:


ioout_uopiw_state:



slot_uopiw_state�issue-slot.scala 264:25mzO
):'
:
:


ioout_uopctrlis_std": 
:



slot_uopctrlis_std�issue-slot.scala 264:25mzO
):'
:
:


ioout_uopctrlis_sta": 
:



slot_uopctrlis_sta�issue-slot.scala 264:25ozQ
*:(
:
:


ioout_uopctrlis_load#:!
:



slot_uopctrlis_load�issue-slot.scala 264:25ozQ
*:(
:
:


ioout_uopctrlcsr_cmd#:!
:



slot_uopctrlcsr_cmd�issue-slot.scala 264:25mzO
):'
:
:


ioout_uopctrlfcn_dw": 
:



slot_uopctrlfcn_dw�issue-slot.scala 264:25mzO
):'
:
:


ioout_uopctrlop_fcn": 
:



slot_uopctrlop_fcn�issue-slot.scala 264:25ozQ
*:(
:
:


ioout_uopctrlimm_sel#:!
:



slot_uopctrlimm_sel�issue-slot.scala 264:25ozQ
*:(
:
:


ioout_uopctrlop2_sel#:!
:



slot_uopctrlop2_sel�issue-slot.scala 264:25ozQ
*:(
:
:


ioout_uopctrlop1_sel#:!
:



slot_uopctrlop1_sel�issue-slot.scala 264:25ozQ
*:(
:
:


ioout_uopctrlbr_type#:!
:



slot_uopctrlbr_type�issue-slot.scala 264:25[z=
 :
:


ioout_uopfu_code:



slot_uopfu_code�issue-slot.scala 264:25[z=
 :
:


ioout_uopiq_type:



slot_uopiq_type�issue-slot.scala 264:25]z?
!:
:


ioout_uopdebug_pc:



slot_uopdebug_pc�issue-slot.scala 264:25Yz;
:
:


ioout_uopis_rvc:



slot_uopis_rvc�issue-slot.scala 264:25azC
#:!
:


ioout_uop
debug_inst:



slot_uop
debug_inst�issue-slot.scala 264:25Uz7
:
:


ioout_uopinst:



slot_uopinst�issue-slot.scala 264:25Uz7
:
:


ioout_uopuopc:



slot_uopuopc�issue-slot.scala 264:25Qz3
!:
:


ioout_uopiw_state


next_state�issue-slot.scala 265:25Lz.
:
:


ioout_uopuopc

	next_uopc�issue-slot.scala 266:25Xz:
#:!
:


ioout_uop
lrs1_rtype

next_lrs1_rtype�issue-slot.scala 267:25Xz:
#:!
:


ioout_uop
lrs2_rtype

next_lrs2_rtype�issue-slot.scala 268:25Rz4
 :
:


ioout_uopbr_mask

next_br_mask�issue-slot.scala 269:25A2#
_T_111R

p1	

0�issue-slot.scala 270:28Nz0
": 
:


ioout_uop	prs1_busy


_T_111�issue-slot.scala 270:25A2#
_T_112R

p2	

0�issue-slot.scala 271:28Nz0
": 
:


ioout_uop	prs2_busy


_T_112�issue-slot.scala 271:25A2#
_T_113R

p3	

0�issue-slot.scala 272:28Nz0
": 
:


ioout_uop	prs3_busy


_T_113�issue-slot.scala 272:25D2&
_T_114R	

ppred	

0�issue-slot.scala 273:28Oz1
#:!
:


ioout_uop
ppred_busy


_T_114�issue-slot.scala 273:25Xz:
':%
:


ioout_uopiw_p1_poisoned

p1_poisoned�issue-slot.scala 274:29Xz:
':%
:


ioout_uopiw_p2_poisoned

p2_poisoned�issue-slot.scala 275:29D2&
_T_115R	

state	

2�issue-slot.scala 277:15�:�



_T_115<2
_T_116R

p1

p2�issue-slot.scala 278:14C2%
_T_117R


_T_116	

ppred�issue-slot.scala 278:20�:�



_T_117?2!
_T_118R

p1	

ppred�issue-slot.scala 280:21�:�



_T_118Qz3
:
:


iouopuopc:



slot_uopuopc�issue-slot.scala 281:19Lz.
:
:


iouop
lrs2_rtype	

2�issue-slot.scala 282:25?2!
_T_119R

p2	

ppred�issue-slot.scala 283:21�:�



_T_119Fz(
:
:


iouopuopc	

3�issue-slot.scala 284:19Lz.
:
:


iouop
lrs1_rtype	

2�issue-slot.scala 285:25�issue-slot.scala 283:31�issue-slot.scala 280:31�issue-slot.scala 278:30�issue-slot.scala 277:30Az#
:
:


iodebugp1

p1�issue-slot.scala 290:15Az#
:
:


iodebugp2

p2�issue-slot.scala 291:15Az#
:
:


iodebugp3

p3�issue-slot.scala 292:15Gz)
:
:


iodebugppred	

ppred�issue-slot.scala 293:18Gz)
:
:


iodebugstate	

state�issue-slot.scala 294:18
IssueSlot_1