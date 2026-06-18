# AGU — --reader slang fails

kind=-

| stage | result |
|---|---|
| yosys+slang gate | PASS |
| --reader slang -> prp | FAIL |
| slang -> lg | NA |
| prp -> lg | NA |
| yosys-slang -> lg | PASS |
| lec prp vs slang | NA |
| lec prp vs yosys-slang | NA |
| abc gen | NA |

**First failure message:**
```
`!=` requires both operands to be the same type (isStore_s1:boolean vs <const>:integer)
```
