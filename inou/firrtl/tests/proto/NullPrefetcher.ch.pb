
�H
�G�G
NullPrefetcher
clock" 
reset
�
io�*�

mshr_avail

req_val

req_addr
(
 req_coh*
state

�prefetch�*�
ready

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
addr
(
data
@
is_hella
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
 Kz.
:
:


ioprefetchvalid	

0�prefetcher.scala 41:21L�.
,:*
:
:


ioprefetchbitsis_hella�prefetcher.scala 42:21H�*
(:&
:
:


ioprefetchbitsdata�prefetcher.scala 42:21H�*
(:&
:
:


ioprefetchbitsaddr�prefetcher.scala 42:21W�9
7:5
':%
:
:


ioprefetchbitsuop
debug_tsrc�prefetcher.scala 42:21W�9
7:5
':%
:
:


ioprefetchbitsuop
debug_fsrc�prefetcher.scala 42:21W�9
7:5
':%
:
:


ioprefetchbitsuop
bp_xcpt_if�prefetcher.scala 42:21X�:
8:6
':%
:
:


ioprefetchbitsuopbp_debug_if�prefetcher.scala 42:21W�9
7:5
':%
:
:


ioprefetchbitsuop
xcpt_ma_if�prefetcher.scala 42:21W�9
7:5
':%
:
:


ioprefetchbitsuop
xcpt_ae_if�prefetcher.scala 42:21W�9
7:5
':%
:
:


ioprefetchbitsuop
xcpt_pf_if�prefetcher.scala 42:21V�8
6:4
':%
:
:


ioprefetchbitsuop	fp_single�prefetcher.scala 42:21S�5
3:1
':%
:
:


ioprefetchbitsuopfp_val�prefetcher.scala 42:21T�6
4:2
':%
:
:


ioprefetchbitsuopfrs3_en�prefetcher.scala 42:21W�9
7:5
':%
:
:


ioprefetchbitsuop
lrs2_rtype�prefetcher.scala 42:21W�9
7:5
':%
:
:


ioprefetchbitsuop
lrs1_rtype�prefetcher.scala 42:21V�8
6:4
':%
:
:


ioprefetchbitsuop	dst_rtype�prefetcher.scala 42:21U�7
5:3
':%
:
:


ioprefetchbitsuopldst_val�prefetcher.scala 42:21Q�3
1:/
':%
:
:


ioprefetchbitsuoplrs3�prefetcher.scala 42:21Q�3
1:/
':%
:
:


ioprefetchbitsuoplrs2�prefetcher.scala 42:21Q�3
1:/
':%
:
:


ioprefetchbitsuoplrs1�prefetcher.scala 42:21Q�3
1:/
':%
:
:


ioprefetchbitsuopldst�prefetcher.scala 42:21X�:
8:6
':%
:
:


ioprefetchbitsuopldst_is_rs1�prefetcher.scala 42:21\�>
<::
':%
:
:


ioprefetchbitsuopflush_on_commit�prefetcher.scala 42:21V�8
6:4
':%
:
:


ioprefetchbitsuop	is_unique�prefetcher.scala 42:21Z�<
::8
':%
:
:


ioprefetchbitsuopis_sys_pc2epc�prefetcher.scala 42:21U�7
5:3
':%
:
:


ioprefetchbitsuopuses_stq�prefetcher.scala 42:21U�7
5:3
':%
:
:


ioprefetchbitsuopuses_ldq�prefetcher.scala 42:21S�5
3:1
':%
:
:


ioprefetchbitsuopis_amo�prefetcher.scala 42:21V�8
6:4
':%
:
:


ioprefetchbitsuop	is_fencei�prefetcher.scala 42:21U�7
5:3
':%
:
:


ioprefetchbitsuopis_fence�prefetcher.scala 42:21W�9
7:5
':%
:
:


ioprefetchbitsuop
mem_signed�prefetcher.scala 42:21U�7
5:3
':%
:
:


ioprefetchbitsuopmem_size�prefetcher.scala 42:21T�6
4:2
':%
:
:


ioprefetchbitsuopmem_cmd�prefetcher.scala 42:21W�9
7:5
':%
:
:


ioprefetchbitsuop
bypassable�prefetcher.scala 42:21V�8
6:4
':%
:
:


ioprefetchbitsuop	exc_cause�prefetcher.scala 42:21V�8
6:4
':%
:
:


ioprefetchbitsuop	exception�prefetcher.scala 42:21W�9
7:5
':%
:
:


ioprefetchbitsuop
stale_pdst�prefetcher.scala 42:21W�9
7:5
':%
:
:


ioprefetchbitsuop
ppred_busy�prefetcher.scala 42:21V�8
6:4
':%
:
:


ioprefetchbitsuop	prs3_busy�prefetcher.scala 42:21V�8
6:4
':%
:
:


ioprefetchbitsuop	prs2_busy�prefetcher.scala 42:21V�8
6:4
':%
:
:


ioprefetchbitsuop	prs1_busy�prefetcher.scala 42:21R�4
2:0
':%
:
:


ioprefetchbitsuopppred�prefetcher.scala 42:21Q�3
1:/
':%
:
:


ioprefetchbitsuopprs3�prefetcher.scala 42:21Q�3
1:/
':%
:
:


ioprefetchbitsuopprs2�prefetcher.scala 42:21Q�3
1:/
':%
:
:


ioprefetchbitsuopprs1�prefetcher.scala 42:21Q�3
1:/
':%
:
:


ioprefetchbitsuoppdst�prefetcher.scala 42:21T�6
4:2
':%
:
:


ioprefetchbitsuoprxq_idx�prefetcher.scala 42:21T�6
4:2
':%
:
:


ioprefetchbitsuopstq_idx�prefetcher.scala 42:21T�6
4:2
':%
:
:


ioprefetchbitsuopldq_idx�prefetcher.scala 42:21T�6
4:2
':%
:
:


ioprefetchbitsuoprob_idx�prefetcher.scala 42:21U�7
5:3
':%
:
:


ioprefetchbitsuopcsr_addr�prefetcher.scala 42:21W�9
7:5
':%
:
:


ioprefetchbitsuop
imm_packed�prefetcher.scala 42:21R�4
2:0
':%
:
:


ioprefetchbitsuoptaken�prefetcher.scala 42:21S�5
3:1
':%
:
:


ioprefetchbitsuoppc_lob�prefetcher.scala 42:21V�8
6:4
':%
:
:


ioprefetchbitsuop	edge_inst�prefetcher.scala 42:21T�6
4:2
':%
:
:


ioprefetchbitsuopftq_idx�prefetcher.scala 42:21S�5
3:1
':%
:
:


ioprefetchbitsuopbr_tag�prefetcher.scala 42:21T�6
4:2
':%
:
:


ioprefetchbitsuopbr_mask�prefetcher.scala 42:21S�5
3:1
':%
:
:


ioprefetchbitsuopis_sfb�prefetcher.scala 42:21S�5
3:1
':%
:
:


ioprefetchbitsuopis_jal�prefetcher.scala 42:21T�6
4:2
':%
:
:


ioprefetchbitsuopis_jalr�prefetcher.scala 42:21R�4
2:0
':%
:
:


ioprefetchbitsuopis_br�prefetcher.scala 42:21[�=
;:9
':%
:
:


ioprefetchbitsuopiw_p2_poisoned�prefetcher.scala 42:21[�=
;:9
':%
:
:


ioprefetchbitsuopiw_p1_poisoned�prefetcher.scala 42:21U�7
5:3
':%
:
:


ioprefetchbitsuopiw_state�prefetcher.scala 42:21]�?
=:;
1:/
':%
:
:


ioprefetchbitsuopctrlis_std�prefetcher.scala 42:21]�?
=:;
1:/
':%
:
:


ioprefetchbitsuopctrlis_sta�prefetcher.scala 42:21^�@
>:<
1:/
':%
:
:


ioprefetchbitsuopctrlis_load�prefetcher.scala 42:21^�@
>:<
1:/
':%
:
:


ioprefetchbitsuopctrlcsr_cmd�prefetcher.scala 42:21]�?
=:;
1:/
':%
:
:


ioprefetchbitsuopctrlfcn_dw�prefetcher.scala 42:21]�?
=:;
1:/
':%
:
:


ioprefetchbitsuopctrlop_fcn�prefetcher.scala 42:21^�@
>:<
1:/
':%
:
:


ioprefetchbitsuopctrlimm_sel�prefetcher.scala 42:21^�@
>:<
1:/
':%
:
:


ioprefetchbitsuopctrlop2_sel�prefetcher.scala 42:21^�@
>:<
1:/
':%
:
:


ioprefetchbitsuopctrlop1_sel�prefetcher.scala 42:21^�@
>:<
1:/
':%
:
:


ioprefetchbitsuopctrlbr_type�prefetcher.scala 42:21T�6
4:2
':%
:
:


ioprefetchbitsuopfu_code�prefetcher.scala 42:21T�6
4:2
':%
:
:


ioprefetchbitsuopiq_type�prefetcher.scala 42:21U�7
5:3
':%
:
:


ioprefetchbitsuopdebug_pc�prefetcher.scala 42:21S�5
3:1
':%
:
:


ioprefetchbitsuopis_rvc�prefetcher.scala 42:21W�9
7:5
':%
:
:


ioprefetchbitsuop
debug_inst�prefetcher.scala 42:21Q�3
1:/
':%
:
:


ioprefetchbitsuopinst�prefetcher.scala 42:21Q�3
1:/
':%
:
:


ioprefetchbitsuopuopc�prefetcher.scala 42:21
NullPrefetcher