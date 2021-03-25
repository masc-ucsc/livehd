
## Small Cleanup Tasks

This document has small cleanup/refactoring tasks


Classes should have only first character upper case. E.g:

* Rename LGraph to Lgraph in all the classes
* Rename Index_ID to Index_id

Replace all the calls that return a pointer for XXX * ref_XXX

Replace all the get_text for get_sview (explicit to std::string_view)

Increase the token position to 64 bits

mmap_lib::tree should support move operators like doCreate in mmap_lib::map. Implement it.

### Attr/Tup

* Do not chain tuples without need (lnast.cpp). TupAdd/Get can handle position like foo.bar.xxx

* Parser to directly generate Tupple_get, Tuple_set. This removes from LNAST the select field.
Quite a bit of code from lnast.cpp like the set2attr_set_get could be removed

* No attr_get/set from lnast_tolg. Also, no need to chain Attr like now. When cprop does the lgtuples,
the attr fields gets generated as AttrSet/Get only when TupAdd/Get uses that attribute or the parent.

* Remove all the `#` and `__q_pin` reg from cprop/bitwidth/lnast_tolg/lnast code. Use `__create_flop`

* Once all the `#` is gone, the prp_lnast should create the `__create_flop` automatically out of `#`

### Deprecate the q_pin and use create_flop

* Less code (few lines shared with last_value). No need for cprop to progate/fix flops

### pass.compile

* In FIRRTL no need to set firrtl:true. If input is a proto, it is firrtl
* In FIRRTL no need to set the top always. If no top set, treat a proto as a top by itself

### lgshell

* autocompletion for LGDB names. E.g: lgraph.open name:a<TAB> should match any module starting with a
* fuzzy backward search

### lnast-ssa
* Clean up analysis based on dot, now the lnast should contains select [] only.
* Modify fir_tolnast to generate select-only lnast tuple.
