## Small Cleanup Tasks

This document has small cleanup/refactoring tasks

Create a source_map out of the Token_list

### Replace mustache for inja

The current C++ mustache library seems no longer maintained. It would be good to replace for inja

```
  https://github.com/pantor/inja
```

### Create a lh directory

-lh::tree
-graph_core
-str_tools

Basic data structures used all over but very LiveHD independent

### Renaming

- Change lgtuple to lbundle (Lgtupe -> Lbundle and lgtuple -> lbundle). The
  reason is that tuples are ordered, and the lbundle can be ordered and
  unordered. Also, the lg implies only a Lgraph structure but LNAST should use
  it too for emulation.

- Rename lconst to lops (livehd cell ops). Other implementations would be sops
  (static C++), vops (verilog), and jops (javascript)
- Rename TupAdd for Tup_add, TupGet for Tup_get, AttrSet for Attr_set, AttrGet for Attr_get

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

- Do not chain tuples without need (lnast.cpp). TupAdd/Get can handle position like foo.bar.xxx

- Parser to directly generate Tupple_get, Tuple_set. This removes from LNAST the select field.
  Quite a bit of code from lnast.cpp like the set2attr_set_get could be removed

- No attr_get/set from lnast_tolg. Also, no need to chain Attr like now. When cprop does the lgtuples,
  the attr fields gets generated as AttrSet/Get only when TupAdd/Get uses that attribute or the parent.

- Once all the `#` is gone, the prp_lnast should create the `__create_flop` automatically out of `#`

- Attr should be only for scalar

- Ann_ssa does not need to be persistent

### pass.compile


### lgshell

- autocompletion for LGDB names. E.g: lgraph.open name:a<TAB> should match any module starting with a
- fuzzy backward search

### lnast-ssa

- Clean up analysis based on dot, now the lnast should contains select [] only.
- Modify fir_tolnast to generate select-only lnast tuple.

### lconst

The lconst implementation of "has_unknowns" is not very efficient. It uses a
string to represent things like 0b?0?. It may be more efficient to have 2
Numbers. The current num and unk_num. If the bit is set to 1, the bit is
unknown. This binary representation allows for faster operations with uknowns
(it should not be frequent, but it can happen).

In C++ generated simulation, the computation can be 2 or 4 bit logic. Inside
LGraph/LNAST, the computation is always 4 bit logic.

## node_type_area

The node_type_area is a "property" for each cell type (not per instance). It
should be a per class attribute (not instance attribute). A small difference
with normal attributes is that "base" cell types have also this property, not
only the tech file. A tech file could also add area properties, so not just for
cells.

* Rename/rework node_type_area (sub_area.hpp).
* Populate the default areas for each cell (this should add cells like `__sum`
  to the sub_node.
* It is not a plain attribute because the value depends on the cell use. E.g: a
  sum with 2 bits is different than a sub with 100 bits.
* There should be also be an option to have a per-instance area modification
  (factor?) but most cells should not have this, maybe some sub_node instances)

* Summary: create a new sub_area class that tracks area per cell sub_node
  instance, and allows to adjust area per instance in some cases. The
  floorplanner could use this enhanced capability to track area.

