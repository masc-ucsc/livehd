## Small Cleanup Tasks

This document has small cleanup/refactoring tasks

Create a source_map out of the Token_list

mmap_lib::tree should support move operators like doCreate in mmap_lib::map. Implement it.

### mmap_lib separate repository (ucsc-masc/mmap_lib?)

mmap_lib could be a separate repo by itself. Other may use it and help to improve LiveHD even for non synthesis projects.

Besides moving mmap_lib, we may want:

- Create the mmap_tree2 because the current mmap_tree is not persistent
- Move the graph_core out of core to mmap_graph

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

- In FIRRTL no need to set firrtl:true. If input is a proto, it is firrtl
- In FIRRTL no need to set the top always. If no top set, treat a proto as a top by itself

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

## mmap_lib::str

* Remove the mmap_lib::str("some string") (most are not needed once not explicit conversion is enabled)
* Do more mmap_lib::str::concat cases (eg: concat with an integer). Optimize for speed.
* Add non-persistent mmap_str option (only for non-sso). mmap_lib::pstr ?
* Unclear what is better to pass mmap_lib (const mmap_lib &str) OR (mmap_lib str). Benchmark?
* Go over all the absl::Str. Most of them could be replaced/avoided with mmap_lib::str::concat ....
* create a mmap_str::str(int val). Equivalent to str(std::to_string(val)) without intermediate step
* mmap_str_test is a test abd a bench. Split the bench to bench_str and convert the rest to google test

* Reorg mmap_str to use a uint64_t instead of size_ctr & ptr_or_start. This allows for single word optimizations (E.g: find char sso). 

* WHen in unique (overflow) allocate with 8byte alignment (put zeroes if needed). The reason is to allow word search

* Optimize the find char for non SSO (once we have 8byte align).
* Do a rfind optimized like the find char
* Change the find to find first char with match, then use the local search (faster)

* Code generation phase (code_gen, cgen...) can not effectively use mmap_str because the comparison is irrelevant and keeping in a map is a HUGE overhead for growing strings (large). Maybe we should have 3 modes:

1-SSO (<=15 strings)
2-unique (16..256?)
3-large  (256..) this do not have a pointer (maybe the persistent vs transient?, transient SSO or large)


If unique is 255 or less, we can have more bytes for data.

 Unique (data: 8byte beginning string, 0x0 1byte, sz 1Byte, ptr: 6byte)
 SSO    (data: 15bytes string,  1 byte sz)
 large  (ptr: 8 byter, sz 6 bytes, 0b00 2 bytes)

empty (last 8 bytes zero)
last 2 zero bytes  -> large
last 1 zero byte   -> unique
last byte non-zero -> SSO 

Both Unique and overflow have first byte at data[0]

future get_n_data_chars()
  return size when sso
  return 1+(size&0xF)

large ptr points to a "memory" not mmap struct/data head of a splay tree.
The large are only serialized explicitly (not in mmap_lib key/value support)

Do we create a rope for large strings?

rope node:
  left  = 40bit ptr
  right = 40bit ptr
  weight = 48bits 

 Splay tree? (similar to rope but dyn-rebalance. Good because we tend to add at the end frequently)

 Splay trees do not serialize

### call

When passing as argument use "const mmap_lib::str". (One mem op less than "const mmap_lib::str &"

int cgen_do_str(const mmap_lib::str s) { // BETTER
  return s.front();
}

int cgen_do_str_ptr(const mmap_lib::str &s) { // MORE CODE
  return s.front();
}

 Disassembly of section .text._Z11cgen_do_strN8mmap_lib3strE:
 1899
 1900 0000000000000000 <_Z11cgen_do_strN8mmap_lib3strE>:
 1901   ¦0: 89 f8                 mov    %edi,%eax
 1902   ¦2: c1 e8 08              shr    $0x8,%eax
 1903   ¦5: 83 e7 01              and    $0x1,%edi
 1904   ¦8: 0f 44 c6              cmove  %esi,%eax
 1905   ¦b: 0f be c0              movsbl %al,%eax
 1906   ¦e: c3                    retq
 1907
 1908 Disassembly of section .text._Z15cgen_do_str_ptrRKN8mmap_lib3strE:
 1909
 1910 0000000000000000 <_Z15cgen_do_str_ptrRKN8mmap_lib3strE>:
 1911   ¦0: 8b 07                 mov    (%rdi),%eax
 1912   ¦2: a8 01                 test   $0x1,%al
 1913   ¦4: 75 0a                 jne    10 <_Z15cgen_do_str_ptrRKN8mmap_lib3strE+0x10>
 1914   ¦6: 0f b6 47 08           movzbl 0x8(%rdi),%eax
 1915   ¦a: 0f be c0              movsbl %al,%eax
 1916   ¦d: c3                    retq
 1917   ¦e: 66 90                 xchg   %ax,%ax
 1918   10: 0f b6 c4              movzbl %ah,%eax
 1919   13: 0f be c0              movsbl %al,%eax
 1920   16: c3                    retq


### mmap_str

Use unsigned char not char (sign extension issues)

### lnast

* Once non-persistent mmap_str, convert the remaining std::string_views (lnast,...)
* LNAST should have lconst too. E.g: create_const(Lconst) not a create_const(string). This will allow lnast copy prop and avoid translation back and forth.

### firrtl pass

* It makes sense to keep it as std::string_view (no need to use mmap_str in the internal traversal)

## pass/fplan

* Many methods/variables use camelCase, it should be camel_case
* "int" is used all over. Even for things that should be size_t (size of stuff)
* std::string is used quite a bit. It should be mmap_lib::str (look for to_s, they should be gone too)
* node_tree is only used by pass/fplan. Move from core to pass/fplan
* Lots of variable declarations c-style (early in the method, then assigned later. Use later on first assign)
* Only classes can start with upper case (Name2Count -> name2count)
* Classes should start uppercase. E.g: Ntype_area::dim
* There are several uses of mmap_lib::map that should be converted to absl::flat_hash_map (only persistent should use mmap_lib). E.g: hint_map should not be persistent

