
��
����
FetchTargetQueue
clock" 
reset
� 
io� *� 
�enq�*�
ready

valid

�bits�*�
pc
(
next_pc
(
	edge_inst2



insts2


 
	exp_insts2


 
sfbs2



	sfb_masks2



	sfb_dests2



shadowable_mask2



shadowed_mask2



.cfi_idx#*!
valid

bits

cfi_type

cfi_is_call


cfi_is_ret

cfi_npc_plus4

ras_top
(
ftq_idx

mask

br_mask

�ghist�*�
old_history
@
&current_saw_branch_not_taken

"new_saw_branch_not_taken

new_saw_branch_taken

ras_idx

lhist2




xcpt_pf_if


xcpt_ae_if

bp_debug_if_oh2



bp_xcpt_if_oh2



/end_half#*!
valid

bits

bpd_meta2



fsrc

tsrc

enq_idx

,deq#*!
valid

bits

�
get_ftq_pc�2�
�*�
ftq_idx

�entry�*�
.cfi_idx#*!
valid

bits

	cfi_taken

cfi_mispredicted

cfi_type

br_mask

cfi_is_call


cfi_is_ret

cfi_npc_plus4

ras_top
(
ras_idx


start_bank

�ghist�*�
old_history
@
&current_saw_branch_not_taken

"new_saw_branch_not_taken

new_saw_branch_taken

ras_idx

pc
(
com_pc
(
next_val

next_pc
(
debug_ftq_idx2



debug_fetch_pc2


(
1redirect#*!
valid

bits

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
�	bpdupdate�*�
valid

�bits�*�
is_mispredict_update

is_repair_update

btb_mispredicts

pc
(
br_mask

.cfi_idx#*!
valid

bits

	cfi_taken

cfi_mispredicted

	cfi_is_br


cfi_is_jal

cfi_is_jalr

�ghist�*�
old_history
@
&current_saw_branch_not_taken

"new_saw_branch_not_taken

new_saw_branch_taken

ras_idx

lhist2



target
(
meta2


x

ras_update

ras_update_idx

ras_update_pc
(�
	

clock�
 �
	

reset�
 �


io�
 Z4
bpd_ptr
	

clock"	

reset*	

0�!fetch-target-queue.scala 133:27Z4
deq_ptr
	

clock"	

reset*	

0�!fetch-target-queue.scala 134:27Z4
enq_ptr
	

clock"	

reset*	

1�!fetch-target-queue.scala 135:27<2$
_TR
	
enq_ptr	

1�util.scala 203:1412
_T_1R

_T
1�util.scala 203:1482 
_T_2R

_T_1
3
0�util.scala 203:20;2#
_T_3R

_T_2	

1�util.scala 203:1432
_T_4R

_T_3
1�util.scala 203:1482 
_T_5R

_T_4
3
0�util.scala 203:20I2#
_T_6R

_T_5
	
bpd_ptr�!fetch-target-queue.scala 137:68>2&
_T_7R
	
enq_ptr	

1�util.scala 203:1432
_T_8R

_T_7
1�util.scala 203:1482 
_T_9R

_T_8
3
0�util.scala 203:20J2$
_T_10R

_T_9
	
bpd_ptr�!fetch-target-queue.scala 138:46G2!
fullR

_T_6	

_T_10�!fetch-target-queue.scala 137:81Z4
pcs2


(	

clock"	

0*

pcs�!fetch-target-queue.scala 141:21C"
meta"
2


x
�!fetch-target-queue.scala 142:29��
ram�2�
�*�
.cfi_idx#*!
valid

bits

	cfi_taken

cfi_mispredicted

cfi_type

br_mask

cfi_is_call


cfi_is_ret

cfi_npc_plus4

ras_top
(
ras_idx


start_bank
	

clock"	

0*

ram�!fetch-target-queue.scala 143:21�"�
ghist_0"�
�*�
old_history
@
&current_saw_branch_not_taken

"new_saw_branch_not_taken

new_saw_branch_taken

ras_idx

�!fetch-target-queue.scala 144:43�"�
ghist_1"�
�*�
old_history
@
&current_saw_branch_not_taken

"new_saw_branch_not_taken

new_saw_branch_taken

ras_idx

�!fetch-target-queue.scala 144:43b2F
do_enq<R::
:


ioenqready:
:


ioenqvalid�Decoupled.scala 40:37�
�
_T_11�*�
old_history
@
&current_saw_branch_not_taken

"new_saw_branch_not_taken

new_saw_branch_taken

ras_idx
�!fetch-target-queue.scala 155:42Kz%
:
	

_T_11ras_idx	

0�!fetch-target-queue.scala 155:42Xz2
#:!
	

_T_11new_saw_branch_taken	

0�!fetch-target-queue.scala 155:42\z6
':%
	

_T_11new_saw_branch_not_taken	

0�!fetch-target-queue.scala 155:42`z:
+:)
	

_T_11current_saw_branch_not_taken	

0�!fetch-target-queue.scala 155:42Oz)
:
	

_T_11old_history	

0@�!fetch-target-queue.scala 155:42��

prev_ghist�*�
old_history
@
&current_saw_branch_not_taken

"new_saw_branch_not_taken

new_saw_branch_taken

ras_idx
	

clock"	

reset*	

_T_11�!fetch-target-queue.scala 155:27�
�
_T_12�*�
.cfi_idx#*!
valid

bits

	cfi_taken

cfi_mispredicted

cfi_type

br_mask

cfi_is_call


cfi_is_ret

cfi_npc_plus4

ras_top
(
ras_idx


start_bank
�!fetch-target-queue.scala 156:42Nz(
:
	

_T_12
start_bank	

0�!fetch-target-queue.scala 156:42Kz%
:
	

_T_12ras_idx	

0�!fetch-target-queue.scala 156:42Kz%
:
	

_T_12ras_top	

0(�!fetch-target-queue.scala 156:42Qz+
:
	

_T_12cfi_npc_plus4	

0�!fetch-target-queue.scala 156:42Nz(
:
	

_T_12
cfi_is_ret	

0�!fetch-target-queue.scala 156:42Oz)
:
	

_T_12cfi_is_call	

0�!fetch-target-queue.scala 156:42Kz%
:
	

_T_12br_mask	

0�!fetch-target-queue.scala 156:42Lz&
:
	

_T_12cfi_type	

0�!fetch-target-queue.scala 156:42Tz.
:
	

_T_12cfi_mispredicted	

0�!fetch-target-queue.scala 156:42Mz'
:
	

_T_12	cfi_taken	

0�!fetch-target-queue.scala 156:42Uz/
 :
:
	

_T_12cfi_idxbits	

0�!fetch-target-queue.scala 156:42Vz0
!:
:
	

_T_12cfi_idxvalid	

0�!fetch-target-queue.scala 156:42��

prev_entry�*�
.cfi_idx#*!
valid

bits

	cfi_taken

cfi_mispredicted

cfi_type

br_mask

cfi_is_call


cfi_is_ret

cfi_npc_plus4

ras_top
(
ras_idx


start_bank
	

clock"	

reset*	

_T_12�!fetch-target-queue.scala 156:27Z4
prev_pc
(	

clock"	

reset*	

0(�!fetch-target-queue.scala 157:27�N:�N



do_enqcz=
J


pcs
	
enq_ptr!:
:
:


ioenqbitspc�!fetch-target-queue.scala 160:28�
�
_T_13�*�
.cfi_idx#*!
valid

bits

	cfi_taken

cfi_mispredicted

cfi_type

br_mask

cfi_is_call


cfi_is_ret

cfi_npc_plus4

ras_top
(
ras_idx


start_bank
�!fetch-target-queue.scala 162:25zzT
 :
:
	

_T_13cfi_idxbits0:.
&:$
:
:


ioenqbitscfi_idxbits�!fetch-target-queue.scala 164:25|zV
!:
:
	

_T_13cfi_idxvalid1:/
&:$
:
:


ioenqbitscfi_idxvalid�!fetch-target-queue.scala 164:25szM
:
	

_T_13	cfi_taken1:/
&:$
:
:


ioenqbitscfi_idxvalid�!fetch-target-queue.scala 167:29Tz.
:
	

_T_13cfi_mispredicted	

0�!fetch-target-queue.scala 168:32hzB
:
	

_T_13cfi_type':%
:
:


ioenqbitscfi_type�!fetch-target-queue.scala 169:29nzH
:
	

_T_13cfi_is_call*:(
:
:


ioenqbitscfi_is_call�!fetch-target-queue.scala 170:29lzF
:
	

_T_13
cfi_is_ret):'
:
:


ioenqbits
cfi_is_ret�!fetch-target-queue.scala 171:29rzL
:
	

_T_13cfi_npc_plus4,:*
:
:


ioenqbitscfi_npc_plus4�!fetch-target-queue.scala 172:29fz@
:
	

_T_13ras_top&:$
:
:


ioenqbitsras_top�!fetch-target-queue.scala 173:29qzK
:
	

_T_13ras_idx1:/
$:"
:
:


ioenqbitsghistras_idx�!fetch-target-queue.scala 174:29�2Z
_T_14QRO&:$
:
:


ioenqbitsbr_mask#:!
:
:


ioenqbitsmask�!fetch-target-queue.scala 175:52Iz#
:
	

_T_13br_mask	

_T_14�!fetch-target-queue.scala 175:29Nz(
:
	

_T_13
start_bank	

0�!fetch-target-queue.scala 176:29w2Q
_T_15HRF:



prev_entrybr_mask%:#
:



prev_entrycfi_idxbits�!fetch-target-queue.scala 183:27H2"
_T_16R	

_T_15
0
0�!fetch-target-queue.scala 183:27Y2>
_T_175R3%:#
:



prev_entrycfi_idxbits
1
0�frontend.scala 87:32>2%
_T_18R
	

1	

_T_17�OneHot.scala 58:35�
�
_T_19�*�
old_history
@
&current_saw_branch_not_taken

"new_saw_branch_not_taken

new_saw_branch_taken

ras_idx
�frontend.scala 89:27=2%
_T_20R	

_T_18	

0�util.scala 373:29=2%
_T_21R	

_T_18	

1�util.scala 373:29=2%
_T_22R	

_T_18	

2�util.scala 373:29=2%
_T_23R	

_T_18	

3�util.scala 373:29;2#
_T_24R	

_T_20	

_T_21�util.scala 373:45;2#
_T_25R	

_T_24	

_T_22�util.scala 373:45;2#
_T_26R	

_T_25	

_T_23�util.scala 373:45R27
_T_27.R,	

_T_16:



prev_entry	cfi_taken�frontend.scala 92:84I2.
_T_28%2#
	

_T_27	

_T_18	

0�frontend.scala 92:7332
_T_29R	

_T_28�frontend.scala 92:69>2#
_T_30R	

_T_26	

_T_29�frontend.scala 92:6752
_T_31R	

0�frontend.scala 93:45d2I
_T_32@2>
&:$
:



prev_entrycfi_idxvalid	

_T_30	

_T_31�frontend.scala 91:44P25
_T_33,R*:



prev_entrybr_mask	

_T_32�frontend.scala 91:394�
:
	

_T_19ras_idx�frontend.scala 97:19A�%
#:!
	

_T_19new_saw_branch_taken�frontend.scala 97:19E�)
':%
	

_T_19new_saw_branch_not_taken�frontend.scala 97:19I�-
+:)
	

_T_19current_saw_branch_not_taken�frontend.scala 97:198�
:
	

_T_19old_history�frontend.scala 97:19Uz:
+:)
	

_T_19current_saw_branch_not_taken	

0�frontend.scala 98:48@2%
_T_34R	

_T_33	

0�frontend.scala 99:53e2J
_T_35AR?	

_T_340:.



prev_ghistcurrent_saw_branch_not_taken�frontend.scala 99:61S27
_T_36.R,	

_T_16:



prev_entry	cfi_taken�frontend.scala 100:48\2@
_T_377R5	

_T_36&:$
:



prev_entrycfi_idxvalid�frontend.scala 100:61O23
_T_38*R(:



prev_ghistold_history
1�frontend.scala 100:91A2%
_T_39R	

_T_38	

1�frontend.scala 100:96O23
_T_40*R(:



prev_ghistold_history
1�frontend.scala 101:91^2B
_T_41927
	

_T_35	

_T_40:



prev_ghistold_history�frontend.scala 101:37H2,
_T_42#2!
	

_T_37	

_T_39	

_T_41�frontend.scala 100:37Cz'
:
	

_T_19old_history	

_T_42�frontend.scala 100:31r2V
_T_43MRK&:$
:



prev_entrycfi_idxvalid:



prev_entrycfi_is_call�frontend.scala 125:42O27
_T_44.R,:



prev_ghistras_idx	

1�util.scala 203:1452
_T_45R	

_T_44
1�util.scala 203:14:2"
_T_46R	

_T_45
4
0�util.scala 203:20q2U
_T_47LRJ&:$
:



prev_entrycfi_idxvalid:



prev_entry
cfi_is_ret�frontend.scala 126:42O27
_T_48.R,:



prev_ghistras_idx	

1�util.scala 220:1452
_T_49R	

_T_48
1�util.scala 220:14:2"
_T_50R	

_T_49
4
0�util.scala 220:20Z2>
_T_51523
	

_T_47	

_T_50:



prev_ghistras_idx�frontend.scala 126:31H2,
_T_52#2!
	

_T_43	

_T_46	

_T_51�frontend.scala 125:31?z#
:
	

_T_19ras_idx	

_T_52�frontend.scala 125:25�2�
_T_53{2y
F:D
$:"
:
:


ioenqbitsghistcurrent_saw_branch_not_taken$:"
:
:


ioenqbitsghist	

_T_19�!fetch-target-queue.scala 178:242�*_T_54ghist_0"
	
enq_ptr*	

clock�
 7z0
:
	

_T_54ras_idx:
	

_T_53ras_idx�
 QzJ
#:!
	

_T_54new_saw_branch_taken#:!
	

_T_53new_saw_branch_taken�
 YzR
':%
	

_T_54new_saw_branch_not_taken':%
	

_T_53new_saw_branch_not_taken�
 azZ
+:)
	

_T_54current_saw_branch_not_taken+:)
	

_T_53current_saw_branch_not_taken�
 ?z8
:
	

_T_54old_history:
	

_T_53old_history�
 2�*_T_55ghist_1"
	
enq_ptr*	

clock�
 7z0
:
	

_T_55ras_idx:
	

_T_53ras_idx�
 QzJ
#:!
	

_T_55new_saw_branch_taken#:!
	

_T_53new_saw_branch_taken�
 YzR
':%
	

_T_55new_saw_branch_not_taken':%
	

_T_53new_saw_branch_not_taken�
 azZ
+:)
	

_T_55current_saw_branch_not_taken+:)
	

_T_53current_saw_branch_not_taken�
 ?z8
:
	

_T_55old_history:
	

_T_53old_history�
 /�'_T_56meta"
	
enq_ptr*	

clock�
 MzF
B
	

_T_56
00B.
':%
:
:


ioenqbitsbpd_meta
0�
 kzE
(:&
J


ram
	
enq_ptr
start_bank:
	

_T_13
start_bank�!fetch-target-queue.scala 195:18ez?
%:#
J


ram
	
enq_ptrras_idx:
	

_T_13ras_idx�!fetch-target-queue.scala 195:18ez?
%:#
J


ram
	
enq_ptrras_top:
	

_T_13ras_top�!fetch-target-queue.scala 195:18qzK
+:)
J


ram
	
enq_ptrcfi_npc_plus4:
	

_T_13cfi_npc_plus4�!fetch-target-queue.scala 195:18kzE
(:&
J


ram
	
enq_ptr
cfi_is_ret:
	

_T_13
cfi_is_ret�!fetch-target-queue.scala 195:18mzG
):'
J


ram
	
enq_ptrcfi_is_call:
	

_T_13cfi_is_call�!fetch-target-queue.scala 195:18ez?
%:#
J


ram
	
enq_ptrbr_mask:
	

_T_13br_mask�!fetch-target-queue.scala 195:18gzA
&:$
J


ram
	
enq_ptrcfi_type:
	

_T_13cfi_type�!fetch-target-queue.scala 195:18wzQ
.:,
J


ram
	
enq_ptrcfi_mispredicted:
	

_T_13cfi_mispredicted�!fetch-target-queue.scala 195:18izC
':%
J


ram
	
enq_ptr	cfi_taken:
	

_T_13	cfi_taken�!fetch-target-queue.scala 195:18yzS
/:-
%:#
J


ram
	
enq_ptrcfi_idxbits :
:
	

_T_13cfi_idxbits�!fetch-target-queue.scala 195:18{zU
0:.
%:#
J


ram
	
enq_ptrcfi_idxvalid!:
:
	

_T_13cfi_idxvalid�!fetch-target-queue.scala 195:18Vz0

	
prev_pc!:
:
:


ioenqbitspc�!fetch-target-queue.scala 197:16az;
:



prev_entry
start_bank:
	

_T_13
start_bank�!fetch-target-queue.scala 198:16[z5
:



prev_entryras_idx:
	

_T_13ras_idx�!fetch-target-queue.scala 198:16[z5
:



prev_entryras_top:
	

_T_13ras_top�!fetch-target-queue.scala 198:16gzA
!:



prev_entrycfi_npc_plus4:
	

_T_13cfi_npc_plus4�!fetch-target-queue.scala 198:16az;
:



prev_entry
cfi_is_ret:
	

_T_13
cfi_is_ret�!fetch-target-queue.scala 198:16cz=
:



prev_entrycfi_is_call:
	

_T_13cfi_is_call�!fetch-target-queue.scala 198:16[z5
:



prev_entrybr_mask:
	

_T_13br_mask�!fetch-target-queue.scala 198:16]z7
:



prev_entrycfi_type:
	

_T_13cfi_type�!fetch-target-queue.scala 198:16mzG
$:"



prev_entrycfi_mispredicted:
	

_T_13cfi_mispredicted�!fetch-target-queue.scala 198:16_z9
:



prev_entry	cfi_taken:
	

_T_13	cfi_taken�!fetch-target-queue.scala 198:16ozI
%:#
:



prev_entrycfi_idxbits :
:
	

_T_13cfi_idxbits�!fetch-target-queue.scala 198:16qzK
&:$
:



prev_entrycfi_idxvalid!:
:
	

_T_13cfi_idxvalid�!fetch-target-queue.scala 198:16[z5
:



prev_ghistras_idx:
	

_T_53ras_idx�!fetch-target-queue.scala 199:16uzO
(:&



prev_ghistnew_saw_branch_taken#:!
	

_T_53new_saw_branch_taken�!fetch-target-queue.scala 199:16}zW
,:*



prev_ghistnew_saw_branch_not_taken':%
	

_T_53new_saw_branch_not_taken�!fetch-target-queue.scala 199:16�z_
0:.



prev_ghistcurrent_saw_branch_not_taken+:)
	

_T_53current_saw_branch_not_taken�!fetch-target-queue.scala 199:16cz=
:



prev_ghistold_history:
	

_T_53old_history�!fetch-target-queue.scala 199:16?2'
_T_57R
	
enq_ptr	

1�util.scala 203:1452
_T_58R	

_T_57
1�util.scala 203:14:2"
_T_59R	

_T_58
3
0�util.scala 203:20>z

	
enq_ptr	

_T_59�!fetch-target-queue.scala 201:13�!fetch-target-queue.scala 158:17Hz"
:


ioenq_idx
	
enq_ptr�!fetch-target-queue.scala 204:14Uz/
 :
:


io	bpdupdatevalid	

0�!fetch-target-queue.scala 206:22[�4
2B0
):'
:
:


io	bpdupdatebitsmeta
0�!fetch-target-queue.scala 207:22T�-
+:)
:
:


io	bpdupdatebitstarget�!fetch-target-queue.scala 207:22\�5
3B1
*:(
:
:


io	bpdupdatebitslhist
0�!fetch-target-queue.scala 207:22`�9
7:5
*:(
:
:


io	bpdupdatebitsghistras_idx�!fetch-target-queue.scala 207:22m�F
D:B
*:(
:
:


io	bpdupdatebitsghistnew_saw_branch_taken�!fetch-target-queue.scala 207:22q�J
H:F
*:(
:
:


io	bpdupdatebitsghistnew_saw_branch_not_taken�!fetch-target-queue.scala 207:22u�N
L:J
*:(
:
:


io	bpdupdatebitsghistcurrent_saw_branch_not_taken�!fetch-target-queue.scala 207:22d�=
;:9
*:(
:
:


io	bpdupdatebitsghistold_history�!fetch-target-queue.scala 207:22Y�2
0:.
:
:


io	bpdupdatebitscfi_is_jalr�!fetch-target-queue.scala 207:22X�1
/:-
:
:


io	bpdupdatebits
cfi_is_jal�!fetch-target-queue.scala 207:22W�0
.:,
:
:


io	bpdupdatebits	cfi_is_br�!fetch-target-queue.scala 207:22^�7
5:3
:
:


io	bpdupdatebitscfi_mispredicted�!fetch-target-queue.scala 207:22W�0
.:,
:
:


io	bpdupdatebits	cfi_taken�!fetch-target-queue.scala 207:22_�8
6:4
,:*
:
:


io	bpdupdatebitscfi_idxbits�!fetch-target-queue.scala 207:22`�9
7:5
,:*
:
:


io	bpdupdatebitscfi_idxvalid�!fetch-target-queue.scala 207:22U�.
,:*
:
:


io	bpdupdatebitsbr_mask�!fetch-target-queue.scala 207:22P�)
':%
:
:


io	bpdupdatebitspc�!fetch-target-queue.scala 207:22]�6
4:2
:
:


io	bpdupdatebitsbtb_mispredicts�!fetch-target-queue.scala 207:22^�7
5:3
:
:


io	bpdupdatebitsis_repair_update�!fetch-target-queue.scala 207:22b�;
9:7
:
:


io	bpdupdatebitsis_mispredict_update�!fetch-target-queue.scala 207:22�:l
:
:


iodeqvalidNz(

	
deq_ptr:
:


iodeqbits�!fetch-target-queue.scala 210:13�!fetch-target-queue.scala 209:23^8
first_empty
	

clock"	

reset*	

1�!fetch-target-queue.scala 214:28


ras_update
�
 $z



ras_update	

0�
 

ras_update_pc
(�
 'z 


ras_update_pc	

0(�
 

ras_update_idx
�
 (z!


ras_update_idx	

0�
 X2
_T_60
	

clock"	

0*	

_T_60�!fetch-target-queue.scala 222:31Az
	

_T_60


ras_update�!fetch-target-queue.scala 222:31Iz#
:


io
ras_update	

_T_60�!fetch-target-queue.scala 222:21T.
_T_61 	

clock"	

0*	

_T_61�!fetch-target-queue.scala 223:31Dz
	

_T_61

ras_update_pc�!fetch-target-queue.scala 223:31Lz&
:


ioras_update_pc	

_T_61�!fetch-target-queue.scala 223:21T.
_T_62 	

clock"	

0*	

_T_62�!fetch-target-queue.scala 224:31Ez
	

_T_62

ras_update_idx�!fetch-target-queue.scala 224:31Mz'
:


ioras_update_idx	

_T_62�!fetch-target-queue.scala 224:21hB
bpd_update_mispredict
	

clock"	

reset*	

0�!fetch-target-queue.scala 226:38d>
bpd_update_repair
	

clock"	

reset*	

0�!fetch-target-queue.scala 227:34jD
bpd_repair_idx
	

clock"	

0*

bpd_repair_idx�!fetch-target-queue.scala 228:27d>
bpd_end_idx
	

clock"	

0*

bpd_end_idx�!fetch-target-queue.scala 229:24hB
bpd_repair_pc
(	

clock"	

0*

bpd_repair_pc�!fetch-target-queue.scala 230:26e2?
_T_636R4

bpd_update_repair

bpd_update_mispredict�!fetch-target-queue.scala 233:27\27
_T_64.2,
	

_T_63

bpd_repair_idx
	
bpd_ptr� fetch-target-queue.scala 233:82Y
bpd_idxN2L
:
:


ioredirectvalid:
:


ioredirectbits	

_T_64�!fetch-target-queue.scala 232:20��
	bpd_entry�*�
.cfi_idx#*!
valid

bits

	cfi_taken

cfi_mispredicted

cfi_type

br_mask

cfi_is_call


cfi_is_ret

cfi_npc_plus4

ras_top
(
ras_idx


start_bank
	

clock"	

0*

	bpd_entry�!fetch-target-queue.scala 234:26ozI
:


	bpd_entry
start_bank(:&
J


ram
	
bpd_idx
start_bank�!fetch-target-queue.scala 234:26izC
:


	bpd_entryras_idx%:#
J


ram
	
bpd_idxras_idx�!fetch-target-queue.scala 234:26izC
:


	bpd_entryras_top%:#
J


ram
	
bpd_idxras_top�!fetch-target-queue.scala 234:26uzO
 :


	bpd_entrycfi_npc_plus4+:)
J


ram
	
bpd_idxcfi_npc_plus4�!fetch-target-queue.scala 234:26ozI
:


	bpd_entry
cfi_is_ret(:&
J


ram
	
bpd_idx
cfi_is_ret�!fetch-target-queue.scala 234:26qzK
:


	bpd_entrycfi_is_call):'
J


ram
	
bpd_idxcfi_is_call�!fetch-target-queue.scala 234:26izC
:


	bpd_entrybr_mask%:#
J


ram
	
bpd_idxbr_mask�!fetch-target-queue.scala 234:26kzE
:


	bpd_entrycfi_type&:$
J


ram
	
bpd_idxcfi_type�!fetch-target-queue.scala 234:26{zU
#:!


	bpd_entrycfi_mispredicted.:,
J


ram
	
bpd_idxcfi_mispredicted�!fetch-target-queue.scala 234:26mzG
:


	bpd_entry	cfi_taken':%
J


ram
	
bpd_idx	cfi_taken�!fetch-target-queue.scala 234:26}zW
$:"
:


	bpd_entrycfi_idxbits/:-
%:#
J


ram
	
bpd_idxcfi_idxbits�!fetch-target-queue.scala 234:26zY
%:#
:


	bpd_entrycfi_idxvalid0:.
%:#
J


ram
	
bpd_idxcfi_idxvalid�!fetch-target-queue.scala 234:261

_T_65 �!fetch-target-queue.scala 235:322�
	

_T_65�!fetch-target-queue.scala 235:32�:�
	

1>z
	

_T_65
	
bpd_idx�!fetch-target-queue.scala 235:32K2%
_T_66R	

_T_65	

0�!fetch-target-queue.scala 235:32H2"
_T_67R	

_T_66
3
0�!fetch-target-queue.scala 235:32S�,	bpd_ghistghist_0"	

_T_67*	

clock�!fetch-target-queue.scala 235:32�!fetch-target-queue.scala 235:32?

	bpd_lhist2


�!fetch-target-queue.scala 239:12Kz%
B


	bpd_lhist
0	

0�!fetch-target-queue.scala 239:121

_T_68 �!fetch-target-queue.scala 241:282�
	

_T_68�!fetch-target-queue.scala 241:28�:�
	

1>z
	

_T_68
	
bpd_idx�!fetch-target-queue.scala 241:28K2%
_T_69R	

_T_68	

0�!fetch-target-queue.scala 241:28H2"
_T_70R	

_T_69
3
0�!fetch-target-queue.scala 241:28O�(bpd_metameta"	

_T_70*	

clock�!fetch-target-queue.scala 241:28�!fetch-target-queue.scala 241:28V0
bpd_pc 	

clock"	

0*


bpd_pc�!fetch-target-queue.scala 242:26Lz&



bpd_pcJ


pcs
	
bpd_idx�!fetch-target-queue.scala 242:26?2'
_T_71R
	
bpd_idx	

1�util.scala 203:1452
_T_72R	

_T_71
1�util.scala 203:14:2"
_T_73R	

_T_72
3
0�util.scala 203:20^8

bpd_target 	

clock"	

0*


bpd_target�!fetch-target-queue.scala 243:27Nz(



bpd_targetJ


pcs	

_T_73�!fetch-target-queue.scala 243:27�:�
:
:


ioredirectvalidNz(


bpd_update_mispredict	

0�!fetch-target-queue.scala 246:27Jz$


bpd_update_repair	

0�!fetch-target-queue.scala 247:27X2
_T_74
	

clock"	

0*	

_T_74�!fetch-target-queue.scala 248:23_z9
	

_T_74,:*
:
:


iobrupdateb2
mispredict�!fetch-target-queue.scala 248:23�:�
	

_T_74Nz(


bpd_update_mispredict	

1�!fetch-target-queue.scala 249:27T.
_T_75 	

clock"	

0*	

_T_75�!fetch-target-queue.scala 250:37ez?
	

_T_752:0
%:#
:
:


iobrupdateb2uopftq_idx�!fetch-target-queue.scala 250:37Ez


bpd_repair_idx	

_T_75�!fetch-target-queue.scala 250:27T.
_T_76 	

clock"	

0*	

_T_76�!fetch-target-queue.scala 251:37>z
	

_T_76
	
enq_ptr�!fetch-target-queue.scala 251:37Bz


bpd_end_idx	

_T_76�!fetch-target-queue.scala 251:27�:�


bpd_update_mispredictNz(


bpd_update_mispredict	

0�!fetch-target-queue.scala 253:27Jz$


bpd_update_repair	

1�!fetch-target-queue.scala 254:27F2.
_T_77%R#

bpd_repair_idx	

1�util.scala 203:1452
_T_78R	

_T_77
1�util.scala 203:14:2"
_T_79R	

_T_78
3
0�util.scala 203:20Ez


bpd_repair_idx	

_T_79�!fetch-target-queue.scala 255:27X2
_T_80
	

clock"	

0*	

_T_80�!fetch-target-queue.scala 256:44Lz&
	

_T_80

bpd_update_mispredict�!fetch-target-queue.scala 256:44U2/
_T_81&R$

bpd_update_repair	

_T_80�!fetch-target-queue.scala 256:34�	:�	
	

_T_81Ez


bpd_repair_pc


bpd_pc�!fetch-target-queue.scala 257:27F2.
_T_82%R#

bpd_repair_idx	

1�util.scala 203:1452
_T_83R	

_T_82
1�util.scala 203:14:2"
_T_84R	

_T_83
3
0�util.scala 203:20Ez


bpd_repair_idx	

_T_84�!fetch-target-queue.scala 258:27�:�


bpd_update_repairF2.
_T_85%R#

bpd_repair_idx	

1�util.scala 203:1452
_T_86R	

_T_85
1�util.scala 203:14:2"
_T_87R	

_T_86
3
0�util.scala 203:20Ez


bpd_repair_idx	

_T_87�!fetch-target-queue.scala 260:27F2.
_T_88%R#

bpd_repair_idx	

1�util.scala 203:1452
_T_89R	

_T_88
1�util.scala 203:14:2"
_T_90R	

_T_89
3
0�util.scala 203:20O2)
_T_91 R	

_T_90

bpd_end_idx�!fetch-target-queue.scala 261:48R2,
_T_92#R!


bpd_pc

bpd_repair_pc�!fetch-target-queue.scala 262:14I2#
_T_93R	

_T_91	

_T_92�!fetch-target-queue.scala 261:64}:W
	

_T_93Jz$


bpd_update_repair	

0�!fetch-target-queue.scala 263:25�!fetch-target-queue.scala 262:34�!fetch-target-queue.scala 259:35�!fetch-target-queue.scala 256:69�!fetch-target-queue.scala 252:39�!fetch-target-queue.scala 248:52�!fetch-target-queue.scala 245:28[25
_T_94,R*

bpd_update_mispredict	

0�!fetch-target-queue.scala 269:31W21
_T_95(R&

bpd_update_repair	

0�!fetch-target-queue.scala 270:31I2#
_T_96R	

_T_94	

_T_95�!fetch-target-queue.scala 269:54M2'
_T_97R
	
bpd_ptr
	
deq_ptr�!fetch-target-queue.scala 271:40I2#
_T_98R	

_T_96	

_T_97�!fetch-target-queue.scala 270:50?2'
_T_99R
	
bpd_ptr	

1�util.scala 203:1462
_T_100R	

_T_99
1�util.scala 203:14<2$
_T_101R


_T_100
3
0�util.scala 203:20M2'
_T_102R
	
enq_ptr


_T_101�!fetch-target-queue.scala 272:40K2%
_T_103R	

_T_98


_T_102�!fetch-target-queue.scala 271:52o2I
_T_104?R=,:*
:
:


iobrupdateb2
mispredict	

0�!fetch-target-queue.scala 273:31L2&
_T_105R


_T_103


_T_104�!fetch-target-queue.scala 272:74b2<
_T_1062R0:
:


ioredirectvalid	

0�!fetch-target-queue.scala 274:31L2&
_T_107R


_T_105


_T_106�!fetch-target-queue.scala 273:58Z4
_T_108
	

clock"	

0*


_T_108�!fetch-target-queue.scala 274:61Sz-



_T_108:
:


ioredirectvalid�!fetch-target-queue.scala 274:61M2'
_T_109R


_T_108	

0�!fetch-target-queue.scala 274:53V20
do_commit_updateR


_T_107


_T_109�!fetch-target-queue.scala 274:50a2;
_T_1101R/

do_commit_update

bpd_update_repair�!fetch-target-queue.scala 278:34[25
_T_111+R)


_T_110

bpd_update_mispredict�!fetch-target-queue.scala 278:54Z4
_T_112
	

clock"	

0*


_T_112�!fetch-target-queue.scala 278:16>z



_T_112


_T_111�!fetch-target-queue.scala 278:16�':�&



_T_112S2-
_T_113#R!


bpd_pc

bpd_repair_pc�!fetch-target-queue.scala 280:31R2,
_T_114"R 

first_empty	

0�!fetch-target-queue.scala 282:28]27
_T_115-R+:


	bpd_entrybr_mask	

0�!fetch-target-queue.scala 283:74g2A
_T_1167R5%:#
:


	bpd_entrycfi_idxvalid


_T_115�!fetch-target-queue.scala 283:53L2&
_T_117R


_T_114


_T_116�!fetch-target-queue.scala 282:41Z4
_T_118
	

clock"	

0*


_T_118�!fetch-target-queue.scala 284:37Iz#



_T_118

bpd_update_repair�!fetch-target-queue.scala 284:37M2'
_T_119R


_T_113	

0�!fetch-target-queue.scala 284:59L2&
_T_120R


_T_118


_T_119�!fetch-target-queue.scala 284:56M2'
_T_121R


_T_120	

0�!fetch-target-queue.scala 284:28L2&
_T_122R


_T_117


_T_121�!fetch-target-queue.scala 283:83Tz.
 :
:


io	bpdupdatevalid


_T_122�!fetch-target-queue.scala 282:24Z4
_T_123
	

clock"	

0*


_T_123�!fetch-target-queue.scala 285:54Mz'



_T_123

bpd_update_mispredict�!fetch-target-queue.scala 285:54mzG
9:7
:
:


io	bpdupdatebitsis_mispredict_update


_T_123�!fetch-target-queue.scala 285:44Z4
_T_124
	

clock"	

0*


_T_124�!fetch-target-queue.scala 286:54Iz#



_T_124

bpd_update_repair�!fetch-target-queue.scala 286:54izC
5:3
:
:


io	bpdupdatebitsis_repair_update


_T_124�!fetch-target-queue.scala 286:44[z5
':%
:
:


io	bpdupdatebitspc


bpd_pc�!fetch-target-queue.scala 287:31izC
4:2
:
:


io	bpdupdatebitsbtb_mispredicts	

0�!fetch-target-queue.scala 288:39Z2A
_T_1257R5
	

1$:"
:


	bpd_entrycfi_idxbits�OneHot.scala 58:35?2'
_T_126R


_T_125	

0�util.scala 373:29?2'
_T_127R


_T_125	

1�util.scala 373:29?2'
_T_128R


_T_125	

2�util.scala 373:29?2'
_T_129R


_T_125	

3�util.scala 373:29>2&
_T_130R


_T_126


_T_127�util.scala 373:45>2&
_T_131R


_T_130


_T_128�util.scala 373:45>2&
_T_132R


_T_131


_T_129�util.scala 373:45\26
_T_133,R*


_T_132:


	bpd_entrybr_mask�!fetch-target-queue.scala 290:36�2[
_T_134Q2O
%:#
:


	bpd_entrycfi_idxvalid


_T_133:


	bpd_entrybr_mask�!fetch-target-queue.scala 289:37`z:
,:*
:
:


io	bpdupdatebitsbr_mask


_T_134�!fetch-target-queue.scala 289:31�z^
6:4
,:*
:
:


io	bpdupdatebitscfi_idxbits$:"
:


	bpd_entrycfi_idxbits�!fetch-target-queue.scala 291:31�z`
7:5
,:*
:
:


io	bpdupdatebitscfi_idxvalid%:#
:


	bpd_entrycfi_idxvalid�!fetch-target-queue.scala 291:31�z\
5:3
:
:


io	bpdupdatebitscfi_mispredicted#:!


	bpd_entrycfi_mispredicted�!fetch-target-queue.scala 292:40tzN
.:,
:
:


io	bpdupdatebits	cfi_taken:


	bpd_entry	cfi_taken�!fetch-target-queue.scala 293:34cz=
+:)
:
:


io	bpdupdatebitstarget


bpd_target�!fetch-target-queue.scala 294:34v2P
_T_135FRD:


	bpd_entrybr_mask$:"
:


	bpd_entrycfi_idxbits�!fetch-target-queue.scala 295:54J2$
_T_136R


_T_135
0
0�!fetch-target-queue.scala 295:54bz<
.:,
:
:


io	bpdupdatebits	cfi_is_br


_T_136�!fetch-target-queue.scala 295:34^28
_T_137.R,:


	bpd_entrycfi_type	

2�!fetch-target-queue.scala 296:56^28
_T_138.R,:


	bpd_entrycfi_type	

3�!fetch-target-queue.scala 296:90L2&
_T_139R


_T_137


_T_138�!fetch-target-queue.scala 296:68cz=
/:-
:
:


io	bpdupdatebits
cfi_is_jal


_T_139�!fetch-target-queue.scala 296:34{zU
7:5
*:(
:
:


io	bpdupdatebitsghistras_idx:


	bpd_ghistras_idx�!fetch-target-queue.scala 297:34�zo
D:B
*:(
:
:


io	bpdupdatebitsghistnew_saw_branch_taken':%


	bpd_ghistnew_saw_branch_taken�!fetch-target-queue.scala 297:34�zw
H:F
*:(
:
:


io	bpdupdatebitsghistnew_saw_branch_not_taken+:)


	bpd_ghistnew_saw_branch_not_taken�!fetch-target-queue.scala 297:34�z
L:J
*:(
:
:


io	bpdupdatebitsghistcurrent_saw_branch_not_taken/:-


	bpd_ghistcurrent_saw_branch_not_taken�!fetch-target-queue.scala 297:34�z]
;:9
*:(
:
:


io	bpdupdatebitsghistold_history:


	bpd_ghistold_history�!fetch-target-queue.scala 297:34szM
3B1
*:(
:
:


io	bpdupdatebitslhist
0B


	bpd_lhist
0�!fetch-target-queue.scala 298:34qzK
2B0
):'
:
:


io	bpdupdatebitsmeta
0B



bpd_meta
0�!fetch-target-queue.scala 299:34Dz


first_empty	

0�!fetch-target-queue.scala 301:17�!fetch-target-queue.scala 278:80�:�


do_commit_update@2(
_T_140R
	
bpd_ptr	

1�util.scala 203:1472
_T_141R


_T_140
1�util.scala 203:14<2$
_T_142R


_T_141
3
0�util.scala 203:20?z

	
bpd_ptr


_T_142�!fetch-target-queue.scala 305:13�!fetch-target-queue.scala 304:27K2%
_T_143R

full	

0�!fetch-target-queue.scala 308:27V20
_T_144&R$


_T_143

do_commit_update�!fetch-target-queue.scala 308:33Z4
_T_145
	

clock"	

0*


_T_145�!fetch-target-queue.scala 308:26>z



_T_145


_T_144�!fetch-target-queue.scala 308:26Nz(
:
:


ioenqready


_T_145�!fetch-target-queue.scala 308:16�
�
redirect_new_entry�*�
.cfi_idx#*!
valid

bits

	cfi_taken

cfi_mispredicted

cfi_type

br_mask

cfi_is_call


cfi_is_ret

cfi_npc_plus4

ras_top
(
ras_idx


start_bank
�
 lze
&:$


redirect_new_entry
start_bank;:9
+J)


ram:
:


ioredirectbits
start_bank�
 fz_
#:!


redirect_new_entryras_idx8:6
+J)


ram:
:


ioredirectbitsras_idx�
 fz_
#:!


redirect_new_entryras_top8:6
+J)


ram:
:


ioredirectbitsras_top�
 rzk
):'


redirect_new_entrycfi_npc_plus4>:<
+J)


ram:
:


ioredirectbitscfi_npc_plus4�
 lze
&:$


redirect_new_entry
cfi_is_ret;:9
+J)


ram:
:


ioredirectbits
cfi_is_ret�
 nzg
':%


redirect_new_entrycfi_is_call<::
+J)


ram:
:


ioredirectbitscfi_is_call�
 fz_
#:!


redirect_new_entrybr_mask8:6
+J)


ram:
:


ioredirectbitsbr_mask�
 hza
$:"


redirect_new_entrycfi_type9:7
+J)


ram:
:


ioredirectbitscfi_type�
 xzq
,:*


redirect_new_entrycfi_mispredictedA:?
+J)


ram:
:


ioredirectbitscfi_mispredicted�
 jzc
%:#


redirect_new_entry	cfi_taken::8
+J)


ram:
:


ioredirectbits	cfi_taken�
 zzs
-:+
#:!


redirect_new_entrycfi_idxbitsB:@
8:6
+J)


ram:
:


ioredirectbitscfi_idxbits�
 |zu
.:,
#:!


redirect_new_entrycfi_idxvalidC:A
8:6
+J)


ram:
:


ioredirectbitscfi_idxvalid�
 �I:�I
:
:


ioredirectvalidS2;
_T_1461R/:
:


ioredirectbits	

1�util.scala 203:1472
_T_147R


_T_146
1�util.scala 203:14<2$
_T_148R


_T_147
3
0�util.scala 203:20?z

	
enq_ptr


_T_148�!fetch-target-queue.scala 315:16�:�
,:*
:
:


iobrupdateb2
mispredict~2X
_T_149NRL;:9
+J)


ram:
:


ioredirectbits
start_bank	

1�!fetch-target-queue.scala 319:37F2 
_T_150R	

1
3�!fetch-target-queue.scala 319:50W21
_T_151'2%



_T_149


_T_150	

0�!fetch-target-queue.scala 319:10s2M
_T_152CRA1:/
%:#
:
:


iobrupdateb2uoppc_lob


_T_151�!fetch-target-queue.scala 318:50J2$
_T_153R


_T_152
2
1�!fetch-target-queue.scala 319:79cz=
.:,
#:!


redirect_new_entrycfi_idxvalid	

1�!fetch-target-queue.scala 320:43az;
-:+
#:!


redirect_new_entrycfi_idxbits


_T_153�!fetch-target-queue.scala 321:43az;
,:*


redirect_new_entrycfi_mispredicted	

1�!fetch-target-queue.scala 322:43vzP
%:#


redirect_new_entry	cfi_taken':%
:
:


iobrupdateb2taken�!fetch-target-queue.scala 323:43�2^
_T_154TRRB:@
8:6
+J)


ram:
:


ioredirectbitscfi_idxbits


_T_153�" fetch-target-queue.scala 324:104~2X
_T_155NRL<::
+J)


ram:
:


ioredirectbitscfi_is_call


_T_154�!fetch-target-queue.scala 324:73[z5
':%


redirect_new_entrycfi_is_call


_T_155�!fetch-target-queue.scala 324:43�2^
_T_156TRRB:@
8:6
+J)


ram:
:


ioredirectbitscfi_idxbits


_T_153�" fetch-target-queue.scala 325:104}2W
_T_157MRK;:9
+J)


ram:
:


ioredirectbits
cfi_is_ret


_T_156�!fetch-target-queue.scala 325:73Zz4
&:$


redirect_new_entry
cfi_is_ret


_T_157�!fetch-target-queue.scala 325:43�!fetch-target-queue.scala 317:38Cz



ras_update	

1�!fetch-target-queue.scala 328:20szM


ras_update_pc8:6
+J)


ram:
:


ioredirectbitsras_top�!fetch-target-queue.scala 329:20tzN


ras_update_idx8:6
+J)


ram:
:


ioredirectbitsras_idx�!fetch-target-queue.scala 330:20Z4
_T_158
	

clock"	

0*


_T_158�!fetch-target-queue.scala 332:23Sz-



_T_158:
:


ioredirectvalid�!fetch-target-queue.scala 332:23�5:�5



_T_158��
_T_159�*�
.cfi_idx#*!
valid

bits

	cfi_taken

cfi_mispredicted

cfi_type

br_mask

cfi_is_call


cfi_is_ret

cfi_npc_plus4

ras_top
(
ras_idx


start_bank
	

clock"	

0*


_T_159�!fetch-target-queue.scala 333:26jzD
:



_T_159
start_bank&:$


redirect_new_entry
start_bank�!fetch-target-queue.scala 333:26dz>
:



_T_159ras_idx#:!


redirect_new_entryras_idx�!fetch-target-queue.scala 333:26dz>
:



_T_159ras_top#:!


redirect_new_entryras_top�!fetch-target-queue.scala 333:26pzJ
:



_T_159cfi_npc_plus4):'


redirect_new_entrycfi_npc_plus4�!fetch-target-queue.scala 333:26jzD
:



_T_159
cfi_is_ret&:$


redirect_new_entry
cfi_is_ret�!fetch-target-queue.scala 333:26lzF
:



_T_159cfi_is_call':%


redirect_new_entrycfi_is_call�!fetch-target-queue.scala 333:26dz>
:



_T_159br_mask#:!


redirect_new_entrybr_mask�!fetch-target-queue.scala 333:26fz@
:



_T_159cfi_type$:"


redirect_new_entrycfi_type�!fetch-target-queue.scala 333:26vzP
 :



_T_159cfi_mispredicted,:*


redirect_new_entrycfi_mispredicted�!fetch-target-queue.scala 333:26hzB
:



_T_159	cfi_taken%:#


redirect_new_entry	cfi_taken�!fetch-target-queue.scala 333:26xzR
!:
:



_T_159cfi_idxbits-:+
#:!


redirect_new_entrycfi_idxbits�!fetch-target-queue.scala 333:26zzT
": 
:



_T_159cfi_idxvalid.:,
#:!


redirect_new_entrycfi_idxvalid�!fetch-target-queue.scala 333:26bz<
:



prev_entry
start_bank:



_T_159
start_bank�!fetch-target-queue.scala 333:16\z6
:



prev_entryras_idx:



_T_159ras_idx�!fetch-target-queue.scala 333:16\z6
:



prev_entryras_top:



_T_159ras_top�!fetch-target-queue.scala 333:16hzB
!:



prev_entrycfi_npc_plus4:



_T_159cfi_npc_plus4�!fetch-target-queue.scala 333:16bz<
:



prev_entry
cfi_is_ret:



_T_159
cfi_is_ret�!fetch-target-queue.scala 333:16dz>
:



prev_entrycfi_is_call:



_T_159cfi_is_call�!fetch-target-queue.scala 333:16\z6
:



prev_entrybr_mask:



_T_159br_mask�!fetch-target-queue.scala 333:16^z8
:



prev_entrycfi_type:



_T_159cfi_type�!fetch-target-queue.scala 333:16nzH
$:"



prev_entrycfi_mispredicted :



_T_159cfi_mispredicted�!fetch-target-queue.scala 333:16`z:
:



prev_entry	cfi_taken:



_T_159	cfi_taken�!fetch-target-queue.scala 333:16pzJ
%:#
:



prev_entrycfi_idxbits!:
:



_T_159cfi_idxbits�!fetch-target-queue.scala 333:16rzL
&:$
:



prev_entrycfi_idxvalid": 
:



_T_159cfi_idxvalid�!fetch-target-queue.scala 333:16_z9
:



prev_ghistras_idx:


	bpd_ghistras_idx�!fetch-target-queue.scala 334:16yzS
(:&



prev_ghistnew_saw_branch_taken':%


	bpd_ghistnew_saw_branch_taken�!fetch-target-queue.scala 334:16�z[
,:*



prev_ghistnew_saw_branch_not_taken+:)


	bpd_ghistnew_saw_branch_not_taken�!fetch-target-queue.scala 334:16�zc
0:.



prev_ghistcurrent_saw_branch_not_taken/:-


	bpd_ghistcurrent_saw_branch_not_taken�!fetch-target-queue.scala 334:16gzA
:



prev_ghistold_history:


	bpd_ghistold_history�!fetch-target-queue.scala 334:16?z

	
prev_pc


bpd_pc�!fetch-target-queue.scala 335:16V0
_T_160 	

clock"	

0*


_T_160�!fetch-target-queue.scala 337:16Rz,



_T_160:
:


ioredirectbits�!fetch-target-queue.scala 337:16.2'
_T_161R


_T_160	

0�
 +2$
_T_162R


_T_161
3
0�
 ��
_T_163�*�
.cfi_idx#*!
valid

bits

	cfi_taken

cfi_mispredicted

cfi_type

br_mask

cfi_is_call


cfi_is_ret

cfi_npc_plus4

ras_top
(
ras_idx


start_bank
	

clock"	

0*


_T_163�!fetch-target-queue.scala 337:46jzD
:



_T_163
start_bank&:$


redirect_new_entry
start_bank�!fetch-target-queue.scala 337:46dz>
:



_T_163ras_idx#:!


redirect_new_entryras_idx�!fetch-target-queue.scala 337:46dz>
:



_T_163ras_top#:!


redirect_new_entryras_top�!fetch-target-queue.scala 337:46pzJ
:



_T_163cfi_npc_plus4):'


redirect_new_entrycfi_npc_plus4�!fetch-target-queue.scala 337:46jzD
:



_T_163
cfi_is_ret&:$


redirect_new_entry
cfi_is_ret�!fetch-target-queue.scala 337:46lzF
:



_T_163cfi_is_call':%


redirect_new_entrycfi_is_call�!fetch-target-queue.scala 337:46dz>
:



_T_163br_mask#:!


redirect_new_entrybr_mask�!fetch-target-queue.scala 337:46fz@
:



_T_163cfi_type$:"


redirect_new_entrycfi_type�!fetch-target-queue.scala 337:46vzP
 :



_T_163cfi_mispredicted,:*


redirect_new_entrycfi_mispredicted�!fetch-target-queue.scala 337:46hzB
:



_T_163	cfi_taken%:#


redirect_new_entry	cfi_taken�!fetch-target-queue.scala 337:46xzR
!:
:



_T_163cfi_idxbits-:+
#:!


redirect_new_entrycfi_idxbits�!fetch-target-queue.scala 337:46zzT
": 
:



_T_163cfi_idxvalid.:,
#:!


redirect_new_entrycfi_idxvalid�!fetch-target-queue.scala 337:46kzE
':%
J


ram


_T_162
start_bank:



_T_163
start_bank�!fetch-target-queue.scala 337:36ez?
$:"
J


ram


_T_162ras_idx:



_T_163ras_idx�!fetch-target-queue.scala 337:36ez?
$:"
J


ram


_T_162ras_top:



_T_163ras_top�!fetch-target-queue.scala 337:36qzK
*:(
J


ram


_T_162cfi_npc_plus4:



_T_163cfi_npc_plus4�!fetch-target-queue.scala 337:36kzE
':%
J


ram


_T_162
cfi_is_ret:



_T_163
cfi_is_ret�!fetch-target-queue.scala 337:36mzG
(:&
J


ram


_T_162cfi_is_call:



_T_163cfi_is_call�!fetch-target-queue.scala 337:36ez?
$:"
J


ram


_T_162br_mask:



_T_163br_mask�!fetch-target-queue.scala 337:36gzA
%:#
J


ram


_T_162cfi_type:



_T_163cfi_type�!fetch-target-queue.scala 337:36wzQ
-:+
J


ram


_T_162cfi_mispredicted :



_T_163cfi_mispredicted�!fetch-target-queue.scala 337:36izC
&:$
J


ram


_T_162	cfi_taken:



_T_163	cfi_taken�!fetch-target-queue.scala 337:36yzS
.:,
$:"
J


ram


_T_162cfi_idxbits!:
:



_T_163cfi_idxbits�!fetch-target-queue.scala 337:36{zU
/:-
$:"
J


ram


_T_162cfi_idxvalid": 
:



_T_163cfi_idxvalid�!fetch-target-queue.scala 337:36�!fetch-target-queue.scala 332:44�!fetch-target-queue.scala 314:28a2I
_T_164?R=,:*
B
:


io
get_ftq_pc
0ftq_idx	

1�util.scala 203:1472
_T_165R


_T_164
1�util.scala 203:14<2$
_T_166R


_T_165
3
0�util.scala 203:20M2'
_T_167R


_T_166
	
enq_ptr�!fetch-target-queue.scala 347:33b2F
_T_168<R::
:


ioenqready:
:


ioenqvalid�Decoupled.scala 40:37L2&
_T_169R


_T_167


_T_168�!fetch-target-queue.scala 347:46z2T
_T_170J2H



_T_169!:
:
:


ioenqbitspcJ


pcs


_T_166�!fetch-target-queue.scala 348:22��
_T_171�*�
.cfi_idx#*!
valid

bits

	cfi_taken

cfi_mispredicted

cfi_type

br_mask

cfi_is_call


cfi_is_ret

cfi_npc_plus4

ras_top
(
ras_idx


start_bank
	

clock"	

0*


_T_171�!fetch-target-queue.scala 351:42�zg
:



_T_171
start_bankI:G
9J7


ram,:*
B
:


io
get_ftq_pc
0ftq_idx
start_bank�!fetch-target-queue.scala 351:42�za
:



_T_171ras_idxF:D
9J7


ram,:*
B
:


io
get_ftq_pc
0ftq_idxras_idx�!fetch-target-queue.scala 351:42�za
:



_T_171ras_topF:D
9J7


ram,:*
B
:


io
get_ftq_pc
0ftq_idxras_top�!fetch-target-queue.scala 351:42�zm
:



_T_171cfi_npc_plus4L:J
9J7


ram,:*
B
:


io
get_ftq_pc
0ftq_idxcfi_npc_plus4�!fetch-target-queue.scala 351:42�zg
:



_T_171
cfi_is_retI:G
9J7


ram,:*
B
:


io
get_ftq_pc
0ftq_idx
cfi_is_ret�!fetch-target-queue.scala 351:42�zi
:



_T_171cfi_is_callJ:H
9J7


ram,:*
B
:


io
get_ftq_pc
0ftq_idxcfi_is_call�!fetch-target-queue.scala 351:42�za
:



_T_171br_maskF:D
9J7


ram,:*
B
:


io
get_ftq_pc
0ftq_idxbr_mask�!fetch-target-queue.scala 351:42�zc
:



_T_171cfi_typeG:E
9J7


ram,:*
B
:


io
get_ftq_pc
0ftq_idxcfi_type�!fetch-target-queue.scala 351:42�zs
 :



_T_171cfi_mispredictedO:M
9J7


ram,:*
B
:


io
get_ftq_pc
0ftq_idxcfi_mispredicted�!fetch-target-queue.scala 351:42�ze
:



_T_171	cfi_takenH:F
9J7


ram,:*
B
:


io
get_ftq_pc
0ftq_idx	cfi_taken�!fetch-target-queue.scala 351:42�zu
!:
:



_T_171cfi_idxbitsP:N
F:D
9J7


ram,:*
B
:


io
get_ftq_pc
0ftq_idxcfi_idxbits�!fetch-target-queue.scala 351:42�zw
": 
:



_T_171cfi_idxvalidQ:O
F:D
9J7


ram,:*
B
:


io
get_ftq_pc
0ftq_idxcfi_idxvalid�!fetch-target-queue.scala 351:42~zX
::8
*:(
B
:


io
get_ftq_pc
0entry
start_bank:



_T_171
start_bank�!fetch-target-queue.scala 351:32xzR
7:5
*:(
B
:


io
get_ftq_pc
0entryras_idx:



_T_171ras_idx�!fetch-target-queue.scala 351:32xzR
7:5
*:(
B
:


io
get_ftq_pc
0entryras_top:



_T_171ras_top�!fetch-target-queue.scala 351:32�z^
=:;
*:(
B
:


io
get_ftq_pc
0entrycfi_npc_plus4:



_T_171cfi_npc_plus4�!fetch-target-queue.scala 351:32~zX
::8
*:(
B
:


io
get_ftq_pc
0entry
cfi_is_ret:



_T_171
cfi_is_ret�!fetch-target-queue.scala 351:32�zZ
;:9
*:(
B
:


io
get_ftq_pc
0entrycfi_is_call:



_T_171cfi_is_call�!fetch-target-queue.scala 351:32xzR
7:5
*:(
B
:


io
get_ftq_pc
0entrybr_mask:



_T_171br_mask�!fetch-target-queue.scala 351:32zzT
8:6
*:(
B
:


io
get_ftq_pc
0entrycfi_type:



_T_171cfi_type�!fetch-target-queue.scala 351:32�zd
@:>
*:(
B
:


io
get_ftq_pc
0entrycfi_mispredicted :



_T_171cfi_mispredicted�!fetch-target-queue.scala 351:32|zV
9:7
*:(
B
:


io
get_ftq_pc
0entry	cfi_taken:



_T_171	cfi_taken�!fetch-target-queue.scala 351:32�zf
A:?
7:5
*:(
B
:


io
get_ftq_pc
0entrycfi_idxbits!:
:



_T_171cfi_idxbits�!fetch-target-queue.scala 351:32�zh
B:@
7:5
*:(
B
:


io
get_ftq_pc
0entrycfi_idxvalid": 
:



_T_171cfi_idxvalid�!fetch-target-queue.scala 351:32`�9
7:5
*:(
B
:


io
get_ftq_pc
0ghistras_idx�!fetch-target-queue.scala 355:32m�F
D:B
*:(
B
:


io
get_ftq_pc
0ghistnew_saw_branch_taken�!fetch-target-queue.scala 355:32q�J
H:F
*:(
B
:


io
get_ftq_pc
0ghistnew_saw_branch_not_taken�!fetch-target-queue.scala 355:32u�N
L:J
*:(
B
:


io
get_ftq_pc
0ghistcurrent_saw_branch_not_taken�!fetch-target-queue.scala 355:32d�=
;:9
*:(
B
:


io
get_ftq_pc
0ghistold_history�!fetch-target-queue.scala 355:32V0
_T_172 	

clock"	

0*


_T_172�!fetch-target-queue.scala 356:42mzG



_T_1729J7


pcs,:*
B
:


io
get_ftq_pc
0ftq_idx�!fetch-target-queue.scala 356:42[z5
':%
B
:


io
get_ftq_pc
0pc


_T_172�!fetch-target-queue.scala 356:32V0
_T_173 	

clock"	

0*


_T_173�!fetch-target-queue.scala 357:42>z



_T_173


_T_170�!fetch-target-queue.scala 357:42`z:
,:*
B
:


io
get_ftq_pc
0next_pc


_T_173�!fetch-target-queue.scala 357:32M2'
_T_174R


_T_166
	
enq_ptr�!fetch-target-queue.scala 358:52L2&
_T_175R


_T_174


_T_169�!fetch-target-queue.scala 358:64Z4
_T_176
	

clock"	

0*


_T_176�!fetch-target-queue.scala 358:42>z



_T_176


_T_175�!fetch-target-queue.scala 358:42az;
-:+
B
:


io
get_ftq_pc
0next_val


_T_176�!fetch-target-queue.scala 358:32v2P
_T_177F2D
:
:


iodeqvalid:
:


iodeqbits
	
deq_ptr�!fetch-target-queue.scala 359:50V0
_T_178 	

clock"	

0*


_T_178�!fetch-target-queue.scala 359:42Kz%



_T_178J


pcs


_T_177�!fetch-target-queue.scala 359:42_z9
+:)
B
:


io
get_ftq_pc
0com_pc


_T_178�!fetch-target-queue.scala 359:32a2I
_T_179?R=,:*
B
:


io
get_ftq_pc
1ftq_idx	

1�util.scala 203:1472
_T_180R


_T_179
1�util.scala 203:14<2$
_T_181R


_T_180
3
0�util.scala 203:20M2'
_T_182R


_T_181
	
enq_ptr�!fetch-target-queue.scala 347:33b2F
_T_183<R::
:


ioenqready:
:


ioenqvalid�Decoupled.scala 40:37L2&
_T_184R


_T_182


_T_183�!fetch-target-queue.scala 347:46z2T
_T_185J2H



_T_184!:
:
:


ioenqbitspcJ


pcs


_T_181�!fetch-target-queue.scala 348:22��
_T_186�*�
.cfi_idx#*!
valid

bits

	cfi_taken

cfi_mispredicted

cfi_type

br_mask

cfi_is_call


cfi_is_ret

cfi_npc_plus4

ras_top
(
ras_idx


start_bank
	

clock"	

0*


_T_186�!fetch-target-queue.scala 351:42�zg
:



_T_186
start_bankI:G
9J7


ram,:*
B
:


io
get_ftq_pc
1ftq_idx
start_bank�!fetch-target-queue.scala 351:42�za
:



_T_186ras_idxF:D
9J7


ram,:*
B
:


io
get_ftq_pc
1ftq_idxras_idx�!fetch-target-queue.scala 351:42�za
:



_T_186ras_topF:D
9J7


ram,:*
B
:


io
get_ftq_pc
1ftq_idxras_top�!fetch-target-queue.scala 351:42�zm
:



_T_186cfi_npc_plus4L:J
9J7


ram,:*
B
:


io
get_ftq_pc
1ftq_idxcfi_npc_plus4�!fetch-target-queue.scala 351:42�zg
:



_T_186
cfi_is_retI:G
9J7


ram,:*
B
:


io
get_ftq_pc
1ftq_idx
cfi_is_ret�!fetch-target-queue.scala 351:42�zi
:



_T_186cfi_is_callJ:H
9J7


ram,:*
B
:


io
get_ftq_pc
1ftq_idxcfi_is_call�!fetch-target-queue.scala 351:42�za
:



_T_186br_maskF:D
9J7


ram,:*
B
:


io
get_ftq_pc
1ftq_idxbr_mask�!fetch-target-queue.scala 351:42�zc
:



_T_186cfi_typeG:E
9J7


ram,:*
B
:


io
get_ftq_pc
1ftq_idxcfi_type�!fetch-target-queue.scala 351:42�zs
 :



_T_186cfi_mispredictedO:M
9J7


ram,:*
B
:


io
get_ftq_pc
1ftq_idxcfi_mispredicted�!fetch-target-queue.scala 351:42�ze
:



_T_186	cfi_takenH:F
9J7


ram,:*
B
:


io
get_ftq_pc
1ftq_idx	cfi_taken�!fetch-target-queue.scala 351:42�zu
!:
:



_T_186cfi_idxbitsP:N
F:D
9J7


ram,:*
B
:


io
get_ftq_pc
1ftq_idxcfi_idxbits�!fetch-target-queue.scala 351:42�zw
": 
:



_T_186cfi_idxvalidQ:O
F:D
9J7


ram,:*
B
:


io
get_ftq_pc
1ftq_idxcfi_idxvalid�!fetch-target-queue.scala 351:42~zX
::8
*:(
B
:


io
get_ftq_pc
1entry
start_bank:



_T_186
start_bank�!fetch-target-queue.scala 351:32xzR
7:5
*:(
B
:


io
get_ftq_pc
1entryras_idx:



_T_186ras_idx�!fetch-target-queue.scala 351:32xzR
7:5
*:(
B
:


io
get_ftq_pc
1entryras_top:



_T_186ras_top�!fetch-target-queue.scala 351:32�z^
=:;
*:(
B
:


io
get_ftq_pc
1entrycfi_npc_plus4:



_T_186cfi_npc_plus4�!fetch-target-queue.scala 351:32~zX
::8
*:(
B
:


io
get_ftq_pc
1entry
cfi_is_ret:



_T_186
cfi_is_ret�!fetch-target-queue.scala 351:32�zZ
;:9
*:(
B
:


io
get_ftq_pc
1entrycfi_is_call:



_T_186cfi_is_call�!fetch-target-queue.scala 351:32xzR
7:5
*:(
B
:


io
get_ftq_pc
1entrybr_mask:



_T_186br_mask�!fetch-target-queue.scala 351:32zzT
8:6
*:(
B
:


io
get_ftq_pc
1entrycfi_type:



_T_186cfi_type�!fetch-target-queue.scala 351:32�zd
@:>
*:(
B
:


io
get_ftq_pc
1entrycfi_mispredicted :



_T_186cfi_mispredicted�!fetch-target-queue.scala 351:32|zV
9:7
*:(
B
:


io
get_ftq_pc
1entry	cfi_taken:



_T_186	cfi_taken�!fetch-target-queue.scala 351:32�zf
A:?
7:5
*:(
B
:


io
get_ftq_pc
1entrycfi_idxbits!:
:



_T_186cfi_idxbits�!fetch-target-queue.scala 351:32�zh
B:@
7:5
*:(
B
:


io
get_ftq_pc
1entrycfi_idxvalid": 
:



_T_186cfi_idxvalid�!fetch-target-queue.scala 351:322

_T_187 �!fetch-target-queue.scala 353:483�



_T_187�!fetch-target-queue.scala 353:48�:�
	

1`z:



_T_187,:*
B
:


io
get_ftq_pc
1ftq_idx�!fetch-target-queue.scala 353:48M2'
_T_188R


_T_187	

0�!fetch-target-queue.scala 353:48J2$
_T_189R


_T_188
3
0�!fetch-target-queue.scala 353:48Q�*_T_190ghist_1"


_T_189*	

clock�!fetch-target-queue.scala 353:48�!fetch-target-queue.scala 353:48xzR
7:5
*:(
B
:


io
get_ftq_pc
1ghistras_idx:



_T_190ras_idx�!fetch-target-queue.scala 353:32�zl
D:B
*:(
B
:


io
get_ftq_pc
1ghistnew_saw_branch_taken$:"



_T_190new_saw_branch_taken�!fetch-target-queue.scala 353:32�zt
H:F
*:(
B
:


io
get_ftq_pc
1ghistnew_saw_branch_not_taken(:&



_T_190new_saw_branch_not_taken�!fetch-target-queue.scala 353:32�z|
L:J
*:(
B
:


io
get_ftq_pc
1ghistcurrent_saw_branch_not_taken,:*



_T_190current_saw_branch_not_taken�!fetch-target-queue.scala 353:32�zZ
;:9
*:(
B
:


io
get_ftq_pc
1ghistold_history:



_T_190old_history�!fetch-target-queue.scala 353:32V0
_T_191 	

clock"	

0*


_T_191�!fetch-target-queue.scala 356:42mzG



_T_1919J7


pcs,:*
B
:


io
get_ftq_pc
1ftq_idx�!fetch-target-queue.scala 356:42[z5
':%
B
:


io
get_ftq_pc
1pc


_T_191�!fetch-target-queue.scala 356:32V0
_T_192 	

clock"	

0*


_T_192�!fetch-target-queue.scala 357:42>z



_T_192


_T_185�!fetch-target-queue.scala 357:42`z:
,:*
B
:


io
get_ftq_pc
1next_pc


_T_192�!fetch-target-queue.scala 357:32M2'
_T_193R


_T_181
	
enq_ptr�!fetch-target-queue.scala 358:52L2&
_T_194R


_T_193


_T_184�!fetch-target-queue.scala 358:64Z4
_T_195
	

clock"	

0*


_T_195�!fetch-target-queue.scala 358:42>z



_T_195


_T_194�!fetch-target-queue.scala 358:42az;
-:+
B
:


io
get_ftq_pc
1next_val


_T_195�!fetch-target-queue.scala 358:32v2P
_T_196F2D
:
:


iodeqvalid:
:


iodeqbits
	
deq_ptr�!fetch-target-queue.scala 359:50V0
_T_197 	

clock"	

0*


_T_197�!fetch-target-queue.scala 359:42Kz%



_T_197J


pcs


_T_196�!fetch-target-queue.scala 359:42_z9
+:)
B
:


io
get_ftq_pc
1com_pc


_T_197�!fetch-target-queue.scala 359:32V0
_T_198 	

clock"	

0*


_T_198�!fetch-target-queue.scala 363:36cz=



_T_198/J-


pcs"B 
:


iodebug_ftq_idx
0�!fetch-target-queue.scala 363:36Wz1
#B!
:


iodebug_fetch_pc
0


_T_198�!fetch-target-queue.scala 363:26
FetchTargetQueue