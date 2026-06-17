# BranchSelector — prp gen fails

kind=mod

| stage | result |
|---|---|
| yosys+slang gate | PASS |
| --reader slang -> prp | PASS |
| slang -> lg | PASS |
| prp -> lg | FAIL |
| yosys-slang -> lg | PASS |
| lec prp vs slang | NA |
| lec prp vs yosys-slang | NA |
| abc gen | NA |

**First failure message:**
```
assignment to undeclared variable 'intPortBranch' (declare it with `mut`/`const` first)
```
