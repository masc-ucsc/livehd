
�4
�4�4
Router
clock" 
reset
�
io�*�
bread_routing_table_requestB*@
ready

valid

bits*
addr
 
Uread_routing_table_response6*4
ready

valid

bits
 
rload_routing_table_requestR*P
ready

valid

*bits"* 
addr
 
data
 
\inT*R
ready

valid

,bits$*"
header

body
@
boutsZ2X
T*R
ready

valid

,bits$*"
header

body
@-"
tbl"


�Router.scala 51:18\z@
1:/
&:$


ioread_routing_table_requestready	

0�Decoupled.scala 72:20\z@
1:/
&:$


ioload_routing_table_requestready	

0�Decoupled.scala 72:20]zA
2:0
':%


ioread_routing_table_responsevalid	

0�Decoupled.scala 56:20P�3
1:/
':%


ioread_routing_table_responsebits�Decoupled.scala 57:19Yz@
1:/
':%


ioread_routing_table_responsebits	

0�Router.scala 57:39Dz(
:
:


ioinready	

0�Decoupled.scala 72:20U
<
_io_outs_0_bits_WIRE$*"
header

body
@�Router.scala 60:29Jz1
": 


_io_outs_0_bits_WIREbody	

0@�Router.scala 60:29Lz3
$:"


_io_outs_0_bits_WIREheader	

0�Router.scala 60:29lzS
-:+
#:!
B
:


ioouts
0bitsbody": 


_io_outs_0_bits_WIREbody�Router.scala 60:14pzW
/:-
#:!
B
:


ioouts
0bitsheader$:"


_io_outs_0_bits_WIREheader�Router.scala 60:14Oz3
$:"
B
:


ioouts
0valid	

0�Decoupled.scala 56:20L�/
-:+
#:!
B
:


ioouts
0bitsbody�Decoupled.scala 57:19N�1
/:-
#:!
B
:


ioouts
0bitsheader�Decoupled.scala 57:19U
<
_io_outs_1_bits_WIRE$*"
header

body
@�Router.scala 60:29Jz1
": 


_io_outs_1_bits_WIREbody	

0@�Router.scala 60:29Lz3
$:"


_io_outs_1_bits_WIREheader	

0�Router.scala 60:29lzS
-:+
#:!
B
:


ioouts
1bitsbody": 


_io_outs_1_bits_WIREbody�Router.scala 60:14pzW
/:-
#:!
B
:


ioouts
1bitsheader$:"


_io_outs_1_bits_WIREheader�Router.scala 60:14Oz3
$:"
B
:


ioouts
1valid	

0�Decoupled.scala 56:20L�/
-:+
#:!
B
:


ioouts
1bitsbody�Decoupled.scala 57:19N�1
/:-
#:!
B
:


ioouts
1bitsheader�Decoupled.scala 57:19U
<
_io_outs_2_bits_WIRE$*"
header

body
@�Router.scala 60:29Jz1
": 


_io_outs_2_bits_WIREbody	

0@�Router.scala 60:29Lz3
$:"


_io_outs_2_bits_WIREheader	

0�Router.scala 60:29lzS
-:+
#:!
B
:


ioouts
2bitsbody": 


_io_outs_2_bits_WIREbody�Router.scala 60:14pzW
/:-
#:!
B
:


ioouts
2bitsheader$:"


_io_outs_2_bits_WIREheader�Router.scala 60:14Oz3
$:"
B
:


ioouts
2valid	

0�Decoupled.scala 56:20L�/
-:+
#:!
B
:


ioouts
2bitsbody�Decoupled.scala 57:19N�1
/:-
#:!
B
:


ioouts
2bitsheader�Decoupled.scala 57:19U
<
_io_outs_3_bits_WIRE$*"
header

body
@�Router.scala 60:29Jz1
": 


_io_outs_3_bits_WIREbody	

0@�Router.scala 60:29Lz3
$:"


_io_outs_3_bits_WIREheader	

0�Router.scala 60:29lzS
-:+
#:!
B
:


ioouts
3bitsbody": 


_io_outs_3_bits_WIREbody�Router.scala 60:14pzW
/:-
#:!
B
:


ioouts
3bitsheader$:"


_io_outs_3_bits_WIREheader�Router.scala 60:14Oz3
$:"
B
:


ioouts
3valid	

0�Decoupled.scala 56:20L�/
-:+
#:!
B
:


ioouts
3bitsbody�Decoupled.scala 57:19N�1
/:-
#:!
B
:


ioouts
3bitsheader�Decoupled.scala 57:19�2q
_TkRi1:/
&:$


ioread_routing_table_requestvalid2:0
':%


ioread_routing_table_responseready�Router.scala 65:44�:�


_T\z@
1:/
&:$


ioread_routing_table_requestready	

1�Decoupled.scala 65:20k2R
_T_1JRH::8
0:.
&:$


ioread_routing_table_requestbitsaddr
3
0�Router.scala 66:43=�#MPORTtbl"

_T_1*	

clock�Router.scala 66:43]zA
2:0
':%


ioread_routing_table_responsevalid	

1�Decoupled.scala 47:20Zz>
1:/
':%


ioread_routing_table_responsebits	

MPORT�Decoupled.scala 48:19�:�
1:/
&:$


ioload_routing_table_requestvalid\z@
1:/
&:$


ioload_routing_table_requestready	

1�Decoupled.scala 65:20j2R
_T_2JRH::8
0:.
&:$


ioload_routing_table_requestbitsaddr
3
0�Router.scala 72:8>�%MPORT_1tbl"

_T_2*	

clock�Router.scala 72:8bzI

	
MPORT_1::8
0:.
&:$


ioload_routing_table_requestbitsdata�Router.scala 72:19:2!
_T_3R	

reset
0
0�Router.scala 73:11<2#
_T_4R

_T_3	

0�Router.scala 73:11�:�


_T_4�R�
setting tbl(%d) to %d
::8
0:.
&:$


ioload_routing_table_requestbitsaddr::8
0:.
&:$


ioload_routing_table_requestbitsdata	

clock"	

1�Router.scala 73:11�Router.scala 73:11�
:�

:
:


ioinvalidW2>
_idx_T4R2$:"
:
:


ioinbitsheader
4
0�Router.scala 77:29?2&
_idx_T_1R


_idx_T
3
0�Router.scala 77:18?�%idxtbl"


_idx_T_1*	

clock�Router.scala 77:18&2
_T_5R

idx
1
0�
 �:�
):'
J
:


ioouts

_T_5readyDz(
:
:


ioinready	

1�Decoupled.scala 65:20&2
_T_6R

idx
1
0�
 Tz8
):'
J
:


ioouts

_T_6valid	

1�Decoupled.scala 47:20tzX
2:0
(:&
J
:


ioouts

_T_6bitsbody": 
:
:


ioinbitsbody�Decoupled.scala 48:19xz\
4:2
(:&
J
:


ioouts

_T_6bitsheader$:"
:
:


ioinbitsheader�Decoupled.scala 48:19V2<
_T_74R2$:"
:
:


ioinbitsheader
3
0�Router.scala 81:108@�%MPORT_2tbl"

_T_7*	

clock�Router.scala 81:108:2!
_T_8R	

reset
0
0�Router.scala 81:13<2#
_T_9R

_T_8	

0�Router.scala 81:13�:�


_T_9�R�
@got packet to route header %d, data %d, being routed to out(%d)
$:"
:
:


ioinbitsheader": 
:
:


ioinbitsbody
	
MPORT_2	

clock"	

1�Router.scala 81:13�Router.scala 81:13�Router.scala 78:30�Router.scala 75:26�Router.scala 70:50�Router.scala 65:85
Router