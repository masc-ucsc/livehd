# IntALU — yosys+slang fails

kind=-

| stage | result |
|---|---|
| yosys+slang gate | FAIL |
| --reader slang -> prp | FAIL |
| slang -> lg | NA |
| prp -> lg | NA |
| yosys-slang -> lg | NA |
| lec prp vs slang | NA |
| lec prp vs yosys-slang | NA |
| abc gen | NA |

**First failure message:**
```
`!=` requires both operands to be the same type (HasFU___ssa_1:boolean vs <const>:integer)
```
