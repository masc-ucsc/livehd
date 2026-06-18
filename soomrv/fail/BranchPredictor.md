# BranchPredictor — prp gen fails

kind=mod

| stage | result |
|---|---|
| yosys+slang gate | FAIL |
| --reader slang -> prp | PASS |
| slang -> lg | PASS |
| prp -> lg | FAIL |
| yosys-slang -> lg | NA |
| lec prp vs slang | NA |
| lec prp vs yosys-slang | NA |
| abc gen | NA |

**First failure message:**
```
upass.tolg: call to 'RegFile' has no hardware lowering yet — only pipe/mod calls become instances (note `comb` may not call a `pipe`/`mod`), and runtime `wrap`/`sat` lowering is pending
```
