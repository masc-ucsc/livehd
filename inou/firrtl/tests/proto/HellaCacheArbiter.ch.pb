
�
��
HellaCacheArbiter
clock" 
reset
�
io�*�
�		requestor�	2�	
�	*�	
�req�*�
ready

valid

�bits�*�
addr
(
tag

cmd

size

signed

dprv

phys

no_alloc

no_xcpt

data
@
mask

s1_kill

-s1_data"* 
data
@
mask

s2_nack

s2_nack_cause_raw

s2_kill

s2_uncached

s2_paddr
 
�resp�*�
valid

�bits�*�
addr
(
tag

cmd

size

signed

dprv

data
@
mask

replay

has_data

data_word_bypass
@
data_raw
@

store_data
@
replay_next

�s2_xcptt*r
$ma*
ld

st

$pf*
ld

st

$ae*
ld

st

ordered

�perf�*�
acquire

release

grant

tlbMiss

blocked

 canAcceptStoreThenLoad

canAcceptStoreThenRMW

canAcceptLoadThenLoad

#storeBufferEmptyAfterLoad

$storeBufferEmptyAfterStore

keep_clock_enabled

clock_enabled

�	mem�	*�	
�req�*�
ready

valid

�bits�*�
addr
(
tag

cmd

size

signed

dprv

phys

no_alloc

no_xcpt

data
@
mask

s1_kill

-s1_data"* 
data
@
mask

s2_nack

s2_nack_cause_raw

s2_kill

s2_uncached

s2_paddr
 
�resp�*�
valid

�bits�*�
addr
(
tag

cmd

size

signed

dprv

data
@
mask

replay

has_data

data_word_bypass
@
data_raw
@

store_data
@
replay_next

�s2_xcptt*r
$ma*
ld

st

$pf*
ld

st

$ae*
ld

st

ordered

�perf�*�
acquire

release

grant

tlbMiss

blocked

 canAcceptStoreThenLoad

canAcceptStoreThenRMW

canAcceptLoadThenLoad

#storeBufferEmptyAfterLoad

$storeBufferEmptyAfterStore

keep_clock_enabled

clock_enabled
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
 V�1
:


iomemB
:


io	requestor
0�HellaCacheArbiter.scala 17:12
HellaCacheArbiter