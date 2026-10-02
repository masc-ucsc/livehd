# Generic interfaces and register-array examples

Each group has exactly two Pyrope forms and one hand-written Verilog reference:

| Group | Working reference | Desired form | Verilog |
| --- | --- | --- | --- |
| Module port widths | `generic_interface_width.prp` | `generic_interface_width_1.prp` | `generic_interface_width.v` |
| Register-array reset | `register_array_immediate_reset.prp` | `register_array_immediate_reset_1.prp` | `register_array_immediate_reset.v` |

The numbered variants state the **intended** equivalence. They are live tests
requiring an unbounded PROVEN result, not expected-error tests or disabled
fixtures. These desired forms are not claims of working compiler support.

The interface reference passes concrete type arguments (`u4` and `u12`). Its
variant computes input and output widths from a generic lane count, exercises
default and nondefault child specializations, and selects a generic top with
`LANES=3`. Selecting the top must resolve its own default interface types with
no caller to infer them from. A first example with only concrete callers passed
because inference concealed this missing support; that is why the top is also
generic. Every input/output bit contributes to an XOR result.

The register reference carries four bytes in a packed `u32`. Its variant uses
an indexed `reg` array with a reset value. A `reg` array's initializer is its
reset value exactly as a scalar `reg`'s is: every entry is restored in **one**
cycle of `reset` through the memory's whole-array reset (`async=true` makes it
asynchronous), reset wins over write enable, and unselected lanes hold. Entry
zero is the low byte. `q` reads the committed selected entry; `all_data`
exposes the entire bank so a partial reset cannot hide in an unread lane.
(An earlier draft spelled a `storage="registers"` attribute for this; there is
no such attribute — the front end rejects the name — because the one-cycle
reset is the array's ordinary semantics.)

Both working references pass against their Verilog, and both numbered variants
are proven unbounded-equivalent to their references. The register-array
variant's solver budget is five seconds per query so a regression fails
promptly.

Run from the LiveHD root:

```sh
bazel test -c opt \
  //inou/prp:prp-equiv-generic_interface_width \
  //inou/prp:prp-lec-generic_interface_width_1 \
  //inou/prp:prp-equiv-register_array_immediate_reset \
  //inou/prp:prp-lec-register_array_immediate_reset_1 \
  --test_output=errors
```

The two `prp-equiv-*` targets compare each reference with its Verilog. The two
`prp-lec-*_1` targets compare the desired forms with those references. No BUILD
or harness changes are required.
