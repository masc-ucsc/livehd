
��
�9�9
PipelinedMultiplier
clock" 
reset
�
io�*�
qreqh*f
valid

SbitsK*I
fn

dw

in1
@
in2
@
tag

Fresp>*<
valid

)bits!*
data
@
tag
R9
inPipe_valid
	

clock"	

reset*	

0�Valid.scala 117:22Gz.


inPipe_valid:
:


ioreqvalid�Valid.scala 117:22��
inPipe_bitsK*I
fn

dw

in1
@
in2
@
tag
	

clock"	

0*

inPipe_bits�Reg.scala 15:16�:�
:
:


ioreqvalidTz>
:


inPipe_bitstag": 
:
:


ioreqbitstag�Reg.scala 16:23Tz>
:


inPipe_bitsin2": 
:
:


ioreqbitsin2�Reg.scala 16:23Tz>
:


inPipe_bitsin1": 
:
:


ioreqbitsin1�Reg.scala 16:23Rz<
:


inPipe_bitsdw!:
:
:


ioreqbitsdw�Reg.scala 16:23Rz<
:


inPipe_bitsfn!:
:
:


ioreqbitsfn�Reg.scala 16:23�Reg.scala 16:19�
n
inh*f
valid

SbitsK*I
fn

dw

in1
@
in2
@
tag
�Valid.scala 112:21>z%
:


invalid

inPipe_valid�Valid.scala 113:17Nz5
:
:


inbitstag:


inPipe_bitstag�Valid.scala 114:16Nz5
:
:


inbitsin2:


inPipe_bitsin2�Valid.scala 114:16Nz5
:
:


inbitsin1:


inPipe_bitsin1�Valid.scala 114:16Lz3
:
:


inbitsdw:


inPipe_bitsdw�Valid.scala 114:16Lz3
:
:


inbitsfn:


inPipe_bitsfn�Valid.scala 114:16J21
_T+R):
:


inbitsfn	

1�Decode.scala 14:65;2!
_T_1R

_T	

1�Decode.scala 14:121L23
_T_2+R):
:


inbitsfn	

2�Decode.scala 14:65=2#
_T_3R

_T_2	

2�Decode.scala 14:121<2#
_T_4R	

0

_T_1�Decode.scala 15:3092 
_T_5R

_T_4

_T_3�Decode.scala 15:30L23
_T_6+R):
:


inbitsfn	

2�Decode.scala 14:65=2#
_T_7R

_T_6	

0�Decode.scala 14:121L23
_T_8+R):
:


inbitsfn	

1�Decode.scala 14:65=2#
_T_9R

_T_8	

0�Decode.scala 14:121=2$
_T_10R	

0

_T_7�Decode.scala 15:30;2"
_T_11R	

_T_10

_T_9�Decode.scala 15:30=2$
_T_12R	

0

_T_7�Decode.scala 15:30?2!
cmdHiR

_T_5
0
0�Multiplier.scala 200:58D2&
	lhsSignedR	

_T_11
0
0�Multiplier.scala 200:58D2&
	rhsSignedR	

_T_12
0
0�Multiplier.scala 200:58R24
_T_13+R):
:


inbitsdw	

0�Multiplier.scala 201:46E2'
cmdHalfR	

1	

_T_13�Multiplier.scala 201:32R24
_T_14+R):
:


inbitsin1
63
63�Multiplier.scala 203:41E2'
_T_15R

	lhsSigned	

_T_14�Multiplier.scala 203:27I23
_T_16*R(	

_T_15:
:


inbitsin1�Cat.scala 29:5842
lhsR	

_T_16�Multiplier.scala 203:65R24
_T_17+R):
:


inbitsin2
63
63�Multiplier.scala 204:41E2'
_T_18R

	rhsSigned	

_T_17�Multiplier.scala 204:27I23
_T_19*R(	

_T_18:
:


inbitsin2�Cat.scala 29:5842
rhsR	

_T_19�Multiplier.scala 204:65<2
prodR

lhs

rhs�Multiplier.scala 205:18B2$
_T_20R

prod
127
64�Multiplier.scala 206:30@2"
_T_21R

prod
31
0�Multiplier.scala 206:67?2$
_T_22R	

_T_21
31
31�package.scala 107:38<2"
_T_23R	

_T_22
0
0�Bitwise.scala 72:15S29
_T_2402.
	

_T_23


4294967295 	

0 �Bitwise.scala 72:1292#
_T_25R	

_T_24	

_T_21�Cat.scala 29:58A2"
_T_26R

prod
63
0�Multiplier.scala 206:101L2.
_T_27%2#

	
cmdHalf	

_T_25	

_T_26�Multiplier.scala 206:53J2,
muxed#2!
	

cmdHi	

_T_20	

_T_27�Multiplier.scala 206:18T;
respPipe_valid
	

clock"	

reset*	

0�Valid.scala 117:22@z'


respPipe_valid:


invalid�Valid.scala 117:22��
respPipe_bitsK*I
fn

dw

in1
@
in2
@
tag
	

clock"	

0*

respPipe_bits�Reg.scala 15:16�:�
:


invalidMz7
:


respPipe_bitstag:
:


inbitstag�Reg.scala 16:23Mz7
:


respPipe_bitsin2:
:


inbitsin2�Reg.scala 16:23Mz7
:


respPipe_bitsin1:
:


inbitsin1�Reg.scala 16:23Kz5
:


respPipe_bitsdw:
:


inbitsdw�Reg.scala 16:23Kz5
:


respPipe_bitsfn:
:


inbitsfn�Reg.scala 16:23�Reg.scala 16:19V=
respPipe_valid_1
	

clock"	

reset*	

0�Valid.scala 117:22Cz*


respPipe_valid_1

respPipe_valid�Valid.scala 117:22��
respPipe_bits_1K*I
fn

dw

in1
@
in2
@
tag
	

clock"	

0*

respPipe_bits_1�Reg.scala 15:16�:�


respPipe_validPz:
:


respPipe_bits_1tag:


respPipe_bitstag�Reg.scala 16:23Pz:
:


respPipe_bits_1in2:


respPipe_bitsin2�Reg.scala 16:23Pz:
:


respPipe_bits_1in1:


respPipe_bitsin1�Reg.scala 16:23Nz8
:


respPipe_bits_1dw:


respPipe_bitsdw�Reg.scala 16:23Nz8
:


respPipe_bits_1fn:


respPipe_bitsfn�Reg.scala 16:23�Reg.scala 16:19�
p
resph*f
valid

SbitsK*I
fn

dw

in1
@
in2
@
tag
�Valid.scala 112:21Dz+
:


respvalid

respPipe_valid_1�Valid.scala 113:17Tz;
:
:


respbitstag:


respPipe_bits_1tag�Valid.scala 114:16Tz;
:
:


respbitsin2:


respPipe_bits_1in2�Valid.scala 114:16Tz;
:
:


respbitsin1:


respPipe_bits_1in1�Valid.scala 114:16Rz9
:
:


respbitsdw:


respPipe_bits_1dw�Valid.scala 114:16Rz9
:
:


respbitsfn:


respPipe_bits_1fn�Valid.scala 114:16Pz2
:
:


iorespvalid:


respvalid�Multiplier.scala 209:17`zB
#:!
:
:


iorespbitstag:
:


respbitstag�Multiplier.scala 210:20K2
_T_28
	

clock"	

reset*	

0�Valid.scala 117:227z
	

_T_28:


invalid�Valid.scala 117:22H2
_T_29
@	

clock"	

0*	

_T_29�Reg.scala 15:16W:A
:


invalid,z
	

_T_29	

muxed�Reg.scala 16:23�Reg.scala 16:19K2
_T_30
	

clock"	

reset*	

0�Valid.scala 117:22/z
	

_T_30	

_T_28�Valid.scala 117:22H2
_T_31
@	

clock"	

0*	

_T_31�Reg.scala 15:16O:9
	

_T_28,z
	

_T_31	

_T_29�Reg.scala 16:23�Reg.scala 16:19E
,
_T_32#*!
valid

bits
@�Valid.scala 112:21:z!
:
	

_T_32valid	

_T_30�Valid.scala 113:179z 
:
	

_T_32bits	

_T_31�Valid.scala 114:16Yz;
$:"
:
:


iorespbitsdata:
	

_T_32bits�Multiplier.scala 211:21
����
PipelinedMulUnit
clock" 
reset
�Q
io�Q*�Q
�req�*�
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
@
rs2_data
@
rs3_data
@
	pred_data

kill

�resp�*�
ready

valid

�bits�*�
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

predicated

data
@
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
addr
(
,mxcpt#*!
valid

bits

gsfence]*[
valid

Hbits@*>
rs1

rs2

addr
'
asid
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
�bypass�2�
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
�
	

clock�
 �
	

reset�
 �


io�
 Lz)
:
:


ioreqready	

1�functional-unit.scala 223:165

_T2


�functional-unit.scala 226:35Az
B


_T
0	

0�functional-unit.scala 226:35Az
B


_T
1	

0�functional-unit.scala 226:35Az
B


_T
2	

0�functional-unit.scala 226:35U2
_T_12


	

clock"	

reset*

_T�functional-unit.scala 226:27��
_T_2�2�
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
	

clock"	

0*

_T_2�functional-unit.scala 227:23�2p
_T_3hRf1:/
:
:


iobrupdateb1mispredict_mask/:-
": 
:
:


ioreqbitsuopbr_mask�util.scala 118:51;2#
_T_4R

_T_3	

0�util.scala 118:59F2#
_T_5R

_T_4	

0�functional-unit.scala 230:36U22
_T_6*R(:
:


ioreqvalid

_T_5�functional-unit.scala 230:33a2>
_T_76R4#:!
:
:


ioreqbitskill	

0�functional-unit.scala 230:87C2 
_T_8R

_T_6

_T_7�functional-unit.scala 230:84@z
B


_T_1
0

_T_8�functional-unit.scala 230:17zzW
!:
B


_T_2
0
debug_tsrc2:0
": 
:
:


ioreqbitsuop
debug_tsrc�functional-unit.scala 231:17zzW
!:
B


_T_2
0
debug_fsrc2:0
": 
:
:


ioreqbitsuop
debug_fsrc�functional-unit.scala 231:17zzW
!:
B


_T_2
0
bp_xcpt_if2:0
": 
:
:


ioreqbitsuop
bp_xcpt_if�functional-unit.scala 231:17|zY
": 
B


_T_2
0bp_debug_if3:1
": 
:
:


ioreqbitsuopbp_debug_if�functional-unit.scala 231:17zzW
!:
B


_T_2
0
xcpt_ma_if2:0
": 
:
:


ioreqbitsuop
xcpt_ma_if�functional-unit.scala 231:17zzW
!:
B


_T_2
0
xcpt_ae_if2:0
": 
:
:


ioreqbitsuop
xcpt_ae_if�functional-unit.scala 231:17zzW
!:
B


_T_2
0
xcpt_pf_if2:0
": 
:
:


ioreqbitsuop
xcpt_pf_if�functional-unit.scala 231:17xzU
 :
B


_T_2
0	fp_single1:/
": 
:
:


ioreqbitsuop	fp_single�functional-unit.scala 231:17rzO
:
B


_T_2
0fp_val.:,
": 
:
:


ioreqbitsuopfp_val�functional-unit.scala 231:17tzQ
:
B


_T_2
0frs3_en/:-
": 
:
:


ioreqbitsuopfrs3_en�functional-unit.scala 231:17zzW
!:
B


_T_2
0
lrs2_rtype2:0
": 
:
:


ioreqbitsuop
lrs2_rtype�functional-unit.scala 231:17zzW
!:
B


_T_2
0
lrs1_rtype2:0
": 
:
:


ioreqbitsuop
lrs1_rtype�functional-unit.scala 231:17xzU
 :
B


_T_2
0	dst_rtype1:/
": 
:
:


ioreqbitsuop	dst_rtype�functional-unit.scala 231:17vzS
:
B


_T_2
0ldst_val0:.
": 
:
:


ioreqbitsuopldst_val�functional-unit.scala 231:17nzK
:
B


_T_2
0lrs3,:*
": 
:
:


ioreqbitsuoplrs3�functional-unit.scala 231:17nzK
:
B


_T_2
0lrs2,:*
": 
:
:


ioreqbitsuoplrs2�functional-unit.scala 231:17nzK
:
B


_T_2
0lrs1,:*
": 
:
:


ioreqbitsuoplrs1�functional-unit.scala 231:17nzK
:
B


_T_2
0ldst,:*
": 
:
:


ioreqbitsuopldst�functional-unit.scala 231:17|zY
": 
B


_T_2
0ldst_is_rs13:1
": 
:
:


ioreqbitsuopldst_is_rs1�functional-unit.scala 231:17�za
&:$
B


_T_2
0flush_on_commit7:5
": 
:
:


ioreqbitsuopflush_on_commit�functional-unit.scala 231:17xzU
 :
B


_T_2
0	is_unique1:/
": 
:
:


ioreqbitsuop	is_unique�functional-unit.scala 231:17�z]
$:"
B


_T_2
0is_sys_pc2epc5:3
": 
:
:


ioreqbitsuopis_sys_pc2epc�functional-unit.scala 231:17vzS
:
B


_T_2
0uses_stq0:.
": 
:
:


ioreqbitsuopuses_stq�functional-unit.scala 231:17vzS
:
B


_T_2
0uses_ldq0:.
": 
:
:


ioreqbitsuopuses_ldq�functional-unit.scala 231:17rzO
:
B


_T_2
0is_amo.:,
": 
:
:


ioreqbitsuopis_amo�functional-unit.scala 231:17xzU
 :
B


_T_2
0	is_fencei1:/
": 
:
:


ioreqbitsuop	is_fencei�functional-unit.scala 231:17vzS
:
B


_T_2
0is_fence0:.
": 
:
:


ioreqbitsuopis_fence�functional-unit.scala 231:17zzW
!:
B


_T_2
0
mem_signed2:0
": 
:
:


ioreqbitsuop
mem_signed�functional-unit.scala 231:17vzS
:
B


_T_2
0mem_size0:.
": 
:
:


ioreqbitsuopmem_size�functional-unit.scala 231:17tzQ
:
B


_T_2
0mem_cmd/:-
": 
:
:


ioreqbitsuopmem_cmd�functional-unit.scala 231:17zzW
!:
B


_T_2
0
bypassable2:0
": 
:
:


ioreqbitsuop
bypassable�functional-unit.scala 231:17xzU
 :
B


_T_2
0	exc_cause1:/
": 
:
:


ioreqbitsuop	exc_cause�functional-unit.scala 231:17xzU
 :
B


_T_2
0	exception1:/
": 
:
:


ioreqbitsuop	exception�functional-unit.scala 231:17zzW
!:
B


_T_2
0
stale_pdst2:0
": 
:
:


ioreqbitsuop
stale_pdst�functional-unit.scala 231:17zzW
!:
B


_T_2
0
ppred_busy2:0
": 
:
:


ioreqbitsuop
ppred_busy�functional-unit.scala 231:17xzU
 :
B


_T_2
0	prs3_busy1:/
": 
:
:


ioreqbitsuop	prs3_busy�functional-unit.scala 231:17xzU
 :
B


_T_2
0	prs2_busy1:/
": 
:
:


ioreqbitsuop	prs2_busy�functional-unit.scala 231:17xzU
 :
B


_T_2
0	prs1_busy1:/
": 
:
:


ioreqbitsuop	prs1_busy�functional-unit.scala 231:17pzM
:
B


_T_2
0ppred-:+
": 
:
:


ioreqbitsuopppred�functional-unit.scala 231:17nzK
:
B


_T_2
0prs3,:*
": 
:
:


ioreqbitsuopprs3�functional-unit.scala 231:17nzK
:
B


_T_2
0prs2,:*
": 
:
:


ioreqbitsuopprs2�functional-unit.scala 231:17nzK
:
B


_T_2
0prs1,:*
": 
:
:


ioreqbitsuopprs1�functional-unit.scala 231:17nzK
:
B


_T_2
0pdst,:*
": 
:
:


ioreqbitsuoppdst�functional-unit.scala 231:17tzQ
:
B


_T_2
0rxq_idx/:-
": 
:
:


ioreqbitsuoprxq_idx�functional-unit.scala 231:17tzQ
:
B


_T_2
0stq_idx/:-
": 
:
:


ioreqbitsuopstq_idx�functional-unit.scala 231:17tzQ
:
B


_T_2
0ldq_idx/:-
": 
:
:


ioreqbitsuopldq_idx�functional-unit.scala 231:17tzQ
:
B


_T_2
0rob_idx/:-
": 
:
:


ioreqbitsuoprob_idx�functional-unit.scala 231:17vzS
:
B


_T_2
0csr_addr0:.
": 
:
:


ioreqbitsuopcsr_addr�functional-unit.scala 231:17zzW
!:
B


_T_2
0
imm_packed2:0
": 
:
:


ioreqbitsuop
imm_packed�functional-unit.scala 231:17pzM
:
B


_T_2
0taken-:+
": 
:
:


ioreqbitsuoptaken�functional-unit.scala 231:17rzO
:
B


_T_2
0pc_lob.:,
": 
:
:


ioreqbitsuoppc_lob�functional-unit.scala 231:17xzU
 :
B


_T_2
0	edge_inst1:/
": 
:
:


ioreqbitsuop	edge_inst�functional-unit.scala 231:17tzQ
:
B


_T_2
0ftq_idx/:-
": 
:
:


ioreqbitsuopftq_idx�functional-unit.scala 231:17rzO
:
B


_T_2
0br_tag.:,
": 
:
:


ioreqbitsuopbr_tag�functional-unit.scala 231:17tzQ
:
B


_T_2
0br_mask/:-
": 
:
:


ioreqbitsuopbr_mask�functional-unit.scala 231:17rzO
:
B


_T_2
0is_sfb.:,
": 
:
:


ioreqbitsuopis_sfb�functional-unit.scala 231:17rzO
:
B


_T_2
0is_jal.:,
": 
:
:


ioreqbitsuopis_jal�functional-unit.scala 231:17tzQ
:
B


_T_2
0is_jalr/:-
": 
:
:


ioreqbitsuopis_jalr�functional-unit.scala 231:17pzM
:
B


_T_2
0is_br-:+
": 
:
:


ioreqbitsuopis_br�functional-unit.scala 231:17�z_
%:#
B


_T_2
0iw_p2_poisoned6:4
": 
:
:


ioreqbitsuopiw_p2_poisoned�functional-unit.scala 231:17�z_
%:#
B


_T_2
0iw_p1_poisoned6:4
": 
:
:


ioreqbitsuopiw_p1_poisoned�functional-unit.scala 231:17vzS
:
B


_T_2
0iw_state0:.
": 
:
:


ioreqbitsuopiw_state�functional-unit.scala 231:17�zc
':%
:
B


_T_2
0ctrlis_std8:6
,:*
": 
:
:


ioreqbitsuopctrlis_std�functional-unit.scala 231:17�zc
':%
:
B


_T_2
0ctrlis_sta8:6
,:*
": 
:
:


ioreqbitsuopctrlis_sta�functional-unit.scala 231:17�ze
(:&
:
B


_T_2
0ctrlis_load9:7
,:*
": 
:
:


ioreqbitsuopctrlis_load�functional-unit.scala 231:17�ze
(:&
:
B


_T_2
0ctrlcsr_cmd9:7
,:*
": 
:
:


ioreqbitsuopctrlcsr_cmd�functional-unit.scala 231:17�zc
':%
:
B


_T_2
0ctrlfcn_dw8:6
,:*
": 
:
:


ioreqbitsuopctrlfcn_dw�functional-unit.scala 231:17�zc
':%
:
B


_T_2
0ctrlop_fcn8:6
,:*
": 
:
:


ioreqbitsuopctrlop_fcn�functional-unit.scala 231:17�ze
(:&
:
B


_T_2
0ctrlimm_sel9:7
,:*
": 
:
:


ioreqbitsuopctrlimm_sel�functional-unit.scala 231:17�ze
(:&
:
B


_T_2
0ctrlop2_sel9:7
,:*
": 
:
:


ioreqbitsuopctrlop2_sel�functional-unit.scala 231:17�ze
(:&
:
B


_T_2
0ctrlop1_sel9:7
,:*
": 
:
:


ioreqbitsuopctrlop1_sel�functional-unit.scala 231:17�ze
(:&
:
B


_T_2
0ctrlbr_type9:7
,:*
": 
:
:


ioreqbitsuopctrlbr_type�functional-unit.scala 231:17tzQ
:
B


_T_2
0fu_code/:-
": 
:
:


ioreqbitsuopfu_code�functional-unit.scala 231:17tzQ
:
B


_T_2
0iq_type/:-
": 
:
:


ioreqbitsuopiq_type�functional-unit.scala 231:17vzS
:
B


_T_2
0debug_pc0:.
": 
:
:


ioreqbitsuopdebug_pc�functional-unit.scala 231:17rzO
:
B


_T_2
0is_rvc.:,
": 
:
:


ioreqbitsuopis_rvc�functional-unit.scala 231:17zzW
!:
B


_T_2
0
debug_inst2:0
": 
:
:


ioreqbitsuop
debug_inst�functional-unit.scala 231:17nzK
:
B


_T_2
0inst,:*
": 
:
:


ioreqbitsuopinst�functional-unit.scala 231:17nzK
:
B


_T_2
0uopc,:*
": 
:
:


ioreqbitsuopuopc�functional-unit.scala 231:17S2<
_T_94R2.:,
:
:


iobrupdateb1resolve_mask�util.scala 85:27_2H
_T_10?R=/:-
": 
:
:


ioreqbitsuopbr_mask

_T_9�util.scala 85:25Nz+
:
B


_T_2
0br_mask	

_T_10�functional-unit.scala 232:23x2`
_T_11WRU1:/
:
:


iobrupdateb1mispredict_mask:
B


_T_2
0br_mask�util.scala 118:51=2%
_T_12R	

_T_11	

0�util.scala 118:59H2%
_T_13R	

_T_12	

0�functional-unit.scala 236:39N2+
_T_14"R B


_T_1
0	

_T_13�functional-unit.scala 236:36b2?
_T_156R4#:!
:
:


ioreqbitskill	

0�functional-unit.scala 236:86F2#
_T_16R	

_T_14	

_T_15�functional-unit.scala 236:83Az
B


_T_1
1	

_T_16�functional-unit.scala 236:19izF
!:
B


_T_2
1
debug_tsrc!:
B


_T_2
0
debug_tsrc�functional-unit.scala 237:19izF
!:
B


_T_2
1
debug_fsrc!:
B


_T_2
0
debug_fsrc�functional-unit.scala 237:19izF
!:
B


_T_2
1
bp_xcpt_if!:
B


_T_2
0
bp_xcpt_if�functional-unit.scala 237:19kzH
": 
B


_T_2
1bp_debug_if": 
B


_T_2
0bp_debug_if�functional-unit.scala 237:19izF
!:
B


_T_2
1
xcpt_ma_if!:
B


_T_2
0
xcpt_ma_if�functional-unit.scala 237:19izF
!:
B


_T_2
1
xcpt_ae_if!:
B


_T_2
0
xcpt_ae_if�functional-unit.scala 237:19izF
!:
B


_T_2
1
xcpt_pf_if!:
B


_T_2
0
xcpt_pf_if�functional-unit.scala 237:19gzD
 :
B


_T_2
1	fp_single :
B


_T_2
0	fp_single�functional-unit.scala 237:19az>
:
B


_T_2
1fp_val:
B


_T_2
0fp_val�functional-unit.scala 237:19cz@
:
B


_T_2
1frs3_en:
B


_T_2
0frs3_en�functional-unit.scala 237:19izF
!:
B


_T_2
1
lrs2_rtype!:
B


_T_2
0
lrs2_rtype�functional-unit.scala 237:19izF
!:
B


_T_2
1
lrs1_rtype!:
B


_T_2
0
lrs1_rtype�functional-unit.scala 237:19gzD
 :
B


_T_2
1	dst_rtype :
B


_T_2
0	dst_rtype�functional-unit.scala 237:19ezB
:
B


_T_2
1ldst_val:
B


_T_2
0ldst_val�functional-unit.scala 237:19]z:
:
B


_T_2
1lrs3:
B


_T_2
0lrs3�functional-unit.scala 237:19]z:
:
B


_T_2
1lrs2:
B


_T_2
0lrs2�functional-unit.scala 237:19]z:
:
B


_T_2
1lrs1:
B


_T_2
0lrs1�functional-unit.scala 237:19]z:
:
B


_T_2
1ldst:
B


_T_2
0ldst�functional-unit.scala 237:19kzH
": 
B


_T_2
1ldst_is_rs1": 
B


_T_2
0ldst_is_rs1�functional-unit.scala 237:19szP
&:$
B


_T_2
1flush_on_commit&:$
B


_T_2
0flush_on_commit�functional-unit.scala 237:19gzD
 :
B


_T_2
1	is_unique :
B


_T_2
0	is_unique�functional-unit.scala 237:19ozL
$:"
B


_T_2
1is_sys_pc2epc$:"
B


_T_2
0is_sys_pc2epc�functional-unit.scala 237:19ezB
:
B


_T_2
1uses_stq:
B


_T_2
0uses_stq�functional-unit.scala 237:19ezB
:
B


_T_2
1uses_ldq:
B


_T_2
0uses_ldq�functional-unit.scala 237:19az>
:
B


_T_2
1is_amo:
B


_T_2
0is_amo�functional-unit.scala 237:19gzD
 :
B


_T_2
1	is_fencei :
B


_T_2
0	is_fencei�functional-unit.scala 237:19ezB
:
B


_T_2
1is_fence:
B


_T_2
0is_fence�functional-unit.scala 237:19izF
!:
B


_T_2
1
mem_signed!:
B


_T_2
0
mem_signed�functional-unit.scala 237:19ezB
:
B


_T_2
1mem_size:
B


_T_2
0mem_size�functional-unit.scala 237:19cz@
:
B


_T_2
1mem_cmd:
B


_T_2
0mem_cmd�functional-unit.scala 237:19izF
!:
B


_T_2
1
bypassable!:
B


_T_2
0
bypassable�functional-unit.scala 237:19gzD
 :
B


_T_2
1	exc_cause :
B


_T_2
0	exc_cause�functional-unit.scala 237:19gzD
 :
B


_T_2
1	exception :
B


_T_2
0	exception�functional-unit.scala 237:19izF
!:
B


_T_2
1
stale_pdst!:
B


_T_2
0
stale_pdst�functional-unit.scala 237:19izF
!:
B


_T_2
1
ppred_busy!:
B


_T_2
0
ppred_busy�functional-unit.scala 237:19gzD
 :
B


_T_2
1	prs3_busy :
B


_T_2
0	prs3_busy�functional-unit.scala 237:19gzD
 :
B


_T_2
1	prs2_busy :
B


_T_2
0	prs2_busy�functional-unit.scala 237:19gzD
 :
B


_T_2
1	prs1_busy :
B


_T_2
0	prs1_busy�functional-unit.scala 237:19_z<
:
B


_T_2
1ppred:
B


_T_2
0ppred�functional-unit.scala 237:19]z:
:
B


_T_2
1prs3:
B


_T_2
0prs3�functional-unit.scala 237:19]z:
:
B


_T_2
1prs2:
B


_T_2
0prs2�functional-unit.scala 237:19]z:
:
B


_T_2
1prs1:
B


_T_2
0prs1�functional-unit.scala 237:19]z:
:
B


_T_2
1pdst:
B


_T_2
0pdst�functional-unit.scala 237:19cz@
:
B


_T_2
1rxq_idx:
B


_T_2
0rxq_idx�functional-unit.scala 237:19cz@
:
B


_T_2
1stq_idx:
B


_T_2
0stq_idx�functional-unit.scala 237:19cz@
:
B


_T_2
1ldq_idx:
B


_T_2
0ldq_idx�functional-unit.scala 237:19cz@
:
B


_T_2
1rob_idx:
B


_T_2
0rob_idx�functional-unit.scala 237:19ezB
:
B


_T_2
1csr_addr:
B


_T_2
0csr_addr�functional-unit.scala 237:19izF
!:
B


_T_2
1
imm_packed!:
B


_T_2
0
imm_packed�functional-unit.scala 237:19_z<
:
B


_T_2
1taken:
B


_T_2
0taken�functional-unit.scala 237:19az>
:
B


_T_2
1pc_lob:
B


_T_2
0pc_lob�functional-unit.scala 237:19gzD
 :
B


_T_2
1	edge_inst :
B


_T_2
0	edge_inst�functional-unit.scala 237:19cz@
:
B


_T_2
1ftq_idx:
B


_T_2
0ftq_idx�functional-unit.scala 237:19az>
:
B


_T_2
1br_tag:
B


_T_2
0br_tag�functional-unit.scala 237:19cz@
:
B


_T_2
1br_mask:
B


_T_2
0br_mask�functional-unit.scala 237:19az>
:
B


_T_2
1is_sfb:
B


_T_2
0is_sfb�functional-unit.scala 237:19az>
:
B


_T_2
1is_jal:
B


_T_2
0is_jal�functional-unit.scala 237:19cz@
:
B


_T_2
1is_jalr:
B


_T_2
0is_jalr�functional-unit.scala 237:19_z<
:
B


_T_2
1is_br:
B


_T_2
0is_br�functional-unit.scala 237:19qzN
%:#
B


_T_2
1iw_p2_poisoned%:#
B


_T_2
0iw_p2_poisoned�functional-unit.scala 237:19qzN
%:#
B


_T_2
1iw_p1_poisoned%:#
B


_T_2
0iw_p1_poisoned�functional-unit.scala 237:19ezB
:
B


_T_2
1iw_state:
B


_T_2
0iw_state�functional-unit.scala 237:19uzR
':%
:
B


_T_2
1ctrlis_std':%
:
B


_T_2
0ctrlis_std�functional-unit.scala 237:19uzR
':%
:
B


_T_2
1ctrlis_sta':%
:
B


_T_2
0ctrlis_sta�functional-unit.scala 237:19wzT
(:&
:
B


_T_2
1ctrlis_load(:&
:
B


_T_2
0ctrlis_load�functional-unit.scala 237:19wzT
(:&
:
B


_T_2
1ctrlcsr_cmd(:&
:
B


_T_2
0ctrlcsr_cmd�functional-unit.scala 237:19uzR
':%
:
B


_T_2
1ctrlfcn_dw':%
:
B


_T_2
0ctrlfcn_dw�functional-unit.scala 237:19uzR
':%
:
B


_T_2
1ctrlop_fcn':%
:
B


_T_2
0ctrlop_fcn�functional-unit.scala 237:19wzT
(:&
:
B


_T_2
1ctrlimm_sel(:&
:
B


_T_2
0ctrlimm_sel�functional-unit.scala 237:19wzT
(:&
:
B


_T_2
1ctrlop2_sel(:&
:
B


_T_2
0ctrlop2_sel�functional-unit.scala 237:19wzT
(:&
:
B


_T_2
1ctrlop1_sel(:&
:
B


_T_2
0ctrlop1_sel�functional-unit.scala 237:19wzT
(:&
:
B


_T_2
1ctrlbr_type(:&
:
B


_T_2
0ctrlbr_type�functional-unit.scala 237:19cz@
:
B


_T_2
1fu_code:
B


_T_2
0fu_code�functional-unit.scala 237:19cz@
:
B


_T_2
1iq_type:
B


_T_2
0iq_type�functional-unit.scala 237:19ezB
:
B


_T_2
1debug_pc:
B


_T_2
0debug_pc�functional-unit.scala 237:19az>
:
B


_T_2
1is_rvc:
B


_T_2
0is_rvc�functional-unit.scala 237:19izF
!:
B


_T_2
1
debug_inst!:
B


_T_2
0
debug_inst�functional-unit.scala 237:19]z:
:
B


_T_2
1inst:
B


_T_2
0inst�functional-unit.scala 237:19]z:
:
B


_T_2
1uopc:
B


_T_2
0uopc�functional-unit.scala 237:19T2=
_T_174R2.:,
:
:


iobrupdateb1resolve_mask�util.scala 85:27O28
_T_18/R-:
B


_T_2
0br_mask	

_T_17�util.scala 85:25Nz+
:
B


_T_2
1br_mask	

_T_18�functional-unit.scala 238:25x2`
_T_19WRU1:/
:
:


iobrupdateb1mispredict_mask:
B


_T_2
1br_mask�util.scala 118:51=2%
_T_20R	

_T_19	

0�util.scala 118:59H2%
_T_21R	

_T_20	

0�functional-unit.scala 236:39N2+
_T_22"R B


_T_1
1	

_T_21�functional-unit.scala 236:36b2?
_T_236R4#:!
:
:


ioreqbitskill	

0�functional-unit.scala 236:86F2#
_T_24R	

_T_22	

_T_23�functional-unit.scala 236:83Az
B


_T_1
2	

_T_24�functional-unit.scala 236:19izF
!:
B


_T_2
2
debug_tsrc!:
B


_T_2
1
debug_tsrc�functional-unit.scala 237:19izF
!:
B


_T_2
2
debug_fsrc!:
B


_T_2
1
debug_fsrc�functional-unit.scala 237:19izF
!:
B


_T_2
2
bp_xcpt_if!:
B


_T_2
1
bp_xcpt_if�functional-unit.scala 237:19kzH
": 
B


_T_2
2bp_debug_if": 
B


_T_2
1bp_debug_if�functional-unit.scala 237:19izF
!:
B


_T_2
2
xcpt_ma_if!:
B


_T_2
1
xcpt_ma_if�functional-unit.scala 237:19izF
!:
B


_T_2
2
xcpt_ae_if!:
B


_T_2
1
xcpt_ae_if�functional-unit.scala 237:19izF
!:
B


_T_2
2
xcpt_pf_if!:
B


_T_2
1
xcpt_pf_if�functional-unit.scala 237:19gzD
 :
B


_T_2
2	fp_single :
B


_T_2
1	fp_single�functional-unit.scala 237:19az>
:
B


_T_2
2fp_val:
B


_T_2
1fp_val�functional-unit.scala 237:19cz@
:
B


_T_2
2frs3_en:
B


_T_2
1frs3_en�functional-unit.scala 237:19izF
!:
B


_T_2
2
lrs2_rtype!:
B


_T_2
1
lrs2_rtype�functional-unit.scala 237:19izF
!:
B


_T_2
2
lrs1_rtype!:
B


_T_2
1
lrs1_rtype�functional-unit.scala 237:19gzD
 :
B


_T_2
2	dst_rtype :
B


_T_2
1	dst_rtype�functional-unit.scala 237:19ezB
:
B


_T_2
2ldst_val:
B


_T_2
1ldst_val�functional-unit.scala 237:19]z:
:
B


_T_2
2lrs3:
B


_T_2
1lrs3�functional-unit.scala 237:19]z:
:
B


_T_2
2lrs2:
B


_T_2
1lrs2�functional-unit.scala 237:19]z:
:
B


_T_2
2lrs1:
B


_T_2
1lrs1�functional-unit.scala 237:19]z:
:
B


_T_2
2ldst:
B


_T_2
1ldst�functional-unit.scala 237:19kzH
": 
B


_T_2
2ldst_is_rs1": 
B


_T_2
1ldst_is_rs1�functional-unit.scala 237:19szP
&:$
B


_T_2
2flush_on_commit&:$
B


_T_2
1flush_on_commit�functional-unit.scala 237:19gzD
 :
B


_T_2
2	is_unique :
B


_T_2
1	is_unique�functional-unit.scala 237:19ozL
$:"
B


_T_2
2is_sys_pc2epc$:"
B


_T_2
1is_sys_pc2epc�functional-unit.scala 237:19ezB
:
B


_T_2
2uses_stq:
B


_T_2
1uses_stq�functional-unit.scala 237:19ezB
:
B


_T_2
2uses_ldq:
B


_T_2
1uses_ldq�functional-unit.scala 237:19az>
:
B


_T_2
2is_amo:
B


_T_2
1is_amo�functional-unit.scala 237:19gzD
 :
B


_T_2
2	is_fencei :
B


_T_2
1	is_fencei�functional-unit.scala 237:19ezB
:
B


_T_2
2is_fence:
B


_T_2
1is_fence�functional-unit.scala 237:19izF
!:
B


_T_2
2
mem_signed!:
B


_T_2
1
mem_signed�functional-unit.scala 237:19ezB
:
B


_T_2
2mem_size:
B


_T_2
1mem_size�functional-unit.scala 237:19cz@
:
B


_T_2
2mem_cmd:
B


_T_2
1mem_cmd�functional-unit.scala 237:19izF
!:
B


_T_2
2
bypassable!:
B


_T_2
1
bypassable�functional-unit.scala 237:19gzD
 :
B


_T_2
2	exc_cause :
B


_T_2
1	exc_cause�functional-unit.scala 237:19gzD
 :
B


_T_2
2	exception :
B


_T_2
1	exception�functional-unit.scala 237:19izF
!:
B


_T_2
2
stale_pdst!:
B


_T_2
1
stale_pdst�functional-unit.scala 237:19izF
!:
B


_T_2
2
ppred_busy!:
B


_T_2
1
ppred_busy�functional-unit.scala 237:19gzD
 :
B


_T_2
2	prs3_busy :
B


_T_2
1	prs3_busy�functional-unit.scala 237:19gzD
 :
B


_T_2
2	prs2_busy :
B


_T_2
1	prs2_busy�functional-unit.scala 237:19gzD
 :
B


_T_2
2	prs1_busy :
B


_T_2
1	prs1_busy�functional-unit.scala 237:19_z<
:
B


_T_2
2ppred:
B


_T_2
1ppred�functional-unit.scala 237:19]z:
:
B


_T_2
2prs3:
B


_T_2
1prs3�functional-unit.scala 237:19]z:
:
B


_T_2
2prs2:
B


_T_2
1prs2�functional-unit.scala 237:19]z:
:
B


_T_2
2prs1:
B


_T_2
1prs1�functional-unit.scala 237:19]z:
:
B


_T_2
2pdst:
B


_T_2
1pdst�functional-unit.scala 237:19cz@
:
B


_T_2
2rxq_idx:
B


_T_2
1rxq_idx�functional-unit.scala 237:19cz@
:
B


_T_2
2stq_idx:
B


_T_2
1stq_idx�functional-unit.scala 237:19cz@
:
B


_T_2
2ldq_idx:
B


_T_2
1ldq_idx�functional-unit.scala 237:19cz@
:
B


_T_2
2rob_idx:
B


_T_2
1rob_idx�functional-unit.scala 237:19ezB
:
B


_T_2
2csr_addr:
B


_T_2
1csr_addr�functional-unit.scala 237:19izF
!:
B


_T_2
2
imm_packed!:
B


_T_2
1
imm_packed�functional-unit.scala 237:19_z<
:
B


_T_2
2taken:
B


_T_2
1taken�functional-unit.scala 237:19az>
:
B


_T_2
2pc_lob:
B


_T_2
1pc_lob�functional-unit.scala 237:19gzD
 :
B


_T_2
2	edge_inst :
B


_T_2
1	edge_inst�functional-unit.scala 237:19cz@
:
B


_T_2
2ftq_idx:
B


_T_2
1ftq_idx�functional-unit.scala 237:19az>
:
B


_T_2
2br_tag:
B


_T_2
1br_tag�functional-unit.scala 237:19cz@
:
B


_T_2
2br_mask:
B


_T_2
1br_mask�functional-unit.scala 237:19az>
:
B


_T_2
2is_sfb:
B


_T_2
1is_sfb�functional-unit.scala 237:19az>
:
B


_T_2
2is_jal:
B


_T_2
1is_jal�functional-unit.scala 237:19cz@
:
B


_T_2
2is_jalr:
B


_T_2
1is_jalr�functional-unit.scala 237:19_z<
:
B


_T_2
2is_br:
B


_T_2
1is_br�functional-unit.scala 237:19qzN
%:#
B


_T_2
2iw_p2_poisoned%:#
B


_T_2
1iw_p2_poisoned�functional-unit.scala 237:19qzN
%:#
B


_T_2
2iw_p1_poisoned%:#
B


_T_2
1iw_p1_poisoned�functional-unit.scala 237:19ezB
:
B


_T_2
2iw_state:
B


_T_2
1iw_state�functional-unit.scala 237:19uzR
':%
:
B


_T_2
2ctrlis_std':%
:
B


_T_2
1ctrlis_std�functional-unit.scala 237:19uzR
':%
:
B


_T_2
2ctrlis_sta':%
:
B


_T_2
1ctrlis_sta�functional-unit.scala 237:19wzT
(:&
:
B


_T_2
2ctrlis_load(:&
:
B


_T_2
1ctrlis_load�functional-unit.scala 237:19wzT
(:&
:
B


_T_2
2ctrlcsr_cmd(:&
:
B


_T_2
1ctrlcsr_cmd�functional-unit.scala 237:19uzR
':%
:
B


_T_2
2ctrlfcn_dw':%
:
B


_T_2
1ctrlfcn_dw�functional-unit.scala 237:19uzR
':%
:
B


_T_2
2ctrlop_fcn':%
:
B


_T_2
1ctrlop_fcn�functional-unit.scala 237:19wzT
(:&
:
B


_T_2
2ctrlimm_sel(:&
:
B


_T_2
1ctrlimm_sel�functional-unit.scala 237:19wzT
(:&
:
B


_T_2
2ctrlop2_sel(:&
:
B


_T_2
1ctrlop2_sel�functional-unit.scala 237:19wzT
(:&
:
B


_T_2
2ctrlop1_sel(:&
:
B


_T_2
1ctrlop1_sel�functional-unit.scala 237:19wzT
(:&
:
B


_T_2
2ctrlbr_type(:&
:
B


_T_2
1ctrlbr_type�functional-unit.scala 237:19cz@
:
B


_T_2
2fu_code:
B


_T_2
1fu_code�functional-unit.scala 237:19cz@
:
B


_T_2
2iq_type:
B


_T_2
1iq_type�functional-unit.scala 237:19ezB
:
B


_T_2
2debug_pc:
B


_T_2
1debug_pc�functional-unit.scala 237:19az>
:
B


_T_2
2is_rvc:
B


_T_2
1is_rvc�functional-unit.scala 237:19izF
!:
B


_T_2
2
debug_inst!:
B


_T_2
1
debug_inst�functional-unit.scala 237:19]z:
:
B


_T_2
2inst:
B


_T_2
1inst�functional-unit.scala 237:19]z:
:
B


_T_2
2uopc:
B


_T_2
1uopc�functional-unit.scala 237:19T2=
_T_254R2.:,
:
:


iobrupdateb1resolve_mask�util.scala 85:27O28
_T_26/R-:
B


_T_2
1br_mask	

_T_25�util.scala 85:25Nz+
:
B


_T_2
2br_mask	

_T_26�functional-unit.scala 238:25x2`
_T_27WRU1:/
:
:


iobrupdateb1mispredict_mask:
B


_T_2
2br_mask�util.scala 118:51=2%
_T_28R	

_T_27	

0�util.scala 118:59H2%
_T_29R	

_T_28	

0�functional-unit.scala 247:50N2+
_T_30"R B


_T_1
2	

_T_29�functional-unit.scala 247:47Kz(
:
:


iorespvalid	

_T_30�functional-unit.scala 247:22\z9
*:(
:
:


iorespbits
predicated	

0�functional-unit.scala 248:29{zX
3:1
#:!
:
:


iorespbitsuop
debug_tsrc!:
B


_T_2
2
debug_tsrc�functional-unit.scala 249:22{zX
3:1
#:!
:
:


iorespbitsuop
debug_fsrc!:
B


_T_2
2
debug_fsrc�functional-unit.scala 249:22{zX
3:1
#:!
:
:


iorespbitsuop
bp_xcpt_if!:
B


_T_2
2
bp_xcpt_if�functional-unit.scala 249:22}zZ
4:2
#:!
:
:


iorespbitsuopbp_debug_if": 
B


_T_2
2bp_debug_if�functional-unit.scala 249:22{zX
3:1
#:!
:
:


iorespbitsuop
xcpt_ma_if!:
B


_T_2
2
xcpt_ma_if�functional-unit.scala 249:22{zX
3:1
#:!
:
:


iorespbitsuop
xcpt_ae_if!:
B


_T_2
2
xcpt_ae_if�functional-unit.scala 249:22{zX
3:1
#:!
:
:


iorespbitsuop
xcpt_pf_if!:
B


_T_2
2
xcpt_pf_if�functional-unit.scala 249:22yzV
2:0
#:!
:
:


iorespbitsuop	fp_single :
B


_T_2
2	fp_single�functional-unit.scala 249:22szP
/:-
#:!
:
:


iorespbitsuopfp_val:
B


_T_2
2fp_val�functional-unit.scala 249:22uzR
0:.
#:!
:
:


iorespbitsuopfrs3_en:
B


_T_2
2frs3_en�functional-unit.scala 249:22{zX
3:1
#:!
:
:


iorespbitsuop
lrs2_rtype!:
B


_T_2
2
lrs2_rtype�functional-unit.scala 249:22{zX
3:1
#:!
:
:


iorespbitsuop
lrs1_rtype!:
B


_T_2
2
lrs1_rtype�functional-unit.scala 249:22yzV
2:0
#:!
:
:


iorespbitsuop	dst_rtype :
B


_T_2
2	dst_rtype�functional-unit.scala 249:22wzT
1:/
#:!
:
:


iorespbitsuopldst_val:
B


_T_2
2ldst_val�functional-unit.scala 249:22ozL
-:+
#:!
:
:


iorespbitsuoplrs3:
B


_T_2
2lrs3�functional-unit.scala 249:22ozL
-:+
#:!
:
:


iorespbitsuoplrs2:
B


_T_2
2lrs2�functional-unit.scala 249:22ozL
-:+
#:!
:
:


iorespbitsuoplrs1:
B


_T_2
2lrs1�functional-unit.scala 249:22ozL
-:+
#:!
:
:


iorespbitsuopldst:
B


_T_2
2ldst�functional-unit.scala 249:22}zZ
4:2
#:!
:
:


iorespbitsuopldst_is_rs1": 
B


_T_2
2ldst_is_rs1�functional-unit.scala 249:22�zb
8:6
#:!
:
:


iorespbitsuopflush_on_commit&:$
B


_T_2
2flush_on_commit�functional-unit.scala 249:22yzV
2:0
#:!
:
:


iorespbitsuop	is_unique :
B


_T_2
2	is_unique�functional-unit.scala 249:22�z^
6:4
#:!
:
:


iorespbitsuopis_sys_pc2epc$:"
B


_T_2
2is_sys_pc2epc�functional-unit.scala 249:22wzT
1:/
#:!
:
:


iorespbitsuopuses_stq:
B


_T_2
2uses_stq�functional-unit.scala 249:22wzT
1:/
#:!
:
:


iorespbitsuopuses_ldq:
B


_T_2
2uses_ldq�functional-unit.scala 249:22szP
/:-
#:!
:
:


iorespbitsuopis_amo:
B


_T_2
2is_amo�functional-unit.scala 249:22yzV
2:0
#:!
:
:


iorespbitsuop	is_fencei :
B


_T_2
2	is_fencei�functional-unit.scala 249:22wzT
1:/
#:!
:
:


iorespbitsuopis_fence:
B


_T_2
2is_fence�functional-unit.scala 249:22{zX
3:1
#:!
:
:


iorespbitsuop
mem_signed!:
B


_T_2
2
mem_signed�functional-unit.scala 249:22wzT
1:/
#:!
:
:


iorespbitsuopmem_size:
B


_T_2
2mem_size�functional-unit.scala 249:22uzR
0:.
#:!
:
:


iorespbitsuopmem_cmd:
B


_T_2
2mem_cmd�functional-unit.scala 249:22{zX
3:1
#:!
:
:


iorespbitsuop
bypassable!:
B


_T_2
2
bypassable�functional-unit.scala 249:22yzV
2:0
#:!
:
:


iorespbitsuop	exc_cause :
B


_T_2
2	exc_cause�functional-unit.scala 249:22yzV
2:0
#:!
:
:


iorespbitsuop	exception :
B


_T_2
2	exception�functional-unit.scala 249:22{zX
3:1
#:!
:
:


iorespbitsuop
stale_pdst!:
B


_T_2
2
stale_pdst�functional-unit.scala 249:22{zX
3:1
#:!
:
:


iorespbitsuop
ppred_busy!:
B


_T_2
2
ppred_busy�functional-unit.scala 249:22yzV
2:0
#:!
:
:


iorespbitsuop	prs3_busy :
B


_T_2
2	prs3_busy�functional-unit.scala 249:22yzV
2:0
#:!
:
:


iorespbitsuop	prs2_busy :
B


_T_2
2	prs2_busy�functional-unit.scala 249:22yzV
2:0
#:!
:
:


iorespbitsuop	prs1_busy :
B


_T_2
2	prs1_busy�functional-unit.scala 249:22qzN
.:,
#:!
:
:


iorespbitsuopppred:
B


_T_2
2ppred�functional-unit.scala 249:22ozL
-:+
#:!
:
:


iorespbitsuopprs3:
B


_T_2
2prs3�functional-unit.scala 249:22ozL
-:+
#:!
:
:


iorespbitsuopprs2:
B


_T_2
2prs2�functional-unit.scala 249:22ozL
-:+
#:!
:
:


iorespbitsuopprs1:
B


_T_2
2prs1�functional-unit.scala 249:22ozL
-:+
#:!
:
:


iorespbitsuoppdst:
B


_T_2
2pdst�functional-unit.scala 249:22uzR
0:.
#:!
:
:


iorespbitsuoprxq_idx:
B


_T_2
2rxq_idx�functional-unit.scala 249:22uzR
0:.
#:!
:
:


iorespbitsuopstq_idx:
B


_T_2
2stq_idx�functional-unit.scala 249:22uzR
0:.
#:!
:
:


iorespbitsuopldq_idx:
B


_T_2
2ldq_idx�functional-unit.scala 249:22uzR
0:.
#:!
:
:


iorespbitsuoprob_idx:
B


_T_2
2rob_idx�functional-unit.scala 249:22wzT
1:/
#:!
:
:


iorespbitsuopcsr_addr:
B


_T_2
2csr_addr�functional-unit.scala 249:22{zX
3:1
#:!
:
:


iorespbitsuop
imm_packed!:
B


_T_2
2
imm_packed�functional-unit.scala 249:22qzN
.:,
#:!
:
:


iorespbitsuoptaken:
B


_T_2
2taken�functional-unit.scala 249:22szP
/:-
#:!
:
:


iorespbitsuoppc_lob:
B


_T_2
2pc_lob�functional-unit.scala 249:22yzV
2:0
#:!
:
:


iorespbitsuop	edge_inst :
B


_T_2
2	edge_inst�functional-unit.scala 249:22uzR
0:.
#:!
:
:


iorespbitsuopftq_idx:
B


_T_2
2ftq_idx�functional-unit.scala 249:22szP
/:-
#:!
:
:


iorespbitsuopbr_tag:
B


_T_2
2br_tag�functional-unit.scala 249:22uzR
0:.
#:!
:
:


iorespbitsuopbr_mask:
B


_T_2
2br_mask�functional-unit.scala 249:22szP
/:-
#:!
:
:


iorespbitsuopis_sfb:
B


_T_2
2is_sfb�functional-unit.scala 249:22szP
/:-
#:!
:
:


iorespbitsuopis_jal:
B


_T_2
2is_jal�functional-unit.scala 249:22uzR
0:.
#:!
:
:


iorespbitsuopis_jalr:
B


_T_2
2is_jalr�functional-unit.scala 249:22qzN
.:,
#:!
:
:


iorespbitsuopis_br:
B


_T_2
2is_br�functional-unit.scala 249:22�z`
7:5
#:!
:
:


iorespbitsuopiw_p2_poisoned%:#
B


_T_2
2iw_p2_poisoned�functional-unit.scala 249:22�z`
7:5
#:!
:
:


iorespbitsuopiw_p1_poisoned%:#
B


_T_2
2iw_p1_poisoned�functional-unit.scala 249:22wzT
1:/
#:!
:
:


iorespbitsuopiw_state:
B


_T_2
2iw_state�functional-unit.scala 249:22�zd
9:7
-:+
#:!
:
:


iorespbitsuopctrlis_std':%
:
B


_T_2
2ctrlis_std�functional-unit.scala 249:22�zd
9:7
-:+
#:!
:
:


iorespbitsuopctrlis_sta':%
:
B


_T_2
2ctrlis_sta�functional-unit.scala 249:22�zf
::8
-:+
#:!
:
:


iorespbitsuopctrlis_load(:&
:
B


_T_2
2ctrlis_load�functional-unit.scala 249:22�zf
::8
-:+
#:!
:
:


iorespbitsuopctrlcsr_cmd(:&
:
B


_T_2
2ctrlcsr_cmd�functional-unit.scala 249:22�zd
9:7
-:+
#:!
:
:


iorespbitsuopctrlfcn_dw':%
:
B


_T_2
2ctrlfcn_dw�functional-unit.scala 249:22�zd
9:7
-:+
#:!
:
:


iorespbitsuopctrlop_fcn':%
:
B


_T_2
2ctrlop_fcn�functional-unit.scala 249:22�zf
::8
-:+
#:!
:
:


iorespbitsuopctrlimm_sel(:&
:
B


_T_2
2ctrlimm_sel�functional-unit.scala 249:22�zf
::8
-:+
#:!
:
:


iorespbitsuopctrlop2_sel(:&
:
B


_T_2
2ctrlop2_sel�functional-unit.scala 249:22�zf
::8
-:+
#:!
:
:


iorespbitsuopctrlop1_sel(:&
:
B


_T_2
2ctrlop1_sel�functional-unit.scala 249:22�zf
::8
-:+
#:!
:
:


iorespbitsuopctrlbr_type(:&
:
B


_T_2
2ctrlbr_type�functional-unit.scala 249:22uzR
0:.
#:!
:
:


iorespbitsuopfu_code:
B


_T_2
2fu_code�functional-unit.scala 249:22uzR
0:.
#:!
:
:


iorespbitsuopiq_type:
B


_T_2
2iq_type�functional-unit.scala 249:22wzT
1:/
#:!
:
:


iorespbitsuopdebug_pc:
B


_T_2
2debug_pc�functional-unit.scala 249:22szP
/:-
#:!
:
:


iorespbitsuopis_rvc:
B


_T_2
2is_rvc�functional-unit.scala 249:22{zX
3:1
#:!
:
:


iorespbitsuop
debug_inst!:
B


_T_2
2
debug_inst�functional-unit.scala 249:22ozL
-:+
#:!
:
:


iorespbitsuopinst:
B


_T_2
2inst�functional-unit.scala 249:22ozL
-:+
#:!
:
:


iorespbitsuopuopc:
B


_T_2
2uopc�functional-unit.scala 249:22T2=
_T_314R2.:,
:
:


iobrupdateb1resolve_mask�util.scala 85:27O28
_T_32/R-:
B


_T_2
2br_mask	

_T_31�util.scala 85:25`z=
0:.
#:!
:
:


iorespbitsuopbr_mask	

_T_32�functional-unit.scala 250:30>*
imulPipelinedMultiplier�functional-unit.scala 703:20'z 
:


imulclock	

clock�
 'z 
:


imulreset	

reset�
 ezB
$:"
:
:


imulioreqvalid:
:


ioreqvalid�functional-unit.scala 705:24�zg
+:)
#:!
:
:


imulioreqbitsfn8:6
,:*
": 
:
:


ioreqbitsuopctrlop_fcn�functional-unit.scala 706:24�zg
+:)
#:!
:
:


imulioreqbitsdw8:6
,:*
": 
:
:


ioreqbitsuopctrlfcn_dw�functional-unit.scala 707:24zzW
,:*
#:!
:
:


imulioreqbitsin1':%
:
:


ioreqbitsrs1_data�functional-unit.scala 708:24zzW
,:*
#:!
:
:


imulioreqbitsin2':%
:
:


ioreqbitsrs2_data�functional-unit.scala 709:24R�.
,:*
#:!
:
:


imulioreqbitstag�functional-unit.scala 710:24yzV
$:"
:
:


iorespbitsdata.:,
$:"
:
:


imuliorespbitsdata�functional-unit.scala 712:24
PipelinedMulUnit