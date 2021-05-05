
## Small Cleanup Tasks

This document has small cleanup/refactoring tasks

Create a source_map out of the Token_list

mmap_lib::tree should support move operators like doCreate in mmap_lib::map. Implement it.

### Pyrope parser

There are some bugs on the pyrope parser when dealing with ranges. This works:

```
foo = bar@(1:fcall(3))
```

But this fails (and it should work too):
```
foo = bar@(fcall(7):fcall(3))
```

Similarly, this works but it should fail (ambiguous):
```
foo = bar@(1+2:4) // bar@((1+2):4) is OK
```

Related, the bit selector should use [] not () and support commas. It should be:

```
foo = bar@[1:3]       // pick bits 1,2,3
foo = bar@[1+2:4, 10] // pick bits 3,4,10
```

The `@[a,b]` creates a one-hot bitmask to selects bits (feed to get_mask). The same is also
used in the for loop.

```
for a in @[0xF] { // 0,1,2,3 iterations
for a in 0xF { // 15 iteration
for b in @[8,1:3,2] { // 1,2,3,8   iterations (bitmask order)
for b in (8,1:3,2)  { // 8,1,2,3,2 iterations (ordered tuple)
```

Allow tuples directly in brackets, no need for unnecessary parenthesis.

Currently, this is a compile error (parser)
```
xx = delay[1,2] 
```
It requires this:
```
xx = delay[(1,2)] 
```

Option 2 may be fine, but option 1 should be the same

### Attr/Tup

* Do not chain tuples without need (lnast.cpp). TupAdd/Get can handle position like foo.bar.xxx

* Parser to directly generate Tupple_get, Tuple_set. This removes from LNAST the select field.
Quite a bit of code from lnast.cpp like the set2attr_set_get could be removed

* No attr_get/set from lnast_tolg. Also, no need to chain Attr like now. When cprop does the lgtuples,
the attr fields gets generated as AttrSet/Get only when TupAdd/Get uses that attribute or the parent.

* Remove all the `#` and `__q_pin` reg from cprop/bitwidth/lnast_tolg/lnast code. Use `__create_flop`

* Once all the `#` is gone, the prp_lnast should create the `__create_flop` automatically out of `#`

* Attr should be only for scalar

* Ann_ssa does not need to be persistent


### pass.compile

* In FIRRTL no need to set firrtl:true. If input is a proto, it is firrtl
* In FIRRTL no need to set the top always. If no top set, treat a proto as a top by itself

### lgshell

* autocompletion for LGDB names. E.g: lgraph.open name:a<TAB> should match any module starting with a
* fuzzy backward search

### lnast-ssa
* Clean up analysis based on dot, now the lnast should contains select [] only.
* Modify fir_tolnast to generate select-only lnast tuple.
