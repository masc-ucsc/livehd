
·©
‘©©
RenameMapTable
clock" 
reset

ioχ*τ
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
rollback

	

clock
 
	

reset
 


io
 4

_T2


 rename-maptable.scala 70:34@z
B


_T
0	

0rename-maptable.scala 70:34@z
B


_T
1	

0rename-maptable.scala 70:34@z
B


_T
2	

0rename-maptable.scala 70:34@z
B


_T
3	

0rename-maptable.scala 70:34@z
B


_T
4	

0rename-maptable.scala 70:34@z
B


_T
5	

0rename-maptable.scala 70:34@z
B


_T
6	

0rename-maptable.scala 70:34@z
B


_T
7	

0rename-maptable.scala 70:34@z
B


_T
8	

0rename-maptable.scala 70:34@z
B


_T
9	

0rename-maptable.scala 70:34Az
B


_T
10	

0rename-maptable.scala 70:34Az
B


_T
11	

0rename-maptable.scala 70:34Az
B


_T
12	

0rename-maptable.scala 70:34Az
B


_T
13	

0rename-maptable.scala 70:34Az
B


_T
14	

0rename-maptable.scala 70:34Az
B


_T
15	

0rename-maptable.scala 70:34Az
B


_T
16	

0rename-maptable.scala 70:34Az
B


_T
17	

0rename-maptable.scala 70:34Az
B


_T
18	

0rename-maptable.scala 70:34Az
B


_T
19	

0rename-maptable.scala 70:34Az
B


_T
20	

0rename-maptable.scala 70:34Az
B


_T
21	

0rename-maptable.scala 70:34Az
B


_T
22	

0rename-maptable.scala 70:34Az
B


_T
23	

0rename-maptable.scala 70:34Az
B


_T
24	

0rename-maptable.scala 70:34Az
B


_T
25	

0rename-maptable.scala 70:34Az
B


_T
26	

0rename-maptable.scala 70:34Az
B


_T
27	

0rename-maptable.scala 70:34Az
B


_T
28	

0rename-maptable.scala 70:34Az
B


_T
29	

0rename-maptable.scala 70:34Az
B


_T
30	

0rename-maptable.scala 70:34Az
B


_T
31	

0rename-maptable.scala 70:34Y7
	map_table2


 	

clock"	

reset*

_Trename-maptable.scala 70:26nL
br_snapshots2
2


 	

clock"	

0*

br_snapshotsrename-maptable.scala 71:25C
!
remap_table2
2


 rename-maptable.scala 74:25]2D
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
0ldstOneHot.scala 58:35\2B
_T_2:R8*:(
B
:


io
remap_reqs
0valid
0
0Bitwise.scala 72:15Q27
_T_3/2-


_T_2


4294967295 	

0 Bitwise.scala 72:12N2,
remap_ldsts_oh_0R

_T_1

_T_3rename-maptable.scala 78:69Rz0
!B
B


remap_table
0
0	

0rename-maptable.scala 84:27Rz0
!B
B


remap_table
1
0	

0rename-maptable.scala 84:27N2,
_T_4$R"

remap_ldsts_oh_0
1
1rename-maptable.scala 87:58y2W
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
1rename-maptable.scala 88:70]z;
!B
B


remap_table
0
1B


	map_table
1rename-maptable.scala 91:27Oz-
!B
B


remap_table
1
1

_T_5rename-maptable.scala 91:27N2,
_T_6$R"

remap_ldsts_oh_0
2
2rename-maptable.scala 87:58y2W
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
2rename-maptable.scala 88:70]z;
!B
B


remap_table
0
2B


	map_table
2rename-maptable.scala 91:27Oz-
!B
B


remap_table
1
2

_T_7rename-maptable.scala 91:27N2,
_T_8$R"

remap_ldsts_oh_0
3
3rename-maptable.scala 87:58y2W
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
3rename-maptable.scala 88:70]z;
!B
B


remap_table
0
3B


	map_table
3rename-maptable.scala 91:27Oz-
!B
B


remap_table
1
3

_T_9rename-maptable.scala 91:27O2-
_T_10$R"

remap_ldsts_oh_0
4
4rename-maptable.scala 87:58{2Y
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
4rename-maptable.scala 88:70]z;
!B
B


remap_table
0
4B


	map_table
4rename-maptable.scala 91:27Pz.
!B
B


remap_table
1
4	

_T_11rename-maptable.scala 91:27O2-
_T_12$R"

remap_ldsts_oh_0
5
5rename-maptable.scala 87:58{2Y
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
5rename-maptable.scala 88:70]z;
!B
B


remap_table
0
5B


	map_table
5rename-maptable.scala 91:27Pz.
!B
B


remap_table
1
5	

_T_13rename-maptable.scala 91:27O2-
_T_14$R"

remap_ldsts_oh_0
6
6rename-maptable.scala 87:58{2Y
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
6rename-maptable.scala 88:70]z;
!B
B


remap_table
0
6B


	map_table
6rename-maptable.scala 91:27Pz.
!B
B


remap_table
1
6	

_T_15rename-maptable.scala 91:27O2-
_T_16$R"

remap_ldsts_oh_0
7
7rename-maptable.scala 87:58{2Y
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
7rename-maptable.scala 88:70]z;
!B
B


remap_table
0
7B


	map_table
7rename-maptable.scala 91:27Pz.
!B
B


remap_table
1
7	

_T_17rename-maptable.scala 91:27O2-
_T_18$R"

remap_ldsts_oh_0
8
8rename-maptable.scala 87:58{2Y
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
8rename-maptable.scala 88:70]z;
!B
B


remap_table
0
8B


	map_table
8rename-maptable.scala 91:27Pz.
!B
B


remap_table
1
8	

_T_19rename-maptable.scala 91:27O2-
_T_20$R"

remap_ldsts_oh_0
9
9rename-maptable.scala 87:58{2Y
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
9rename-maptable.scala 88:70]z;
!B
B


remap_table
0
9B


	map_table
9rename-maptable.scala 91:27Pz.
!B
B


remap_table
1
9	

_T_21rename-maptable.scala 91:27Q2/
_T_22&R$

remap_ldsts_oh_0
10
10rename-maptable.scala 87:58|2Z
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
10rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
10B


	map_table
10rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
10	

_T_23rename-maptable.scala 91:27Q2/
_T_24&R$

remap_ldsts_oh_0
11
11rename-maptable.scala 87:58|2Z
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
11rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
11B


	map_table
11rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
11	

_T_25rename-maptable.scala 91:27Q2/
_T_26&R$

remap_ldsts_oh_0
12
12rename-maptable.scala 87:58|2Z
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
12rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
12B


	map_table
12rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
12	

_T_27rename-maptable.scala 91:27Q2/
_T_28&R$

remap_ldsts_oh_0
13
13rename-maptable.scala 87:58|2Z
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
13rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
13B


	map_table
13rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
13	

_T_29rename-maptable.scala 91:27Q2/
_T_30&R$

remap_ldsts_oh_0
14
14rename-maptable.scala 87:58|2Z
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
14rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
14B


	map_table
14rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
14	

_T_31rename-maptable.scala 91:27Q2/
_T_32&R$

remap_ldsts_oh_0
15
15rename-maptable.scala 87:58|2Z
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
15rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
15B


	map_table
15rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
15	

_T_33rename-maptable.scala 91:27Q2/
_T_34&R$

remap_ldsts_oh_0
16
16rename-maptable.scala 87:58|2Z
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
16rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
16B


	map_table
16rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
16	

_T_35rename-maptable.scala 91:27Q2/
_T_36&R$

remap_ldsts_oh_0
17
17rename-maptable.scala 87:58|2Z
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
17rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
17B


	map_table
17rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
17	

_T_37rename-maptable.scala 91:27Q2/
_T_38&R$

remap_ldsts_oh_0
18
18rename-maptable.scala 87:58|2Z
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
18rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
18B


	map_table
18rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
18	

_T_39rename-maptable.scala 91:27Q2/
_T_40&R$

remap_ldsts_oh_0
19
19rename-maptable.scala 87:58|2Z
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
19rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
19B


	map_table
19rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
19	

_T_41rename-maptable.scala 91:27Q2/
_T_42&R$

remap_ldsts_oh_0
20
20rename-maptable.scala 87:58|2Z
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
20rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
20B


	map_table
20rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
20	

_T_43rename-maptable.scala 91:27Q2/
_T_44&R$

remap_ldsts_oh_0
21
21rename-maptable.scala 87:58|2Z
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
21rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
21B


	map_table
21rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
21	

_T_45rename-maptable.scala 91:27Q2/
_T_46&R$

remap_ldsts_oh_0
22
22rename-maptable.scala 87:58|2Z
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
22rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
22B


	map_table
22rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
22	

_T_47rename-maptable.scala 91:27Q2/
_T_48&R$

remap_ldsts_oh_0
23
23rename-maptable.scala 87:58|2Z
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
23rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
23B


	map_table
23rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
23	

_T_49rename-maptable.scala 91:27Q2/
_T_50&R$

remap_ldsts_oh_0
24
24rename-maptable.scala 87:58|2Z
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
24rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
24B


	map_table
24rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
24	

_T_51rename-maptable.scala 91:27Q2/
_T_52&R$

remap_ldsts_oh_0
25
25rename-maptable.scala 87:58|2Z
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
25rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
25B


	map_table
25rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
25	

_T_53rename-maptable.scala 91:27Q2/
_T_54&R$

remap_ldsts_oh_0
26
26rename-maptable.scala 87:58|2Z
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
26rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
26B


	map_table
26rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
26	

_T_55rename-maptable.scala 91:27Q2/
_T_56&R$

remap_ldsts_oh_0
27
27rename-maptable.scala 87:58|2Z
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
27rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
27B


	map_table
27rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
27	

_T_57rename-maptable.scala 91:27Q2/
_T_58&R$

remap_ldsts_oh_0
28
28rename-maptable.scala 87:58|2Z
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
28rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
28B


	map_table
28rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
28	

_T_59rename-maptable.scala 91:27Q2/
_T_60&R$

remap_ldsts_oh_0
29
29rename-maptable.scala 87:58|2Z
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
29rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
29B


	map_table
29rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
29	

_T_61rename-maptable.scala 91:27Q2/
_T_62&R$

remap_ldsts_oh_0
30
30rename-maptable.scala 87:58|2Z
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
30rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
30B


	map_table
30rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
30	

_T_63rename-maptable.scala 91:27Q2/
_T_64&R$

remap_ldsts_oh_0
31
31rename-maptable.scala 87:58|2Z
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
31rename-maptable.scala 88:70_z=
"B 
B


remap_table
0
31B


	map_table
31rename-maptable.scala 91:27Qz/
"B 
B


remap_table
1
31	

_T_65rename-maptable.scala 91:27ά%:Ή%
+:)
 B
:


ioren_br_tags
0validzn
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
0rename-maptable.scala 99:44zn
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
1rename-maptable.scala 99:44zn
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
2rename-maptable.scala 99:44zn
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
3rename-maptable.scala 99:44zn
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
4rename-maptable.scala 99:44zn
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
5rename-maptable.scala 99:44zn
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
6rename-maptable.scala 99:44zn
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
7rename-maptable.scala 99:44zn
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
8rename-maptable.scala 99:44zn
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
9rename-maptable.scala 99:44zp
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
10rename-maptable.scala 99:44zp
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
11rename-maptable.scala 99:44zp
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
12rename-maptable.scala 99:44zp
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
13rename-maptable.scala 99:44zp
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
14rename-maptable.scala 99:44zp
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
15rename-maptable.scala 99:44zp
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
16rename-maptable.scala 99:44zp
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
17rename-maptable.scala 99:44zp
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
18rename-maptable.scala 99:44zp
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
19rename-maptable.scala 99:44zp
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
20rename-maptable.scala 99:44zp
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
21rename-maptable.scala 99:44zp
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
22rename-maptable.scala 99:44zp
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
23rename-maptable.scala 99:44zp
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
24rename-maptable.scala 99:44zp
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
25rename-maptable.scala 99:44zp
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
26rename-maptable.scala 99:44zp
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
27rename-maptable.scala 99:44zp
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
28rename-maptable.scala 99:44zp
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
29rename-maptable.scala 99:44zp
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
30rename-maptable.scala 99:44zp
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
31rename-maptable.scala 99:44rename-maptable.scala 98:36ͺ=:=
,:*
:
:


iobrupdateb2
mispredictzj
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
0rename-maptable.scala 105:15zj
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
1rename-maptable.scala 105:15zj
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
2rename-maptable.scala 105:15zj
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
3rename-maptable.scala 105:15zj
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
4rename-maptable.scala 105:15zj
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
5rename-maptable.scala 105:15zj
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
6rename-maptable.scala 105:15zj
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
7rename-maptable.scala 105:15zj
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
8rename-maptable.scala 105:15zj
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
9rename-maptable.scala 105:15zl
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
10rename-maptable.scala 105:15zl
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
11rename-maptable.scala 105:15zl
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
12rename-maptable.scala 105:15zl
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
13rename-maptable.scala 105:15zl
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
14rename-maptable.scala 105:15zl
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
15rename-maptable.scala 105:15zl
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
16rename-maptable.scala 105:15zl
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
17rename-maptable.scala 105:15zl
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
18rename-maptable.scala 105:15zl
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
19rename-maptable.scala 105:15zl
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
20rename-maptable.scala 105:15zl
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
21rename-maptable.scala 105:15zl
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
22rename-maptable.scala 105:15zl
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
23rename-maptable.scala 105:15zl
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
24rename-maptable.scala 105:15zl
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
25rename-maptable.scala 105:15zl
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
26rename-maptable.scala 105:15zl
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
27rename-maptable.scala 105:15zl
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
28rename-maptable.scala 105:15zl
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
29rename-maptable.scala 105:15zl
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
30rename-maptable.scala 105:15zl
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
31rename-maptable.scala 105:15^z;
B


	map_table
0!B
B


remap_table
1
0rename-maptable.scala 108:15^z;
B


	map_table
1!B
B


remap_table
1
1rename-maptable.scala 108:15^z;
B


	map_table
2!B
B


remap_table
1
2rename-maptable.scala 108:15^z;
B


	map_table
3!B
B


remap_table
1
3rename-maptable.scala 108:15^z;
B


	map_table
4!B
B


remap_table
1
4rename-maptable.scala 108:15^z;
B


	map_table
5!B
B


remap_table
1
5rename-maptable.scala 108:15^z;
B


	map_table
6!B
B


remap_table
1
6rename-maptable.scala 108:15^z;
B


	map_table
7!B
B


remap_table
1
7rename-maptable.scala 108:15^z;
B


	map_table
8!B
B


remap_table
1
8rename-maptable.scala 108:15^z;
B


	map_table
9!B
B


remap_table
1
9rename-maptable.scala 108:15`z=
B


	map_table
10"B 
B


remap_table
1
10rename-maptable.scala 108:15`z=
B


	map_table
11"B 
B


remap_table
1
11rename-maptable.scala 108:15`z=
B


	map_table
12"B 
B


remap_table
1
12rename-maptable.scala 108:15`z=
B


	map_table
13"B 
B


remap_table
1
13rename-maptable.scala 108:15`z=
B


	map_table
14"B 
B


remap_table
1
14rename-maptable.scala 108:15`z=
B


	map_table
15"B 
B


remap_table
1
15rename-maptable.scala 108:15`z=
B


	map_table
16"B 
B


remap_table
1
16rename-maptable.scala 108:15`z=
B


	map_table
17"B 
B


remap_table
1
17rename-maptable.scala 108:15`z=
B


	map_table
18"B 
B


remap_table
1
18rename-maptable.scala 108:15`z=
B


	map_table
19"B 
B


remap_table
1
19rename-maptable.scala 108:15`z=
B


	map_table
20"B 
B


remap_table
1
20rename-maptable.scala 108:15`z=
B


	map_table
21"B 
B


remap_table
1
21rename-maptable.scala 108:15`z=
B


	map_table
22"B 
B


remap_table
1
22rename-maptable.scala 108:15`z=
B


	map_table
23"B 
B


remap_table
1
23rename-maptable.scala 108:15`z=
B


	map_table
24"B 
B


remap_table
1
24rename-maptable.scala 108:15`z=
B


	map_table
25"B 
B


remap_table
1
25rename-maptable.scala 108:15`z=
B


	map_table
26"B 
B


remap_table
1
26rename-maptable.scala 108:15`z=
B


	map_table
27"B 
B


remap_table
1
27rename-maptable.scala 108:15`z=
B


	map_table
28"B 
B


remap_table
1
28rename-maptable.scala 108:15`z=
B


	map_table
29"B 
B


remap_table
1
29rename-maptable.scala 108:15`z=
B


	map_table
30"B 
B


remap_table
1
30rename-maptable.scala 108:15`z=
B


	map_table
31"B 
B


remap_table
1
31rename-maptable.scala 108:15rename-maptable.scala 103:36G2@
_T_667R5':%
B
:


iomap_reqs
0lrs1
4
0
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
_T_66rename-maptable.scala 113:32G2@
_T_677R5':%
B
:


iomap_reqs
0lrs2
4
0
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
_T_67rename-maptable.scala 115:32G2@
_T_687R5':%
B
:


iomap_reqs
0lrs3
4
0
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
_T_68rename-maptable.scala 117:32G2@
_T_697R5':%
B
:


iomap_reqs
0ldst
4
0
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
_T_69rename-maptable.scala 119:32N*
(:&
B
:


io	map_resps
0prs3rename-maptable.scala 122:38i2F
_T_70=R;*:(
B
:


io
remap_reqs
0valid	

0rename-maptable.scala 128:13s2P
_T_71GREB


	map_table
0):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38s2P
_T_72GREB


	map_table
1):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38s2P
_T_73GREB


	map_table
2):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38s2P
_T_74GREB


	map_table
3):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38s2P
_T_75GREB


	map_table
4):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38s2P
_T_76GREB


	map_table
5):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38s2P
_T_77GREB


	map_table
6):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38s2P
_T_78GREB


	map_table
7):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38s2P
_T_79GREB


	map_table
8):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38s2P
_T_80GREB


	map_table
9):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_81HRFB


	map_table
10):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_82HRFB


	map_table
11):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_83HRFB


	map_table
12):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_84HRFB


	map_table
13):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_85HRFB


	map_table
14):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_86HRFB


	map_table
15):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_87HRFB


	map_table
16):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_88HRFB


	map_table
17):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_89HRFB


	map_table
18):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_90HRFB


	map_table
19):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_91HRFB


	map_table
20):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_92HRFB


	map_table
21):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_93HRFB


	map_table
22):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_94HRFB


	map_table
23):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_95HRFB


	map_table
24):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_96HRFB


	map_table
25):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_97HRFB


	map_table
26):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_98HRFB


	map_table
27):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38t2Q
_T_99HRFB


	map_table
28):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38u2R
_T_100HRFB


	map_table
29):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38u2R
_T_101HRFB


	map_table
30):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38u2R
_T_102HRFB


	map_table
31):'
B
:


io
remap_reqs
0pdstrename-maptable.scala 128:38I2&
_T_103R	

0	

_T_71rename-maptable.scala 128:38H2%
_T_104R


_T_103	

_T_72rename-maptable.scala 128:38H2%
_T_105R


_T_104	

_T_73rename-maptable.scala 128:38H2%
_T_106R


_T_105	

_T_74rename-maptable.scala 128:38H2%
_T_107R


_T_106	

_T_75rename-maptable.scala 128:38H2%
_T_108R


_T_107	

_T_76rename-maptable.scala 128:38H2%
_T_109R


_T_108	

_T_77rename-maptable.scala 128:38H2%
_T_110R


_T_109	

_T_78rename-maptable.scala 128:38H2%
_T_111R


_T_110	

_T_79rename-maptable.scala 128:38H2%
_T_112R


_T_111	

_T_80rename-maptable.scala 128:38H2%
_T_113R


_T_112	

_T_81rename-maptable.scala 128:38H2%
_T_114R


_T_113	

_T_82rename-maptable.scala 128:38H2%
_T_115R


_T_114	

_T_83rename-maptable.scala 128:38H2%
_T_116R


_T_115	

_T_84rename-maptable.scala 128:38H2%
_T_117R


_T_116	

_T_85rename-maptable.scala 128:38H2%
_T_118R


_T_117	

_T_86rename-maptable.scala 128:38H2%
_T_119R


_T_118	

_T_87rename-maptable.scala 128:38H2%
_T_120R


_T_119	

_T_88rename-maptable.scala 128:38H2%
_T_121R


_T_120	

_T_89rename-maptable.scala 128:38H2%
_T_122R


_T_121	

_T_90rename-maptable.scala 128:38H2%
_T_123R


_T_122	

_T_91rename-maptable.scala 128:38H2%
_T_124R


_T_123	

_T_92rename-maptable.scala 128:38H2%
_T_125R


_T_124	

_T_93rename-maptable.scala 128:38H2%
_T_126R


_T_125	

_T_94rename-maptable.scala 128:38H2%
_T_127R


_T_126	

_T_95rename-maptable.scala 128:38H2%
_T_128R


_T_127	

_T_96rename-maptable.scala 128:38H2%
_T_129R


_T_128	

_T_97rename-maptable.scala 128:38H2%
_T_130R


_T_129	

_T_98rename-maptable.scala 128:38H2%
_T_131R


_T_130	

_T_99rename-maptable.scala 128:38I2&
_T_132R


_T_131


_T_100rename-maptable.scala 128:38I2&
_T_133R


_T_132


_T_101rename-maptable.scala 128:38I2&
_T_134R


_T_133


_T_102rename-maptable.scala 128:38J2'
_T_135R


_T_134	

0rename-maptable.scala 128:19H2%
_T_136R	

_T_70


_T_135rename-maptable.scala 128:16i2F
_T_137<R:):'
B
:


io
remap_reqs
0pdst	

0rename-maptable.scala 128:47S20
_T_138&R$


_T_137:


iorollbackrename-maptable.scala 128:55I2&
_T_139R


_T_136


_T_138rename-maptable.scala 128:42F2#
_T_140R	

reset
0
0rename-maptable.scala 128:12I2&
_T_141R


_T_139


_T_140rename-maptable.scala 128:12J2'
_T_142R


_T_141	

0rename-maptable.scala 128:12:ζ



_T_142Rτ
ΩAssertion failed: [maptable] Trying to write a duplicate mapping.
    at rename-maptable.scala:128 assert (!r || !map_table.contains(p) || p === 0.U && io.rollback, "[maptable] Trying to write a duplicate mapping.")}
	

clock"	

1rename-maptable.scala 128:12=B	

clock	

1rename-maptable.scala 128:12rename-maptable.scala 128:12
RenameMapTable