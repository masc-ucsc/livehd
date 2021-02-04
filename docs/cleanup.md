
## Small Cleanup Tasks

This document has small cleanup/refactoring tasks


Classes should have only first character upper case. E.g:

* Rename LGraph to Lgraph in all the classes
* Rename Index_ID to Index_id

Replace all the calls that return a pointer for XXX * ref_XXX

Replace all the get_text for get_sview (explicit to std::string_view)

Increase the token position to 64 bits

mmap_lib::tree should support move operators like doCreate in mmap_lib::map. Implement it.

### pass.compile

* In FIRRTL no need to set firrtl:true. If input is a proto, it is firrtl
* In FIRRTL no need to set the top always. If no top set, treat a proto as a top by itself

### lgshell

* autocompletion for LGDB names. E.g: lgraph.open name:a<TAB> should match any module starting with a
* fuzzy backward search

### lnast-ssa
* Clean up analysis based on dot, now the lnast should contains select [] only.
* Modify fir_tolnast to generate select-only lnast tuple.
