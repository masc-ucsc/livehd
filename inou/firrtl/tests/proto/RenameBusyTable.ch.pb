
)
ÿ(ü(
RenameBusyTable
clock" 
reset
¡
io*
Áren_uops²2¯
ª*§
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

Æctrl½*º
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
 Z7

busy_table
4	

clock"	

reset*	

04rename-busytable.scala 48:27O26
_T0R.
	

1B
:


iowb_pdsts
0OneHot.scala 58:35P26
_T_1.R,B
:


io	wb_valids
0
0
0Bitwise.scala 72:15W2=
_T_2523


_T_1

45035996273704954	

04Bitwise.scala 72:12A2
_T_3R

_T

_T_2rename-busytable.scala 51:48Q28
_T_40R.
	

1B
:


iowb_pdsts
1OneHot.scala 58:35P26
_T_5.R,B
:


io	wb_valids
1
0
0Bitwise.scala 72:15W2=
_T_6523


_T_5

45035996273704954	

04Bitwise.scala 72:12C2 
_T_7R

_T_4

_T_6rename-busytable.scala 51:48Q28
_T_80R.
	

1B
:


iowb_pdsts
2OneHot.scala 58:35P26
_T_9.R,B
:


io	wb_valids
2
0
0Bitwise.scala 72:15X2>
_T_10523


_T_9

45035996273704954	

04Bitwise.scala 72:12E2"
_T_11R

_T_8	

_T_10rename-busytable.scala 51:48D2!
_T_12R

_T_3

_T_7rename-busytable.scala 51:88F2#
_T_13R	

_T_12	

_T_11rename-busytable.scala 51:88;2
_T_14R	

_T_13rename-busytable.scala 50:36S20
busy_table_wbR


busy_table	

_T_14rename-busytable.scala 50:34\2C
_T_15:R8
	

1':%
B
:


ioren_uops
0pdstOneHot.scala 58:35S29
_T_160R. B
:


iorebusy_reqs
0
0
0Bitwise.scala 72:15Y2?
_T_17624
	

_T_16

45035996273704954	

04Bitwise.scala 72:12F2#
_T_18R	

_T_15	

_T_17rename-busytable.scala 54:49X25
busy_table_next"R 

busy_table_wb	

_T_18rename-busytable.scala 53:39Hz%



busy_table

busy_table_nextrename-busytable.scala 56:14i2F
_T_19=R;


busy_table':%
B
:


ioren_uops
0prs1rename-busytable.scala 67:45E2"
_T_20R	

_T_19
0
0rename-busytable.scala 67:45J2'
_T_21R	

0	

0rename-busytable.scala 67:88F2#
_T_22R	

_T_20	

_T_21rename-busytable.scala 67:67^z;
.:,
B
:


io
busy_resps
0	prs1_busy	

_T_22rename-busytable.scala 67:32i2F
_T_23=R;


busy_table':%
B
:


ioren_uops
0prs2rename-busytable.scala 68:45E2"
_T_24R	

_T_23
0
0rename-busytable.scala 68:45J2'
_T_25R	

0	

0rename-busytable.scala 68:88F2#
_T_26R	

_T_24	

_T_25rename-busytable.scala 68:67^z;
.:,
B
:


io
busy_resps
0	prs2_busy	

_T_26rename-busytable.scala 68:32i2F
_T_27=R;


busy_table':%
B
:


ioren_uops
0prs3rename-busytable.scala 69:45E2"
_T_28R	

_T_27
0
0rename-busytable.scala 69:45J2'
_T_29R	

0	

0rename-busytable.scala 69:88F2#
_T_30R	

_T_28	

_T_29rename-busytable.scala 69:67^z;
.:,
B
:


io
busy_resps
0	prs3_busy	

_T_30rename-busytable.scala 69:32`z=
.:,
B
:


io
busy_resps
0	prs3_busy	

0rename-busytable.scala 70:44Uz2
 :
:


iodebug	busytable


busy_tablerename-busytable.scala 73:22
RenameBusyTable