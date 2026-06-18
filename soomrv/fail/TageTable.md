# TageTable — prp gen fails

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
upass.tolg: call to 'BranchPredictionTable' has no hardware lowering yet — only pipe/mod calls become instances (note `comb` may not call a `pipe`/`mod`), and runtime `wrap`/`sat` lowering is pending
```
